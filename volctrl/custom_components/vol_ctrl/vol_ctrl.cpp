#include "vol_ctrl.h"
#include "esphome/core/log.h"
#include <TFT_eSPI.h>
#include "esphome/components/wifi/wifi_component.h"
#include "device_state.h"
#include "display.h"
#include "network.h"
#include "utils.h"
#include "wiim_pro.h"
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
  
  // Initialize WiiM Pro instance
  wiim_pro_.init();
  
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
  if (loop_state == LoopState::WAIT_FOR_WIFI) {
    std::map<std::string, DeviceState>& device_states = const_cast<std::map<std::string, DeviceState>&>(network::get_device_states());  // get list of devices and its states

    // 500ms after last encoder change, we can process the accumulated changes
    for (auto &entry : device_states) {
      DeviceState &state = entry.second;
      float requested_vol = state.get_requested_volume();
      float last_sent_vol = state.get_last_sent_volume();
      if (requested_vol > 0.0f && now - this->last_volume_change_ >= 300 && !in_menu_) {  // there was some rotary change
        // Only call volume_change if requested volume is different from last sent volume
        if (fabs(requested_vol - last_sent_vol) > 1e-4) {
          const std::string &ipv6 = entry.first;
          volume_change(ipv6, requested_vol);
          state.set_last_sent_volume(requested_vol);
        }
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
        state.set_requested_volume(-1.0f);
        network::DeviceVolStdbyData current_device_data;
        bool is_up = network::get_device_data(ipv6, current_device_data);
        is_up_changed |= state.set_is_up(is_up);
        standby_countdown_changed = state.set_standby_countdown(current_device_data.standby_countdown);
        volume_changed = state.set_volume(current_device_data.volume);
        mute_changed = state.set_mute(current_device_data.mute);
        last_state = &state; // keep reference to the last processed state
      }
      
      if (!in_menu_) {
        // Update changed values on display (every 5sec)
        if (standby_countdown_changed && last_state)
          esphome::vol_ctrl::display::update_standby_time(this->tft_, last_state->standby_countdown);
        if (is_up_changed)
          esphome::vol_ctrl::display::update_speaker_dots(this->tft_, device_states);
        esphome::vol_ctrl::display::update_datetime(this->tft_, utils::get_datetime_string());
        if (volume_changed && last_state)
          esphome::vol_ctrl::display::update_volume_display(this->tft_, last_state->volume);
        if (mute_changed && last_state)
          esphome::vol_ctrl::display::update_mute_status(this->tft_, last_state->muted, last_state->volume);
        esphome::vol_ctrl::display::update_status_message(this->tft_, "Long-press for menu");
        esphome::vol_ctrl::display::update_wifi_status(this->tft_, wifi_connected);
      }
    }
  }
}  // end of loop()

void VolCtrl::update_whole_screen() {
  std::map<std::string, DeviceState>& device_states = const_cast<std::map<std::string, DeviceState>&>(network::get_device_states());  // get list of devices and its states
  DeviceState* last_state = nullptr;
  for (auto &entry : device_states) {  // for every known device
    const std::string &ipv6 = entry.first;
    DeviceState &state = entry.second;
    network::DeviceVolStdbyData current_device_data;
    bool is_up = network::get_device_data(ipv6, current_device_data);
    state.set_is_up(is_up);
    state.set_standby_countdown(current_device_data.standby_countdown);
    state.set_volume(current_device_data.volume);
    state.set_mute(current_device_data.mute);
    last_state = &state; // keep reference to the last processed state
  }
  this->tft_->fillScreen(TFT_BLACK);
  esphome::vol_ctrl::display::update_standby_time(this->tft_, last_state->standby_countdown);
  esphome::vol_ctrl::display::update_speaker_dots(this->tft_, device_states);
  esphome::vol_ctrl::display::update_datetime(this->tft_, utils::get_datetime_string());
  esphome::vol_ctrl::display::update_volume_display(this->tft_, last_state->volume);
  esphome::vol_ctrl::display::update_mute_status(this->tft_, last_state->muted, last_state->volume);
  esphome::vol_ctrl::display::update_status_message(this->tft_, "Long-press for menu");
  esphome::vol_ctrl::display::update_wifi_status(this->tft_, wifi::global_wifi_component->is_connected());
}

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
}

