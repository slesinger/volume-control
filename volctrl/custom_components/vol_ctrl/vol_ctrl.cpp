#include "vol_ctrl.h"
#include "esphome/core/log.h"
#include <TFT_eSPI.h>
#include "esphome/components/wifi/wifi_component.h"
#include "device_state.h"
#include "display.h"
#include "network.h"
#include "utils.h"
#include "esphome/core/hal.h"
#include "esphome/core/application.h"
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <soc/gpio_sig_map.h>
#include <esp_sleep.h>

namespace esphome
{
  namespace vol_ctrl
  {

    static const char *const TAG = "vol_ctrl";
    constexpr uint32_t LONG_PRESS_MS = 300;
    constexpr gpio_num_t BACKLIGHT_GPIO = GPIO_NUM_17;  // same pin as the ledc output in volume_control.yaml
    constexpr int MIN_BRIGHTNESS = 5;                   // %, so the editor cannot make the screen unreadable

    void VolCtrl::setup()
    {
      ESP_LOGCONFIG(TAG, "Setting up Volume Control...");

      // Initialize the display
      this->tft_ = new TFT_eSPI();
      this->tft_->init();
      this->tft_->setRotation(0);
      this->tft_->fillScreen(TFT_BLACK);
      // Nothing is drawn here: loop() draws the first frame, and it runs even while WiFi is still connecting

      // After a wake-up from deep sleep the backlight pin is still latched low (see deep_sleep())
      gpio_hold_dis(BACKLIGHT_GPIO);
      gpio_deep_sleep_hold_dis();

      // Restore persisted settings (brightness, deep sleep timeout)
      this->settings_pref_ = global_preferences->make_preference<Settings>(fnv1_hash("vol_ctrl_settings"));
      Settings saved;
      if (this->settings_pref_.load(&saved) && saved.brightness >= MIN_BRIGHTNESS && saved.brightness <= 100 &&
          saved.deep_sleep_timeout >= 0)
      {
        this->backlight_level_ = saved.brightness;
        this->deep_sleep_timeout_ = saved.deep_sleep_timeout;
      }
      apply_brightness_();

      // Initialize network subsystem
      network::init();
      network::start();
      if (this->wiim_enabled_)
      {
        wiim::init(this->wiim_ip_);
        wiim::start();
      }
    }

    void VolCtrl::dump_config()
    {
      ESP_LOGCONFIG(TAG, "Volume Control:");
      ESP_LOGCONFIG(TAG, "  Max volume: %.1f dB", this->max_volume_);
      ESP_LOGCONFIG(TAG, "  Brightness: %d%%", this->backlight_level_);
      ESP_LOGCONFIG(TAG, "  Deep sleep timeout: %d s (0 = off)", this->deep_sleep_timeout_);
    }

    float VolCtrl::clamp_volume_(float volume) const
    {
      if (volume < 0.0f)
        return 0.0f;
      if (volume > this->max_volume_)
        return this->max_volume_;
      return volume;
    }

    DeviceState *VolCtrl::representative_state_()
    {
      auto &device_states = network::get_device_states();
      for (auto &entry : device_states)
      {
        if (entry.second.is_up)
          return &entry.second;
      }
      return device_states.empty() ? nullptr : &device_states.begin()->second;
    }

    VolCtrl::PollResult VolCtrl::apply_poll_updates_()
    {
      PollResult result;
      std::vector<network::PollUpdate> updates;
      if (!network::take_updates(updates))
        return result;

      result.received = true;
      auto &device_states = network::get_device_states();
      for (const auto &update : updates)
      {
        auto it = device_states.find(update.ipv6);
        if (it == device_states.end())
          continue;
        DeviceState &state = it->second;
        if (!state.known)
        {
          state.known = true;
          result.is_up_changed = true;  // dot turns from orange to green/red
        }
        if (state.set_is_up(update.is_up))
        {
          result.is_up_changed = true;
          ESP_LOGI(TAG, "Speaker %s is %s", update.ipv6.c_str(), update.is_up ? "reachable" : "unreachable");
        }
        if (!update.is_up)
          continue; // keep the last known values instead of overwriting them with defaults
        result.standby_changed |= state.set_standby_countdown(update.data.standby_countdown);
        state.requested_volume = update.data.volume;
        result.mute_changed |= state.set_mute(update.data.mute);
      }
      return result;
    }

