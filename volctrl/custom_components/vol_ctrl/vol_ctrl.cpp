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
  
  // Initialize backlight if configured
  if (this->backlight_pin_ != nullptr) {
    ESP_LOGCONFIG(TAG, "Setting backlight to 100%%");
    this->backlight_pin_->set_level(1.0);
  }
  
  // Initialize network subsystem
  network::init();
  
  // Set initial state
  uint32_t now = millis();
  last_interaction_ = now;
  last_menu_toggle_ = now;
  display_active_ = true;
  
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
  
  // Check if any display update is needed
  bool any_update = need_redraw || 
                    devices_changed || 
                    wifi_connected != this->wifi_connected_prev_ || 
                    this->datetime_ != this->datetime_prev_ ||
                    standby_time != this->standby_time_prev_ ||
                    volume != this->volume_prev_ ||
                    muted != this->muted_prev_ ||
                    standby != this->standby_prev_ ||
                    this->status_ != this->status_prev_ ||
                    this->normal_ != this->normal_prev_;
                    
  // Only update screen every 100ms to avoid flicker, unless we have specific changes
  if (now - this->last_redraw_ < 100 && !this->first_run_ && !any_update) return;
  
  // Log when we're doing a screen update and why
  if (any_update && !this->first_run_) {
    if (devices_changed) ESP_LOGD(TAG, "Screen update: Device status changed");
    if (wifi_connected != this->wifi_connected_prev_) ESP_LOGD(TAG, "Screen update: WiFi status changed");
    if (this->datetime_ != this->datetime_prev_) ESP_LOGD(TAG, "Screen update: DateTime changed");
    if (standby_time != this->standby_time_prev_) ESP_LOGD(TAG, "Screen update: Standby time changed");
    if (volume != this->volume_prev_) ESP_LOGD(TAG, "Screen update: Volume changed");
    if (muted != this->muted_prev_) ESP_LOGD(TAG, "Screen update: Mute status changed");
    if (standby != this->standby_prev_) ESP_LOGD(TAG, "Screen update: Standby status changed");
    if (this->status_ != this->status_prev_ || this->normal_ != this->normal_prev_) 
      ESP_LOGD(TAG, "Screen update: Status message changed");
  } else if (this->first_run_) {
    ESP_LOGD(TAG, "Screen update: Initial draw");
  } else if (now - this->last_redraw_ >= 100) {
    ESP_LOGD(TAG, "Screen update: Regular refresh");
  }
  
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
  
  // Only update screen if not in menu mode
  if (!in_menu_) {
    // Initial screen draw on first run
    if (this->first_run_) {
      this->tft_->fillScreen(TFT_BLACK);
      esphome::vol_ctrl::display::draw_top_line(this->tft_, wifi_connected, device_states, standby_time, this->datetime_);
      esphome::vol_ctrl::display::draw_middle_area(this->tft_, volume, muted, standby, volume_ok);
      esphome::vol_ctrl::display::draw_bottom_line(this->tft_, this->status_, this->normal_);
    } else {
      // Partial updates for changed elements only
      if (wifi_connected != this->wifi_connected_prev_) {
        esphome::vol_ctrl::display::update_wifi_status(this->tft_, 60, 17, wifi_connected, this->wifi_connected_prev_);
      }
      
      if (this->datetime_ != this->datetime_prev_) {
        esphome::vol_ctrl::display::update_datetime(this->tft_, this->datetime_, this->datetime_prev_);
      }
      
      if (standby_time != this->standby_time_prev_) {
        esphome::vol_ctrl::display::update_standby_time(this->tft_, standby_time, this->standby_time_prev_);
      }
      
      if (volume != this->volume_prev_) {
        esphome::vol_ctrl::display::update_volume_display(this->tft_, volume, this->volume_prev_, volume_ok);
      }
      
      if (muted != this->muted_prev_) {
        esphome::vol_ctrl::display::update_mute_status(this->tft_, muted, this->muted_prev_);
      }
      
      if (standby != this->standby_prev_) {
        esphome::vol_ctrl::display::update_standby_status(this->tft_, standby, this->standby_prev_);
      }
      
      if (this->status_ != this->status_prev_ || this->normal_ != this->normal_prev_) {
        esphome::vol_ctrl::display::update_status_message(this->tft_, this->status_, this->status_prev_, this->normal_, this->normal_prev_);
      }
      
      // Update speaker dots if device status changed
      if (devices_changed) {
        // Use the defined screen region for speaker dots
        esphome::vol_ctrl::display::ScreenRegion speaker_region = esphome::vol_ctrl::display::get_speaker_dots_region();
        // Clear only the needed area
        this->tft_->fillRect(speaker_region.x, speaker_region.y, speaker_region.w, speaker_region.h, TFT_BLACK);
        esphome::vol_ctrl::display::draw_speaker_dots(this->tft_, 90, 17 + 5, device_states);
      }
    }
    
    // Update previous values to avoid unnecessary redraws
    this->wifi_connected_prev_ = wifi_connected;
    this->datetime_prev_ = this->datetime_;
    this->status_prev_ = this->status_;
    this->normal_prev_ = this->normal_;
    this->muted_prev_ = muted;
    this->standby_prev_ = standby;
    this->volume_prev_ = volume;
    this->standby_time_prev_ = standby_time;
  }
}

