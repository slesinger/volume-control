#pragma once

#include "esphome/core/component.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/output/float_output.h"
#include <map>
#include <string>
#include "device_state.h"
#include "network.h"
#include "wiim.h"
#include "display.h"
#include "esphome/core/preferences.h"
#include "esphome/core/automation.h"
#include "esphome/core/gpio.h"
#include <functional>
#include <vector>

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
  
  // Before WiFi: the display comes up immediately instead of after the connection (setups of lower
  // priority wait for it). Everything that needs the network is deferred to loop() / the worker tasks.
  float get_setup_priority() const override { return esphome::setup_priority::HARDWARE; }
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
  // Upper limit from yaml; the menu can only lower the active max volume below it
  void set_max_volume(float max_volume) { max_volume_limit_ = max_volume; max_volume_ = max_volume; }
  void set_wiim_ip(const std::string &ip) { wiim_ip_ = ip; wiim_enabled_ = true; }
  float get_max_volume() const { return max_volume_; }

  // Display brightness (0-100 %), persisted in NVS
  void set_display_brightness(int brightness);
  int get_display_brightness() const { return backlight_level_; }

  // Deep sleep: after this many seconds without a reachable speaker (0 = never), persisted in NVS
  void set_deep_sleep_timeout(int seconds);
  int get_deep_sleep_timeout() const { return deep_sleep_timeout_; }
  void deep_sleep();  // wakes on the encoder button

  // Rotary encoder pins (decoded in an interrupt handler, see EncoderStore)
  void set_encoder_pins(InternalGPIOPin *pin_a, InternalGPIOPin *pin_b) { pin_a_ = pin_a; pin_b_ = pin_b; }

  // Entries of the Home Assistant menu
  void add_quick_action(const std::string &name, Trigger<> *trigger) { quick_actions_.emplace_back(name, trigger); }

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
  display::LinkState wiim_link_state_();
  float clamp_volume_(float volume) const;
  // Clamp and queue a volume for one speaker, updating its state optimistically. Skips speakers known to be down.
  bool apply_volume_(const std::string &ipv6, DeviceState &state, float volume);
  // Redraw the main screen; with force=true every region, otherwise only the regions flagged in `changed`.
  void draw_status_(const PollResult &changed, bool force);
  // Move every reachable speaker by `diff` dB, clamped to [0, max_volume_].
  float group_volume_();
    void resync_volumes_();
    uint32_t last_volume_request_{0};
    void step_volume_(float diff);

  // TFT display instance
  TFT_eSPI *tft_{nullptr};

  // UI state tracking
  uint32_t last_draw_{0};
    uint32_t last_art_version_{0};
  PollResult pending_changes_;  // changes folded in since the last redraw
  bool wifi_shown_{false};  // for detecting changes that deserve an immediate redraw
  display::LinkState wiim_shown_{display::LinkState::PENDING};
  bool force_redraw_{true};  // draw everything on the first poll, e.g. dots for speakers that start offline

  // Encoder button
  uint32_t button_press_time_{0};
  bool button_down_{false};
  bool long_press_handled_{false};

  // Rotary encoder. The interrupt handler tracks all four quarter steps and only reports a detent when a full
  // cycle was completed, so contact bounce and half steps cannot add or lose clicks.
  struct EncoderStore {
    ISRInternalGPIOPin pin_a;
    ISRInternalGPIOPin pin_b;
    volatile int32_t detents{0};  // net clicks since loop() last took them (+ = clockwise)
    uint8_t prev{0};
    int8_t acc{0};  // quarter steps towards the next detent
    static void gpio_intr(EncoderStore *arg);
  };
  EncoderStore encoder_;
  InternalGPIOPin *pin_a_{nullptr};
  InternalGPIOPin *pin_b_{nullptr};

  // Menu (menu.cpp). The rows are rebuilt from the current state whenever they are drawn or refreshed.
  enum class MenuId { MAIN, SPEAKERS, SPEAKER_PARAMS, VOLUME_SETUP, INFO, QUICK_ACTIONS, EQ_SPEAKERS, INFO_SPEAKERS };
  struct MenuItem {
    display::MenuRow row;
    std::function<void()> on_select;  // empty = read-only row
  };
  struct MenuLevel {
    MenuId id;
    int position;
    int first_visible;
  };
  // Number editor screen (encoder = value, press = save)
  struct Editor {
    bool active{false};
    std::string title;
    int min{0}, max{100}, step{1}, value{0};
    std::function<std::string(int)> format;
    std::function<void(int)> apply;  // called on every change
    std::function<void()> done;      // called when saved
  };
  // Read-only scrollable page with speaker details (encoder scrolls, press goes back)
  enum class PageKind { EQ, INFO };
  struct Page {
    bool active{false};
    PageKind kind{PageKind::EQ};
    std::string ipv6;
    std::string title;
    std::vector<std::string> lines;
    int first{0};
  };
  Page page_;
  void open_page_(PageKind kind, const std::string &ipv6);
  void close_page_();
  void redraw_page_();
  void refresh_page_();
  std::vector<std::string> build_page_lines_();
  std::vector<MenuLevel> menu_stack_;
  std::vector<MenuItem> menu_items_;
  Editor editor_;
  uint32_t last_menu_refresh_{0};
  std::vector<MenuItem> build_menu_(MenuId id, std::string &title);
  void open_menu_(MenuId id);
  void menu_back_();
  void redraw_menu_();
  void refresh_menu_values_();
  void menu_move_(int diff);
  void menu_select_();
  void open_editor_(const std::string &title, int min, int max, int step, int value,
                    std::function<std::string(int)> format, std::function<void(int)> apply,
                    std::function<void()> done);
  void close_editor_();
  bool in_menu_{false};
  void send_to_speakers_(const std::string &json);
  DeviceState *first_up_state_();

  // Short press of the encoder button outside the menu
  void short_press_action_();

  // Quick actions
  std::vector<std::pair<std::string, Trigger<> *>> quick_actions_;

  // WiiM streamer
  bool wiim_enabled_{false};
  std::string wiim_ip_;  // empty = discover

  // Upper bound for any volume sent to the speakers (dB): active value, and the limit set in yaml
  float max_volume_{120.0f};
  float max_volume_limit_{120.0f};
  float volume_step_{1.0f};  // dB per encoder click

  // Backlight control
  output::FloatOutput *backlight_pin_{nullptr};
  int backlight_level_{100};  // 0-100 %
  void apply_brightness_();

  // Screen off after this many seconds without input (0 = never); any input wakes it
  int display_timeout_{0};
  uint32_t last_interaction_{0};
  bool display_off_{false};
  void note_interaction_();
  uint32_t last_standby_draw_{0};

  // Deep sleep
  int deep_sleep_timeout_{600};  // seconds, 0 = disabled
  uint32_t unavailable_since_{0};  // millis() when the last speaker went away, 0 = at least one is reachable
  void check_deep_sleep_(uint32_t now);

  // Settings persisted in NVS
  struct Settings
  {
    uint32_t version;
    int brightness;
    int deep_sleep_timeout;
    float max_volume;
    float volume_step;
    int display_timeout;
  };
  static constexpr uint32_t SETTINGS_VERSION = 2;
  ESPPreferenceObject settings_pref_;
  void save_settings_();
};

}  // namespace vol_ctrl
}  // namespace esphome
