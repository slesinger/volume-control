#pragma once

#include "esphome/core/component.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/output/float_output.h"
#include <map>
#include <string>
#include "device_state.h"
#include "network.h"
#include "wiim.h"
#include "esphome/core/preferences.h"

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
  void mute();
  void unmute();
  void set_mute(bool mute);  // all reachable speakers
  void enter_menu();
  void exit_menu();

  // Configuration
  void set_backlight_pin(output::FloatOutput *backlight_pin) { backlight_pin_ = backlight_pin; }
  void set_max_volume(float max_volume) { max_volume_ = max_volume; }
  void set_wiim_ip(const std::string &ip) { wiim_ip_ = ip; wiim_enabled_ = true; }
  float get_max_volume() const { return max_volume_; }

  // Display brightness (0-100 %), persisted in NVS
  void set_display_brightness(int brightness);
  int get_display_brightness() const { return backlight_level_; }

  // Deep sleep: after this many seconds without a reachable speaker (0 = never), persisted in NVS
  void set_deep_sleep_timeout(int seconds);
  int get_deep_sleep_timeout() const { return deep_sleep_timeout_; }
  void deep_sleep();  // wakes on the encoder button

  // Menu navigation methods
  void menu_up();
  void menu_down();
  void menu_select();

  // Home Assistant entry points. Ignored while the menu is open.
  void set_volume_from_hass(float level);
  void volume_change_from_hass(float diff);

  // WiiM streamer controls (no-ops while the WiiM is not configured or offline)
  void pause() { wiim::toggle_play(); }
  void next() { wiim::next(); }
  void previous() { wiim::previous(); }
  void cycle_input() { wiim::cycle_input(); }
  void set_input(const std::string &input) { wiim::set_input(input); }
  std::string get_current_input() { return wiim::get_status().input; }

  // State for Home Assistant entities (representative speaker). Volume is -1 while unknown.
  float get_volume();
  bool is_muted();

  // Encoder entry point: diff is the number of detents turned (negative = counter-clockwise).
  void process_encoder_change(int diff);

 protected:
  struct PollResult {
    bool is_up_changed{false};
    bool standby_changed{false};
    bool mute_changed{false};
    bool received{false}; // any poll result arrived (e.g. confirms the volume shown in blue)
  };

  // Fold the background poll results into the speaker states. Failed polls only mark the speaker as down.
  // Never blocks: all socket I/O happens on the network worker task.
  PollResult apply_poll_updates_();
  // Speaker whose values represent the group on screen (first reachable one, else the first).
  DeviceState *representative_state_();
  float clamp_volume_(float volume) const;
  // Clamp and queue a volume for one speaker, updating its state optimistically. Skips speakers known to be down.
  bool apply_volume_(const std::string &ipv6, DeviceState &state, float volume);
  // Redraw the main screen; with force=true every region, otherwise only the regions flagged in `changed`.
  void draw_status_(const PollResult &changed, bool force);
  // Move every reachable speaker by `diff` dB, clamped to [0, max_volume_].
  void step_volume_(float diff);

  // TFT display instance
  TFT_eSPI *tft_{nullptr};

  // UI state tracking
  uint32_t last_draw_{0};
  PollResult pending_changes_;  // changes folded in since the last redraw
  uint32_t last_wifi_draw_{0};
  bool force_redraw_{true};  // draw everything on the first poll, e.g. dots for speakers that start offline

  // Menu state
  bool in_menu_{false};
  int menu_level_{0};  // 0 = main menu, 1 = submenu, etc.
  int menu_position_{0};
  int menu_items_count_{0};

  // WiiM streamer
  bool wiim_enabled_{false};
  std::string wiim_ip_;  // empty = discover

  // Upper bound for any volume sent to the speakers (dB)
  float max_volume_{120.0f};

  // Backlight control
  output::FloatOutput *backlight_pin_{nullptr};
  int backlight_level_{100};  // 0-100 %
  bool adjusting_brightness_{false};  // menu brightness editor is open
  void apply_brightness_();
  void exit_brightness_adjustment();
  void draw_volume_settings_values_();
  void cycle_deep_sleep_timeout_();

  // Deep sleep
  int deep_sleep_timeout_{600};  // seconds, 0 = disabled
  uint32_t unavailable_since_{0};  // millis() when the last speaker went away, 0 = at least one is reachable
  void check_deep_sleep_(uint32_t now);

  // Settings persisted in NVS
  struct Settings
  {
    int brightness;
    int deep_sleep_timeout;
  };
  ESPPreferenceObject settings_pref_;
  void save_settings_();
};

}  // namespace vol_ctrl
}  // namespace esphome
