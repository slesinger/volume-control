#include "vol_ctrl.h"
#include "esphome/core/log.h"
#include <TFT_eSPI.h>
#include "esphome/components/wifi/wifi_component.h"
#include "device_state.h"
#include "display.h"
#include "network.h"
#include "utils.h"
#include "esphome/core/hal.h"

namespace esphome
{
  namespace vol_ctrl
  {

    static const char *const TAG = "vol_ctrl";
    static uint32_t button_press_time_ = 0;

    void VolCtrl::setup()
    {
      ESP_LOGCONFIG(TAG, "Setting up Volume Control...");

      // Initialize the display
      this->tft_ = new TFT_eSPI();
      this->tft_->init();
      this->tft_->setRotation(0);
      this->tft_->fillScreen(TFT_BLACK);
      // Do not display anything yet, wait for main loop to draw the UI

      // Initialize backlight if configured
      if (this->backlight_pin_ != nullptr)
      {
        ESP_LOGCONFIG(TAG, "Setting backlight to 100%%");
        this->backlight_pin_->set_level(1.0);
      }

      // Initialize network subsystem
      network::init();
      network::start();

      // Add a small delay to let things settle
      esphome::delay(500);
    }

    void VolCtrl::dump_config()
    {
      ESP_LOGCONFIG(TAG, "Volume Control:");
      ESP_LOGCONFIG(TAG, "  Max volume: %.1f dB", this->max_volume_);
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
      display::update_status_message(this->tft_, "Long-press for menu");
      display::update_wifi_status(this->tft_, wifi::global_wifi_component->is_connected());
    }

    // Everything runs in ESPHome's cooperative loop, so loop() must return quickly or the watchdog resets the chip.
    // Network I/O is done by the worker task in network.cpp; this only consumes its results.
    // Encoder and button events arrive via the callbacks below (see yaml).
    void VolCtrl::loop()
    {
      uint32_t now = millis();
      const bool wifi_connected = wifi::global_wifi_component->is_connected();
      network::set_online(wifi_connected);

      if (!wifi_connected)
      {
        if (now - last_wifi_draw_ > 1000)
        {
          last_wifi_draw_ = now;
          display::update_status_message(this->tft_, "Connecting to WiFi");
          display::update_wifi_status(this->tft_, false);
        }
        return;
      }

      PollResult changed = apply_poll_updates_();
      pending_changes_.is_up_changed |= changed.is_up_changed;
      pending_changes_.standby_changed |= changed.standby_changed;
      pending_changes_.mute_changed |= changed.mute_changed;

      // The menu owns the screen; state keeps updating underneath and update_whole_screen() catches up on exit
      if (in_menu_)
        return;

      // Redraw when speaker data arrived, plus periodically for the clock and WiFi icon
      bool got_data = changed.received || force_redraw_;
      if (!got_data && now - last_draw_ <= 1500)
        return;
      last_draw_ = now;
      draw_status_(pending_changes_, force_redraw_);
      pending_changes_ = PollResult{};
      force_redraw_ = false;
    } // end of loop()

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
    }

    void VolCtrl::button_released()
    {
      uint32_t press_duration = millis() - button_press_time_;
      if (press_duration > 300)
      { // long press threshold
        ESP_LOGI(TAG, "Long press detected (%ums)", press_duration);
        enter_menu();
      }
      else
      {
        toggle_mute();
      }
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
      auto &device_states = network::get_device_states();
      bool any_unmuted = false;
      for (auto &entry : device_states)
      {
        if (entry.second.is_up && !entry.second.muted)
          any_unmuted = true;
      }
      const bool target = any_unmuted;
      ESP_LOGI(TAG, "Setting mute to %s on all speakers", target ? "on" : "off");

      for (auto &entry : device_states)
      {
        if (!entry.second.is_up)
          continue;
        network::request_mute(entry.first, target);
        entry.second.set_mute(target); // optimistic, the next poll confirms
      }

      DeviceState *state = representative_state_();
      if (state != nullptr)
        display::update_mute_status(this->tft_, state->muted, state->requested_volume);
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
            menu_items_count_ = 7;
            // TODO: Implement volume settings submenu
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
