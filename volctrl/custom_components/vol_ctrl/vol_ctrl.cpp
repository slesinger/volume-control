#include "vol_ctrl.h"
#include "art.h"
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
      display::clear_screen(this->tft_);
      // Nothing is drawn here: loop() draws the first frame, and it runs even while WiFi is still connecting

      // After a wake-up from deep sleep the backlight pin is still latched low (see deep_sleep())
      gpio_hold_dis(BACKLIGHT_GPIO);
      gpio_deep_sleep_hold_dis();

      // Restore persisted settings
      this->settings_pref_ = global_preferences->make_preference<Settings>(fnv1_hash("vol_ctrl_settings"));
      Settings saved;
      if (this->settings_pref_.load(&saved) && saved.version == SETTINGS_VERSION &&
          saved.brightness >= MIN_BRIGHTNESS && saved.brightness <= 100 && saved.deep_sleep_timeout >= 0 &&
          saved.max_volume >= 10.0f && saved.volume_step >= 0.5f && saved.display_timeout >= 0)
      {
        this->backlight_level_ = saved.brightness;
        this->deep_sleep_timeout_ = saved.deep_sleep_timeout;
        this->max_volume_ = std::min(saved.max_volume, this->max_volume_limit_);  // yaml stays the upper limit
        this->volume_step_ = saved.volume_step;
        this->display_timeout_ = saved.display_timeout;
      }
      apply_brightness_();
      this->last_interaction_ = millis();

      // Rotary encoder: both pins interrupt on every edge
      this->pin_a_->setup();
      this->pin_b_->setup();
      this->encoder_.pin_a = this->pin_a_->to_isr();
      this->encoder_.pin_b = this->pin_b_->to_isr();
      this->encoder_.prev = (this->pin_a_->digital_read() ? 2 : 0) | (this->pin_b_->digital_read() ? 1 : 0);
      this->pin_a_->attach_interrupt(EncoderStore::gpio_intr, &this->encoder_, gpio::INTERRUPT_ANY_EDGE);
      this->pin_b_->attach_interrupt(EncoderStore::gpio_intr, &this->encoder_, gpio::INTERRUPT_ANY_EDGE);

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
      ESP_LOGCONFIG(TAG, "  Max volume: %.1f dB (limit %.1f dB), step %.1f dB", this->max_volume_,
                    this->max_volume_limit_, this->volume_step_);
      ESP_LOGCONFIG(TAG, "  Display timeout: %d s (0 = off)", this->display_timeout_);
      LOG_PIN("  Encoder pin A: ", this->pin_a_);
      LOG_PIN("  Encoder pin B: ", this->pin_b_);
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
        state.logo_brightness = update.data.logo_brightness;
        state.auto_standby_time = update.data.auto_standby_time;
        state.auto_standby_enabled = update.data.auto_standby_enabled;
      }
      resync_volumes_();
      return result;
    }

    void VolCtrl::draw_status_(const PollResult &changed, bool force)
    {
      DeviceState *state = representative_state_();
      if (force || changed.is_up_changed)
        display::update_speaker_dots(this->tft_, network::get_device_states());
      // The countdown is polled from the speaker every cycle; redraw it at least once a minute even if unchanged
      const uint32_t now = millis();
      if (state != nullptr && (force || changed.standby_changed || now - last_standby_draw_ >= 60000))
      {
        display::update_standby_time(this->tft_, state->standby_countdown);
        last_standby_draw_ = now;
      }
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
      std::string above, below;
      if (this->wiim_enabled_ && wifi_connected && !speakers_pending)
      {
        wiim::Status wiim_status = wiim::get_status();
        if (wiim_status.available && !wiim_status.input.empty())
        {
          message = wiim_status.input;
          above = wiim_status.artist;
          if (!wiim_status.album.empty())
            above += above.empty() ? wiim_status.album : " / " + wiim_status.album;
          below = wiim_status.title;
        }
      }
      display::update_track_info(this->tft_, above, below);
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

      // Clicks counted by the encoder interrupt handler since the last loop
      int32_t detents;
      {
        InterruptLock lock;
        detents = this->encoder_.detents;
        this->encoder_.detents = 0;
      }
      if (detents != 0)
        process_encoder_change(detents);

      // Act on a long press as soon as the button has been held long enough, not only when it is released:
      // open the menu, or close it again when it is already open
      if (button_down_ && !long_press_handled_ && now - button_press_time_ > LONG_PRESS_MS)
      {
        long_press_handled_ = true;
        ESP_LOGI(TAG, "Long press detected");
        if (!editor_.active)  // an editor is saved by the release, see button_released()
        {
          if (in_menu_)
            exit_menu();
          else
            enter_menu();
        }
      }

      // Switch the screen off after a while without input
      if (display_timeout_ > 0 && !display_off_ && !button_down_ && now - last_interaction_ > display_timeout_ * 1000UL)
      {
        display_off_ = true;
        if (backlight_pin_ != nullptr)
          backlight_pin_->set_level(0.0f);
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
      {
        if (!editor_.active && now - last_menu_refresh_ > 500)
        {
          last_menu_refresh_ = now;
          refresh_menu_values_();
        }
        return;
      }

      // Redraw when speaker data arrived, plus periodically for the clock and the status icons
      bool got_data = changed.received || force_redraw_;
      if (!got_data && now - last_draw_ <= 1500)
        return;
      last_draw_ = now;
      // New album art (or none any more) changes the background of everything: repaint the whole screen
      const uint32_t art_version = art::version();
      if (art_version != last_art_version_ && !in_menu_ && !display_off_)
      {
        last_art_version_ = art_version;
        update_whole_screen();
        return;
      }
      draw_status_(pending_changes_, force_redraw_);
      pending_changes_ = PollResult{};
      force_redraw_ = false;
    } // end of loop()

    void VolCtrl::save_settings_()
    {
      Settings settings{SETTINGS_VERSION,        this->backlight_level_, this->deep_sleep_timeout_,
                        this->max_volume_,       this->volume_step_,     this->display_timeout_};
      this->settings_pref_.save(&settings);
    }

    void VolCtrl::apply_brightness_()
    {
      if (this->backlight_pin_ != nullptr && !this->display_off_)
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
        display::clear_screen(this->tft_);
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

    void VolCtrl::update_whole_screen()
    {
      display::clear_screen(this->tft_);
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
      note_interaction_();
      button_press_time_ = millis();
      button_down_ = true;
      long_press_handled_ = false;
    }

    void VolCtrl::button_released()
    {
      button_down_ = false;
      if (editor_.active)
      {
        // Pressing saves the value being edited
        if (editor_.done)
          editor_.done();
        close_editor_();
        return;
      }
      if (long_press_handled_)
        return;  // loop() already acted on the long press (opened or closed the menu)
      if (in_menu_)
      {
        menu_select_();
        return;
      }
      short_press_action_();
    }

    // Short press outside the menu: always mute toggle (play/pause is in the menu)
    void VolCtrl::short_press_action_()
    {
      toggle_mute();
    }

    void VolCtrl::toggle_mute()
    {
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

    // Both speakers always get the same level: the step is applied to the lowest reachable speaker's level
    // and the result is sent to all of them.
    void VolCtrl::step_volume_(float diff)
    {
      float base = group_volume_();
      if (base < 0.0f)
        return; // nothing reachable or not synced yet
      float target = clamp_volume_(base + diff);
      bool first = true;
      for (auto &entry : network::get_device_states())
      {
        DeviceState &state = entry.second;
        if (!state.is_up)
          continue;
        if (target == state.get_requested_volume())
          continue; // already there
        last_volume_request_ = millis();
        if (apply_volume_(entry.first, state, target) && first)
        {
          display::update_volume_display(this->tft_, target, true); // blue until the next poll confirms
          first = false;
        }
      }
    }

    // Lowest known level of the reachable speakers (-1 if none)
    float VolCtrl::group_volume_()
    {
      float result = -1.0f;
      for (auto &entry : network::get_device_states())
      {
        const DeviceState &state = entry.second;
        if (!state.is_up || state.requested_volume < 0.0f)
          continue;
        if (result < 0.0f || state.requested_volume < result)
          result = state.requested_volume;
      }
      return result;
    }

    // Called after every poll: if the reachable speakers disagree (one missed a command, or came back from
    // standby with its own level) and nothing was requested recently, push the lowest level to all.
    void VolCtrl::resync_volumes_()
    {
      if (millis() - last_volume_request_ < 4000)
        return;
      float low = group_volume_();
      if (low < 0.0f)
        return;
      for (auto &entry : network::get_device_states())
      {
        DeviceState &state = entry.second;
        if (state.is_up && state.requested_volume >= 0.0f && fabs(state.requested_volume - low) > 0.05f)
        {
          ESP_LOGW(TAG, "Speakers out of sync, setting %s to %.1f", entry.first.c_str(), low);
          last_volume_request_ = millis();
          apply_volume_(entry.first, state, low);
        }
      }
    }

    // This function is only called from Home Assistant service
    void VolCtrl::set_volume_from_hass(float level)
    {
      if (in_menu_)
        return; // the menu owns the screen and the knob
      note_interaction_();
      ESP_LOGI(TAG, "Setting volume from Home Assistant to %.1f", level);
      if (!(level >= 0.0f)) // also rejects NaN
        return;

      level = clamp_volume_(level);
      last_volume_request_ = millis();
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
      note_interaction_();

      if (editor_.active)
      {
        int value = std::min(std::max(editor_.value + diff * editor_.step, editor_.min), editor_.max);
        if (value != editor_.value)
        {
          editor_.value = value;
          if (editor_.apply)
            editor_.apply(value);
          display::draw_editor_screen(this->tft_, editor_.title, editor_.format(value),
                                      static_cast<float>(value - editor_.min) / (editor_.max - editor_.min));
        }
        return;
      }

      if (in_menu_)
      {
        menu_move_(diff);
        return;
      }

      ESP_LOGD(TAG, "Encoder diff %d", diff);
      step_volume_(diff * volume_step_);
    }

    // index = (previous << 2) | current, state = (A << 1) | B; clockwise cycle: 10 -> 11 -> 01 -> 00.
    // In DRAM: it is read from an interrupt handler.
#ifndef DRAM_ATTR
#define DRAM_ATTR
#endif
    static const int8_t DRAM_ATTR STEP[16] = {0, -1, +1, 0, +1, 0, 0, -1, -1, 0, 0, +1, 0, +1, -1, 0};

    // The interrupt handler of both encoder pins. A detent is four quarter steps in one direction; steps that go
    // back and forth (contact bounce) cancel out in `acc`, and a skipped step resets it.
    void IRAM_ATTR HOT VolCtrl::EncoderStore::gpio_intr(EncoderStore *arg)
    {
      const uint8_t current = (arg->pin_a.digital_read() ? 2 : 0) | (arg->pin_b.digital_read() ? 1 : 0);
      if (current == arg->prev)
        return;
      const int8_t step = STEP[(arg->prev << 2) | current];
      arg->prev = current;
      if (step == 0)
      {
        arg->acc = 0;  // skipped a step: resynchronise
        return;
      }
      arg->acc += step;
      if (arg->acc >= 4)
      {
        arg->detents = arg->detents + 1;
        arg->acc = 0;
      }
      else if (arg->acc <= -4)
      {
        arg->detents = arg->detents - 1;
        arg->acc = 0;
      }
    }

    void VolCtrl::note_interaction_()
    {
      last_interaction_ = millis();
      if (display_off_)
      {
        display_off_ = false;
        apply_brightness_();
      }
    }

  } // namespace vol_ctrl
} // namespace esphome
