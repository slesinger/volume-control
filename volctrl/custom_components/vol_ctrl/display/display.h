#pragma once

#include <string>
#include <map>
#include <TFT_eSPI.h>
#include "../device_state.h"

namespace esphome {
namespace vol_ctrl {
namespace display {

// Display drawing functions
void draw_wifi_icon(TFT_eSPI *tft, int x, int y, bool connected);
void draw_speaker_dots(TFT_eSPI *tft, int x, int y, const std::map<std::string, DeviceState> &states);
void draw_forbidden_icon(TFT_eSPI *tft, int x, int y);
void draw_zzz_icon(TFT_eSPI *tft, int x, int y);
void draw_top_line(TFT_eSPI *tft, bool wifi_connected, const std::map<std::string, DeviceState> &states, int standby_time, const std::string &datetime);
void draw_bottom_line(TFT_eSPI *tft, const std::string &status, bool normal);
void draw_middle_area(TFT_eSPI *tft, float volume, bool muted, bool standby);

}  // namespace display
}  // namespace vol_ctrl
}  // namespace esphome
