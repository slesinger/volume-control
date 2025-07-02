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
uint32_t button_press_time_ = 0;

enum class LoopState {
  WAIT_FOR_WIFI,
  MAIN_LOOP,
};
LoopState loop_state = LoopState::WAIT_FOR_WIFI;


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

// This loop() logically loops only occasionally. there are inner loops that loop in various frequencies. Variable loop_state tells what inner loop to enter. It is necessary to exit loop swiftly else RTOS will restart the ESP.
void VolCtrl::loop() {
  uint32_t now = millis();
  bool wifi_connected = wifi::global_wifi_component->is_connected();
  
  // Wait for wifi to connect before proceeding
  if (!wifi_connected) {
      esphome::vol_ctrl::display::update_status_message(this->tft_, "Connecting to WiFi");
      esphome::vol_ctrl::display::update_wifi_status(this->tft_, wifi_connected);
      esphome::delay(1000);
      return;  // exit loop() to satisfy ESP watchdog timer
  }

  // Loop as frequently as possible to keep the UI responsive
  // Rotary encoder changes are read by esphome, see yaml lambda
  if (loop_state == LoopState::WAIT_FOR_WIFI) {  // TODO this handles case when not in menu mode
    std::map<std::string, DeviceState>& device_states = const_cast<std::map<std::string, DeviceState>&>(network::get_device_states());  // get list of devices and its states

    // 500ms after last encoder change, we can process the accumulated changes
    for (auto &entry : device_states) {
      DeviceState &state = entry.second;
      float requested_vol = state.get_requested_volume();
      if (requested_vol > 0.0f && now - this->last_volume_change_ >= 500 && !in_menu_) {  // there was some rotary change
        const std::string &ipv6 = entry.first;
        volume_change(ipv6, requested_vol);
        state.set_requested_volume(-1.0f);
      }
      else
        break;
    }
    if (now - this->last_volume_change_ >= 2000 && !in_menu_) {  // reset
      this->last_volume_change_ = now;
    }

    // every 5 seconds, we check the device states and update the display if needed
    if (now - this->last_device_check_ > 4000) {
      bool is_up_changed = false;
      bool standby_countdown_changed = false;
      bool volume_changed = false;
      bool mute_changed = false;
      this->last_device_check_ = now;
      DeviceState* last_state = nullptr;
      for (auto &entry : device_states) {  // for every known device
        const std::string &ipv6 = entry.first;
        DeviceState &state = entry.second;
        network::DeviceVolStdbyData current_device_data;
        bool is_up = network::get_device_data(ipv6, current_device_data);
        is_up_changed |= state.set_is_up(is_up);
        standby_countdown_changed = state.set_standby_countdown(current_device_data.standby_countdown);
        volume_changed = state.set_volume(current_device_data.volume);
        mute_changed = state.set_mute(current_device_data.mute);
        last_state = &state; // keep reference to the last processed state
      }
      
      // Update changed values on display (every 5sec)
      if (standby_countdown_changed && last_state)
        esphome::vol_ctrl::display::update_standby_time(this->tft_, last_state->standby_countdown);
      if (is_up_changed)
        esphome::vol_ctrl::display::update_speaker_dots(this->tft_, device_states);
      esphome::vol_ctrl::display::update_datetime(this->tft_, utils::get_datetime_string());
      if (volume_changed && last_state)
        esphome::vol_ctrl::display::update_volume_display(this->tft_, last_state->volume);
      if (mute_changed && last_state)
        esphome::vol_ctrl::display::update_mute_status(this->tft_, last_state->muted);
      esphome::vol_ctrl::display::update_status_message(this->tft_, "Long-press for menu");
      esphome::vol_ctrl::display::update_wifi_status(this->tft_, wifi_connected);
    }
  }
}  // end of loop()

// Handle volume change based on encoder ticks. It can be positive or negative.
// If in menu mode, it will navigate the menu instead.
// If volume is not initialized yet, it will do nothing.
void VolCtrl::volume_change(const std::string &ipv6, float requested_volume) {
  // If we're in menu mode, use this for menu navigation
  if (in_menu_) {
    ESP_LOGI(TAG, "Menu navigation - down");  // TODO this has to be UP also
    menu_down();
    return;
  }
  
  if (requested_volume < 0.0) {  // volume is not initialized yet
    return;
  }
// TODO make constant setting for maximum volume
  if (requested_volume > 120.0) {  // volume is over limit
    return;
  }
  network::set_device_volume(ipv6, requested_volume);
}