void VolCtrl::button_released() {
  uint32_t press_duration = millis() - button_press_time_;
  if (press_duration > 300) {  // long press threshold
    ESP_LOGI(TAG, "Long press detected (%ums)", press_duration);
    enter_menu();
  } else {
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
      esphome::vol_ctrl::display::update_mute_status(this->tft_, false, state.get_volume());
    } else {
      state.set_mute(true);
      network::set_device_mute(ipv6, true);
      esphome::vol_ctrl::display::update_mute_status(this->tft_, true, state.get_volume());
    }
  }
  
}

void VolCtrl::mute() {
  ESP_LOGI(TAG, "Muting all speakers");
  std::map<std::string, DeviceState>& device_states = const_cast<std::map<std::string, DeviceState>&>(network::get_device_states());
  for (auto &entry : device_states) {
    const std::string &ipv6 = entry.first;
    network::set_device_mute(ipv6, true);
  }
}

void VolCtrl::unmute() {
  ESP_LOGI(TAG, "Unmuting all speakers");
  std::map<std::string, DeviceState>& device_states = const_cast<std::map<std::string, DeviceState>&>(network::get_device_states());
  for (auto &entry : device_states) {
    const std::string &ipv6 = entry.first;
    network::set_device_mute(ipv6, false);
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
    update_whole_screen();
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
          menu_items_count_ = 4;
        } else if (menu_position_ == 4) {
          // Discover devices
          // This needs to trigger a new network discovery
          // TODO: Implement discovery trigger
        } else if (menu_position_ == 5) {
          // Speaker parameters submenu
          menu_level_ = 1;  // Enter speaker parameters submenu
          menu_position_ = 0;
          menu_items_count_ = 6;
        } else if (menu_position_ == 6) {
          // Volume settings submenu
          menu_level_ = 1;  // Enter volume settings submenu
          menu_position_ = 0;
          menu_items_count_ = 7;
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
    ESP_LOGI("vol_ctrl", "Menu: level=%d, position=%d, items=%d", menu_level_, menu_position_, menu_items_count_);
    // Redraw the menu screen
    esphome::vol_ctrl::display::draw_menu_screen(this->tft_, menu_level_, menu_position_, menu_items_count_);
  }
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
    state.set_last_sent_volume(level);
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

void VolCtrl::pause() {
  ESP_LOGI(TAG, "Pause/Play toggle command received");
  
  // Try to pause/play toggle WiiM devices first
  if (wiim_pro_.pause_play_toggle()) {
    ESP_LOGI(TAG, "Successfully sent pause/play toggle command to WiiM device");
  } else {
    ESP_LOGI(TAG, "Pause/Play toggle command - not implemented for KH speakers");
    // KH speakers don't have pause functionality as they are monitors
    // This could be extended to control connected audio sources if needed
  }
}

void VolCtrl::next() {
  ESP_LOGI(TAG, "Next command received");
  
  // Try to send next command to WiiM devices first
  if (wiim_pro_.next()) {
    ESP_LOGI(TAG, "Successfully sent next command to WiiM device");
  } else {
    ESP_LOGI(TAG, "Next command - not implemented for KH speakers");
    // KH speakers don't have track control functionality as they are monitors
    // This could be extended to control connected audio sources if needed
  }
}

void VolCtrl::cycle_input() {
  ESP_LOGI(TAG, "Cycle input command received");
  
  // Try to cycle input on WiiM devices first
  if (wiim_pro_.cycle_input()) {
    ESP_LOGI(TAG, "Successfully cycled input on WiiM device");
  } else {
    ESP_LOGW(TAG, "Failed to cycle input on WiiM device");
  }
}

void VolCtrl::set_input(const std::string &input) {
  ESP_LOGI(TAG, "Set input command received: %s", input.c_str());
  
  // Try to set input on WiiM devices first
  if (wiim_pro_.set_input(input)) {
    ESP_LOGI(TAG, "Successfully set input to '%s' on WiiM device", input.c_str());
  } else {
    ESP_LOGW(TAG, "Failed to set input to '%s' on WiiM device", input.c_str());
  }
}

}  // namespace vol_ctrl
}  // namespace esphome
