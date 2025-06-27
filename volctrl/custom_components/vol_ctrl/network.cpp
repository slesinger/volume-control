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
  
  ESP_LOGI(TAG, "Attempting to connect to [%s]:45", ipv6.c_str());

  // Create socket
  sock = socket(AF_INET6, SOCK_STREAM, 0);
  if (sock < 0) {
    ESP_LOGE(TAG, "Failed to create socket: %d", errno);
    return false;
  }

  // Set socket options
  struct timeval timeout;
  timeout.tv_sec = 1;  // 1 second timeout (shorter for better UI responsiveness)
  timeout.tv_usec = 0;
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
    ESP_LOGE(TAG, "Failed to set receive timeout: %d", errno);
    close(sock);
    return false;
  }
  
  if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
    ESP_LOGE(TAG, "Failed to set send timeout: %d", errno);
    close(sock);
    return false;
  }

  // Connect to the device
  struct sockaddr_in6 sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin6_family = AF_INET6;
  sa.sin6_port = htons(45);  // Default SSC port is 45
  int pton_result = inet_pton(AF_INET6, ipv6.c_str(), &sa.sin6_addr);
  ESP_LOGI(TAG, "inet_pton result: %d", pton_result);
  if (pton_result != 1) {
    ESP_LOGE(TAG, "Invalid IPv6 address format: %s", ipv6.c_str());
    close(sock);
    return false;
  }

  int connect_result = connect(sock, (struct sockaddr *)&sa, sizeof(sa));
  ESP_LOGI(TAG, "connect() result: %d, errno: %d", connect_result, errno);
  if (connect_result < 0) {
    ESP_LOGE(TAG, "Failed to connect to %s: %d (errno: %d)", ipv6.c_str(), connect_result, errno);
    close(sock);
    return false;
  }

  // Always send command with CRLF line ending as required by the protocol
  std::string request = command + "\r\n";
  int sent = 0, total_sent = 0;
  while (total_sent < (int)request.length()) {
    sent = send(sock, request.c_str() + total_sent, request.length() - total_sent, 0);
    ESP_LOGI(TAG, "send() returned: %d, errno: %d", sent, errno);
    if (sent < 0) {
      ESP_LOGE(TAG, "Failed to send command: %d", errno);
      close(sock);
      return false;
    }
    total_sent += sent;
  }

  // Receive the response
  char buffer[512];
  memset(buffer, 0, sizeof(buffer));
  int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
  ESP_LOGI(TAG, "recv() returned: %d, errno: %d", bytes_received, errno);
  if (bytes_received < 0) {
    ESP_LOGE(TAG, "Failed to receive response: %d", errno);
    close(sock);
    return false;
  }

  response = std::string(buffer, bytes_received);
  success = true;
  
  // Always close the socket
  close(sock);
  
  return success;
}

bool is_device_up(const std::string &ipv6) {
  uint32_t now = millis();
  auto &state = device_states[ipv6];
  
  // Check every 10 seconds
  if (state.status != UNKNOWN && now - state.last_check < 10000) {
    return state.status == UP;
  }
  
  state.last_check = now;
  
  // Rotate symbol for this device
  device_rot[ipv6] = (device_rot[ipv6] + 1) % ROT_SYMBOLS_LEN;
  
  // Try to connect and send an SSC command
  std::string response;
  // Query if the device is in standby mode
  bool success = send_ssc_command(ipv6, "{\"device\":{\"standby\":{\"enabled\":null}}}", response);
  
  if (success) {
    state.status = UP;
    // Extract standby status from response
    bool standby_enabled;
    if (utils::check_json_boolean(response, "enabled", standby_enabled)) {
      state.standby = standby_enabled;
    }
    return true;
  } else {
    state.status = DOWN;
    return false;
  }
}

bool get_device_volume(const std::string &ipv6, float &volume) {
  std::string response;
  bool success = send_ssc_command(ipv6, "{\"audio\":{\"out\":{\"level\":null}}}", response);
  
  if (success) {
    return utils::extract_json_number(response, "level", volume);
  }
  return false;
}

bool get_device_mute_status(const std::string &ipv6, bool &muted) {
  std::string response;
  bool success = send_ssc_command(ipv6, "{\"audio\":{\"out\":{\"mute\":null}}}", response);
  
  if (success) {
    return utils::check_json_boolean(response, "mute", muted);
  }
  return false;
}

bool get_device_standby_time(const std::string &ipv6, int &standby_time) {
  std::string response;
  bool success = send_ssc_command(ipv6, "{\"device\":{\"standby\":{\"auto_standby_time\":null}}}", response);
  
  if (success) {
    float time_float;
    if (utils::extract_json_number(response, "auto_standby_time", time_float)) {
      standby_time = static_cast<int>(time_float);
      return true;
    }
  }
  return false;
}

void register_device(const std::string &name, const std::string &ipv6) {
  device_map[name] = ipv6;
  device_states[ipv6] = DeviceState();
  device_rot[ipv6] = 0;
}

const std::map<std::string, DeviceState>& get_device_states() {
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