void VolCtrl::button_pressed() {
  button_press_time_ = millis();
  ESP_LOGI(TAG, "press detected");
}

void VolCtrl::button_released() {
  uint32_t press_duration = millis() - button_press_time_;
  if (press_duration > 300) {  // long press threshold
    ESP_LOGI(TAG, "Long press detected (%ums)", press_duration);
    enter_menu();
  } else {
    ESP_LOGI(TAG, "Short press detected (%ums)", press_duration);
    toggle_mute();
  }
}

void VolCtrl::toggle_mute() {
  // If we're in menu mode, use this as a select button
  if (in_menu_) {
    ESP_LOGI(TAG, "Button pressed in menu - selecting item");
    menu_select();
    return;
  }
  
  ESP_LOGI(TAG, "Toggling mute state");
  // Determine the current mute state from one of the speakers
  auto &device_states = const_cast<std::map<std::string, DeviceState>&>(network::get_device_states());
  for (auto &entry : device_states) {
    const std::string &ipv6 = entry.first;
    DeviceState &state = entry.second;
    if (state.muted) {
      state.set_mute(false);
      network::set_device_mute(ipv6, false);
    } else {
      state.set_mute(true);
      network::set_device_mute(ipv6, true);
    }
  }
  
}

void VolCtrl::enter_menu() {
  uint32_t now = millis();
  
  if (!in_menu_) {
    ESP_LOGI(TAG, "Entering menu");
    in_menu_ = true;
    menu_level_ = 0;
    menu_position_ = 0;
    menu_items_count_ = 7; // Number of items in main menu
    
    // Draw the menu
    display::draw_menu_screen(this->tft_, menu_level_, menu_position_, menu_items_count_);
  }
}

void VolCtrl::exit_menu() {
  if (in_menu_) {
    ESP_LOGI(TAG, "Exiting menu");
    in_menu_ = false;
    menu_level_ = 0;
    menu_position_ = 0;
    
    // Force a full redraw when exiting menu
    // TODO create full redraw function
    // this->first_run_ = true;
  }
}

void VolCtrl::menu_up() {
  if (in_menu_) {
    int prev_position = menu_position_;
    menu_position_--;
    if (menu_position_ < 0) {
      menu_position_ = menu_items_count_ - 1;
    }
    
    // Update the menu display
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
    
    // Update the menu display
    display::draw_menu_item_highlight(this->tft_, menu_position_, prev_position);
  }
}

void VolCtrl::menu_select() {
  if (in_menu_) {
    ESP_LOGI(TAG, "Selected menu item %d", menu_position_);
    
    switch (menu_level_) {
      case 0:  // Main menu
        if (menu_position_ == 0) {
          // Exit menu
          exit_menu();
          return;
        } else if (menu_position_ == 1) {
          // Show devices
          // TODO: Implement device listing screen
        } else if (menu_position_ == 2) {
          // Show settings
          // TODO: Implement settings screen
        } else if (menu_position_ == 3) {
          // Parametric EQ submenu
          menu_level_ = 1;  // Enter EQ submenu
          menu_position_ = 0;
          menu_items_count_ = 3; // .. (back), Show curve, Add
        } else if (menu_position_ == 4) {
          // Discover devices
          // This needs to trigger a new network discovery
          // TODO: Implement discovery trigger
        } else if (menu_position_ == 5) {
          // Speaker parameters submenu
          menu_level_ = 2;  // Enter speaker parameters submenu
          menu_position_ = 0;
          // TODO: Implement speaker parameters submenu
        } else if (menu_position_ == 6) {
          // Volume settings submenu
          menu_level_ = 3;  // Enter volume settings submenu
          menu_position_ = 0;
          // TODO: Implement volume settings submenu
        }
        break;
        
      case 1:  // EQ submenu
        if (menu_position_ == 0) {
          // Back to main menu
          menu_level_ = 0;
          menu_position_ = 0;
          menu_items_count_ = 7;
        }
        // TODO: Implement other EQ submenu items
        break;
        
      case 2:  // Speaker parameters submenu
        if (menu_position_ == 0) {
          // Back to main menu
          menu_level_ = 0;
          menu_position_ = 0;
          menu_items_count_ = 7;
        }
        // TODO: Implement other speaker parameters submenu items
        break;
        
      case 3:  // Volume settings submenu
        if (menu_position_ == 0) {
          // Back to main menu
          menu_level_ = 0;
          menu_position_ = 0;
          menu_items_count_ = 7;
        }
        // TODO: Implement other volume settings submenu items
        break;
    }
    
    // Redraw the menu screen
    this->tft_->fillScreen(TFT_BLACK);
    this->tft_->setTextFont(4);
    this->tft_->setTextColor(TFT_WHITE, TFT_BLACK);
    
    // Draw the appropriate menu based on the current level
    switch (menu_level_) {
      case 0:  // Main menu
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
        
      case 1:  // EQ submenu
        this->tft_->drawString("EQ MENU", 10, 10);
        this->tft_->setTextFont(2);
        this->tft_->drawString("..", 20, 50);
        this->tft_->drawString("1. Show curve", 20, 70);
        this->tft_->drawString("2. Add EQ", 20, 90);
        break;
        
      case 2:  // Speaker parameters submenu
        this->tft_->drawString("SPEAKER SETTINGS", 10, 10);
        this->tft_->setTextFont(2);
        this->tft_->drawString("..", 20, 50);
        this->tft_->drawString("1. Logo brightness", 20, 70);
        this->tft_->drawString("2. Set delay", 20, 90);
        this->tft_->drawString("3. Standby timeout", 20, 110);
        this->tft_->drawString("4. Auto standby", 20, 130);
        break;
        
      case 3:  // Volume settings submenu
        this->tft_->drawString("VOLUME SETTINGS", 10, 10);
        this->tft_->setTextFont(2);
        this->tft_->drawString("..", 20, 50);
        this->tft_->drawString("1. Volume step", 20, 70);
        this->tft_->drawString("2. Backlight", 20, 90);
        this->tft_->drawString("3. Display timeout", 20, 110);
        this->tft_->drawString("4. ESP sleep", 20, 130);
        break;
    }
    
    // Highlight the selected item
    this->tft_->fillRect(10, 50 + (menu_position_ * 20), 10, 10, TFT_YELLOW);
  }
}