    void VolCtrl::draw_status_(const PollResult &changed, bool force)
    {
      DeviceState *state = representative_state_();
      if (force || changed.is_up_changed)
        display::update_speaker_dots(this->tft_, network::get_device_states());
      if (state != nullptr && (force || changed.standby_changed))
        display::update_standby_time(this->tft_, state->standby_countdown);
      display::update_datetime(this->tft_, utils::get_datetime_string());
      display::update_volume_display(this->tft_, state != nullptr ? state->requested_volume : -1.0f);
      if (state != nullptr && state->muted)
        display::update_mute_status(this->tft_, true, state->requested_volume);
      else if (state != nullptr && (force || changed.mute_changed))
        display::update_mute_status(this->tft_, false, state->requested_volume);
      const bool wifi_connected = wifi::global_wifi_component->is_connected();
      bool speakers_pending = false;
      for (auto &entry : network::get_device_states())
        speakers_pending |= !entry.second.known;

      // Bottom line: what the device is busy with, then input / track from the WiiM, else the menu hint
      std::string message = "Long-press for menu";
      if (!wifi_connected)
      {
        message = "Connecting to WiFi";
      }
      else if (speakers_pending)
      {
        message = "Finding speakers";
      }
      else if (this->wiim_enabled_)
      {
        wiim::Status wiim_status = wiim::get_status();
        if (wiim_status.available && !wiim_status.input.empty())
        {
          message = wiim_status.input;
          if (wiim_status.playing && !wiim_status.title.empty())
            message += ": " + (wiim_status.artist.empty() ? wiim_status.title : wiim_status.artist + " - " + wiim_status.title);
        }
      }
      display::update_status_message(this->tft_, message);

      if (this->wiim_enabled_)
        display::update_wiim_status(this->tft_, wiim_link_state_());
      display::update_wifi_status(this->tft_, wifi_connected ? display::LinkState::UP : display::LinkState::PENDING);
    }

    display::LinkState VolCtrl::wiim_link_state_()
    {
      wiim::Status status = wiim::get_status();
      if (!status.checked)
        return display::LinkState::PENDING;
      return status.available ? display::LinkState::UP : display::LinkState::DOWN;
    }

    // Everything runs in ESPHome's cooperative loop, so loop() must return quickly or the watchdog resets the chip.
    // Network I/O is done by the worker task in network.cpp; this only consumes its results.
    // Encoder and button events arrive via the callbacks below (see yaml).
    void VolCtrl::loop()
    {
      uint32_t now = millis();

      // Open the menu as soon as the button has been held long enough, not only when it is released
      if (button_down_ && !long_press_handled_ && now - button_press_time_ > LONG_PRESS_MS)
      {
        long_press_handled_ = true;
        ESP_LOGI(TAG, "Long press detected");
        if (!in_menu_)
          enter_menu();
      }

      // This loop already runs while WiFi is still connecting (see get_setup_priority), so the screen comes up
      // at once and fills in as the asynchronous steps (WiFi, speakers, WiiM) finish.
      const bool wifi_connected = wifi::global_wifi_component->is_connected();
      network::set_online(wifi_connected);
      wiim::set_online(wifi_connected);
      check_deep_sleep_(now);

      // Redraw immediately when one of the steps finished, not at the next periodic tick
      const display::LinkState wiim_state = this->wiim_enabled_ ? wiim_link_state_() : display::LinkState::UP;
      if (wifi_connected != wifi_shown_ || wiim_state != wiim_shown_)
      {
        wifi_shown_ = wifi_connected;
        wiim_shown_ = wiim_state;
        last_draw_ = 0;
      }

      PollResult changed;
      if (wifi_connected)
      {
        changed = apply_poll_updates_();
        pending_changes_.is_up_changed |= changed.is_up_changed;
        pending_changes_.standby_changed |= changed.standby_changed;
        pending_changes_.mute_changed |= changed.mute_changed;
      }

      // The menu owns the screen; state keeps updating underneath and update_whole_screen() catches up on exit
      if (in_menu_)
        return;

      // Redraw when speaker data arrived, plus periodically for the clock and the status icons
      bool got_data = changed.received || force_redraw_;
      if (!got_data && now - last_draw_ <= 1500)
        return;
      last_draw_ = now;
      draw_status_(pending_changes_, force_redraw_);
      pending_changes_ = PollResult{};
      force_redraw_ = false;
    } // end of loop()

