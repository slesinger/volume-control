// ...existing code...
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

void update_wifi_status(TFT_eSPI *tft, int x, int y, bool connected) {
  // Clear previous icon
  ScreenRegion region = get_wifi_region(x, y);
  tft->fillRect(region.x, region.y, region.w, region.h, TFT_BLACK);
  
  // Draw new icon
  draw_wifi_icon(tft, x, y, connected);
}

void update_speaker_dots(TFT_eSPI *tft, const std::map<std::string, DeviceState> &states) {
  int idx = 0;
  int x = 94; // starting x coordinate for the first dot (adjust as needed)
  int y = 22; // y coordinate for the dots (adjust as needed)
  int dx = 18; // horizontal spacing between dots (should match update_speaker_dots)
  for (const auto &entry : states) {
    const DeviceState &state = entry.second;
    // You may want to clear the area before drawing each dot if needed
    uint16_t color;
    if (!state.is_up) {
      color = TFT_RED;
    } else {
      color = TFT_GREEN;
    }
    tft->fillCircle(x + idx * dx, y, 7, color);
    tft->drawCircle(x + idx * dx, y, 7, TFT_WHITE);
    idx++;
  }
}

void update_datetime(TFT_eSPI *tft, const std::string &datetime) {
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

void update_standby_time(TFT_eSPI *tft, int standby_time) {
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

void update_volume_display(TFT_eSPI *tft, float volume, bool user_adjusting) {  // TODO make sure this function for requested (blue) volume also
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
  if (user_adjusting) {
    // Use blue for user-initiated changes as per requirements
    tft->setTextColor(TFT_BLUE, TFT_BLACK);
  } else {
    tft->setTextColor(TFT_YELLOW, TFT_BLACK);
  }

  // Draw volume
  int y = tft->height()/2 - 8;
  tft->drawString(buf, tft->width()/2, y);
}

void update_mute_status(TFT_eSPI *tft, bool muted) {
  if (!muted) return; // Avoid drawing mute sign if not muted
  
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
  
}

void update_status_message(TFT_eSPI *tft, const std::string &status) {
  // Clear previous message
  ScreenRegion region = get_bottom_line_region();
  tft->fillRect(region.x, region.y, region.w, region.h, TFT_BLACK);
  
  int y = tft->height() - 28;  // Adjusted y position for larger font
  tft->setTextFont(2);  // Use smaller font 2 for status messages to ensure they fit
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextSize(1);
  tft->drawString(status.c_str(), tft->width()/2, y);

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
