#include "network.h"
#include "utils.h"
#include "esphome/core/log.h"
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
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

// Rotating symbol state
static std::map<std::string, int> device_rot;

bool send_ssc_command(const std::string &ipv6, const std::string &command, std::string &response) {
  int sock = -1;
  bool success = false;
  uint32_t start_time = millis();
  
  ESP_LOGD(TAG, "Attempting to connect to [%s]:45 to send command: %s", ipv6.c_str(), command.c_str());

  // Create socket
  sock = socket(AF_INET6, SOCK_STREAM, 0);
  if (sock < 0) {
    ESP_LOGE(TAG, "Failed to create socket: %d (%s)", errno, strerror(errno));
    return false;
  }

  // Set socket options
  struct timeval timeout;
  timeout.tv_sec = 1;  // 1 second timeout (shorter for better UI responsiveness)
  timeout.tv_usec = 0;
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
    ESP_LOGE(TAG, "Failed to set receive timeout: %d (%s)", errno, strerror(errno));
    close(sock);
    return false;
  }
  
  if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
    ESP_LOGE(TAG, "Failed to set send timeout: %d (%s)", errno, strerror(errno));
    close(sock);
    return false;
  }

  // Connect to the device
  struct sockaddr_in6 sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin6_family = AF_INET6;
  sa.sin6_port = htons(45);  // Default SSC port is 45
  int pton_result = inet_pton(AF_INET6, ipv6.c_str(), &sa.sin6_addr);
  if (pton_result != 1) {
    ESP_LOGE(TAG, "Invalid IPv6 address format: %s", ipv6.c_str());
    close(sock);
    return false;
  }

  ESP_LOGI(TAG, "Socket created, attempting to connect to [%s]:45...", ipv6.c_str());
  int connect_result = connect(sock, (struct sockaddr *)&sa, sizeof(sa));
  if (connect_result < 0) {
    ESP_LOGE(TAG, "Failed to connect to %s: %d (errno: %d - %s)", 
             ipv6.c_str(), connect_result, errno, strerror(errno));
    close(sock);
    return false;
  }
  ESP_LOGI(TAG, "Connected to [%s]:45 in %u ms", ipv6.c_str(), millis() - start_time);

  // Always send command with CRLF line ending as required by the protocol
  std::string request = command + "\r\n";
  int sent = 0, total_sent = 0;
  ESP_LOGI(TAG, "Sending %d bytes: %s", (int)request.length(), request.c_str());
  
  while (total_sent < (int)request.length()) {
    sent = send(sock, request.c_str() + total_sent, request.length() - total_sent, 0);
    if (sent < 0) {
      ESP_LOGE(TAG, "Failed to send command: %d (%s)", errno, strerror(errno));
      close(sock);
      return false;
    }
    total_sent += sent;
  }
  ESP_LOGI(TAG, "Successfully sent %d bytes in %u ms", total_sent, millis() - start_time);

  // Receive the response
  char buffer[512];
  memset(buffer, 0, sizeof(buffer));
  ESP_LOGI(TAG, "Waiting for response...");
  int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
  if (bytes_received < 0) {
    ESP_LOGE(TAG, "Failed to receive response: %d (%s)", errno, strerror(errno));
    close(sock);
    return false;
  }

  response = std::string(buffer, bytes_received);
  success = true;
  
  ESP_LOGI(TAG, "Received %d bytes in %u ms: %s", 
          bytes_received, millis() - start_time, response.c_str());
  
  // Always close the socket
  close(sock);
  
  return success;
}

void register_device(const std::string &name, const std::string &ipv6) {
  device_map[name] = ipv6;
  device_states[ipv6] = DeviceState();
  device_rot[ipv6] = 0;
}

const std::map<std::string, DeviceState>& get_device_states() {
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
    ESP_LOGI(TAG, "Successfully set volume to %.1f for device %s, response: %s", volume, ipv6.c_str(), response.c_str());
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
