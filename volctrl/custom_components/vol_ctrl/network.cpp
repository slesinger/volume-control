#include "network.h"
#include "utils.h"
#include "esphome/core/log.h"
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/select.h>
#include <map>
#include "esphome/core/hal.h"
#include <lwip/netif.h>
#include <lwip/ip_addr.h>

namespace esphome {
namespace vol_ctrl {
namespace network {

static const char *const TAG = "vol_ctrl.network";

// Device map - name to IPv6 address
static std::map<std::string, std::string> device_map;

// Device status cache
static std::map<std::string, DeviceState> device_states;

namespace {

// Closes the socket on every exit path.
struct SocketGuard {
  int fd;
  explicit SocketGuard(int f) : fd(f) {}
  ~SocketGuard() {
    if (fd >= 0) close(fd);
  }
};

constexpr uint32_t CONNECT_TIMEOUT_MS = 300;
constexpr uint32_t IO_TIMEOUT_MS = 500;
constexpr size_t MAX_RESPONSE_BYTES = 2048;

// lwIP's connect() cannot be bounded with SO_SNDTIMEO, so connect non-blocking and wait in select().
bool connect_with_timeout(int sock, const sockaddr *addr, socklen_t len, uint32_t timeout_ms) {
  int flags = fcntl(sock, F_GETFL, 0);
  if (flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) return false;

  int rc = connect(sock, addr, len);
  if (rc < 0 && errno != EINPROGRESS) return false;

  if (rc < 0) {
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(sock, &wfds);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (select(sock + 1, nullptr, &wfds, nullptr, &tv) <= 0) {
      errno = ETIMEDOUT;
      return false;
    }
    int err = 0;
    socklen_t elen = sizeof(err);
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &elen) < 0 || err != 0) {
      errno = err;
      return false;
    }
  }
  return fcntl(sock, F_SETFL, flags) >= 0;  // back to blocking, I/O is bounded by SO_*TIMEO
}

}  // namespace

// Failures are logged at DEBUG only: offline speakers are polled every cycle and would flood the log.
// Callers decide whether a failure deserves a louder message.
bool send_ssc_command(const std::string &ipv6, const std::string &command, std::string &response) {
  uint32_t start_time = millis();
  ESP_LOGD(TAG, "Sending to [%s]:45: %s", ipv6.c_str(), command.c_str());

  SocketGuard guard(socket(AF_INET6, SOCK_STREAM, 0));
  const int sock = guard.fd;
  if (sock < 0) {
    ESP_LOGE(TAG, "Failed to create socket: %d (%s)", errno, strerror(errno));
    return false;
  }

  // Close with RST instead of FIN: no TIME_WAIT. We open a connection per request (every 1.5 s per speaker), and
  // lingering lwIP PCBs eventually exhaust the pool ("Failed to create socket: 105 No buffer space available").
  struct linger abortive;
  abortive.l_onoff = 1;
  abortive.l_linger = 0;
  setsockopt(sock, SOL_SOCKET, SO_LINGER, &abortive, sizeof(abortive));

  struct timeval timeout;
  timeout.tv_sec = IO_TIMEOUT_MS / 1000;
  timeout.tv_usec = (IO_TIMEOUT_MS % 1000) * 1000;
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
      setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
    ESP_LOGE(TAG, "Failed to set socket timeouts: %d (%s)", errno, strerror(errno));
    return false;
  }

  struct sockaddr_in6 sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin6_family = AF_INET6;
  sa.sin6_port = htons(45);  // Default SSC port is 45
  if (inet_pton(AF_INET6, ipv6.c_str(), &sa.sin6_addr) != 1) {
    ESP_LOGE(TAG, "Invalid IPv6 address format: %s", ipv6.c_str());
    return false;
  }

  if (!connect_with_timeout(sock, (struct sockaddr *) &sa, sizeof(sa), CONNECT_TIMEOUT_MS)) {
    ESP_LOGD(TAG, "Failed to connect to %s: errno %d (%s)", ipv6.c_str(), errno, strerror(errno));
    return false;
  }

  // SSC messages are terminated by CRLF
  std::string request = command + "\r\n";
  size_t total_sent = 0;
  while (total_sent < request.length()) {
    int sent = send(sock, request.c_str() + total_sent, request.length() - total_sent, 0);
    if (sent <= 0) {
      ESP_LOGD(TAG, "Failed to send to %s: errno %d (%s)", ipv6.c_str(), errno, strerror(errno));
      return false;
    }
    total_sent += sent;
  }

