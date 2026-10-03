// ...existing code...
#include "display.h"
#include "device_state.h"
#include <TFT_eSPI.h>
#include <algorithm>

namespace esphome {
namespace vol_ctrl {
namespace display {

// Screen layout. The top bar is two rows of font 4 (26 px): status icons, then the date/time.
const int TOP_ROW_HEIGHT = 28;
const int TOP_AREA_HEIGHT = 2 * TOP_ROW_HEIGHT;
const int BOTTOM_AREA_HEIGHT = 30;
// The volume digits (and the mute sign) are centred in what is left in between
const int VOLUME_AREA_Y = TOP_AREA_HEIGHT;
const int VOLUME_AREA_HEIGHT = 240 - TOP_AREA_HEIGHT - BOTTOM_AREA_HEIGHT;
const int VOLUME_CENTER_Y = VOLUME_AREA_Y + VOLUME_AREA_HEIGHT / 2;

// Menu rows: font 4, one row every MENU_ROW_HEIGHT px below the title
const int MENU_TOP = 32;
const int MENU_ROW_HEIGHT = 28;
const int MENU_LEFT = 22;  // room for the highlight dot

int menu_row_y(int position) {
  return MENU_TOP + position * MENU_ROW_HEIGHT;
}

// Screen region getters for partial updates
ScreenRegion get_standby_time_region() {
  return {0, 0, 50, TOP_ROW_HEIGHT};
}

// The wifi icon and the speaker blocks are as high as the digits of the standby time next to them
const int ICON_TOP = 3;
const int ICON_HEIGHT = 20;

ScreenRegion get_wifi_region() {
  return {52, ICON_TOP, 48, ICON_HEIGHT};
}

ScreenRegion get_speaker_dots_region() {
  return {108, ICON_TOP, 90, ICON_HEIGHT};  // Area where speaker dots appear
}

ScreenRegion get_wiim_region() {
  return {208, 0, 28, TOP_ROW_HEIGHT};
}

ScreenRegion get_datetime_region() {
  return {0, TOP_ROW_HEIGHT, 240, TOP_ROW_HEIGHT};  // second row, centred
}

ScreenRegion get_volume_region() {
  return {0, VOLUME_AREA_Y, 240, VOLUME_AREA_HEIGHT};  // Center area where volume is displayed
}

ScreenRegion get_bottom_line_region() {
  return {0, 240 - BOTTOM_AREA_HEIGHT, 240, BOTTOM_AREA_HEIGHT};
}

// Every drawing function sets the text datum and padding it relies on: they are global TFT state.
void update_standby_time(TFT_eSPI *tft, int standby_time) {
  ScreenRegion region = get_standby_time_region();
  tft->fillRect(region.x, region.y, region.w, region.h, TFT_BLACK);

  tft->setTextFont(4);
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextSize(1);
  tft->setTextDatum(TL_DATUM);
  char buf[16];
  if (standby_time > 0) {
    snprintf(buf, sizeof(buf), "%dm", standby_time);
  } else {
    snprintf(buf, sizeof(buf), "--m");
  }
  tft->drawString(buf, region.x, region.y + 1);
}

static uint16_t link_color(LinkState state) {
  switch (state) {
    case LinkState::UP: return TFT_GREEN;
    case LinkState::DOWN: return TFT_RED;
    default: return TFT_ORANGE;
  }
}

void update_wifi_status(TFT_eSPI *tft, LinkState state) {
  ScreenRegion region = get_wifi_region();
  uint16_t color = link_color(state);

  // Arcs centred on the bottom edge of the top row; the viewport clips the lower halves away
  const int cx = region.x + region.w / 2;
  const int cy = region.y + region.h;
  tft->setViewport(region.x, region.y, region.w, region.h, false);
  tft->fillRect(region.x, region.y, region.w, region.h, TFT_BLACK);
  tft->fillCircle(cx, cy, 3, color);
  for (int radius = 7; radius <= 19; radius += 6) {
    tft->drawCircle(cx, cy, radius, color);
    tft->drawCircle(cx, cy, radius - 1, color);
  }
  tft->resetViewport();
}

void update_wiim_status(TFT_eSPI *tft, LinkState state) {
  ScreenRegion region = get_wiim_region();
  tft->fillRect(region.x, region.y, region.w, region.h, TFT_BLACK);
  tft->setTextFont(4);
  tft->setTextSize(1);
  tft->setTextColor(link_color(state), TFT_BLACK);
  tft->setTextDatum(TL_DATUM);
  tft->drawString("W", region.x + 2, region.y + 1);
}

void update_speaker_dots(TFT_eSPI *tft, const std::map<std::string, DeviceState> &states) {
  ScreenRegion region = get_speaker_dots_region();
  int idx = 0;
  int rect_height = region.h;
  int rect_width = 14;
  int spacing = 4;

  for (const auto &entry : states) {
    const DeviceState &state = entry.second;
    uint16_t color = !state.known ? TFT_ORANGE : (state.is_up ? TFT_GREEN : TFT_RED);
    int x = region.x + idx * (rect_width + spacing);
    tft->fillRect(x, region.y, rect_width, rect_height, color);
    tft->drawRect(x, region.y, rect_width, rect_height, TFT_DARKGREY);
    idx++;
  }
}

void update_datetime(TFT_eSPI *tft, const std::string &datetime) {
  ScreenRegion region = get_datetime_region();
  tft->setTextFont(4);
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextSize(1);
  tft->setTextDatum(MC_DATUM);
  tft->setTextPadding(region.w);  // clears the rest of the row, so a shorter string leaves nothing behind
  tft->drawString(datetime.c_str(), region.x + region.w / 2, region.y + region.h / 2);
  tft->setTextPadding(0);
}

void update_volume_display(TFT_eSPI *tft, float volume, bool user_adjusting) {
  // Digits are drawn with a background colour, so a narrower number (100 -> 99) leaves stray columns of
  // the wider one behind. Clear the whole band only when the digit count changes.
  static int last_digits = 0;
  int digits = volume < 0.0f ? 2 : (static_cast<int>(volume) >= 100 ? 3 : 2);
  if (digits != last_digits) {
    ScreenRegion region = get_volume_region();
    tft->fillRect(region.x, region.y, region.w, region.h, TFT_BLACK);
    last_digits = digits;
  }

  tft->setTextFont(8);
  tft->setTextSize(1);
  tft->setTextDatum(MC_DATUM);
  char buf[8];

  // Format volume display
  if (volume < -0.0f) {
    snprintf(buf, sizeof(buf), " -- ");  // padded: overwrites leftover digits
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

  tft->drawString(buf, tft->width() / 2, VOLUME_CENTER_Y);
}

void update_mute_status(TFT_eSPI *tft, bool muted, float volume) {
  if (!muted) {
    // Wipe the red mute icon (it extends beyond the digits) before redrawing the volume
    ScreenRegion region = get_volume_region();
    tft->fillRect(region.x, region.y, region.w, region.h, TFT_BLACK);
    update_volume_display(tft, volume);
    return; // Avoid drawing mute sign if not muted
  }
  
  // Draw new mute icon if muted
  if (muted) {
    int x = tft->width()/2;
    int y = VOLUME_CENTER_Y;
    int halfsize = 36;
    int thickness = 8;
    int radius = halfsize+16;
    
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
  
  tft->setTextFont(4);
  tft->setTextColor(TFT_ORANGE, TFT_BLACK);
  tft->setTextSize(1);
  tft->setTextDatum(MC_DATUM);

  // Shorten long messages (e.g. track titles) so they stay on the screen
  std::string text = status;
  while (text.size() > 3 && tft->textWidth(text.c_str()) > region.w - 4) {
    text.resize(text.size() - 4);
    text += "...";
  }
  tft->drawString(text.c_str(), region.w / 2, region.y + region.h / 2);
}

// Menu drawing functions
int menu_visible_rows() {
  return (240 - MENU_TOP) / MENU_ROW_HEIGHT;
}

void draw_menu_row(TFT_eSPI *tft, const MenuRow &row, int visible_index, bool selected) {
  const int y = menu_row_y(visible_index);
  tft->fillRect(0, y, 234, MENU_ROW_HEIGHT, TFT_BLACK);
  tft->setTextFont(4);
  tft->setTextSize(1);
  tft->setTextPadding(0);

  if (selected)
    tft->fillCircle(9, y + 13, 6, TFT_ORANGE);

  tft->setTextColor(selected ? TFT_ORANGE : TFT_WHITE, TFT_BLACK);
  tft->setTextDatum(TL_DATUM);
  tft->drawString(row.label.c_str(), MENU_LEFT, y + 1);

  if (row.submenu) {
    // chevron
    tft->fillTriangle(222, y + 6, 222, y + 20, 230, y + 13, selected ? TFT_ORANGE : TFT_DARKGREY);
  } else if (!row.value.empty()) {
    tft->setTextColor(TFT_YELLOW, TFT_BLACK);
    tft->setTextDatum(TR_DATUM);
    tft->drawString(row.value.c_str(), 230, y + 1);
    tft->setTextDatum(TL_DATUM);
  }
}

void draw_menu(TFT_eSPI *tft, const std::string &title, const std::vector<MenuRow> &rows, int selected,
               int first_visible) {
  tft->fillScreen(TFT_BLACK);
  tft->setTextDatum(TL_DATUM);
  tft->setTextSize(1);
  tft->setTextPadding(0);
  tft->setTextFont(4);
  tft->setTextColor(TFT_ORANGE, TFT_BLACK);
  tft->drawString(title.c_str(), 10, 2);

  const int visible = menu_visible_rows();
  for (int i = 0; i < visible && first_visible + i < static_cast<int>(rows.size()); i++)
    draw_menu_row(tft, rows[first_visible + i], i, first_visible + i == selected);

  // Scroll bar when the list does not fit
  const int total = static_cast<int>(rows.size());
  if (total > visible) {
    const int track_y = MENU_TOP;
    const int track_h = visible * MENU_ROW_HEIGHT;
    tft->fillRect(236, track_y, 3, track_h, TFT_DARKGREY);
    const int thumb_h = track_h * visible / total;
    const int thumb_y = track_y + (track_h - thumb_h) * first_visible / (total - visible);
    tft->fillRect(236, thumb_y, 3, thumb_h, TFT_WHITE);
  }
}

void draw_editor_screen(TFT_eSPI *tft, const std::string &title, const std::string &value, float fraction) {
  tft->fillScreen(TFT_BLACK);
  tft->setTextSize(1);
  tft->setTextPadding(0);
  tft->setTextDatum(TL_DATUM);

  tft->setTextFont(4);
  tft->setTextColor(TFT_ORANGE, TFT_BLACK);
  tft->drawString(title.c_str(), 10, 2);

  tft->setTextFont(6);
  tft->setTextColor(TFT_YELLOW, TFT_BLACK);
  tft->drawString(value.c_str(), (tft->width() - tft->textWidth(value.c_str())) / 2, 60);

  const int BAR_WIDTH = 200;
  const int BAR_HEIGHT = 20;
  const int BAR_X = (tft->width() - BAR_WIDTH) / 2;
  const int BAR_Y = 140;
  tft->drawRect(BAR_X, BAR_Y, BAR_WIDTH, BAR_HEIGHT, TFT_WHITE);
  int fill_width = static_cast<int>((BAR_WIDTH - 4) * std::min(std::max(fraction, 0.0f), 1.0f));
  if (fill_width > 0)
    tft->fillRect(BAR_X + 2, BAR_Y + 2, fill_width, BAR_HEIGHT - 4, TFT_YELLOW);

  tft->setTextFont(4);
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextDatum(TC_DATUM);
  tft->drawString("Turn: adjust", tft->width() / 2, 176);
  tft->drawString("Press: save", tft->width() / 2, 206);
  tft->setTextDatum(TL_DATUM);
}

}  // namespace display
}  // namespace vol_ctrl
}  // namespace esphome
