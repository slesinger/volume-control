#include <string>
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
#include "utils.h"
#include "esphome/core/log.h"
#include <cstring>
#include <time.h>

namespace esphome {
namespace vol_ctrl {
namespace utils {

static const char *const TAG = "vol_ctrl.utils";

bool extract_json_value(const std::string &json, const std::string &key, std::string &value) {
  size_t key_pos = json.find("\"" + key + "\":");
  if (key_pos == std::string::npos) return false;
  
  size_t start_pos = key_pos + key.length() + 2; // Skip over the key and ":
  
  // Skip whitespace
  while (start_pos < json.length() && isspace(json[start_pos])) start_pos++;
  
  if (start_pos >= json.length()) return false;
  
  if (json[start_pos] == '"') {
    // String value
    start_pos++;  // Skip opening quote
    size_t end_pos = json.find("\"", start_pos);
    if (end_pos == std::string::npos) return false;
    value = json.substr(start_pos, end_pos - start_pos);
  } else {
    // Non-string value (number, boolean, or object/array)
    size_t end_pos = json.find_first_of(",}", start_pos);
    if (end_pos == std::string::npos) return false;
    value = json.substr(start_pos, end_pos - start_pos);
    // Trim whitespace
    size_t first_non_space = value.find_first_not_of(" \t\r\n");
    size_t last_non_space = value.find_last_not_of(" \t\r\n");
    if (first_non_space != std::string::npos && last_non_space != std::string::npos) {
      value = value.substr(first_non_space, last_non_space - first_non_space + 1);
    }
  }
  
  return true;
}

bool check_json_boolean(const std::string &response, const std::string &key, bool &result) {
  ESP_LOGD(TAG, "Checking for boolean key '%s' in JSON: %s", key.c_str(), response.c_str());
  // Look for the key with proper JSON format
  std::string key_pattern = "\"" + key + "\":";
  size_t pos = response.find(key_pattern);
  if (pos == std::string::npos) {
    ESP_LOGW(TAG, "Key '%s' not found in JSON response", key.c_str());
    return false;
  }
  
  // Move position to after the key and colon
  pos += key_pattern.length();
  
  // Skip any whitespace
  while (pos < response.length() && (response[pos] == ' ' || response[pos] == '\t' || 
         response[pos] == '\n' || response[pos] == '\r')) {
    pos++;
  }
  
  // Check if we have enough characters left
  if (pos + 4 >= response.length()) {
    ESP_LOGW(TAG, "Not enough characters after key '%s'", key.c_str());
    return false;
  }
  
  // Check for "true" or "false" specifically at this position
  if (response.compare(pos, 4, "true") == 0) {
    result = true;
    ESP_LOGD(TAG, "Found value 'true' for key '%s'", key.c_str());
    return true;
  } else if (response.compare(pos, 5, "false") == 0) {
    result = false;
    ESP_LOGD(TAG, "Found value 'false' for key '%s'", key.c_str());
    return true;
  }
  
  ESP_LOGW(TAG, "Value for key '%s' is neither 'true' nor 'false'", key.c_str());
  return false;
  
  return false;
}

bool extract_json_number(const std::string &response, const std::string &key, float &result) {
  ESP_LOGD(TAG, "Extracting '%s' from JSON: %s", key.c_str(), response.c_str());
  size_t pos = response.find("\"" + key + "\":");
  if (pos == std::string::npos) {
    ESP_LOGE(TAG, "Key '%s' not found in response", key.c_str());
    return false;
  }
  
  pos += key.length() + 3; // Skip past "key":
  size_t end_pos = response.find_first_of(",}", pos);
  if (end_pos == std::string::npos) {
    ESP_LOGE(TAG, "End of value for '%s' not found", key.c_str());
    return false;
  }
  
  std::string value = response.substr(pos, end_pos - pos);
  ESP_LOGD(TAG, "Raw extracted value for '%s': '%s'", key.c_str(), value.c_str());
  
  // Remove whitespace
  size_t first = value.find_first_not_of(" \t\n\r");
  size_t last = value.find_last_not_of(" \t\n\r");
  if (first == std::string::npos || last == std::string::npos) {
    ESP_LOGE(TAG, "Value for '%s' contains only whitespace", key.c_str());
    return false;
  }
  
  value = value.substr(first, last - first + 1);
  ESP_LOGD(TAG, "Trimmed value for '%s': '%s'", key.c_str(), value.c_str());
  
  char* endptr = nullptr;
  result = std::strtof(value.c_str(), &endptr);
  if (endptr == value.c_str() || *endptr != '\0') {
    ESP_LOGE(TAG, "Failed to convert '%s' to float", value.c_str());
    return false;
  }
  
  ESP_LOGD(TAG, "Successfully extracted %s = %.2f", key.c_str(), result);
  return true;
}

std::string get_datetime_string() {
  time_t now = ::time(nullptr);
  if (now != 0) {
    struct tm timeinfo;
    char buf[32];
    localtime_r(&now, &timeinfo);
    
    // Month abbreviations
    static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", 
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int month = timeinfo.tm_mon;
    
    snprintf(buf, sizeof(buf), "%02d:%02d %s %02d", 
             timeinfo.tm_hour, timeinfo.tm_min,
             (month >= 0 && month < 12) ? months[month] : "---",
             timeinfo.tm_mday);
    return std::string(buf);
  }
  return "--:-- --- --";
}

}  // namespace utils
}  // namespace vol_ctrl
}  // namespace esphome