  // TCP is a stream: keep reading until the CRLF that terminates the reply
  response.clear();
  char buffer[256];
  while (response.size() < MAX_RESPONSE_BYTES && response.find("\r\n") == std::string::npos) {
    int n = recv(sock, buffer, sizeof(buffer), 0);
    if (n < 0) {
      ESP_LOGD(TAG, "Failed to receive from %s: errno %d (%s)", ipv6.c_str(), errno, strerror(errno));
      return false;
    }
    if (n == 0) break;  // peer closed
    response.append(buffer, n);
  }
  if (response.empty()) return false;

  ESP_LOGD(TAG, "Received %d bytes in %u ms: %s", (int) response.size(), millis() - start_time, response.c_str());
  return true;
}

namespace {

// Speaker as seen by the worker task. Pending-write fields and bookkeeping are guarded by `mtx`.
struct Link {
  std::string ipv6;
  bool has_volume = false;
  float volume = 0.0f;
  bool has_mute = false;
  bool mute = false;
  uint32_t write_epoch = 0;   // bumped on every request, used to discard polls that raced with a write
  uint32_t last_write_ms = 0;
  uint32_t next_poll_ms = 0;
  uint8_t failed_polls = 0;   // consecutive, drives the back-off
  bool was_up = false;
};

constexpr uint32_t POLL_INTERVAL_UP_MS = 1500;
constexpr uint32_t POLL_INTERVAL_FAST_RETRY_MS = 500;  // first failures: the first connect often loses to neighbour discovery
constexpr uint8_t FAST_RETRIES = 6;
constexpr uint32_t POLL_INTERVAL_DOWN_MS = 5000;  // back off from unreachable speakers (each attempt costs a connect timeout)
constexpr uint32_t QUIET_AFTER_WRITE_MS = 400;    // let the speaker settle before reading its state back

std::vector<Link> links;  // filled before start(), never resized afterwards
std::vector<PollUpdate> updates;
std::mutex mtx;
TaskHandle_t worker = nullptr;
std::atomic<bool> online{false};

Link *find_link(const std::string &ipv6) {
  for (auto &l : links)
    if (l.ipv6 == ipv6) return &l;
  return nullptr;
}

// Blocking: only call from the worker task.
bool get_device_data_blocking(const std::string &ipv6, DeviceVolStdbyData &data) {
  std::string response;
  if (!send_ssc_command(
          ipv6, "{\"device\":{\"standby\":{\"countdown\":null}},\"audio\":{\"out\":{\"level\":null,\"mute\":null}}}",
          response))
    return false;

  float level = 0.0f;
  float countdown = 0.0f;
  bool mute = false;
  bool ok = true;
  ok &= utils::extract_json_number(response, "level", level);
  ok &= utils::extract_json_number(response, "countdown", countdown);
  ok &= utils::check_json_boolean(response, "mute", mute);
  if (!ok) return false;
  data.volume = level;
  data.standby_countdown = static_cast<int>(countdown);
  data.mute = mute;
  return true;
}

bool send_volume_blocking(const std::string &ipv6, float volume) {
  std::string response;
  std::string command = "{\"audio\":{\"out\":{\"level\":" + std::to_string(volume) + "}}}";
  if (send_ssc_command(ipv6, command, response)) return true;
  ESP_LOGW(TAG, "Failed to set volume for device %s", ipv6.c_str());
  return false;
}

bool send_mute_blocking(const std::string &ipv6, bool mute) {
  std::string response;
  std::string command = "{\"audio\":{\"out\":{\"mute\":" + std::string(mute ? "true" : "false") + "}}}";
  if (send_ssc_command(ipv6, command, response)) return true;
  ESP_LOGW(TAG, "Failed to %s device %s", mute ? "mute" : "unmute", ipv6.c_str());
  return false;
}

bool time_reached(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }

void worker_task(void *) {
  for (;;) {
    // Woken by request_*(), otherwise tick often enough to notice due polls
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
    if (!online.load()) continue;

    // 1) Writes first, for every speaker, so a slow poll never delays the knob
    for (auto &l : links) {
      bool has_volume, has_mute, mute;
      float volume;
      {
        std::lock_guard<std::mutex> lock(mtx);
        has_volume = l.has_volume;
        volume = l.volume;
        has_mute = l.has_mute;
        mute = l.mute;
        l.has_volume = l.has_mute = false;
        if (has_volume || has_mute) l.last_write_ms = millis();
      }
      bool ok = true;
      if (has_mute) ok &= send_mute_blocking(l.ipv6, mute);
      if (has_volume) ok &= send_volume_blocking(l.ipv6, volume);
      if (!ok) {
        std::lock_guard<std::mutex> lock(mtx);
        l.next_poll_ms = millis();  // resync UI with the real speaker state
      }
    }

    // 2) At most one poll per iteration, so writes queued meanwhile wait for one poll only
    for (auto &l : links) {
      uint32_t now = millis();
      uint32_t epoch;
      {
        std::lock_guard<std::mutex> lock(mtx);
        bool due = time_reached(now, l.next_poll_ms) && !l.has_volume && !l.has_mute &&
                   (l.last_write_ms == 0 || now - l.last_write_ms >= QUIET_AFTER_WRITE_MS);
        if (!due) continue;
        epoch = l.write_epoch;
      }

      PollUpdate update;
      update.ipv6 = l.ipv6;
      update.is_up = get_device_data_blocking(l.ipv6, update.data);

      std::lock_guard<std::mutex> lock(mtx);
      if (epoch != l.write_epoch) {
        l.next_poll_ms = millis() + 100;  // a write happened meanwhile, the reading is stale
      } else if (!update.is_up && l.was_up && l.failed_polls == 0) {
        // A single failed poll of a speaker that was fine is usually a glitch: look again before reporting it down
        l.failed_polls = 1;
        l.next_poll_ms = millis() + 300;
      } else {
        l.was_up = update.is_up;
        l.failed_polls = update.is_up ? 0 : (l.failed_polls < 255 ? l.failed_polls + 1 : 255);
        l.next_poll_ms = millis() + (update.is_up ? POLL_INTERVAL_UP_MS
                                     : l.failed_polls <= FAST_RETRIES ? POLL_INTERVAL_FAST_RETRY_MS
                                                                       : POLL_INTERVAL_DOWN_MS);
        bool replaced = false;
        for (auto &u : updates) {
          if (u.ipv6 == update.ipv6) {
            u = update;
            replaced = true;
          }
        }
        if (!replaced) updates.push_back(update);
      }
      break;
    }
  }
}

}  // namespace

