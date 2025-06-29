#include "display.h"
#include "device_state.h"
#include <TFT_eSPI.h>

namespace esphome {
namespace vol_ctrl {
namespace display {

// Screen region constants
const int TOP_AREA_HEIGHT = 30;
const int MIDDLE_AREA_Y = 40;
const int MIDDLE_AREA_HEIGHT = 140;
const int BOTTOM_AREA_Y = 200;
const int BOTTOM_AREA_HEIGHT = 40;
const int WIFI_ICON_X = 60;
const int WIFI_ICON_Y = 17;
const int STANDBY_TIME_X = 15;
const int STANDBY_TIME_Y = 12;
const int DATETIME_X_END = 235;
const int DATETIME_Y = 4;

// Screen region getters for partial updates
ScreenRegion get_wifi_region(int x, int y) {
  return {static_cast<int16_t>(x - 15), static_cast<int16_t>(y - 15), static_cast<int16_t>(30), static_cast<int16_t>(30)};
}

ScreenRegion get_datetime_region() {
  return {DATETIME_X_END - 100, DATETIME_Y, 100, 20};
}

ScreenRegion get_standby_time_region() {
  return {STANDBY_TIME_X - 5, STANDBY_TIME_Y - 5, 40, 20};
}

ScreenRegion get_volume_region() {
  return {60, 80, 120, 80};  // Center area where volume is displayed
}

ScreenRegion get_mute_region() {
  return {90, 80, 60, 60};  // Area where mute icon appears
}

ScreenRegion get_standby_icon_region() {
  return {90, 40, 60, 30};  // Area where standby (Zzz) icon appears
}

ScreenRegion get_bottom_line_region() {
  return {20, BOTTOM_AREA_Y, 200, 30};
}

ScreenRegion get_speaker_dots_region() {
  return {85, 12, 90, 20};  // Area where speaker dots appear
}

ScreenRegion get_status_region() {
  return {40, BOTTOM_AREA_Y, 160, 30};  // Area where status messages appear
}

void draw_wifi_icon(TFT_eSPI *tft, int x, int y, bool connected) {
  uint16_t color = connected ? TFT_GREEN : TFT_RED;
  
  // Draw a dot at the bottom center
  tft->fillCircle(x, y + 5, 2, color);
  
  // Draw three curves with increasing size to represent signal strength
  if (connected) {
    // Small arc
    tft->drawCircle(x, y + 5, 5, color);
    tft->fillRect(x - 5, y + 5, 10, 6, TFT_BLACK); // Erase bottom half
    
    // Medium arc 
    tft->drawCircle(x, y + 5, 9, color);
    tft->fillRect(x - 9, y + 5, 18, 10, TFT_BLACK); // Erase bottom half
    
    // Large arc
    tft->drawCircle(x, y + 5, 13, color);
    tft->fillRect(x - 13, y + 5, 28, 14, TFT_BLACK); // Erase bottom half
  } else {
    // Draw a cross for disconnected state
    tft->drawLine(x - 6, y - 6, x + 6, y + 6, TFT_RED);
    tft->drawLine(x + 6, y - 6, x - 6, y + 6, TFT_RED);
  }
}

void draw_speaker_dots(TFT_eSPI *tft, int x, int y, const std::map<std::string, DeviceState> &states) {
  int dx = 18;
  int i = 0;
  for (const auto &entry : states) {
    const DeviceState &state = entry.second;
    bool up = (state.status == UP);
    bool standby = (up && state.standby);
    uint16_t color;
    if (!up) {
      color = TFT_RED; // Device down - red
    } else if (standby) {
      color = TFT_BLUE; // Standby - blue
    } else {
      color = TFT_GREEN; // Connected - green
    }
    tft->fillCircle(x + i * dx, y, 7, color);
    tft->drawCircle(x + i * dx, y, 7, TFT_WHITE);
    if (up && state.muted) {
      tft->setTextFont(1);
      tft->setTextColor(TFT_BLACK, color);
      tft->setTextSize(1);
      tft->setTextDatum(MC_DATUM);
      tft->drawString("M", x + i * dx, y);
    }
    i++;
  }
}

void draw_exclamation_mark(TFT_eSPI *tft, int x, int y, int height) {
  int mark_height = height / 2;
  int mark_width = mark_height / 4;
  int ex_x = x + height / 2; // right of number
  int ex_y = y - mark_height / 2;
  tft->fillRect(ex_x, ex_y, mark_width, mark_height - mark_width, TFT_RED); // vertical
  tft->fillRect(ex_x, ex_y + mark_height - mark_width, mark_width, mark_width, TFT_RED); // dot
}

void draw_forbidden_icon(TFT_eSPI *tft, int x, int y) {
  tft->drawCircle(x, y, 24, TFT_RED);
  tft->drawLine(x-17, y-17, x+17, y+17, TFT_RED);
  tft->drawLine(x+17, y-17, x-17, y+17, TFT_RED);
}

void draw_zzz_icon(TFT_eSPI *tft, int x, int y) {
  tft->setTextFont(4);  // Use smoother font 4
  tft->setTextColor(TFT_CYAN, TFT_BLACK);
  tft->setTextSize(1);
  tft->drawString("Zzz", x, y);
}

void draw_top_line(TFT_eSPI *tft, bool wifi_connected, const std::map<std::string, DeviceState> &states, int standby_time, const std::string &datetime) {
  tft->setTextFont(2);  // Use smaller smooth font 2 for header
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextSize(1);  // Normal size but smaller font
  int y = 12;  // Adjusted position for the smaller font
  
  // Standby time (left)
  char buf[16];
  if (standby_time > 0) {
    snprintf(buf, sizeof(buf), "%dm", standby_time);
    tft->drawString(buf, 15, y);
  } else {
    tft->drawString("--m", 15, y);
  }
  
  // WiFi icon (left of datetime)
  draw_wifi_icon(tft, 60, y+5, wifi_connected);
  
  // Speaker dots (left)
  draw_speaker_dots(tft, 90, y+5, states);
  
  // Date/time (right)
  tft->setTextDatum(TR_DATUM);
  tft->drawString(datetime.c_str(), tft->width()-5, y-8);
  tft->setTextDatum(MC_DATUM);
}

void draw_bottom_line(TFT_eSPI *tft, const std::string &status, bool normal) {
  int y = tft->height() - 28;  // Adjusted y position for larger font
  if (normal) {
    tft->setTextFont(4);  // Use smoother font 4
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->setTextSize(1);  // Reduced size since font 4 is larger
    tft->drawString("Press for menu", tft->width()/2, y);
  } else {
    tft->setTextFont(2);  // Use smaller font 2 for status messages to ensure they fit
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->setTextSize(1);
    tft->drawString(status.c_str(), tft->width()/2, y);
  }
}

void draw_middle_area(TFT_eSPI *tft, float volume, bool muted, bool standby, bool volume_ok) {
  int y = tft->height()/2 - 8;
  int font_height = 64; // Approximate for font 8, size 2
  tft->setTextFont(8);
  tft->setTextSize(2);
  char buf[8];
  // Always show the last known volume value, except for the initial invalid state
  if (volume == -999.0f) {
    // Show -- only for the initial invalid state
    snprintf(buf, sizeof(buf), "--");
  } else {
    // Show the volume value (current or previous)
    snprintf(buf, sizeof(buf), "%.0f", volume);
  }
  if (volume_ok) {
    tft->setTextColor(TFT_YELLOW, TFT_BLACK);
    tft->drawString(buf, tft->width()/2, y);
  } else {
    // Show previous known information with gray color
    tft->setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft->drawString(buf, tft->width()/2, y);
    // Show exclamation mark to indicate error retrieving data
    draw_exclamation_mark(tft, tft->width()/2 + 40, y, font_height); 
  }
  if (muted) draw_forbidden_icon(tft, tft->width()/2, y);
  if (standby) draw_zzz_icon(tft, tft->width()/2, y-40);
  
  // Draw volume bar with the last known value, even when there's an error
  // Only skip drawing for the initial invalid state
  if (volume != -999.0f) {
    int bar_width = tft->width() - 40;
    int bar_height = 8;
    int bar_y = y + 50;
    int fill_width = 0;
    if (volume >= -60 && volume <= 0) {
      fill_width = (int)((volume + 60) / 60.0f * bar_width);
    }
    // Always draw the empty bar outline
    tft->fillRect(20, bar_y, bar_width, bar_height, 0x4208);
    if (fill_width > 0) {
      uint16_t bar_color;
      if (volume_ok) {
        // Normal colors when data is valid
        bar_color = TFT_GREEN;
        if (volume > -15) bar_color = TFT_YELLOW;
        if (volume > -5) bar_color = TFT_RED;
      } else {
        // Use a dimmer color when showing previous data due to error
        bar_color = 0x3186; // Dimmer green
        if (volume > -15) bar_color = 0x5284; // Dimmer yellow
        if (volume > -5) bar_color = 0x4124;  // Dimmer red
      }
      tft->fillRect(20, bar_y, fill_width, bar_height, bar_color);
    }
  }
}

// Partial update functions

void update_wifi_status(TFT_eSPI *tft, int x, int y, bool connected, bool prev_connected) {
  if (connected == prev_connected) return; // No change
  
  // Clear previous icon
  ScreenRegion region = get_wifi_region(x, y);
  tft->fillRect(region.x, region.y, region.w, region.h, TFT_BLACK);
  
  // Draw new icon
  draw_wifi_icon(tft, x, y, connected);
}

void update_datetime(TFT_eSPI *tft, const std::string &datetime, const std::string &prev_datetime) {
  if (datetime == prev_datetime) return; // No change
  
  // Clear previous text
  ScreenRegion region = get_datetime_region();
  tft->fillRect(region.x, region.y, region.w, region.h, TFT_BLACK);
  
  // Draw new datetime
  tft->setTextFont(2);
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextSize(1);
  tft->setTextDatum(TR_DATUM);
  tft->drawString(datetime.c_str(), DATETIME_X_END, DATETIME_Y);
  tft->setTextDatum(MC_DATUM);
}

void update_standby_time(TFT_eSPI *tft, int standby_time, int prev_standby_time) {
  if (standby_time == prev_standby_time) return; // No change
  
  // Clear previous text
  ScreenRegion region = get_standby_time_region();
  tft->fillRect(region.x, region.y, region.w, region.h, TFT_BLACK);
  
  // Draw new standby time
  tft->setTextFont(2);
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextSize(1);
  char buf[16];
  if (standby_time > 0) {
    snprintf(buf, sizeof(buf), "%dm", standby_time);
    tft->drawString(buf, STANDBY_TIME_X, STANDBY_TIME_Y);
  } else {
    tft->drawString("--m", STANDBY_TIME_X, STANDBY_TIME_Y);
  }
}

// Backward-compatible version of update_volume_display for existing callers
void update_volume_display(TFT_eSPI *tft, float volume, float prev_volume, bool volume_ok) {
  // Call the full version with the same volume_ok value for previous state
  update_volume_display(tft, volume, prev_volume, volume_ok, volume_ok);
}

void update_volume_display(TFT_eSPI *tft, float volume, float prev_volume, bool volume_ok, bool prev_volume_ok) {
  if (volume == prev_volume && volume_ok == prev_volume_ok) return; // No change

  // Clear previous volume display
  ScreenRegion region = get_volume_region();
  tft->fillRect(region.x, region.y, region.w, region.h, TFT_BLACK);

  // Draw new volume
  tft->setTextFont(8);
  tft->setTextSize(2);
  char buf[8];

  // Format volume display
  if (volume == -999.0f) {
    snprintf(buf, sizeof(buf), "--");
  } else {
    snprintf(buf, sizeof(buf), "%.0f", volume);
  }

  // Set color based on status
  if (volume_ok) {
    tft->setTextColor(TFT_YELLOW, TFT_BLACK);
  } else {
    tft->setTextColor(TFT_DARKGREY, TFT_BLACK);
  }

  // Draw volume
  int y = tft->height()/2 - 8;
  tft->drawString(buf, tft->width()/2, y);

  // Add exclamation mark for error if needed
  if (!volume_ok && volume != -999.0f) {
    draw_exclamation_mark(tft, tft->width()/2 + 40, y, 64);
  }
  
  // Draw volume bar always when the big number updates
  if (volume != -999.0f) {
    int bar_width = tft->width() - 40;
    int bar_height = 8;
    int bar_x = 20; 
    int bar_y = tft->height() - 8;
    
    // Clear the bar area before drawing
    tft->fillRect(bar_x, bar_y, bar_width, bar_height, TFT_BLACK);
    // Draw bar outline
    tft->drawRect(bar_x, bar_y, bar_width, bar_height, TFT_WHITE);
    
    // Calculate filled portion (volume range from -60 to 0, scale to bar width)
    int fill_width = 0;
    if (volume >= -60 && volume <= 0) {
      fill_width = static_cast<int>((volume + 60) / 60.0f * bar_width);
    }
    
    if (fill_width > 0) {
      uint16_t fill_color = volume_ok ? TFT_YELLOW : TFT_DARKGREY;
      tft->fillRect(bar_x, bar_y, fill_width, bar_height, fill_color);
    }
  }

  // Draw volume bar always when the big number updates
  if (volume != -999.0f) {
    int bar_width = tft->width() - 40;
    int bar_height = 8;
    int bar_x = 20;
    int bar_y = tft->height() - 8;

    // Clear the bar area before drawing
    tft->fillRect(bar_x, bar_y, bar_width, bar_height, TFT_BLACK);
    // Draw bar outline
    tft->drawRect(bar_x, bar_y, bar_width, bar_height, TFT_WHITE);

    // Calculate filled portion (volume is 0-120, ensure float math)
    int fill_width = static_cast<int>((static_cast<float>(volume) / 120.0f) * static_cast<float>(bar_width));
    if (fill_width > 0) {
      uint16_t fill_color = volume_ok ? TFT_YELLOW : TFT_DARKGREY;
      tft->fillRect(bar_x, bar_y, fill_width, bar_height, fill_color);
    }
  }
}

void update_mute_status(TFT_eSPI *tft, bool muted, bool prev_muted) {
  if (muted == prev_muted) return; // No change
  
  // Clear previous mute icon
  ScreenRegion region = get_mute_region();
  tft->fillRect(region.x, region.y, region.w, region.h, TFT_BLACK);
  
  // Draw new mute icon if muted
  if (muted) {
    draw_forbidden_icon(tft, tft->width()/2, tft->height()/2 - 8);
  }
}

void update_standby_status(TFT_eSPI *tft, bool standby, bool prev_standby) {
  if (standby == prev_standby) return; // No change
  
  // Clear previous standby icon
  ScreenRegion region = get_standby_icon_region();
  tft->fillRect(region.x, region.y, region.w, region.h, TFT_BLACK);
  
  // Draw new standby icon if in standby
  if (standby) {
    draw_zzz_icon(tft, tft->width()/2, tft->height()/2 - 40);
  }
}

void update_status_message(TFT_eSPI *tft, const std::string &status, const std::string &prev_status, bool normal, bool prev_normal) {
  if (status == prev_status && normal == prev_normal) return; // No change
  
  // Clear previous message
  ScreenRegion region = get_bottom_line_region();
  tft->fillRect(region.x, region.y, region.w, region.h, TFT_BLACK);
  
  // Draw bottom line
  draw_bottom_line(tft, status, normal);
}

// Menu drawing functions
void draw_menu_item_highlight(TFT_eSPI *tft, int position, int prev_position) {
  // Clear previous highlight
  if (prev_position >= 0) {
    tft->fillRect(10, 50 + (prev_position * 20), 10, 10, TFT_BLACK);
  }
  
  // Draw new highlight
  tft->fillRect(10, 50 + (position * 20), 10, 10, TFT_YELLOW);
}

void draw_menu_screen(TFT_eSPI *tft, int menu_level, int menu_position, int menu_items_count) {
  // Clear screen
  tft->fillScreen(TFT_BLACK);
  tft->setTextDatum(TL_DATUM); // Top-left alignment
  
  // Draw menu title
  tft->setTextFont(4);
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  
  if (menu_level == 0) {
    // Main menu
    tft->drawString("MENU", 10, 10);
    
    // Draw menu items
    tft->setTextFont(2);
    tft->drawString("1. Exit menu", 20, 50);
    tft->drawString("2. Show devices", 20, 70);
    tft->drawString("3. Show settings", 20, 90);
    tft->drawString("4. Parametric EQ", 20, 110);
    tft->drawString("5. Discover devices", 20, 130);
    tft->drawString("6. Speaker parameters", 20, 150);
    tft->drawString("7. Volume settings", 20, 170);
  } else if (menu_level == 1) {
    // Submenu rendering based on parent menu item
    switch (menu_items_count) {
      case 4: // Parametric EQ submenu
        tft->drawString("PARAMETRIC EQ", 10, 10);
        tft->setTextFont(2);
        tft->drawString(".. Back", 20, 50);
        tft->drawString("1. List EQs", 20, 70);
        tft->drawString("2. Add EQ", 20, 90);
        break;
        
      case 6: // Speaker parameters submenu
        tft->drawString("SPEAKER PARAMETERS", 10, 10);
        tft->setTextFont(2);
        tft->drawString(".. Back", 20, 50);
        tft->drawString("1. Logo brightness", 20, 70);
        tft->drawString("2. Auto standby time", 20, 90);
        break;
        
      case 7: // Volume settings submenu
        tft->drawString("VOLUME SETTINGS", 10, 10);
        tft->setTextFont(2);
        tft->drawString(".. Back", 20, 50);
        tft->drawString("1. Volume step", 20, 70);
        tft->drawString("2. Deep sleep timeout", 20, 90);
        break;
        
      default:
        tft->drawString("SUBMENU", 10, 10);
        tft->setTextFont(2);
        tft->drawString(".. Back", 20, 50);
        break;
    }
  }
  
  // Draw highlight for current position
  draw_menu_item_highlight(tft, menu_position, -1);
}

}  // namespace display
}  // namespace vol_ctrl
}  // namespace esphome