void VolCtrl::dump_config() {
  ESP_LOGCONFIG(TAG, "Volume Control:");
}

// This function is only called from Home Assistant service
void VolCtrl::set_volume_from_hass(float level) {
  // Ignore volume setting when in menu
  
  ESP_LOGI(TAG, "Setting volume from Home Assistant to %.1f", level);
  
  // Cap volume level to valid range
  if (level < 0.0f || level > 120.0f)  // TODO use configurable constant
    return;

  std::map<std::string, DeviceState>& device_states = const_cast<std::map<std::string, DeviceState>&>(network::get_device_states());
  for (auto &entry : device_states) {
    DeviceState &state = entry.second;
    const std::string &ipv6 = entry.first;
    volume_change(ipv6, level);
  }
}

// Diff can be negative, see yaml lambda
void VolCtrl::volume_change_from_hass(float diff) {
  std::map<std::string, DeviceState>& device_states = const_cast<std::map<std::string, DeviceState>&>(network::get_device_states());
  for (auto &entry : device_states) {
    DeviceState &state = entry.second;
    const std::string &ipv6 = entry.first;
    float current_volume = state.get_volume();
    if (current_volume < 0.0f) {
      return;
    }
    set_volume_from_hass(current_volume + diff);
  }

}

void VolCtrl::process_encoder_change(int diff) {
  // Ignore encoder input when in menu
  if (in_menu_) {
    // Use encoder for menu navigation
    if (diff > 0) {
      menu_down();  // Move menu selection down
    } else if (diff < 0) {
      menu_up();    // Move menu selection up
    }
    return;
  }

  if (fabs(diff) > 10) {
    return;  // Ignore very large changes
  }
  this->last_volume_change_ = millis();  // volume will commit since last encoder change
  this->last_device_check_ = millis();  // reset device check timer to force update display
  // Not in menu mode, so process volume change
  std::map<std::string, DeviceState>& device_states = const_cast<std::map<std::string, DeviceState>&>(network::get_device_states());  // get list of devices and its states
  for (auto &entry : device_states) { 
    DeviceState &state = entry.second;
    float requested_vol = state.get_requested_volume();
    if (requested_vol < 0.0f) {
      float vol = state.get_volume();
      if (vol < 0.0f) {
        ESP_LOGI(TAG, "Requested volume is not set and volume not yet read from device, ignoring encoder change");
        return;  // No valid volume to change
      } else {
        // Since volume from device was confirmed yellow, this is the first rotation diff
        requested_vol = vol;
      }
    }
    requested_vol = requested_vol + diff;  // TODO handle sensitivity well here
    state.set_requested_volume(requested_vol);
    esphome::vol_ctrl::display::update_volume_display(this->tft_, requested_vol, true);
  }
}

}  // namespace vol_ctrl
}  // namespace esphome