    void VolCtrl::save_settings_()
    {
      Settings settings{this->backlight_level_, this->deep_sleep_timeout_};
      this->settings_pref_.save(&settings);
    }

    void VolCtrl::apply_brightness_()
    {
      if (this->backlight_pin_ != nullptr)
        this->backlight_pin_->set_level(this->backlight_level_ / 100.0f);
    }

    void VolCtrl::set_display_brightness(int brightness)
    {
      this->backlight_level_ = std::min(std::max(brightness, MIN_BRIGHTNESS), 100);
      ESP_LOGI(TAG, "Display brightness %d%%", this->backlight_level_);
      apply_brightness_();
      save_settings_();
    }

    void VolCtrl::set_deep_sleep_timeout(int seconds)
    {
      this->deep_sleep_timeout_ = std::max(seconds, 0);
      this->unavailable_since_ = 0;
      save_settings_();
    }

    // Deep sleep once every speaker has been unreachable for deep_sleep_timeout_ seconds (0 = disabled)
    void VolCtrl::check_deep_sleep_(uint32_t now)
    {
      if (this->deep_sleep_timeout_ <= 0)
        return;
      bool any_up = false;
      for (auto &entry : network::get_device_states())
        any_up |= entry.second.is_up;
      if (any_up)
      {
        this->unavailable_since_ = 0;
        return;
      }
      if (this->unavailable_since_ == 0)
      {
        this->unavailable_since_ = now ? now : 1;
        ESP_LOGI(TAG, "No speaker reachable, deep sleep in %d s", this->deep_sleep_timeout_);
      }
      else if ((now - this->unavailable_since_) / 1000 >= static_cast<uint32_t>(this->deep_sleep_timeout_))
      {
        deep_sleep();
      }
    }

    void VolCtrl::deep_sleep()
    {
      ESP_LOGI(TAG, "Entering deep sleep, press the encoder button to wake up");
      if (this->tft_ != nullptr)
      {
        this->tft_->fillScreen(TFT_BLACK);
        this->tft_->writecommand(0x10);  // ST7789 SLPIN
      }
      // Backlight really off: with PWM at 0 the output would still sit at min_power (see yaml), and a pin left to
      // the PWM peripheral floats in deep sleep. Take the pin over, drive it low and latch it through the sleep.
      if (this->backlight_pin_ != nullptr)
        this->backlight_pin_->set_level(0.0f);
      gpio_set_direction(BACKLIGHT_GPIO, GPIO_MODE_OUTPUT);
      gpio_matrix_out(BACKLIGHT_GPIO, SIG_GPIO_OUT_IDX, false, false);  // plain GPIO output instead of LEDC
      gpio_set_level(BACKLIGHT_GPIO, 0);
      gpio_hold_en(BACKLIGHT_GPIO);
      gpio_deep_sleep_hold_en();

      // The encoder button (GPIO25, active low) wakes the chip. The digital pull-up is off in deep sleep,
      // so enable the RTC pull-up or the pin floats and wakes the chip spuriously.
      rtc_gpio_pullup_en(GPIO_NUM_25);
      rtc_gpio_pulldown_dis(GPIO_NUM_25);
      esp_sleep_enable_ext0_wakeup(GPIO_NUM_25, 0);
      App.run_safe_shutdown_hooks();  // flush logs / preferences, close API connections
      esp_deep_sleep_start();
    }

    // Menu editors for the volume settings submenu (rows: 0 back, 1 step, 2 backlight, 3 display timeout, 4 deep sleep)
    void VolCtrl::draw_volume_settings_values_()
    {
      display::draw_menu_value(this->tft_, 2, std::to_string(this->backlight_level_) + "%");
      display::draw_menu_value(this->tft_, 4,
                               this->deep_sleep_timeout_ > 0 ? std::to_string(this->deep_sleep_timeout_ / 60) + " min" : "off");
    }

    void VolCtrl::cycle_deep_sleep_timeout_()
    {
      static const int values[] = {300, 600, 900, 1800, 0};  // seconds, 0 = off
      int next = 0;
      for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++)
      {
        if (values[i] == this->deep_sleep_timeout_)
        {
          next = (i + 1) % (sizeof(values) / sizeof(values[0]));
          break;
        }
      }
      set_deep_sleep_timeout(values[next]);
    }

    void VolCtrl::exit_brightness_adjustment()
    {
      this->adjusting_brightness_ = false;
      display::draw_menu_screen(this->tft_, menu_level_, menu_position_, menu_items_count_);
      draw_volume_settings_values_();
    }

