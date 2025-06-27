#include "vol_ctrl.h"
#include "esphome/core/log.h"
#include <TFT_eSPI.h>
#include "esphome/components/wifi/wifi_component.h"
#include "device_state.h"
#include "display.h"
#include "network.h"
#include "utils.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace vol_ctrl {

static const char *const TAG = "vol_ctrl";

void VolCtrl::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Volume Control...");
  
  // Initialize the display
  this->tft_ = new TFT_eSPI();
  this->tft_->init();
  this->tft_->setRotation(2);
  this->tft_->fillScreen(TFT_BLACK);
  // Do not display anything yet, wait for main loop to draw the UI
  
  // Initialize network subsystem
  network::init();
  
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
  // Iterate directly through device states
  for (const auto &entry : states) {
    const std::string &ipv6 = entry.first;
    bool up = (entry.second.status == UP);
    bool standby = (up && entry.second.standby);
    
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
  int y = tft->height() - 28;  // Adjusted y position for larger font
  if (normal) {
    tft->setTextFont(4);  // Use smoother font 4
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->setTextSize(1);  // Reduced size since font 4 is larger
    tft->drawString("Press for menu", tft->width()/2, y);
  } else {
    tft->setTextFont(2);  // Use smaller font 2 for status messages to ensure they fit
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->setTextSize(1);
    tft->drawString(status.c_str(), tft->width()/2, y);
  }
}

void VolCtrl::loop() {
  
  uint32_t now = millis();
  bool wifi_connected = wifi::global_wifi_component->is_connected();
  bool need_redraw = false;
  bool devices_changed = false;
  bool muted = this->muted_prev_;
  bool standby = this->standby_prev_;
  float volume = this->volume_prev_;
  int standby_time = this->standby_time_prev_;
  int active_device_count = 0;
  bool volume_ok = false;
  
  // Get reference to device states
  const auto& device_states = network::get_device_states();
  
  // Check for active devices every 2 seconds
  if (now - this->last_device_check_ > 2000 || this->first_run_) {
    this->last_device_check_ = now;
    for (const auto &entry : device_states) {
      const std::string &ipv6 = entry.first;
      bool was_up = (entry.second.status == UP);
      bool is_up = network::is_device_up(ipv6);
      if (was_up != is_up) devices_changed = true;
      if (is_up) active_device_count++;
    }
  }
  
  // Check device details every 5 seconds
  if ((now - this->last_detail_check_ > 5000 || this->first_run_) && wifi_connected) {
    this->last_detail_check_ = now;
    for (const auto &entry : device_states) {
      const std::string &ipv6 = entry.first;
      if (entry.second.status == UP) {
        float new_volume = 0.0f;
        if (network::get_device_volume(ipv6, new_volume)) {
          if (this->volume_prev_ != new_volume) need_redraw = true;
          volume = new_volume;
          volume_ok = true;
        } else {
          volume = this->volume_prev_;
          volume_ok = false;
        }
        
        // Query mute status
        bool new_muted = false;
        if (network::get_device_mute_status(ipv6, new_muted)) {
          if (this->muted_prev_ != new_muted) need_redraw = true;
          muted = new_muted;
        }
        
        // Standby time
        int new_standby_time = 0;
        if (network::get_device_standby_time(ipv6, new_standby_time)) {
          if (this->standby_time_prev_ != new_standby_time) need_redraw = true;
          standby_time = new_standby_time;
        } else {
          // Keep previous value if we couldn't get a new one
          standby_time = this->standby_time_prev_;
        }
        
        // Standby status already fetched in is_device_up
        if (entry.second.standby != this->standby_prev_) need_redraw = true;
        standby = entry.second.standby;
        
        break; // Only need data from one active device
      }
    }
  }
  
  // Only update screen every 100ms to avoid flicker
  if (now - this->last_redraw_ < 100 && !this->first_run_ && !need_redraw) return;
  this->last_redraw_ = now;
  this->first_run_ = false;
  
  // Update status based on connection state
  if (!wifi_connected) {
    this->status_ = "Connecting to WiFi";
    this->normal_ = false;
  } else if (active_device_count == 0) {
    this->status_ = "No speakers connected";
    this->normal_ = false;
  } else {
    this->normal_ = true;
  }
  
  // Always update datetime from RTC
  this->datetime_ = utils::get_datetime_string();
  
  // Determine if redraw is needed
  bool need_full_redraw = wifi_connected != this->wifi_connected_prev_ || 
                          this->datetime_ != this->datetime_prev_ ||
                          this->normal_ != this->normal_prev_ ||
                          devices_changed ||
                          muted != this->muted_prev_ ||
                          standby != this->standby_prev_ ||
                          volume != this->volume_prev_ ||
                          standby_time != this->standby_time_prev_;
  
  // Perform redraw if needed
  if (need_full_redraw) {
    this->tft_->fillScreen(TFT_BLACK);
    esphome::vol_ctrl::display::draw_top_line(this->tft_, wifi_connected, device_states, standby_time, this->datetime_);
    esphome::vol_ctrl::display::draw_middle_area(this->tft_, volume, muted, standby, volume_ok);
    esphome::vol_ctrl::display::draw_bottom_line(this->tft_, this->status_, this->normal_);
    
    // Update previous values
    this->wifi_connected_prev_ = wifi_connected;
    this->datetime_prev_ = this->datetime_;
    this->normal_prev_ = this->normal_;
    this->muted_prev_ = muted;
    this->standby_prev_ = standby;
    this->volume_prev_ = volume;
    this->standby_time_prev_ = standby_time;
  }
}

void VolCtrl::dump_config() {
  ESP_LOGCONFIG(TAG, "Volume Control component configured.");
}

}  // namespace vol_ctrl
}  // namespace esphome