// Volume control methods implementation
void VolCtrl::volume_increase() {
  // In menu mode, use rotary for navigation
  if (in_menu_) {
    ESP_LOGI(TAG, "Menu navigation - down");
    menu_down();
    return;
  }
  
  ESP_LOGI(TAG, "Increasing volume");
  
  // Iterate through all devices and increase volume
  const auto &states = network::get_device_states();
  for (const auto &entry : states) {
    const std::string &ipv6 = entry.first;
    const DeviceState &state = entry.second;
    
    // Only attempt to control devices that are up
    if (state.status == UP && !state.standby) {
      float new_volume = state.volume + this->volume_step_;
      // Cap at 120dB maximum
      if (new_volume > 120.0f) {
        new_volume = 120.0f;
      }
      
      std::string command = "{\"audio\":{\"out\":{\"level\":" + std::to_string(new_volume) + "}}}";
      std::string response;
      if (network::send_ssc_command(ipv6, command, response)) {
        ESP_LOGI(TAG, "Successfully set volume to %.1f for device %s", new_volume, ipv6.c_str());
      }
    }
  }
}

void VolCtrl::volume_decrease() {
  // In menu mode, use rotary for navigation
  if (in_menu_) {
    ESP_LOGI(TAG, "Menu navigation - up");
    menu_up();
    return;
  }
  
  ESP_LOGI(TAG, "Decreasing volume");
  
  // Iterate through all devices and decrease volume
  const auto &states = network::get_device_states();
  for (const auto &entry : states) {
    const std::string &ipv6 = entry.first;
    const DeviceState &state = entry.second;
    
    // Only attempt to control devices that are up
    if (state.status == UP && !state.standby) {
      float new_volume = state.volume - this->volume_step_;
      // Cap at 0dB minimum
      if (new_volume < 0.0f) {
        new_volume = 0.0f;
      }
      
      std::string command = "{\"audio\":{\"out\":{\"level\":" + std::to_string(new_volume) + "}}}";
      std::string response;
      if (network::send_ssc_command(ipv6, command, response)) {
        ESP_LOGI(TAG, "Successfully set volume to %.1f for device %s", new_volume, ipv6.c_str());
      }
    }
  }
}

void VolCtrl::toggle_mute() {
  // If in menu, treat button press as a select action
  if (in_menu_) {
    ESP_LOGI(TAG, "Button pressed in menu - selecting item");
    menu_select();
    return;
  }
  
  ESP_LOGI(TAG, "Toggling mute state");
  
  // Determine the current mute state - if any device is not muted, we mute all
  bool any_unmuted = false;
  const auto &states = network::get_device_states();
  for (const auto &entry : states) {
    if (entry.second.status == UP && !entry.second.standby && !entry.second.muted) {
      any_unmuted = true;
      break;
    }
  }
  
  // Toggle the mute state for all devices
  for (const auto &entry : states) {
    const std::string &ipv6 = entry.first;
    const DeviceState &state = entry.second;
    
    // Only attempt to control devices that are up
    if (state.status == UP && !state.standby) {
      // If any device is unmuted, mute all. Otherwise unmute all.
      std::string command = "{\"audio\":{\"out\":{\"mute\":" + std::string(any_unmuted ? "true" : "false") + "}}}";
      std::string response;
      if (network::send_ssc_command(ipv6, command, response)) {
        ESP_LOGI(TAG, "Successfully %s device %s", 
                any_unmuted ? "muted" : "unmuted", ipv6.c_str());
      }
    }
  }
}

