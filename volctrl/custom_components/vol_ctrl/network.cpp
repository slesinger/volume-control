#include "network.h"
#include "utils.h"
#include "esphome/core/log.h"
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
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

void register_device(const std::string &name, const std::string &ipv6) {
  device_map[name] = ipv6;
  device_states[ipv6] = DeviceState();
}

std::map<std::string, DeviceState>& get_device_states() {
  return device_states;
}

// Return value indicates whether speaker is up or down, while data struct carrye volume, mute and standby-countdown
bool get_device_data(const std::string &ipv6, DeviceVolStdbyData &data) {
  std::string response;
  bool success = send_ssc_command(
    ipv6, 
    "{\"device\":{\"standby\":{\"countdown\":null}},\"audio\":{\"out\":{\"level\":null,\"mute\":null}}}",
    response);
  
  if (success) {
    float level = 0.0f;
    float countdown = 0.0f;
    bool mute = false;
    bool ok = true;
    ok &= utils::extract_json_number(response, "level", level);
    ok &= utils::extract_json_number(response, "countdown", countdown);
    ok &= utils::check_json_boolean(response, "mute", mute);
    if (ok) {
      data.volume = level;
      data.standby_countdown = static_cast<int>(countdown);
      data.mute = mute;
      return true;
    }
  }
  return false;
}

bool set_device_volume(const std::string &ipv6, float volume) {
  std::string command = "{\"audio\":{\"out\":{\"level\":" + std::to_string(volume) + "}}}";
  std::string response;
  if (network::send_ssc_command(ipv6, command, response)) {
    // ESP_LOGI(TAG, "Successfully set volume to %.1f for device %s, response: %s", volume, ipv6.c_str(), response.c_str());
    return true;
  } else {
    ESP_LOGE(TAG, "Failed to set volume for device %s - network error", ipv6.c_str());
    return false;
  }
}

bool set_device_mute(const std::string &ipv6, bool mute) {
  std::string command = "{\"audio\":{\"out\":{\"mute\":" + std::string(mute ? "true" : "false") + "}}}";
  std::string response;
  if (network::send_ssc_command(ipv6, command, response)) {
    ESP_LOGI(TAG, "Successfully %s device %s, response: %s", mute ? "muted" : "unmuted", ipv6.c_str(), response.c_str());
    return true;
  } else {
    ESP_LOGE(TAG, "Failed to %s device %s - network error", mute ? "mute" : "unmute", ipv6.c_str());
    return false;
  }
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
