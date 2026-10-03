// The on-device menu. Rows are rebuilt from the current state every time they are drawn or refreshed, so values
// (input, mute, speaker parameters, ...) stay current without bookkeeping.
#include "vol_ctrl.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esphome/components/wifi/wifi_component.h"
#include <TFT_eSPI.h>
#include <esp_system.h>
#include <algorithm>

namespace esphome {
namespace vol_ctrl {

static const char *const TAG = "vol_ctrl.menu";

namespace {

std::string format(const char *fmt, int value) {
  char buf[24];
  snprintf(buf, sizeof(buf), fmt, value);
  return buf;
}

std::string seconds_text(int seconds) {
  if (seconds <= 0)
    return "off";
  return seconds < 60 ? format("%d s", seconds) : format("%d min", seconds / 60);
}

template<size_t N> int next_value(const int (&values)[N], int current) {
  for (size_t i = 0; i < N; i++)
    if (values[i] == current)
      return values[(i + 1) % N];
  return values[0];
}

// "Left-6473470117" -> "Left"
std::string short_name(const std::string &name) {
  size_t dash = name.find('-');
  return dash == std::string::npos ? name : name.substr(0, dash);
}

}  // namespace

void VolCtrl::enter_menu() {
  if (in_menu_)
    return;
  ESP_LOGI(TAG, "Entering menu");
  in_menu_ = true;
  menu_stack_.clear();
  open_menu_(MenuId::MAIN);
}

void VolCtrl::exit_menu() {
  if (!in_menu_)
    return;
  ESP_LOGI(TAG, "Exiting menu");
  in_menu_ = false;
  editor_.active = false;
  menu_stack_.clear();
  update_whole_screen();  // full redraw of the main screen
}

void VolCtrl::open_menu_(MenuId id) {
  menu_stack_.push_back({id, 0, 0});
  redraw_menu_();
}

void VolCtrl::menu_back_() {
  if (!menu_stack_.empty())
    menu_stack_.pop_back();
  if (menu_stack_.empty())
    exit_menu();
  else
    redraw_menu_();
}

void VolCtrl::redraw_menu_() {
  MenuLevel &level = menu_stack_.back();
  std::string title;
  menu_items_ = build_menu_(level.id, title);
  const int count = static_cast<int>(menu_items_.size());
  const int visible = display::menu_visible_rows();
  level.position = std::min(std::max(level.position, 0), count - 1);
  level.first_visible = std::min(std::max(level.first_visible, level.position - visible + 1), level.position);

  std::vector<display::MenuRow> rows;
  for (const auto &item : menu_items_)
    rows.push_back(item.row);
  display::draw_menu(this->tft_, title, rows, level.position, level.first_visible);
}

// Redraw only the rows whose value changed since the last build (e.g. the WiiM input after it was switched)
void VolCtrl::refresh_menu_values_() {
  if (menu_stack_.empty())
    return;
  MenuLevel &level = menu_stack_.back();
  std::vector<MenuItem> old_items = std::move(menu_items_);
  std::string title;
  menu_items_ = build_menu_(level.id, title);
  if (menu_items_.size() != old_items.size()) {
    redraw_menu_();
    return;
  }
  const int visible = display::menu_visible_rows();
  for (int i = 0; i < visible && level.first_visible + i < static_cast<int>(menu_items_.size()); i++) {
    const int index = level.first_visible + i;
    if (menu_items_[index].row.value != old_items[index].row.value)
      display::draw_menu_row(this->tft_, menu_items_[index].row, i, index == level.position);
  }
}

void VolCtrl::menu_move_(int diff) {
  MenuLevel &level = menu_stack_.back();
  const int count = static_cast<int>(menu_items_.size());
  if (count == 0)
    return;
  const int visible = display::menu_visible_rows();
  const int previous = level.position;
  level.position = ((level.position + diff) % count + count) % count;

  const int previous_first = level.first_visible;
  if (level.position < level.first_visible)
    level.first_visible = level.position;
  else if (level.position >= level.first_visible + visible)
    level.first_visible = level.position - visible + 1;

  if (level.first_visible != previous_first) {
    redraw_menu_();  // scrolled
  } else {
    display::draw_menu_row(this->tft_, menu_items_[previous].row, previous - level.first_visible, false);
    display::draw_menu_row(this->tft_, menu_items_[level.position].row, level.position - level.first_visible, true);
  }
}

void VolCtrl::menu_select_() {
  if (menu_stack_.empty())
    return;
  const MenuLevel level = menu_stack_.back();
  const size_t depth = menu_stack_.size();
  if (level.position < 0 || level.position >= static_cast<int>(menu_items_.size()))
    return;
  auto handler = menu_items_[level.position].on_select;
  if (!handler)
    return;
  handler();

  // Handlers that navigate (submenu, back, exit) or open an editor draw for themselves; the others change a value
  // in place, so show it
  if (in_menu_ && !editor_.active && menu_stack_.size() == depth && menu_stack_.back().id == level.id)
    refresh_menu_values_();
}

void VolCtrl::open_editor_(const std::string &title, int min, int max, int step, int value,
                           std::function<std::string(int)> format, std::function<void(int)> apply,
                           std::function<void()> done) {
  editor_.active = true;
  editor_.title = title;
  editor_.min = min;
  editor_.max = std::max(max, min + 1);
  editor_.step = step;
  editor_.value = std::min(std::max(value, min), editor_.max);
  editor_.format = format;
  editor_.apply = apply;
  editor_.done = done;
  display::draw_editor_screen(this->tft_, editor_.title, editor_.format(editor_.value),
                              static_cast<float>(editor_.value - editor_.min) / (editor_.max - editor_.min));
}

void VolCtrl::close_editor_() {
  editor_.active = false;
  redraw_menu_();
}

DeviceState *VolCtrl::first_up_state_() {
  for (auto &entry : network::get_device_states())
    if (entry.second.is_up)
      return &entry.second;
  return nullptr;
}

void VolCtrl::send_to_speakers_(const std::string &json) {
  for (auto &entry : network::get_device_states())
    if (entry.second.is_up)
      network::request_raw(entry.first, json);
}

std::vector<VolCtrl::MenuItem> VolCtrl::build_menu_(MenuId id, std::string &title) {
  std::vector<MenuItem> items;
  auto add = [&items](const std::string &label, const std::string &value, std::function<void()> on_select,
                      bool submenu = false) {
    MenuItem item;
    item.row.label = label;
    item.row.value = value;
    item.row.submenu = submenu;
    item.on_select = on_select;
    items.push_back(item);
  };
  auto submenu = [this, &add](const std::string &label, MenuId target) {
    add(label, "", [this, target]() { open_menu_(target); }, true);
  };
  auto back = [this, &add]() { add("Back", "", [this]() { menu_back_(); }); };

  switch (id) {
    case MenuId::MAIN: {
      title = "MENU";
      add("Exit menu", "", [this]() { exit_menu(); });
      if (this->wiim_enabled_) {
        const wiim::Status wiim_status = wiim::get_status();
        add("Play / Pause", wiim_status.available ? (wiim_status.playing ? "playing" : "paused") : "offline",
            [this]() {
              wiim::toggle_play();
              exit_menu();
            });
        add("Next track", "", [this]() {
          wiim::next();
          exit_menu();
        });
        add("Prev. track", "", [this]() {
          wiim::previous();
          exit_menu();
        });
        add("Input", wiim_status.input.empty() ? "--" : wiim_status.input, [this]() { wiim::cycle_input(); });
      }
      add("Mute", is_muted() ? "on" : "off", [this]() { set_mute(!is_muted()); });
      if (!quick_actions_.empty())
        submenu("Home Assistant", MenuId::QUICK_ACTIONS);
      submenu("Speakers", MenuId::SPEAKERS);
      submenu("Speaker params", MenuId::SPEAKER_PARAMS);
      submenu("Volume setup", MenuId::VOLUME_SETUP);
      submenu("Info", MenuId::INFO);
      add("Sleep now", "", [this]() { deep_sleep(); });
      add("Restart", "", []() { App.safe_reboot(); });
      break;
    }

    case MenuId::QUICK_ACTIONS: {
      title = "ACTIONS";
      back();
      for (auto &action : quick_actions_) {
        Trigger<> *trigger = action.second;
        add(action.first, "", [this, trigger]() {
          trigger->trigger();
          exit_menu();
        });
      }
      break;
    }

    case MenuId::SPEAKERS: {
      title = "SPEAKERS";
      back();
      for (auto &entry : network::get_device_states()) {
        const DeviceState &state = entry.second;
        std::string value = !state.known ? "..." : !state.is_up ? "offline"
                                                 : state.requested_volume < 0.0f
                                                     ? "--"
                                                     : format("%d dB", static_cast<int>(state.requested_volume));
        if (state.is_up && state.muted)
          value = "muted";
        add(short_name(network::device_name(entry.first)), value, nullptr);
      }
      break;
    }

    case MenuId::SPEAKER_PARAMS: {
      title = "SPEAKER PARAMS";
      back();
      DeviceState *state = first_up_state_();
      const int logo = state != nullptr ? state->logo_brightness : -1;
      const int standby_time = state != nullptr ? state->auto_standby_time : -1;
      const int standby_enabled = state != nullptr ? state->auto_standby_enabled : -1;

      add("Logo bright.", logo >= 0 ? format("%d%%", logo) : "--", [this, logo]() {
        if (logo < 0)
          return;
        open_editor_(
            "LOGO BRIGHTNESS", 0, 100, 10, logo, [](int v) { return format("%d%%", v); }, nullptr,
            [this]() {
              const int value = editor_.value;
              send_to_speakers_("{\"ui\":{\"logo\":{\"brightness\":" + std::to_string(value) + "}}}");
              for (auto &entry : network::get_device_states())
                entry.second.logo_brightness = value;
            });
      });

      add("Auto standby", standby_enabled < 0 ? "--" : (standby_enabled ? "on" : "off"), [this, standby_enabled]() {
        if (standby_enabled < 0)
          return;
        const bool target = standby_enabled == 0;
        send_to_speakers_(std::string("{\"device\":{\"standby\":{\"enabled\":") + (target ? "true" : "false") + "}}}");
        for (auto &entry : network::get_device_states())
          entry.second.auto_standby_enabled = target ? 1 : 0;
      });

      add("Standby time", standby_time < 0 ? "--" : format("%d min", standby_time), [this, standby_time]() {
        if (standby_time < 0)
          return;
        static const int values[] = {10, 30, 60, 90, 120, 240};
        const int next = next_value(values, standby_time);
        send_to_speakers_("{\"device\":{\"standby\":{\"auto_standby_time\":" + std::to_string(next) + "}}}");
        for (auto &entry : network::get_device_states())
          entry.second.auto_standby_time = next;
      });
      break;
    }

    case MenuId::VOLUME_SETUP: {
      title = "VOLUME SETUP";
      back();

      add("Max volume", format("%d dB", static_cast<int>(max_volume_)), [this]() {
        open_editor_(
            "MAX VOLUME", 10, static_cast<int>(max_volume_limit_), 5, static_cast<int>(max_volume_),
            [](int v) { return format("%d dB", v); }, [this](int v) { max_volume_ = v; },
            [this]() { save_settings_(); });
      });

      add("Volume step", format("%d dB", static_cast<int>(volume_step_)), [this]() {
        static const int values[] = {1, 2, 3, 5};
        volume_step_ = static_cast<float>(next_value(values, static_cast<int>(volume_step_)));
        save_settings_();
      });

      add("Backlight", format("%d%%", backlight_level_), [this]() {
        open_editor_(
            "BACKLIGHT", 5, 100, 5, backlight_level_, [](int v) { return format("%d%%", v); },
            [this](int v) {
              backlight_level_ = v;
              apply_brightness_();
            },
            [this]() { save_settings_(); });
      });

      add("Screen off", seconds_text(display_timeout_), [this]() {
        static const int values[] = {0, 30, 60, 300, 600};
        display_timeout_ = next_value(values, display_timeout_);
        save_settings_();
      });

      add("Deep sleep", seconds_text(deep_sleep_timeout_), [this]() {
        static const int values[] = {300, 600, 900, 1800, 0};
        set_deep_sleep_timeout(next_value(values, deep_sleep_timeout_));
      });
      break;
    }

    case MenuId::INFO: {
      title = "INFO";
      back();
      add("WiFi", format("%d dBm", static_cast<int>(wifi::global_wifi_component->wifi_rssi())), nullptr);
      std::string ip = "--";
      for (auto &address : wifi::global_wifi_component->wifi_sta_ip_addresses()) {
        if (address.is_set() && address.is_ip4()) {
          ip = address.str();
          break;
        }
      }
      add("IP", ip, nullptr);
      const uint32_t seconds = millis() / 1000;
      char uptime[24];
      snprintf(uptime, sizeof(uptime), "%luh %02lum", static_cast<unsigned long>(seconds / 3600),
               static_cast<unsigned long>(seconds / 60 % 60));
      add("Uptime", uptime, nullptr);
      add("Free heap", format("%d kB", static_cast<int>(esp_get_free_heap_size() / 1024)), nullptr);
      add("Build", __DATE__, nullptr);
      break;
    }
  }
  return items;
}

}  // namespace vol_ctrl
}  // namespace esphome
