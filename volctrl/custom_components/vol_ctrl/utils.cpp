#include "utils.h"
#include <cstring>
#include <time.h>

namespace esphome {
namespace vol_ctrl {
namespace utils {

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
