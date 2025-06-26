#include "esphome/core/log.h"
#include <TFT_eSPI.h>
#include "tft_hello_world.h"
#include <map>
#include <string>
#include "esphome/components/wifi/wifi_component.h"
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include "esphome/core/time.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include <time.h>

namespace esphome {
namespace tft_hello_world {

static const char *const TAG = "tft_hello_world";

static const std::map<std::string, std::string> DEVICE_MAP = {
  {"Left-6473470117", "2a00:1028:8390:75ee:2a36:38ff:fe61:25b9"},
  {"Right-6194478038", "2a00:1028:8390:75ee:2a36:38ff:fe61:279e"}
};

// Device status cache
enum DeviceStatus { UNKNOWN, UP, DOWN };
struct DeviceState {
  DeviceStatus status = UNKNOWN;
  uint32_t last_check = 0;
  bool standby = false;
  bool muted = false;
  float volume = 0.0f;
  int standby_time = 90;
};
static std::map<std::string, DeviceState> device_states;

// Rotating symbol state
static std::map<std::string, int> device_rot;
const char ROT_SYMBOLS[] = {'|', '/', '-', '\\'};
const int ROT_SYMBOLS_LEN = 4;

// Send SSC command to device
bool send_ssc_command(const std::string &ipv6, const std::string &command, std::string &response) {
  int sock = -1;
  bool success = false;
  
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
  if (inet_pton(AF_INET6, ipv6.c_str(), &sa.sin6_addr) != 1) {
    ESP_LOGE(TAG, "Invalid IPv6 address: %s", ipv6.c_str());
    close(sock);
    return false;
  }

  if (connect(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
    ESP_LOGW(TAG, "Failed to connect to %s: %d", ipv6.c_str(), errno);
    close(sock);
    return false;
  }

  // Send the command
  std::string request = command + "\r\n";
  int sent = 0, total_sent = 0;
  while (total_sent < request.length()) {
    sent = send(sock, request.c_str() + total_sent, request.length() - total_sent, 0);
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

// Simple JSON value extractor (not a full parser)
bool extract_json_value(const std::string &json, const std::string &key, std::string &value) {
  size_t key_pos = json.find("\"" + key + "\":");
  if (key_pos == std::string::npos) return false;
  
  size_t start_pos = key_pos + key.length() + 2; // Skip over the key and ":
  
  // Skip whitespace
  while (start_pos < json.length() && isspace(json[start_pos])) start_pos++;
  
  if (start_pos >= json.length()) return false;
  
  if (json[start_pos] == '"') {
    // String value
    start_pos++; // Skip the opening quote
    size_t end_pos = json.find('"', start_pos);
    if (end_pos == std::string::npos) return false;
    value = json.substr(start_pos, end_pos - start_pos);
  } else if (json[start_pos] == '{') {
    // Object value - find matching closing brace
    int depth = 1;
    size_t end_pos = start_pos + 1;
    while (depth > 0 && end_pos < json.length()) {
      if (json[end_pos] == '{') depth++;
      else if (json[end_pos] == '}') depth--;
      end_pos++;
    }
    if (depth != 0) return false;
    value = json.substr(start_pos, end_pos - start_pos);
  } else if (json[start_pos] == '[') {
    // Array value - find matching closing bracket
    int depth = 1;
    size_t end_pos = start_pos + 1;
    while (depth > 0 && end_pos < json.length()) {
      if (json[end_pos] == '[') depth++;
      else if (json[end_pos] == ']') depth--;
      end_pos++;
    }
    if (depth != 0) return false;
    value = json.substr(start_pos, end_pos - start_pos);
  } else {
    // Number, boolean, or null
    size_t end_pos = json.find_first_of(",}]", start_pos);
    if (end_pos == std::string::npos) return false;
    value = json.substr(start_pos, end_pos - start_pos);
    
    // Trim trailing whitespace
    while (!value.empty() && isspace(value.back())) value.pop_back();
  }
  
  return true;
}

// Simple function to check if a JSON response contains a boolean value
bool check_json_boolean(const std::string &response, const std::string &key, bool &result) {
  // Look for the key followed by true or false
  size_t pos = response.find(key);
  if (pos == std::string::npos) return false;
  
  // Look for true or false after the key
  size_t true_pos = response.find("true", pos);
  size_t false_pos = response.find("false", pos);
  
  if (true_pos != std::string::npos && (false_pos == std::string::npos || true_pos < false_pos)) {
    result = true;
    return true;
  } else if (false_pos != std::string::npos) {
    result = false;
    return true;
  }
  
  return false;
}

// Extract a number from a JSON response
bool extract_json_number(const std::string &response, const std::string &key, float &result) {
  size_t pos = response.find("\"" + key + "\":");
  if (pos == std::string::npos) return false;
  pos += key.length() + 3; // Skip past "key":
  size_t end_pos = response.find_first_of(",}", pos);
  if (end_pos == std::string::npos) return false;
  std::string value = response.substr(pos, end_pos - pos);
  // Remove whitespace
  size_t first = value.find_first_not_of(" \t\n\r");
  size_t last = value.find_last_not_of(" \t\n\r");
  if (first == std::string::npos || last == std::string::npos) return false;
  value = value.substr(first, last - first + 1);
  char* endptr = nullptr;
  result = std::strtof(value.c_str(), &endptr);
  if (endptr == value.c_str() || *endptr != '\0') return false;
  return true;
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
    // Use the JSON parser to extract standby status
    bool enabled = false;
    if (check_json_boolean(response, "enabled", enabled)) {
      state.standby = enabled;
    }
    
    // If we got a response, the device is UP
    state.status = UP;
    ESP_LOGI(TAG, "Device %s is UP, standby: %s", ipv6.c_str(), state.standby ? "true" : "false");
    return true;
  } else {
    state.status = DOWN;
    ESP_LOGW(TAG, "Device %s is DOWN", ipv6.c_str());
    return false;
  }
}

// Function to query device volume
bool get_device_volume(const std::string &ipv6, float &volume) {
  std::string response;
  bool success = send_ssc_command(ipv6, "{\"audio\":{\"out\":{\"level\":null}}}", response);
  if (success) {
    float level = 0.0f;
    if (extract_json_number(response, "level", level)) {
      volume = level;
      return true;
    }
  }
  return false;
}

// Function to query device mute status
bool get_device_mute_status(const std::string &ipv6, bool &muted) {
  std::string response;
  bool success = send_ssc_command(ipv6, "{\"audio\":{\"out\":{\"mute\":null}}}", response);
  if (success) {
    return check_json_boolean(response, "mute", muted);
  }
  return false;
}

// Function to query device auto standby time
bool get_device_standby_time(const std::string &ipv6, int &standby_time) {
  std::string response;
  bool success = send_ssc_command(ipv6, "{\"device\":{\"standby\":{\"auto_standby_time\":null}}}", response);
  if (success) {
    float time_value = 0.0f;
    if (extract_json_number(response, "auto_standby_time", time_value)) {
      standby_time = static_cast<int>(time_value);
      return true;
    }
  }
  return false;
}

// Find and use the time component to access RTC
static ESPTime last_time;

void TFTHelloWorld::setup() {
  ESP_LOGCONFIG(TAG, "Setting up TFT Hello World...");
  this->tft_ = new TFT_eSPI();
  this->tft_->init();
  this->tft_->setRotation(2);
  this->tft_->fillScreen(TFT_BLACK);
  this->tft_->setTextFont(4);  // Use smoother font 4
  this->tft_->setTextColor(TFT_WHITE, TFT_BLACK);
  this->tft_->setTextSize(1);
  this->tft_->setTextDatum(MC_DATUM);
  this->tft_->drawString("Volume Control", this->tft_->width() / 2, this->tft_->height() / 2);
  // Show connecting to wifi message below main text
  this->tft_->setTextSize(1);
  int y = this->tft_->height() / 2 + 40;
  this->tft_->drawString("Connecting to wifi", this->tft_->width() / 2, y);
  
  // Initialize all device states to unknown
  for (const auto &entry : DEVICE_MAP) {
    device_states[entry.second].status = UNKNOWN;
    device_states[entry.second].last_check = 0;
    device_rot[entry.second] = 0;
  }
  
  // Add a small delay to let things settle
  esphome::delay(500);
}

// Helper to format date/time string
std::string get_datetime_string() {
  time_t now = ::time(nullptr);
  if (now != 0) {
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char buf[32];
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int month = timeinfo.tm_mon;
    snprintf(buf, sizeof(buf), "%02d:%02d %s %02d", 
             timeinfo.tm_hour, timeinfo.tm_min,
             (month >= 0 && month < 12) ? months[month] : "---",
             timeinfo.tm_mday);
    return std::string(buf);
  }
  return "--:-- --- --";
}

void draw_wifi_icon(TFT_eSPI *tft, int x, int y, bool connected) {
  uint16_t color = connected ? TFT_GREEN : TFT_RED;
  
  // Draw a dot at the bottom center
  tft->fillCircle(x, y + 5, 2, color);
  
  // Draw three curves with increasing size to represent signal strength
  if (connected) {
    // Small arc
    tft->drawCircle(x, y + 5, 5, color);
    tft->fillRect(x - 5, y + 5, 10, 6, TFT_BLACK); // Erase bottom half
    
    // Medium arc 
    tft->drawCircle(x, y + 5, 9, color);
    tft->fillRect(x - 9, y + 5, 18, 10, TFT_BLACK); // Erase bottom half
    
    // Large arc
    tft->drawCircle(x, y + 5, 13, color);
    tft->fillRect(x - 13, y + 5, 28, 14, TFT_BLACK); // Erase bottom half
  } else {
    // Draw a cross for disconnected state
    tft->drawLine(x - 6, y - 6, x + 6, y + 6, TFT_RED);
    tft->drawLine(x + 6, y - 6, x - 6, y + 6, TFT_RED);
  }
}

void draw_speaker_dots(TFT_eSPI *tft, int x, int y, const std::map<std::string, DeviceState> &states) {
  int dx = 18;
  int i = 0;
  for (const auto &entry : DEVICE_MAP) {
    const std::string &ipv6 = entry.second;
    bool up = (states.count(ipv6) && states.at(ipv6).status == UP);
    bool standby = (up && states.at(ipv6).standby);
    
    // Different colors for different states
    uint16_t color;
    if (!up) {
      color = 0x8410; // gray for offline devices
    } else if (standby) {
      color = 0xFD20; // orange for standby mode
    } else {
      color = TFT_GREEN; // green for online and active
    }
    
    tft->fillCircle(x + i * dx, y, 7, color);
    tft->drawCircle(x + i * dx, y, 7, TFT_WHITE);
    
    // Show a small "M" if the device is muted
    if (up && states.at(ipv6).muted) {
      tft->setTextFont(1);  // Small font
      tft->setTextColor(TFT_BLACK);
      tft->setTextDatum(MC_DATUM);
      tft->drawString("M", x + i * dx, y);
    }
    
    i++;
  }
}

void draw_forbidden_icon(TFT_eSPI *tft, int x, int y) {
  tft->drawCircle(x, y, 24, TFT_RED);
  tft->drawLine(x-17, y-17, x+17, y+17, TFT_RED);
}

void draw_zzz_icon(TFT_eSPI *tft, int x, int y) {
  tft->setTextFont(4);  // Use smoother font 4
  tft->setTextColor(TFT_CYAN, TFT_BLACK);
  tft->setTextSize(1);
  tft->drawString("Zzz", x, y);
}

void draw_top_line(TFT_eSPI *tft, bool wifi_connected, const std::map<std::string, DeviceState> &states, int standby_time, const std::string &datetime) {
  tft->setTextFont(2);  // Use smaller smooth font 2 for header
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextSize(1);  // Normal size but smaller font
  int y = 12;  // Adjusted position for the smaller font
  // Standby time (left)
  char buf[16];
  snprintf(buf, sizeof(buf), "%dm", standby_time);
  tft->drawString(buf, 15, y);
  // WiFi icon (left of datetime)
  draw_wifi_icon(tft, 60, y+5, wifi_connected);
  // Speaker dots (left)
  draw_speaker_dots(tft, 90, y+5, states);
  // Date/time (right)
  tft->setTextDatum(TR_DATUM);
  tft->drawString(datetime.c_str(), tft->width()-5, y-8);
  tft->setTextDatum(MC_DATUM);
}

void draw_bottom_line(TFT_eSPI *tft, const std::string &status, bool normal) {
  tft->setTextFont(4);  // Use smoother font 4
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  int y = tft->height() - 28;  // Adjusted y position for larger font
  if (normal) {
    tft->setTextSize(1);  // Reduced size since font 4 is larger
    tft->drawString("Press for menu", tft->width()/2, y);
  } else {
    tft->setTextSize(1);  // Reduced size since font 4 is larger
    tft->drawString(status.c_str(), tft->width()/2, y);
  }
}

void draw_middle_area(TFT_eSPI *tft, float volume, bool muted, bool standby) {
  int y = tft->height()/2 - 8;  // Moved 8 pixels up
  tft->setTextFont(8);  // Use the largest smooth font for the volume display
  tft->setTextColor(TFT_YELLOW, TFT_BLACK);
  tft->setTextSize(2);
  
  char buf[8];
  // Handle the case when no speakers are connected (volume is 0)
  if (volume == 0.0f && device_states.empty()) {
    snprintf(buf, sizeof(buf), "--");
  } else {
    snprintf(buf, sizeof(buf), "%.0f", volume);
  }
  
  tft->drawString(buf, tft->width()/2, y);
  
  // Display muted and standby icons
  if (muted) draw_forbidden_icon(tft, tft->width()/2, y);
  if (standby) draw_zzz_icon(tft, tft->width()/2, y-40);
  
  // Draw volume bar below the number
  if (!device_states.empty()) {
    int bar_width = tft->width() - 40;  // Leave 20px margin on each side
    int bar_height = 8;
    int bar_y = y + 50;
    int fill_width = 0;
    
    // Volume ranges from -60 to 0 dB in SSC protocol
    if (volume >= -60 && volume <= 0) {
      fill_width = (int)((volume + 60) / 60.0f * bar_width);
    }
    
    // Draw background bar
    tft->fillRect(20, bar_y, bar_width, bar_height, 0x4208); // Dark gray
    // Draw filled part
    if (fill_width > 0) {
      uint16_t bar_color = TFT_GREEN;
      if (volume > -15) bar_color = TFT_YELLOW;  // Getting loud
      if (volume > -5) bar_color = TFT_RED;      // Very loud
      tft->fillRect(20, bar_y, fill_width, bar_height, bar_color);
    }
  }
}

void TFTHelloWorld::loop() {
  // State tracking variables
  static bool wifi_connected_prev = false;
  static bool muted_prev = false;
  static bool standby_prev = false;
  static float volume_prev = -999.0;  // Invalid value to force first update
  static int standby_time_prev = -1;  // Invalid value to force first update
  static std::string status = "Connecting to WiFi";
  static std::string datetime = "12:34 Jun 24";
  static std::string datetime_prev = "";
  static bool normal = false;
  static bool normal_prev = false;
  static uint32_t last_redraw = 0;
  static uint32_t last_device_check = 0;
  static uint32_t last_detail_check = 0;
  static bool first_run = true;
  
  uint32_t now = millis();
  bool wifi_connected = wifi::global_wifi_component->is_connected();
  bool need_redraw = false;
  bool devices_changed = false;
  bool muted = false;
  bool standby = false;
  float volume = 0.0f;
  int standby_time = 90; // Default value
  int active_device_count = 0;
  
  // Check for active devices every 2 seconds
  if (now - last_device_check > 2000 || first_run) {
    last_device_check = now;
    for (const auto &entry : DEVICE_MAP) {
      bool was_up = (device_states.count(entry.second) && device_states[entry.second].status == UP);
      bool is_up = is_device_up(entry.second);
      if (was_up != is_up) devices_changed = true;
      if (is_up) active_device_count++;
    }
  }
  
  // Check device details every 5 seconds
  if ((now - last_detail_check > 5000 || first_run) && wifi_connected) {
    last_detail_check = now;
    // Find first active device to get settings from
    for (const auto &entry : DEVICE_MAP) {
      const std::string &ipv6 = entry.second;
      if (device_states.count(ipv6) && device_states[ipv6].status == UP) {
        // Query volume
        float new_volume = 0.0f;
        if (get_device_volume(ipv6, new_volume)) {
          if (volume_prev != new_volume) need_redraw = true;
          volume = new_volume;
          device_states[ipv6].volume = new_volume;
        }
        
        // Query mute status
        bool new_muted = false;
        if (get_device_mute_status(ipv6, new_muted)) {
          if (muted_prev != new_muted) need_redraw = true;
          muted = new_muted;
          device_states[ipv6].muted = new_muted;
        }
        
        // Standby time
        int new_standby_time = 90;
        if (get_device_standby_time(ipv6, new_standby_time)) {
          if (standby_time_prev != new_standby_time) need_redraw = true;
          standby_time = new_standby_time;
          device_states[ipv6].standby_time = new_standby_time;
        }
        
        // Standby status already fetched in is_device_up
        if (device_states[ipv6].standby != standby_prev) need_redraw = true;
        standby = device_states[ipv6].standby;
        
        break; // Only need data from one active device
      }
    }
  }
  
  // Only update screen every 100ms to avoid flicker
  if (now - last_redraw < 100 && !first_run && !need_redraw) return;
  last_redraw = now;
  first_run = false;
  
  // Update status based on connection state
  if (!wifi_connected) {
    status = "Connecting to WiFi";
    normal = false;
  } else if (active_device_count == 0) {
    status = "No speakers connected";
    normal = false;
  } else {
    normal = true;
  }
  
  // Always update datetime from RTC
  datetime = get_datetime_string();
  
  // Determine if redraw is needed
  bool need_full_redraw = wifi_connected != wifi_connected_prev || 
                          datetime != datetime_prev ||
                          normal != normal_prev ||
                          devices_changed ||
                          muted != muted_prev ||
                          standby != standby_prev ||
                          volume != volume_prev ||
                          standby_time != standby_time_prev ||
                          first_run;
  
  // Perform redraw if needed
  if (need_full_redraw) {
    this->tft_->fillScreen(TFT_BLACK);
    draw_top_line(this->tft_, wifi_connected, device_states, standby_time, datetime);
    draw_middle_area(this->tft_, volume, muted, standby);
    draw_bottom_line(this->tft_, status, normal);
    
    // Update previous values
    wifi_connected_prev = wifi_connected;
    datetime_prev = datetime;
    normal_prev = normal;
    muted_prev = muted;
    standby_prev = standby;
    volume_prev = volume;
    standby_time_prev = standby_time;
  }
}

void TFTHelloWorld::dump_config() {
  ESP_LOGCONFIG(TAG, "TFT Hello World component configured.");
}

}  // namespace tft_hello_world
}  // namespace esphome