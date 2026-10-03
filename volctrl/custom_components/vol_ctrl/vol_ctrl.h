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
  void update_whole_screen();

  // User interface methods
  void button_pressed();
  void button_released();
  void toggle_mute();
  void enter_menu();
  void exit_menu();

  // Configuration
  void set_backlight_pin(output::FloatOutput *backlight_pin) { backlight_pin_ = backlight_pin; }
  void set_max_volume(float max_volume) { max_volume_ = max_volume; }

  // Menu navigation methods
  void menu_up();
  void menu_down();
  void menu_select();

  // Home Assistant entry points. Ignored while the menu is open.
  void set_volume_from_hass(float level);
  void volume_change_from_hass(float diff);

  // Encoder entry point: diff is the number of detents turned (negative = counter-clockwise).
  void process_encoder_change(int diff);

 protected:
  struct PollResult {
    bool is_up_changed{false};
    bool standby_changed{false};
    bool mute_changed{false};
  };

  // Query all speakers and update their state. Failed polls only mark the speaker as down.
  PollResult poll_devices_();
  // Speaker whose values represent the group on screen (first reachable one, else the first).
  DeviceState *representative_state_();
  float clamp_volume_(float volume) const;
  // Clamp and send a volume to one speaker, updating its state. Skips speakers known to be down.
  bool apply_volume_(const std::string &ipv6, DeviceState &state, float volume);
  // Redraw the main screen; with force=true every region, otherwise only the regions flagged in `changed`.
  void draw_status_(const PollResult &changed, bool force);
  // Move every reachable speaker by `diff` dB, clamped to [0, max_volume_].
  void step_volume_(float diff);

  // TFT display instance
  TFT_eSPI *tft_{nullptr};

  // UI state tracking
  uint32_t last_device_check_{0};
  uint32_t last_wifi_draw_{0};
  bool force_redraw_{true};  // draw everything on the first poll, e.g. dots for speakers that start offline

  // Menu state
  bool in_menu_{false};
  int menu_level_{0};  // 0 = main menu, 1 = submenu, etc.
  int menu_position_{0};
  int menu_items_count_{0};

  // Upper bound for any volume sent to the speakers (dB)
  float max_volume_{120.0f};

  // Backlight control
  output::FloatOutput *backlight_pin_{nullptr};
};

}  // namespace vol_ctrl
}  // namespace esphome
