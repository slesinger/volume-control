#pragma once

#include <string>
#include <map>
#include <vector>
#include <TFT_eSPI.h>
#include "device_state.h"

namespace esphome
{
    namespace vol_ctrl
    {
        namespace display
        {

            // State of something that is established asynchronously: shown orange while PENDING
            enum class LinkState { PENDING, UP, DOWN };

            // Display drawing functions
            void draw_wifi_icon(TFT_eSPI *tft, bool connected);
            void draw_forbidden_icon(TFT_eSPI *tft, int x, int y);

            // Partial section updates for efficient rendering
            void update_wifi_status(TFT_eSPI *tft, LinkState state);
            void update_wiim_status(TFT_eSPI *tft, LinkState state);
            void update_speaker_dots(TFT_eSPI *tft, const std::map<std::string, DeviceState> &states);
            void update_datetime(TFT_eSPI *tft, const std::string &datetime);
            void update_standby_time(TFT_eSPI *tft, int standby_countdown);
            void update_volume_display(TFT_eSPI *tft, float volume, bool user_adjusting = false);
            void update_mute_status(TFT_eSPI *tft, bool muted, float volume = -1.0f);
            void update_standby_status(TFT_eSPI *tft, bool standby, bool prev_standby);
            void clear_screen(TFT_eSPI *tft);  // repaints the stone background
            void update_track_info(TFT_eSPI *tft, const std::string &above, const std::string &below);
            void update_status_message(TFT_eSPI *tft, const std::string &status);

            // Menu drawing
            struct MenuRow
            {
                std::string label;
                std::string value; // right-aligned
                bool submenu = false;
            };
            int menu_visible_rows();
            void draw_menu(TFT_eSPI *tft, const std::string &title, const std::vector<MenuRow> &rows, int selected,
                           int first_visible);
            void draw_menu_row(TFT_eSPI *tft, const MenuRow &row, int visible_index, bool selected);
            // Full-screen value editor: big value and a bar (fraction 0..1)
            void draw_editor_screen(TFT_eSPI *tft, const std::string &title, const std::string &value, float fraction);

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
            ScreenRegion get_wiim_region();
            ScreenRegion get_speaker_dots_region();
            ScreenRegion get_datetime_region();
            ScreenRegion get_volume_region();
            ScreenRegion get_bottom_line_region();
        } // namespace display
    } // namespace vol_ctrl
} // namespace esphome
