#pragma once

#include "esphome/core/component.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/output/float_output.h"
#include <map>
#include <string>
#include "device_state.h"
#include "network.h"

// Forward-declare the TFT_eSPI class instead of including the whole header
class TFT_eSPI;

namespace esphome {
namespace vol_ctrl {

// Main VolCtrl component class
class VolCtrl : public Component, public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, 
                                                       spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_40MHZ> {
 public:
  // Standard ESPHome methods
  void setup() override;
  void loop() override;
  void dump_config() override;
  
  float get_setup_priority() const override { return esphome::setup_priority::AFTER_CONNECTION; }
  
  // User interface methods
  void volume_change(const std::string &ipv6, float requested_volume);
  void toggle_mute();
  void enter_menu();
  void exit_menu();
  
  // Set backlight control pin
  void set_backlight_pin(output::FloatOutput *backlight_pin) { backlight_pin_ = backlight_pin; }
  void set_volume_step(float step) { volume_step_ = step; }
  
  // Menu navigation methods
  void menu_up();
  void menu_down();
  void menu_select();
  
  // Direct volume setting for Home Assistant
  void set_volume_from_hass(float level);
  void volume_change_from_hass(float diff);

  // Process encoder changes by directly querying speakers for current volume
  void process_encoder_change(int diff);

  // Helper for HA services
  const std::map<std::string, DeviceState>& get_device_states() {
    return network::get_device_states();
  }

 protected:
  // TFT display instance
  TFT_eSPI *tft_{nullptr};
  
  // UI state tracking
  uint32_t last_device_check_{0};
  uint32_t last_detail_check_{0};
  
  // Menu state
  bool in_menu_{false};
  int menu_level_{0};  // 0 = main menu, 1 = submenu, etc.
  int menu_position_{0};
  int menu_items_count_{0};
  float volume_step_{1.0f};  // Default 1dB steps
  uint32_t last_menu_toggle_{0};  // Timestamp of last menu enter/exit to prevent rapid toggling
  
  // Display settings
  int backlight_level_{100};  // 0-100%
  int display_timeout_{60};  // In seconds
  uint32_t last_interaction_{0};
  bool display_active_{true};
  
  // Backlight control
  output::FloatOutput *backlight_pin_{nullptr};

  // Rate limiting for volume changes
  uint32_t last_volume_change_{0}; // Timestamp of last volume change to rate limit
  bool user_adjusting_volume_{false}; // Flag to indicate user is actively changing volume
  
};

}  // namespace vol_ctrl
}  // namespace esphome