    void VolCtrl::update_whole_screen()
    {
      this->tft_->fillScreen(TFT_BLACK);
      draw_status_(PollResult{}, true);
      pending_changes_ = PollResult{};
      last_draw_ = millis();
    }

    // Clamp and queue a volume for one speaker; never blocks.
    bool VolCtrl::apply_volume_(const std::string &ipv6, DeviceState &state, float volume)
    {
      if (!state.is_up)
        return false;
      volume = clamp_volume_(volume);
      network::request_volume(ipv6, volume);
      state.set_requested_volume(volume); // the next poll confirms (or corrects) it
      return true;
    }

    void VolCtrl::button_pressed()
    {
      button_press_time_ = millis();
      button_down_ = true;
      long_press_handled_ = false;
    }

    void VolCtrl::button_released()
    {
      button_down_ = false;
      if (adjusting_brightness_)
      {
        exit_brightness_adjustment();
        return;
      }
      if (long_press_handled_)
        return;  // loop() already acted on the long press (opened the menu)
      toggle_mute();  // short press: mute, or select in the menu
    }

    void VolCtrl::toggle_mute()
    {
      // If we're in menu mode, use this as a select button
      if (in_menu_)
      {
        ESP_LOGI(TAG, "Button pressed in menu - selecting item");
        menu_select();
        return;
      }

      // One target for all speakers: mute unless every reachable speaker is already muted
      bool any_unmuted = false;
      for (auto &entry : network::get_device_states())
      {
        if (entry.second.is_up && !entry.second.muted)
          any_unmuted = true;
      }
      set_mute(any_unmuted);
    }

    void VolCtrl::mute() { set_mute(true); }
    void VolCtrl::unmute() { set_mute(false); }

    void VolCtrl::set_mute(bool target)
    {
      ESP_LOGI(TAG, "Setting mute to %s on all speakers", target ? "on" : "off");

      for (auto &entry : network::get_device_states())
      {
        if (!entry.second.is_up)
          continue;
        network::request_mute(entry.first, target);
        entry.second.set_mute(target); // optimistic, the next poll confirms
      }

      DeviceState *state = representative_state_();
      if (state != nullptr && !in_menu_)
        display::update_mute_status(this->tft_, state->muted, state->requested_volume);
    }

    float VolCtrl::get_volume()
    {
      DeviceState *state = representative_state_();
      return state != nullptr ? state->requested_volume : -1.0f;
    }

    bool VolCtrl::is_muted()
    {
      DeviceState *state = representative_state_();
      return state != nullptr && state->muted;
    }

    void VolCtrl::enter_menu()
    {
      uint32_t now = millis();

      if (!in_menu_)
      {
        ESP_LOGI(TAG, "Entering menu");
        in_menu_ = true;
        menu_level_ = 0;
        menu_position_ = 0;
        menu_items_count_ = 7; // Number of items in main menu

        // Draw the menu
        display::draw_menu_screen(this->tft_, menu_level_, menu_position_, menu_items_count_);
      }
    }

    void VolCtrl::exit_menu()
    {
      if (in_menu_)
      {
        ESP_LOGI(TAG, "Exiting menu");
        in_menu_ = false;
        menu_level_ = 0;
        menu_position_ = 0;

        // Force a full redraw when exiting menu
        update_whole_screen();
      }
    }

    void VolCtrl::menu_up()
    {
      if (in_menu_)
      {
        int prev_position = menu_position_;
        menu_position_--;
        if (menu_position_ < 0)
        {
          menu_position_ = menu_items_count_ - 1;
        }

        // Update the menu display
        display::draw_menu_item_highlight(this->tft_, menu_position_, prev_position);
      }
    }

    void VolCtrl::menu_down()
    {
      if (in_menu_)
      {
        int prev_position = menu_position_;
        menu_position_++;
        if (menu_position_ >= menu_items_count_)
        {
          menu_position_ = 0;
        }

        // Update the menu display
        display::draw_menu_item_highlight(this->tft_, menu_position_, prev_position);
      }
    }

