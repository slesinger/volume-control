#pragma once

#include "esphome/core/component.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/output/float_output.h"
#include <map>
#include <string>
#include "device_state.h"
#include "network.h"
#include "esphome/core/hal.h"
#include <esp_sleep.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// Forward-declare the TFT_eSPI class instead of including the whole header
class TFT_eSPI;

// Enum to define network request types
enum class NetworkRequestType {
  GET_DEVICE_DATA,
  SET_VOLUME,
  SET_MUTE
};

// Struct for messages sent to the network task
struct NetworkRequest {
  NetworkRequestType type;
  std::string ipv6;
  float volume;
  bool mute;
};

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
  void set_mute(bool new_mute);
  void enter_menu();
  void exit_menu();
  
  // Media control methods
  void pause();
  void next();
  void cycle_input();
  void set_input(const std::string &input);
  std::string get_current_input();
  
  // Set backlight control pin
  void set_backlight_pin(output::FloatOutput *backlight_pin) { backlight_pin_ = backlight_pin; }
  void set_volume_step(float step) { volume_step_ = step; }
  
  // Display brightness control (0-100%)
  void set_display_brightness(int brightness);
  int get_display_brightness() const { return backlight_level_; }
  
  // Deep sleep settings
  void set_deep_sleep_timeout(int timeout_seconds) { deep_sleep_timeout_ = timeout_seconds; }
  int get_deep_sleep_timeout() const { return deep_sleep_timeout_; }
  
  // Deep sleep functionality
  void deep_sleep();
  
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
  
  // Menu state
  bool in_menu_{false};
  int menu_level_{0};  // 0 = main menu, 1 = submenu, etc.
  int menu_position_{0};
  int menu_items_count_{0};
  bool adjusting_brightness_{false};  // Flag to indicate brightness adjustment mode
  float volume_step_{1.0f};  // Default 1dB steps

  // Display settings
  int backlight_level_{100};  // 0-100%
  int display_timeout_{60};  // In seconds
  
  // Deep sleep settings
  int deep_sleep_timeout_{600};  // In seconds (10 minutes default)
  uint32_t speakers_unavailable_since_{0};  // Timestamp when all speakers became unavailable
  
  // Task and Queue handles
  TaskHandle_t network_task_handle_ = nullptr;
  QueueHandle_t network_queue_ = nullptr;

  // Static wrapper for task
  static void network_task_wrapper(void *param) {
    static_cast<VolCtrl*>(param)->network_task();
  }
  void network_task();

  // Backlight control
  output::FloatOutput *backlight_pin_{nullptr};
  void exit_brightness_adjustment();

  // Rate limiting for volume changes
  uint32_t last_volume_change_{0}; // Timestamp of last volume change to rate limit
  uint32_t main_loop_counter{0}; // Counter for main loop timing
  
  bool user_adjusting_volume_{false}; // Flag to indicate user is actively changing volume

private:
  // Last known state for change detection
  float last_known_volume_{-1.0f};
  bool last_known_mute_state_{false};
  int last_known_standby_countdown_{-1};
  bool last_known_is_up_{false};
};

}  // namespace vol_ctrl
}  // namespace esphome