void VolCtrl::enter_menu() {
  // Prevent rapid menu re-entry
  uint32_t now = millis();
  if (!in_menu_ && (now - this->last_menu_toggle_ > 1000)) {
    ESP_LOGI(TAG, "Entering menu");
    this->last_menu_toggle_ = now;
    in_menu_ = true;
    menu_level_ = 0;
    menu_position_ = 0;
    menu_items_count_ = 7; // Number of items in main menu
    
    // Draw menu screen
    display::draw_menu_screen(this->tft_, menu_level_, menu_position_, menu_items_count_);
  }
}

void VolCtrl::exit_menu() {
  if (in_menu_) {
    ESP_LOGI(TAG, "Exiting menu");
    this->last_menu_toggle_ = millis();
    in_menu_ = false;
    menu_level_ = 0;
    menu_position_ = 0;
    
    // We do need a full redraw when exiting the menu
    // But set first_run to true so the loop will handle it with the proper data
    this->first_run_ = true;
    
    // Reset the rotary encoder value to current volume to avoid unexpected jumps
    // when returning to volume control mode
    const auto &states = network::get_device_states();
    for (const auto &entry : states) {
      if (entry.second.status == UP && !entry.second.standby) {
        // Found an active device, use its current volume
        this->volume_prev_ = entry.second.volume;
        break;
      }
    }
  }
}

void VolCtrl::menu_up() {
  if (in_menu_) {
    int prev_position = menu_position_;
    menu_position_--;
    if (menu_position_ < 0) {
      menu_position_ = menu_items_count_ - 1;
    }
    
    // Update only the highlight, not the entire screen
    display::draw_menu_item_highlight(this->tft_, menu_position_, prev_position);
  }
}

void VolCtrl::menu_down() {
  if (in_menu_) {
    int prev_position = menu_position_;
    menu_position_++;
    if (menu_position_ >= menu_items_count_) {
      menu_position_ = 0;
    }
    
    // Update only the highlight, not the entire screen
    display::draw_menu_item_highlight(this->tft_, menu_position_, prev_position);
  }
}

