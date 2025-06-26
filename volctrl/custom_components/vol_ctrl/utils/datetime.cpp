#include "datetime.h"
#include <time.h>
#include <cstring>

namespace esphome {
namespace vol_ctrl {
namespace utils {

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