    void VolCtrl::menu_select()
    {
      if (in_menu_)
      {
        ESP_LOGI(TAG, "Selected menu item %d", menu_position_);

        switch (menu_level_)
        {
        case 0: // Main menu
          if (menu_position_ == 0)
          {
            // Exit menu
            exit_menu();
            return;
          }
          else if (menu_position_ == 1)
          {
            // Show devices
            // TODO: Implement device listing screen
          }
          else if (menu_position_ == 2)
          {
            // Show settings
            // TODO: Implement settings screen
          }
          else if (menu_position_ == 3)
          {
            // Parametric EQ submenu
            menu_level_ = 1; // Enter EQ submenu
            menu_position_ = 0;
            menu_items_count_ = 4;
          }
          else if (menu_position_ == 4)
          {
            // Discover devices
            // This needs to trigger a new network discovery
            // TODO: Implement discovery trigger
          }
          else if (menu_position_ == 5)
          {
            // Speaker parameters submenu
            menu_level_ = 1; // Enter speaker parameters submenu
            menu_position_ = 0;
            menu_items_count_ = 6;
          }
          else if (menu_position_ == 6)
          {
            // Volume settings submenu
            menu_level_ = 1; // Enter volume settings submenu
            menu_position_ = 0;
            menu_items_count_ = 5;
            // TODO: volume step and display timeout items
          }
          break;

        case 1: // EQ submenu
          if (menu_position_ == 0)
          {
            // Back to main menu
            menu_level_ = 0;
            menu_position_ = 0;
            menu_items_count_ = 7;
          }
          else if (menu_items_count_ == 5)
          {
            // Volume settings submenu
            if (menu_position_ == 2)
            {
              ESP_LOGI(TAG, "Entering brightness adjustment");
              adjusting_brightness_ = true;
              display::draw_brightness_adjustment_screen(this->tft_, backlight_level_);
              return;
            }
            else if (menu_position_ == 4)
            {
              cycle_deep_sleep_timeout_();
            }
          }
          // TODO: Implement other EQ submenu items
          break;

        case 2: // Speaker parameters submenu
          if (menu_position_ == 0)
          {
            // Back to main menu
            menu_level_ = 0;
            menu_position_ = 0;
            menu_items_count_ = 7;
          }
          // TODO: Implement other speaker parameters submenu items
          break;

        case 3: // Volume settings submenu
          if (menu_position_ == 0)
          {
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
        if (menu_level_ == 1 && menu_items_count_ == 5)
          draw_volume_settings_values_();
      }
    }

    void VolCtrl::step_volume_(float diff)
    {
      bool first = true;
      for (auto &entry : network::get_device_states())
      {
        DeviceState &state = entry.second;
        if (!state.is_up || state.get_requested_volume() < 0.0f)
          continue; // unreachable or not synced yet
        float target = clamp_volume_(state.get_requested_volume() + diff);
        if (target == state.get_requested_volume())
          continue; // already at the limit
        if (apply_volume_(entry.first, state, target) && first)
        {
          display::update_volume_display(this->tft_, target, true); // blue until the next poll confirms
          first = false;
        }
      }
    }

    // This function is only called from Home Assistant service
    void VolCtrl::set_volume_from_hass(float level)
    {
      if (in_menu_)
        return; // the menu owns the screen and the knob
      ESP_LOGI(TAG, "Setting volume from Home Assistant to %.1f", level);
      if (!(level >= 0.0f)) // also rejects NaN
        return;

      level = clamp_volume_(level);
      bool first = true;
      for (auto &entry : network::get_device_states())
      {
        if (apply_volume_(entry.first, entry.second, level) && first)
        {
          display::update_volume_display(this->tft_, level, true);
          first = false;
        }
      }
    }

    // Diff can be negative, see yaml lambda
    void VolCtrl::volume_change_from_hass(float diff)
    {
      if (in_menu_)
        return;
      step_volume_(diff);
    }

    void VolCtrl::process_encoder_change(int diff)
    {
      if (diff == 0)
        return;

      if (adjusting_brightness_)
      {
        int before = backlight_level_;
        int target = std::min(std::max(before + diff * 5, MIN_BRIGHTNESS), 100);
        if (target != before)
        {
          set_display_brightness(target);
          display::draw_brightness_adjustment_screen(this->tft_, backlight_level_);
        }
        return;
      }

      if (in_menu_)
      {
        // Use encoder for menu navigation
        if (diff > 0)
          menu_down();
        else
          menu_up();
        return;
      }

      ESP_LOGD(TAG, "Encoder diff %d", diff);
      step_volume_(static_cast<float>(diff));
    }

  } // namespace vol_ctrl
} // namespace esphome
