#include "display.h"
#include "device_state.h"
#include <TFT_eSPI.h>

namespace esphome {
namespace vol_ctrl {
namespace display {

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
  if (volume == -999.0f || !volume_ok) {
    // Show -- for the initial invalid state (-999.0f) or when volume data is not reliable
    snprintf(buf, sizeof(buf), "--");
  } else {
    snprintf(buf, sizeof(buf), "%.0f", volume);
  }
  if (volume_ok) {
    tft->setTextColor(TFT_YELLOW, TFT_BLACK);
    tft->drawString(buf, tft->width()/2, y);
  } else {
    tft->setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft->drawString(buf, tft->width()/2, y);
    draw_exclamation_mark(tft, tft->width()/2 + 40, y, font_height); // adjust offset as needed
  }
  if (muted) draw_forbidden_icon(tft, tft->width()/2, y);
  if (standby) draw_zzz_icon(tft, tft->width()/2, y-40);
  
  // Only draw volume bar if we have valid volume data
  if (volume != -999.0f && volume_ok) {
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
      uint16_t bar_color = TFT_GREEN;
      if (volume > -15) bar_color = TFT_YELLOW;
      if (volume > -5) bar_color = TFT_RED;
      tft->fillRect(20, bar_y, fill_width, bar_height, bar_color);
    }
  }
}

}  // namespace display
}  // namespace vol_ctrl
}  // namespace esphome
