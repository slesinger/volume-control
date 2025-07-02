// ...existing code...
#include "display.h"
#include "device_state.h"
#include <TFT_eSPI.h>

namespace esphome {
namespace vol_ctrl {
namespace display {

// Screen region constants
const int TOP_AREA_HEIGHT = 14;
const int BOTTOM_AREA_HEIGHT = 20;

// Screen region getters for partial updates
ScreenRegion get_standby_time_region() {
  return {0, 0, 41, TOP_AREA_HEIGHT};
}

ScreenRegion get_wifi_region() {
  return {42, 0, 20, TOP_AREA_HEIGHT};
}

ScreenRegion get_speaker_dots_region() {
  return {62, 12, 50, TOP_AREA_HEIGHT};  // Area where speaker dots appear
}

ScreenRegion get_datetime_region() {
  return {240, 0, 100, TOP_AREA_HEIGHT};
}

ScreenRegion get_volume_region() {
  return {0, TOP_AREA_HEIGHT+10, 240, 120};  // Center area where volume is displayed
}

ScreenRegion get_bottom_line_region() {
  return {0, 240 - BOTTOM_AREA_HEIGHT, 240, BOTTOM_AREA_HEIGHT};
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
    tft->drawString(buf, region.x, region.y);
  } else {
    tft->drawString("--m", region.x, region.y);
  }
}

void update_wifi_status(TFT_eSPI *tft, bool connected) {
  ScreenRegion region = get_wifi_region();
  // Draw icon
  int x = region.x;
  int y = region.y;
  uint16_t color = connected ? TFT_GREEN : TFT_RED;
  
  // Draw a dot at the bottom center
  tft->fillCircle(x, y + region.h, 2, color);
  
  // Draw three curves with increasing size to represent signal strength
  // Small arc
  tft->drawCircle(x, y + region.h, 5, color);
  tft->fillRect(x - 5, y + region.h, 10, 6, TFT_BLACK); // Erase bottom half
  
  // Medium arc 
  tft->drawCircle(x, y + region.h, 9, color);
  tft->fillRect(x - 9, y + region.h, 18, 10, TFT_BLACK); // Erase bottom half
  
  // Large arc
  tft->drawCircle(x, y + region.h, 13, color);
  tft->fillRect(x - 13, y + region.h, 28, 14, TFT_BLACK); // Erase bottom half
}

void update_speaker_dots(TFT_eSPI *tft, const std::map<std::string, DeviceState> &states) {
  ScreenRegion region = get_speaker_dots_region();
  int idx = 0;
  int rect_height = region.h;
  int rect_width = region.h / 2 + 2;
  int spacing = 4;

  for (const auto &entry : states) {
    const DeviceState &state = entry.second;
    uint16_t color = state.is_up ? TFT_GREEN : TFT_RED;
    int x = region.x + idx * (rect_width + spacing);
    tft->fillRect(x, 0, rect_width, rect_height, color);
    tft->drawRect(x, 0, rect_width, rect_height, TFT_DARKGREY);
    idx++;
  }
}

void update_datetime(TFT_eSPI *tft, const std::string &datetime) {
  ScreenRegion region = get_datetime_region();
  // Draw new datetime
  tft->setTextFont(2);
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextSize(1);
  tft->setTextDatum(TR_DATUM);
  tft->drawString(datetime.c_str(), region.x, region.y);
  tft->setTextDatum(MC_DATUM);
}

void update_volume_display(TFT_eSPI *tft, float volume, bool user_adjusting) {
  // Clear previous volume display
  ScreenRegion region = get_volume_region();
  // tft->fillRect(region.x, region.y, region.w, region.h, TFT_BLACK);

  // Draw new volume
  tft->setTextFont(8);
  tft->setTextSize(2);
  char buf[8];

  // Format volume display
  if (volume < -0.0f) {
    snprintf(buf, sizeof(buf), "--");
  } else {
    int vol_int = static_cast<int>(volume);
    snprintf(buf, sizeof(buf), "%02d", vol_int);
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

void update_mute_status(TFT_eSPI *tft, bool muted, float volume) {
  if (!muted) {
    update_volume_display(tft, volume);
    return; // Avoid drawing mute sign if not muted
  }
  
  // Draw new mute icon if muted
  if (muted) {
    int x = tft->width()/2;
    int y = tft->height()/2 - 8;
    int halfsize = 44;
    int thickness = 8;
    int radius = halfsize+18;
    
    for (int i = -thickness/2; i <= thickness/2; ++i) {
      tft->drawLine(x-halfsize+i, y-halfsize-i, x+halfsize+i, y+halfsize-i, TFT_RED);
    }
    for (int r = radius - thickness/2; r <= radius + thickness/2; ++r) {
      tft->drawCircle(x, y, r, TFT_RED);
    }
  }
}

void update_status_message(TFT_eSPI *tft, const std::string &status) {
  // Clear previous message
  ScreenRegion region = get_bottom_line_region();
  tft->fillRect(region.x, region.y, region.w, region.h, TFT_BLACK);
  
  tft->setTextFont(4);  // Use smaller font 2 for status messages to ensure they fit
  tft->setTextColor(TFT_ORANGE, TFT_BLACK);
  tft->setTextSize(1);
  tft->drawString(status.c_str(), region.w / 2, region.y);

}

// Menu drawing functions
void draw_menu_item_highlight(TFT_eSPI *tft, int position, int prev_position) {
  // Clear previous highlight
  if (prev_position >= 0) {
    tft->fillRect(0, 52 + (prev_position * 20), 14, 14, TFT_BLACK);
  }
  
  // Draw new highlight
  tft->fillCircle(7, 58 + (position * 20), 5, TFT_ORANGE);
}

void draw_menu_screen(TFT_eSPI *tft, int menu_level, int menu_position, int menu_items_count) {
  const int MENU_LEFT = 20; // Left margin for menu items
  // Clear screen
  tft->fillScreen(TFT_BLACK);
  tft->setTextDatum(TL_DATUM); // Top-left alignment
  
  // Draw menu title
  tft->setTextFont(4);
  tft->setTextColor(TFT_ORANGE, TFT_BLACK);
  
  if (menu_level == 0) {
    // Main menu
    tft->drawString("MENU", 10, 10);
    
    // Draw menu items
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->setTextFont(2);
    tft->drawString("1. Exit menu", MENU_LEFT, 50);
    tft->drawString("2. List speakers", MENU_LEFT, 70);
    tft->drawString("3. Speaker details", MENU_LEFT, 90);
    tft->drawString("4. Parametric EQ", MENU_LEFT, 110);
    tft->drawString("5. Discover devices", MENU_LEFT, 130);
    tft->drawString("6. Set speaker params", MENU_LEFT, 150);
    tft->drawString("7. Volume Control settings", MENU_LEFT, 170);
  } else if (menu_level == 1) {
    // Submenu rendering based on parent menu item
    switch (menu_items_count) {
      case 4: // Parametric EQ submenu
        tft->drawString("PARAMETRIC EQ", 10, 10);
        tft->setTextColor(TFT_WHITE, TFT_BLACK);
        tft->setTextFont(2);
        tft->drawString(".. Back", MENU_LEFT, 50);
        tft->drawString("1. List EQs", MENU_LEFT, 70);
        tft->drawString("2. Add EQ", MENU_LEFT, 90);
        break;
        
      case 6: // Speaker parameters submenu
        tft->drawString("SET SPEAKER PRMS", 0, 10);
        tft->setTextColor(TFT_WHITE, TFT_BLACK);
        tft->setTextFont(2);
        tft->drawString(".. Back", MENU_LEFT, 50);
        tft->drawString("1. Logo brightness", MENU_LEFT, 70);
        tft->drawString("2. Set delay", MENU_LEFT, 90);
        tft->drawString("3. Standby timeout", MENU_LEFT, 110);
        tft->drawString("4. Auto standby", MENU_LEFT, 130);
        break;
        
      case 7: // Volume settings submenu
        tft->drawString("VOLUME SETTINGS", 10, 10);
        tft->setTextColor(TFT_WHITE, TFT_BLACK);
        tft->setTextFont(2);
        tft->drawString(".. Back", MENU_LEFT, 50);
        tft->drawString("1. Volume step", MENU_LEFT, 70);
        tft->drawString("2. Backlight intensity", MENU_LEFT, 90);
        tft->drawString("3. Display timeout", MENU_LEFT, 110);
        tft->drawString("4. Deep sleep timeout", MENU_LEFT, 90);
        break;
        
      default:
        tft->drawString("SUBMENU ErRoR", 10, 10);
        tft->setTextFont(2);
        tft->drawString(".. Back", MENU_LEFT, 50);
        break;
    }
  }
  
  // Draw highlight for current position
  draw_menu_item_highlight(tft, menu_position, -1);
}

}  // namespace display
}  // namespace vol_ctrl
}  // namespace esphome
