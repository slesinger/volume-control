#pragma once

#include <string>
#include <map>
#include <TFT_eSPI.h>
#include "device_state.h"

namespace esphome {
namespace vol_ctrl {
namespace display {

// Display drawing functions
void draw_wifi_icon(TFT_eSPI *tft, int x, int y, bool connected);
void draw_speaker_dots(TFT_eSPI *tft, int x, int y, const std::map<std::string, DeviceState> &states);
void draw_forbidden_icon(TFT_eSPI *tft, int x, int y);
void draw_zzz_icon(TFT_eSPI *tft, int x, int y);

// Full section drawing
void draw_top_line(TFT_eSPI *tft, bool wifi_connected, const std::map<std::string, DeviceState> &states, int standby_time, const std::string &datetime);
void draw_bottom_line(TFT_eSPI *tft, const std::string &status, bool normal);
void draw_middle_area(TFT_eSPI *tft, float volume, bool muted, bool standby, bool volume_ok);
void draw_exclamation_mark(TFT_eSPI *tft, int x, int y, int height);

// Partial section updates for efficient rendering
void update_wifi_status(TFT_eSPI *tft, int x, int y, bool connected, bool prev_connected);
void update_datetime(TFT_eSPI *tft, const std::string &datetime, const std::string &prev_datetime);
void update_standby_time(TFT_eSPI *tft, int standby_time, int prev_standby_time);
void update_volume_display(TFT_eSPI *tft, float volume, float prev_volume, bool volume_ok);
void update_volume_display(TFT_eSPI *tft, float volume, float prev_volume, bool volume_ok, bool prev_volume_ok);
void update_mute_status(TFT_eSPI *tft, bool muted, bool prev_muted);
void update_standby_status(TFT_eSPI *tft, bool standby, bool prev_standby);
void update_status_message(TFT_eSPI *tft, const std::string &status, const std::string &prev_status, bool normal, bool prev_normal);

// Menu drawing functions
void draw_menu_item_highlight(TFT_eSPI *tft, int position, int prev_position);
void draw_menu_screen(TFT_eSPI *tft, int menu_level, int menu_position, int menu_items_count);

// Get screen regions for partial updates
struct ScreenRegion {
    int16_t x;
    int16_t y;
    uint16_t w;
    uint16_t h;
};

ScreenRegion get_wifi_region(int x, int y);
ScreenRegion get_datetime_region();
ScreenRegion get_standby_time_region();
ScreenRegion get_volume_region();
ScreenRegion get_mute_region();
ScreenRegion get_standby_icon_region();
ScreenRegion get_bottom_line_region();
ScreenRegion get_speaker_dots_region();
ScreenRegion get_status_region();

}  // namespace display
}  // namespace vol_ctrl
}  // namespace esphome
