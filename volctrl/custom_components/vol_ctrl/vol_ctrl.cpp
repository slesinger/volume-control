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
#include <esp_sleep.h>

namespace esphome {
namespace vol_ctrl {

static const char *const TAG = "vol_ctrl";
uint32_t button_press_time_ = 0;
WiimPro wiim_pro_;

void VolCtrl::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Volume Control...");
  
  // Initialize the display
  this->tft_ = new TFT_eSPI();
  this->tft_->init();
  this->tft_->setRotation(0);
  this->tft_->fillScreen(TFT_BLACK);
  
  // Show startup message immediately
  esphome::vol_ctrl::display::update_status_message(this->tft_, "Starting up...");
  
  // Initialize backlight if configured
  if (this->backlight_pin_ != nullptr) {
    ESP_LOGCONFIG(TAG, "Setting backlight to 100%%");
    this->backlight_pin_->set_level(1.0);
  }
  
  // Initialize network subsystem (non-blocking)
  network::init();  // this registers speaker's IPv6 addresses
  
  main_loop_counter = millis();
  
  // Add a small delay to let things settle
  esphome::delay(500);
  update_whole_screen();
}


void VolCtrl::loop() {
  uint32_t now = millis();
  bool wifi_connected = wifi::global_wifi_component->is_connected();
  // Wait for wifi to connect before proceeding
  if (!wifi_connected) {
      esphome::vol_ctrl::display::update_status_message(this->tft_, "Connecting to WiFi");
      esphome::vol_ctrl::display::update_wifi_status(this->tft_, wifi_connected);
      return;  // exit loop() to satisfy ESP watchdog timer
  }

  // WiFi is connected at this stage
  // Loop as frequently as possible to keep the UI responsive
  // Rotary encoder changes are read by esphome, see yaml lambda
  const std::map<std::string, DeviceState>& device_states_const = network::get_device_states();
  std::map<std::string, DeviceState>& device_states = const_cast<std::map<std::string, DeviceState>&>(device_states_const);  // get list of devices and its states

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
  if (now - this->last_volume_change_ >= 2000 && !in_menu_) {  // reset time to commit the volume
    this->last_volume_change_ = now;
  }

  // every 30 seconds, we check the device states and update the display if needed
  // Give more time on the first check after WiFi connects
  if (now - this->main_loop_counter > 30000) {
    bool is_up_changed = false;
    bool standby_countdown_changed = false;
    bool volume_changed = false;
    bool mute_changed = false;
    this->main_loop_counter = now;
    DeviceState* last_state = nullptr;
    
    // Process devices one at a time and yield between each to prevent watchdog timeout
    static size_t device_index = 0;
    
    if (device_states.size() > 0) {
      auto it = device_states.begin();
      std::advance(it, device_index % device_states.size());
      
      const std::string &ipv6 = it->first;
      DeviceState &state = it->second;
      state.set_requested_volume(-1.0f);
      
      ESP_LOGD(TAG, "Checking device status for %s", ipv6.c_str());
      
      // Feed the watchdog before potentially blocking network call
      esphome::yield();
      
      network::DeviceVolStdbyData current_device_data;
      bool is_up = network::get_device_data(ipv6, current_device_data);
      
      // Feed the watchdog after network call
      esphome::yield();
      
      is_up_changed |= state.set_is_up(is_up);
      standby_countdown_changed = state.set_standby_countdown(current_device_data.standby_countdown);
      volume_changed = state.set_volume(current_device_data.volume);
      mute_changed = state.set_mute(current_device_data.mute);
      last_state = &state;
      
      device_index++; // Move to next device for next iteration
      
      ESP_LOGD(TAG, "Device %s status: %s", ipv6.c_str(), is_up ? "online" : "offline");
    }

    wiim_pro_.try_reconnect();   // this is fast if connected

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
      esphome::vol_ctrl::display::update_wiim_status(this->tft_, wiim_pro_.is_available());
    }
    
    // Check if all speakers are unavailable for deep sleep timeout
    if (deep_sleep_timeout_ > 0) {  // Only check if deep sleep timeout is enabled
      bool any_speaker_available = false;
      
      // Check only regular speakers (WiiM is irrelevant for deep sleep)
      for (const auto &entry : device_states) {
        if (entry.second.is_up) {
          any_speaker_available = true;
          break;
        }
      }
      
      if (any_speaker_available) {
        // Reset the unavailable timer if any speaker is available
        speakers_unavailable_since_ = 0;
      } else {
        // All speakers are unavailable
        if (speakers_unavailable_since_ == 0) {
          // First time all speakers became unavailable
          speakers_unavailable_since_ = now;
          ESP_LOGI(TAG, "All speakers unavailable, starting deep sleep countdown (timeout: %d seconds)", deep_sleep_timeout_);
        } else {
          // Check if we've been without speakers for the timeout duration
          uint32_t unavailable_duration = (now - speakers_unavailable_since_) / 1000; // Convert to seconds
          if (unavailable_duration >= deep_sleep_timeout_) {
            ESP_LOGI(TAG, "All speakers unavailable for %d seconds, entering deep sleep", unavailable_duration);
            deep_sleep();
          } else {
            ESP_LOGD(TAG, "All speakers unavailable for %d seconds (timeout: %d seconds)", unavailable_duration, deep_sleep_timeout_);
          }
        }
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
    
    // Yield after processing each device to prevent watchdog timeout
    esphome::yield();
  }
  this->tft_->fillScreen(TFT_BLACK);
  esphome::vol_ctrl::display::update_standby_time(this->tft_, last_state->standby_countdown);
  esphome::vol_ctrl::display::update_speaker_dots(this->tft_, device_states);
  esphome::vol_ctrl::display::update_datetime(this->tft_, utils::get_datetime_string());
  esphome::vol_ctrl::display::update_volume_display(this->tft_, last_state->volume);
  esphome::vol_ctrl::display::update_mute_status(this->tft_, last_state->muted, last_state->volume);
  esphome::vol_ctrl::display::update_status_message(this->tft_, "Long-press for menu");
  esphome::vol_ctrl::display::update_wifi_status(this->tft_, wifi::global_wifi_component->is_connected());
  esphome::vol_ctrl::display::update_wiim_status(this->tft_, wiim_pro_.is_available());
}

// Handle volume change based on encoder ticks. It can be positive or negative.
// If in menu mode, it will navigate the menu instead.
// If volume is not initialized yet, it will do nothing.
void VolCtrl::volume_change(const std::string &ipv6, float requested_volume) {
  // Reset deep sleep timer on user interaction
  speakers_unavailable_since_ = 0;
  
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
  // Yield control after network operation to prevent watchdog timeout
  esphome::yield();
}

void VolCtrl::button_pressed() {
  // Reset deep sleep timer on user interaction
  speakers_unavailable_since_ = 0;
  button_press_time_ = millis();
}

void VolCtrl::button_released() {
  // If in brightness adjustment mode, exit it
  if (adjusting_brightness_) {
    exit_brightness_adjustment();
    return;
  }
  
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
  
  // First, determine what the new state should be
  bool should_mute = false;
  for (auto &entry : device_states) {
    DeviceState &state = entry.second;
    if (!state.muted) {  // If any device is not muted, we should mute all
      should_mute = true;
      break;
    }
  }
  set_mute(should_mute);
}

void VolCtrl::mute() {
  set_mute(true);
}

void VolCtrl::unmute() {
  set_mute(false);
}

void VolCtrl::set_mute(bool new_mute) {
  ESP_LOGI(TAG, "Muting all speakers %d", new_mute);
  std::map<std::string, DeviceState>& device_states = const_cast<std::map<std::string, DeviceState>&>(network::get_device_states());
  
  // Update local state immediately
  for (auto &entry : device_states) {
    network::set_device_mute(entry.first, new_mute);
    DeviceState &state = entry.second;
    state.set_mute(new_mute);
    esphome::vol_ctrl::display::update_mute_status(this->tft_, new_mute, state.get_volume());
    esphome::yield();
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
    adjusting_brightness_ = false;  // Reset brightness adjustment mode
    
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
        } else if (menu_position_ == 1) {
          // Volume step adjustment
          // TODO: Implement volume step adjustment
        } else if (menu_position_ == 2) {
          // Backlight intensity adjustment
          ESP_LOGI(TAG, "Entering brightness adjustment mode");
          adjusting_brightness_ = true;
          esphome::vol_ctrl::display::draw_brightness_adjustment_screen(this->tft_, backlight_level_);
          return; // Don't redraw menu
        } else if (menu_position_ == 3) {
          // Display timeout adjustment
          // TODO: Implement display timeout adjustment
        } else if (menu_position_ == 4) {
          // Deep sleep timeout adjustment
          ESP_LOGI(TAG, "Entering deep sleep timeout adjustment mode");
          // TODO: Implement deep sleep timeout adjustment screen similar to brightness
          // For now, cycle through common timeout values: 5min, 10min, 15min, 30min, disabled
          static const int timeout_values[] = {300, 600, 900, 1800, 0}; // seconds (0 = disabled)
          static const int num_timeouts = sizeof(timeout_values) / sizeof(timeout_values[0]);
          
          // Find current timeout index
          int current_index = 0;
          for (int i = 0; i < num_timeouts; i++) {
            if (timeout_values[i] == deep_sleep_timeout_) {
              current_index = i;
              break;
            }
          }
          
          // Move to next timeout value
          current_index = (current_index + 1) % num_timeouts;
          deep_sleep_timeout_ = timeout_values[current_index];
          
          // Log the change
          if (deep_sleep_timeout_ == 0) {
            ESP_LOGI(TAG, "Deep sleep timeout disabled");
          } else {
            ESP_LOGI(TAG, "Deep sleep timeout set to %d minutes", deep_sleep_timeout_ / 60);
          }
          
          // Reset the unavailable timer since we changed the setting
          speakers_unavailable_since_ = 0;
        }
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
    set_volume_from_hass(current_volume + (diff /2 ));
  }

}

void VolCtrl::process_encoder_change(int diff) {
  // Reset deep sleep timer on user interaction
  speakers_unavailable_since_ = 0;
  
  // Handle brightness adjustment mode
  if (adjusting_brightness_) {
    // Adjust brightness in steps of 5%
    int new_brightness = backlight_level_ + (diff * 5);
    
    // Clamp to valid range
    if (new_brightness < 0) new_brightness = 0;
    if (new_brightness > 100) new_brightness = 100;
    
    // Only update if brightness actually changed
    if (new_brightness != backlight_level_) {
      set_display_brightness(new_brightness);
      esphome::vol_ctrl::display::draw_brightness_adjustment_screen(this->tft_, backlight_level_);
    }
    return;
  }
  
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
  this->main_loop_counter = millis();  // reset device check timer to force update display
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
  
  // Check if WiiM features are available before attempting command
  if (wiim_pro_.is_available()) {
    if (wiim_pro_.pause_play_toggle()) {
      ESP_LOGI(TAG, "Successfully sent pause/play toggle command to WiiM device");
    } else {
      ESP_LOGW(TAG, "Failed to send pause/play toggle command to WiiM device");
    }
  } else {
    ESP_LOGD(TAG, "WiiM device not available - pause/play feature disabled");
    ESP_LOGI(TAG, "Pause/Play toggle command - not implemented for KH speakers");
    // KH speakers don't have pause functionality as they are monitors
    // This could be extended to control connected audio sources if needed
  }
}

void VolCtrl::next() {
  ESP_LOGI(TAG, "Next command received");
  
  // Check if WiiM features are available before attempting command
  if (wiim_pro_.is_available()) {
    if (wiim_pro_.next()) {
      ESP_LOGI(TAG, "Successfully sent next command to WiiM device");
    } else {
      ESP_LOGW(TAG, "Failed to send next command to WiiM device");
    }
  } else {
    ESP_LOGD(TAG, "WiiM device not available - next track feature disabled");
    ESP_LOGI(TAG, "Next command - not implemented for KH speakers");
    // KH speakers don't have track control functionality as they are monitors
    // This could be extended to control connected audio sources if needed
  }
}

void VolCtrl::cycle_input() {
  ESP_LOGI(TAG, "Cycle input command received");
  
  // Check if WiiM features are available before attempting command
  if (wiim_pro_.is_available()) {
    if (wiim_pro_.cycle_input()) {
      ESP_LOGI(TAG, "Successfully cycled input on WiiM device");
    } else {
      ESP_LOGW(TAG, "Failed to cycle input on WiiM device");
    }
  } else {
    ESP_LOGD(TAG, "WiiM device not available - input cycling feature disabled");
  }
}

void VolCtrl::set_input(const std::string &input) {
  ESP_LOGI(TAG, "Set input command received: %s", input.c_str());
  
  // Check if WiiM features are available before attempting command
  if (wiim_pro_.is_available()) {
    if (wiim_pro_.set_input(input)) {
      ESP_LOGI(TAG, "Successfully set input to '%s' on WiiM device", input.c_str());
    } else {
      ESP_LOGW(TAG, "Failed to set input to '%s' on WiiM device", input.c_str());
    }
  } else {
    ESP_LOGD(TAG, "WiiM device not available - input setting feature disabled");
  }
}

std::string VolCtrl::get_current_input() {
  // Check if WiiM features are available before attempting to get input
  if (wiim_pro_.is_available()) {
    return wiim_pro_.get_current_input();
  } else {
    ESP_LOGD(TAG, "WiiM device not available - returning default input");
    return "Network"; // Default fallback when device is offline
  }
}

void VolCtrl::set_display_brightness(int brightness) {
  // Clamp brightness to valid range (0-100%)
  if (brightness < 0) brightness = 0;
  if (brightness > 100) brightness = 100;
  
  backlight_level_ = brightness;
  
  // If backlight pin is configured, update the PWM output
  if (this->backlight_pin_ != nullptr) {
    // Convert percentage (0-100) to float range (0.0-1.0)
    float level = brightness / 100.0f;
    
    ESP_LOGI(TAG, "Setting display brightness to %d%% (%.2f)", brightness, level);
    this->backlight_pin_->set_level(level);
  } else {
    ESP_LOGW(TAG, "Backlight pin not configured, cannot set brightness");
  }
}

void VolCtrl::exit_brightness_adjustment() {
  if (adjusting_brightness_) {
    ESP_LOGI(TAG, "Exiting brightness adjustment mode");
    adjusting_brightness_ = false;
    
    // Return to volume settings submenu
    menu_level_ = 1;
    menu_position_ = 0;
    menu_items_count_ = 7;
    
    // Redraw the menu screen
    esphome::vol_ctrl::display::draw_menu_screen(this->tft_, menu_level_, menu_position_, menu_items_count_);
  }
}

void VolCtrl::deep_sleep() {
  ESP_LOGI(TAG, "Entering deep sleep mode...");
  
  // Turn off the display
  if (this->tft_ != nullptr) {
    this->tft_->fillScreen(TFT_BLACK);
    this->tft_->writecommand(0x10); // Enter sleep mode
  }
  
  // Turn off backlight
  if (this->backlight_pin_ != nullptr) {
    ESP_LOGI(TAG, "Turning off backlight");
    this->backlight_pin_->set_level(0.0);
  }
  
  // Configure wake-up source - wake up on GPIO25 (encoder button) press
  // GPIO25 is the encoder button according to the YAML config
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_25, 0); // Wake on LOW (button pressed, considering pullup)
  
  ESP_LOGI(TAG, "Configured wake-up on GPIO25 (encoder button)");
  ESP_LOGI(TAG, "Starting deep sleep now...");
  
  // Small delay to ensure log message is sent
  esphome::delay(100);
  
  // Enter deep sleep
  esp_deep_sleep_start();
}

}  // namespace vol_ctrl
}  // namespace esphome