void VolCtrl::menu_select() {
  if (in_menu_) {
    ESP_LOGI(TAG, "Selected menu item %d", menu_position_);
    
    switch (menu_level_) {
      case 0: // Main menu
        switch (menu_position_) {
          case 0: // Exit menu
            exit_menu();
            break;
          case 1: // Show devices
            // TODO: Implement device display screen
            break;
          case 2: // Show settings
            // TODO: Implement settings display
            break;
          case 3: // Parametric EQ
            // TODO: Implement EQ submenu
            menu_level_ = 1;
            menu_position_ = 0;
            menu_items_count_ = 3; // .. (back), Show curve, Add
            break;
          case 4: // Discover devices
            // TODO: Implement device discovery
            break;
          case 5: // Speaker parameters
            // TODO: Implement speaker parameter submenu
            menu_level_ = 2;
            menu_position_ = 0;
            menu_items_count_ = 5; // .. (back), Logo brightness, Delay, Standby timeout, Auto standby
            break;
          case 6: // Volume settings
            // TODO: Implement volume settings submenu
            menu_level_ = 3;
            menu_position_ = 0;
            menu_items_count_ = 5; // .. (back), Volume step, Backlight, Display timeout, ESP sleep
            break;
        }
        break;
        
      // Handle submenus
      case 1: // Parametric EQ submenu
        if (menu_position_ == 0) {
          // Back to main menu
          menu_level_ = 0;
          menu_position_ = 3; // Return to EQ position
          menu_items_count_ = 7;
        } else {
          // Handle EQ submenu items
        }
        break;
        
      case 2: // Speaker parameters submenu
        if (menu_position_ == 0) {
          // Back to main menu
          menu_level_ = 0;
          menu_position_ = 5; // Return to Speaker parameters position
          menu_items_count_ = 7;
        } else {
          // Handle speaker parameters submenu items
        }
        break;
        
      case 3: // Volume settings submenu
        if (menu_position_ == 0) {
          // Back to main menu
          menu_level_ = 0;
          menu_position_ = 6; // Return to Volume settings position
          menu_items_count_ = 7;
        } else {
          // Handle volume settings submenu items
        }
        break;
    }
    
    // Redraw menu after selection
    if (in_menu_) {
      this->tft_->fillScreen(TFT_BLACK);
      this->tft_->setTextFont(4);
      this->tft_->setTextColor(TFT_WHITE, TFT_BLACK);
      
      // Draw appropriate menu based on level
      switch (menu_level_) {
        case 0: // Main menu
          this->tft_->drawString("MENU", 10, 10);
          this->tft_->setTextFont(2);
          this->tft_->drawString("1. Exit menu", 20, 50);
          this->tft_->drawString("2. Show devices", 20, 70);
          this->tft_->drawString("3. Show settings", 20, 90);
          this->tft_->drawString("4. Parametric EQ", 20, 110);
          this->tft_->drawString("5. Discover devices", 20, 130);
          this->tft_->drawString("6. Speaker parameters", 20, 150);
          this->tft_->drawString("7. Volume settings", 20, 170);
          break;
          
        case 1: // Parametric EQ submenu
          this->tft_->drawString("EQ MENU", 10, 10);
          this->tft_->setTextFont(2);
          this->tft_->drawString("..", 20, 50);
          this->tft_->drawString("1. Show curve", 20, 70);
          this->tft_->drawString("2. Add EQ", 20, 90);
          break;
          
        case 2: // Speaker parameters submenu
          this->tft_->drawString("SPEAKER SETTINGS", 10, 10);
          this->tft_->setTextFont(2);
          this->tft_->drawString("..", 20, 50);
          this->tft_->drawString("1. Logo brightness", 20, 70);
          this->tft_->drawString("2. Set delay", 20, 90);
          this->tft_->drawString("3. Standby timeout", 20, 110);
          this->tft_->drawString("4. Auto standby", 20, 130);
          break;
          
        case 3: // Volume settings submenu
          this->tft_->drawString("VOLUME SETTINGS", 10, 10);
          this->tft_->setTextFont(2);
          this->tft_->drawString("..", 20, 50);
          this->tft_->drawString("1. Volume step", 20, 70);
          this->tft_->drawString("2. Backlight", 20, 90);
          this->tft_->drawString("3. Display timeout", 20, 110);
          this->tft_->drawString("4. ESP sleep", 20, 130);
          break;
      }
      
      // Highlight current selection
      this->tft_->fillRect(10, 50 + (menu_position_ * 20), 10, 10, TFT_YELLOW);
    }
  }
}

void VolCtrl::dump_config() {
  ESP_LOGCONFIG(TAG, "Volume Control component configured.");
}

void VolCtrl::set_volume(float level) {
  // Ignore volume setting commands when in menu
  if (in_menu_) {
    ESP_LOGI(TAG, "Ignoring volume set in menu mode");
    return;
  }
  
  ESP_LOGI(TAG, "Setting volume to %.1f", level);
  
  // Clamp volume to valid range
  if (level < 0.0f) level = 0.0f;
  if (level > 120.0f) level = 120.0f;
  
  // Iterate through all devices and set volume
  const auto &states = network::get_device_states();
  for (const auto &entry : states) {
    const std::string &ipv6 = entry.first;
    const DeviceState &state = entry.second;
    
    // Only attempt to control devices that are up
    if (state.status == UP && !state.standby) {
      std::string command = "{\"audio\":{\"out\":{\"level\":" + std::to_string(level) + "}}}";
      std::string response;
      if (network::send_ssc_command(ipv6, command, response)) {
        ESP_LOGI(TAG, "Successfully set volume to %.1f for device %s", level, ipv6.c_str());
      }
    }
  }
}

}  // namespace vol_ctrl
}  // namespace esphome
