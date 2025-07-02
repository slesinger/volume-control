#pragma once

#include <string>
#include <map>
#include <TFT_eSPI.h>
#include "device_state.h"

namespace esphome
{
    namespace vol_ctrl
    {
        namespace display
        {

            // Display drawing functions
            void draw_wifi_icon(TFT_eSPI *tft, bool connected);
            void draw_forbidden_icon(TFT_eSPI *tft, int x, int y);

            // Partial section updates for efficient rendering
            void update_wifi_status(TFT_eSPI *tft, bool connected);
            void update_speaker_dots(TFT_eSPI *tft, const std::map<std::string, DeviceState> &states);
            void update_datetime(TFT_eSPI *tft, const std::string &datetime);
            void update_standby_time(TFT_eSPI *tft, int standby_countdown);
            void update_volume_display(TFT_eSPI *tft, float volume, bool user_adjusting = false);
            void update_mute_status(TFT_eSPI *tft, bool muted);
            void update_standby_status(TFT_eSPI *tft, bool standby, bool prev_standby);
            void update_status_message(TFT_eSPI *tft, const std::string &status);

            // Menu drawing functions
            void draw_menu_item_highlight(TFT_eSPI *tft, int position, int prev_position);
            void draw_menu_screen(TFT_eSPI *tft, int menu_level, int menu_position, int menu_items_count);

            // Get screen regions for partial updates
            struct ScreenRegion
            {
                int16_t x;
                int16_t y;
                uint16_t w;
                uint16_t h;
            };

            ScreenRegion get_standby_time_region();
            ScreenRegion get_wifi_region();
            ScreenRegion get_speaker_dots_region();
            ScreenRegion get_datetime_region();
            ScreenRegion get_volume_region();
            ScreenRegion get_bottom_line_region();
        } // namespace display
    } // namespace vol_ctrl
} // namespace esphome