void register_device(const std::string &name, const std::string &ipv6) {
  device_map[name] = ipv6;
  device_states[ipv6] = DeviceState();
  Link link;
  link.ipv6 = ipv6;
  links.push_back(link);
}

void start() {
  if (worker != nullptr) return;
  xTaskCreate(worker_task, "vol_net", 8192, nullptr, 1, &worker);
}

void set_online(bool value) { online.store(value); }

void request_volume(const std::string &ipv6, float volume) {
  Link *l = find_link(ipv6);
  if (l == nullptr) return;
  {
    std::lock_guard<std::mutex> lock(mtx);
    l->has_volume = true;
    l->volume = volume;
    l->write_epoch++;
  }
  if (worker != nullptr) xTaskNotifyGive(worker);
}

void request_mute(const std::string &ipv6, bool mute) {
  Link *l = find_link(ipv6);
  if (l == nullptr) return;
  {
    std::lock_guard<std::mutex> lock(mtx);
    l->has_mute = true;
    l->mute = mute;
    l->write_epoch++;
  }
  if (worker != nullptr) xTaskNotifyGive(worker);
}

bool take_updates(std::vector<PollUpdate> &out) {
  std::lock_guard<std::mutex> lock(mtx);
  if (updates.empty()) return false;
  out.swap(updates);
  updates.clear();
  return true;
}

std::map<std::string, DeviceState>& get_device_states() {
  return device_states;
}

void log_ipv6_addresses() {
  ESP_LOGI(TAG, "log_ipv6_addresses() called");
  struct netif *nif = netif_list;
  while (nif != nullptr) {
    char ifname[8];
    snprintf(ifname, sizeof(ifname), "%c%c%d", nif->name[0], nif->name[1], nif->num);
    for (int i = 0; i < LWIP_IPV6_NUM_ADDRESSES; ++i) {
      if (!ip6_addr_isvalid(netif_ip6_addr_state(nif, i))) continue;
      char buf[64];
      ip6addr_ntoa_r(netif_ip6_addr(nif, i), buf, sizeof(buf));
      ESP_LOGI(TAG, "Interface %s IPv6 addr[%d]: %s", ifname, i, buf);
    }
    nif = nif->next;
  }
}

// Initialize the network module with default devices
void init() {
  ESP_LOGI(TAG, "init() called");
  // Log all IPv6 addresses at startup
  log_ipv6_addresses();
  
  // Here you would normally load devices from persistent storage
  // For now, hardcoding the known devices from the original code
  register_device("Left-6473470117", "2a00:1028:8390:75ee:2a36:38ff:fe61:25b9");
  register_device("Right-6194478038", "2a00:1028:8390:75ee:2a36:38ff:fe61:279e");
  
  ESP_LOGI(TAG, "Network module initialized with %d devices", device_map.size());
}

}  // namespace network
}  // namespace vol_ctrl
}  // namespace esphome
