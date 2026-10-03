#include "display.h"
#include "art.h"
#include "background.h"
#include "volume_font.h"
#include "device_state.h"
#include <TFT_eSPI.h>
#include <algorithm>

namespace esphome {
namespace vol_ctrl {
namespace display {


// ---- Background ----
// The default background is a 240x240 picture in flash (background.h), drawn in absolute screen coordinates so any
// partial update restores exactly what was there. While the WiiM plays, the album art replaces it (art.cpp).
// Text is drawn transparent (single-colour setTextColor) on top.
static uint32_t screen_epoch = 1;  // bumped whenever the whole screen is wiped; cached draws compare against it

static void fill_bg(TFT_eSPI *tft, int x, int y, int w, int h) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > 240) w = 240 - x;
  if (y + h > 240) h = 240 - y;
  if (w <= 0 || h <= 0) return;
  static uint16_t buf[240 * 8];
  tft->setSwapBytes(true);
  const bool album = art::ready();
  for (int row = 0; row < h; row += 8) {
    const int rows = std::min(8, h - row);
    for (int r = 0; r < rows; r++) {
      if (album) {
        art::row(y + row + r, x, w, &buf[r * w]);
        continue;
      }
      memcpy(&buf[r * w], &BACKGROUND_IMAGE[(y + row + r) * 240 + x], w * sizeof(uint16_t));
    }
    tft->pushImage(x, y + row, w, rows, buf);
  }
}

void clear_screen(TFT_eSPI *tft) {
  screen_epoch++;
  fill_bg(tft, 0, 0, 240, 240);
}

// Keeps only plain ASCII: the built-in fonts have no other glyphs
static std::string ascii_only(const std::string &in) {
  std::string out;
  for (unsigned char c : in)
    if (c >= 32 && c < 127) out += static_cast<char>(c);
  return out;
}

static void shorten(TFT_eSPI *tft, std::string &text, int max_width) {
  while (text.size() > 3 && tft->textWidth(text.c_str()) > max_width) {
    text.resize(text.size() - 4);
    text += "...";
  }
}

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
const int ICON_HEIGHT = 17;

ScreenRegion get_wifi_region() {
  return {52, ICON_TOP, 48, ICON_HEIGHT};
}

ScreenRegion get_wiim_region() {
  return {140, 0, 20, TOP_AREA_HEIGHT}; // Position WiiM indicator after datetime but before edge
}

ScreenRegion get_speaker_dots_region() {
  return {108, ICON_TOP, 68, ICON_HEIGHT};  // Area where speaker dots appear
}

ScreenRegion get_wiim_region() {
  return {176, 0, 64, TOP_ROW_HEIGHT};
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
  fill_bg(tft, region.x, region.y, region.w, region.h);

  tft->setTextFont(4);
  tft->setTextColor(TFT_WHITE);
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
  fill_bg(tft, region.x, region.y, region.w, region.h);
  tft->fillCircle(cx, cy, 3, color);
  for (int radius = 5; radius <= 15; radius += 5) {
    tft->drawCircle(cx, cy, radius, color);
    tft->drawCircle(cx, cy, radius - 1, color);
  }
  tft->resetViewport();
}

void update_wiim_status(TFT_eSPI *tft, LinkState state) {
  ScreenRegion region = get_wiim_region();
  fill_bg(tft, region.x, region.y, region.w, region.h);
  tft->setTextFont(4);
  tft->setTextSize(1);
  tft->setTextColor(link_color(state));
  tft->setTextDatum(TR_DATUM);
  tft->drawString("WiiM", region.x + region.w - 2, region.y + 1);
  tft->setTextDatum(TL_DATUM);
}

void update_wiim_status(TFT_eSPI *tft, bool available) {
  ScreenRegion region = get_wiim_region();
  // Draw WiiM status indicator
  uint16_t color = available ? TFT_GREEN : TFT_RED;
  
  tft->setTextFont(2);
  tft->setTextSize(1);
  tft->setTextColor(color, TFT_BLACK);
  tft->drawString("W", region.x, region.y+8); // "W" for WiiM
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
  static std::string last;
  static uint32_t seen_epoch = 0;
  if (seen_epoch == screen_epoch && datetime == last)
    return;
  seen_epoch = screen_epoch;
  last = datetime;
  ScreenRegion region = get_datetime_region();
  fill_bg(tft, region.x, region.y, region.w, region.h);
  tft->setTextFont(4);
  tft->setTextColor(TFT_WHITE);
  tft->setTextSize(1);
  tft->setTextDatum(MC_DATUM);
  tft->setTextPadding(0);
  tft->drawString(datetime.c_str(), region.x + region.w / 2, region.y + region.h / 2);
}

// The digits and the mute sign share this band; artist/album and title live above and below it
const int DIGITS_HALF = 42;
static ScreenRegion digits_band() {
  return {0, VOLUME_CENTER_Y - DIGITS_HALF, 240, 2 * DIGITS_HALF};
}
static ScreenRegion above_band() {
  return {0, VOLUME_AREA_Y + 4, 240, 28};
}
static ScreenRegion below_band() {
  return {0, VOLUME_AREA_Y + VOLUME_AREA_HEIGHT - 32, 240, 28};
}

static bool volume_dirty = true;  // something else painted over the digits band

void update_volume_display(TFT_eSPI *tft, float volume, bool user_adjusting) {
  static int last_value = -2;
  static bool last_adjusting = false;
  static uint32_t seen_epoch = 0;
  int value = volume < 0.0f ? -1 : static_cast<int>(volume);
  if (!volume_dirty && seen_epoch == screen_epoch && value == last_value && user_adjusting == last_adjusting)
    return;
  volume_dirty = false;
  seen_epoch = screen_epoch;
  last_value = value;
  last_adjusting = user_adjusting;

  ScreenRegion band = digits_band();
  fill_bg(tft, band.x, band.y, band.w, band.h);
  tft->setFreeFont(&OrbitronDigits);  // digits are 66 px tall and sit on the baseline
  tft->setTextSize(1);
  tft->setTextPadding(0);
  tft->setTextDatum(C_BASELINE);
  char buf[8];

  // Format volume display
  if (volume < -0.0f) {
    snprintf(buf, sizeof(buf), " -- ");
  } else {
    int vol_int = static_cast<int>(volume);
    snprintf(buf, sizeof(buf), "%02d", vol_int);
  }

  int x = tft->width() / 2;
  int y = VOLUME_CENTER_Y;
  int halfsize = 26;
  int thickness = 7;
  int radius = 38;
  for (int i = -thickness / 2; i <= thickness / 2; ++i) {
    tft->drawLine(x - halfsize + i, y - halfsize - i, x + halfsize + i, y + halfsize - i, TFT_RED);
  }
  for (int r = radius - thickness / 2; r <= radius + thickness / 2; ++r) {
    tft->drawCircle(x, y, r, TFT_RED);
  }
}

// Artist / album above the volume, song title below (either may be empty)
static void draw_centered_line(TFT_eSPI *tft, const ScreenRegion &region, const std::string &raw) {
  fill_bg(tft, region.x, region.y, region.w, region.h);
  if (raw.empty())
    return;
  std::string text = ascii_only(raw);
  tft->setTextFont(4);
  tft->setTextSize(1);
  tft->setTextPadding(0);
  tft->setTextColor(TFT_WHITE);
  tft->setTextDatum(MC_DATUM);
  shorten(tft, text, region.w - 8);
  tft->drawString(text.c_str(), region.w / 2, region.y + region.h / 2);
}

void update_track_info(TFT_eSPI *tft, const std::string &above, const std::string &below) {
  static std::string last_above, last_below;
  static uint32_t seen_epoch = 0;
  if (seen_epoch != screen_epoch) {
    last_above = last_below = "\x01";  // force both
    seen_epoch = screen_epoch;
  }
  if (above != last_above) {
    draw_centered_line(tft, above_band(), above);
    last_above = above;
  }
  if (below != last_below) {
    draw_centered_line(tft, below_band(), below);
    last_below = below;
  }
}

void update_status_message(TFT_eSPI *tft, const std::string &status) {
  static std::string last;
  static uint32_t seen_epoch = 0;
  if (seen_epoch == screen_epoch && status == last)
    return;
  seen_epoch = screen_epoch;
  last = status;
  ScreenRegion region = get_bottom_line_region();
  fill_bg(tft, region.x, region.y, region.w, region.h);

  tft->setTextFont(4);
  tft->setTextColor(TFT_ORANGE);
  tft->setTextSize(1);
  tft->setTextPadding(0);
  tft->setTextDatum(MC_DATUM);
  std::string text = ascii_only(status);
  shorten(tft, text, region.w - 4);
  tft->drawString(text.c_str(), region.w / 2, region.y + region.h / 2);
}

// Menu drawing functions
int menu_visible_rows() {
  return (240 - MENU_TOP) / MENU_ROW_HEIGHT;
}

void draw_menu_row(TFT_eSPI *tft, const MenuRow &row, int visible_index, bool selected) {
  const int y = menu_row_y(visible_index);
  fill_bg(tft, 0, y, 234, MENU_ROW_HEIGHT);
  tft->setTextFont(4);
  tft->setTextSize(1);
  tft->setTextPadding(0);

  if (selected)
    tft->fillCircle(9, y + 13, 6, TFT_ORANGE);

  tft->setTextColor(selected ? TFT_ORANGE : TFT_WHITE);
  tft->setTextDatum(TL_DATUM);
  tft->drawString(row.label.c_str(), MENU_LEFT, y + 1);

  if (row.submenu) {
    // chevron
    tft->fillTriangle(222, y + 6, 222, y + 20, 230, y + 13, selected ? TFT_ORANGE : TFT_DARKGREY);
  } else if (!row.value.empty()) {
    tft->setTextColor(TFT_YELLOW);
    tft->setTextDatum(TR_DATUM);
    tft->drawString(row.value.c_str(), 230, y + 1);
    tft->setTextDatum(TL_DATUM);
  }
}

void draw_menu(TFT_eSPI *tft, const std::string &title, const std::vector<MenuRow> &rows, int selected,
               int first_visible) {
  clear_screen(tft);
  tft->setTextDatum(TL_DATUM);
  tft->setTextSize(1);
  tft->setTextPadding(0);
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
        tft->drawString("4. Deep sleep timeout", MENU_LEFT, 130);
        break;
        
      default:
        tft->drawString("SUBMENU ErRoR", 10, 10);
        tft->setTextFont(2);
        tft->drawString(".. Back", MENU_LEFT, 50);
        break;
    }
  }
}

void draw_editor_screen(TFT_eSPI *tft, const std::string &title, const std::string &value, float fraction) {
  clear_screen(tft);
  tft->setTextSize(1);
  tft->setTextPadding(0);
  tft->setTextDatum(TL_DATUM);

  tft->setTextFont(4);
  tft->setTextColor(TFT_ORANGE);
  tft->drawString(title.c_str(), 10, 2);

  tft->setTextFont(6);
  tft->setTextColor(TFT_YELLOW);
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
  tft->setTextColor(TFT_WHITE);
  tft->setTextDatum(TC_DATUM);
  tft->drawString("Turn: adjust", tft->width() / 2, 176);
  tft->drawString("Press: save", tft->width() / 2, 206);
  tft->setTextDatum(TL_DATUM);
}

void draw_brightness_adjustment_screen(TFT_eSPI *tft, int brightness) {
  // Clear screen
  tft->fillScreen(TFT_BLACK);
  tft->setTextDatum(TL_DATUM); // Top-left alignment
  
  // Draw title
  tft->setTextFont(4);
  tft->setTextColor(TFT_ORANGE, TFT_BLACK);
  tft->drawString("BRIGHTNESS", 10, 10);
  
  // Draw current brightness value
  tft->setTextFont(6);
  tft->setTextColor(TFT_YELLOW, TFT_BLACK);
  char brightness_str[16];
  snprintf(brightness_str, sizeof(brightness_str), "%d%%", brightness);
  
  // Center the brightness value
  int text_width = tft->textWidth(brightness_str);
  int x_pos = (tft->width() - text_width) / 2;
  tft->drawString(brightness_str, x_pos, 80);
  
  // Draw progress bar
  const int BAR_WIDTH = 200;
  const int BAR_HEIGHT = 20;
  const int BAR_X = (tft->width() - BAR_WIDTH) / 2;
  const int BAR_Y = 150;
  
  // Draw bar outline
  tft->drawRect(BAR_X, BAR_Y, BAR_WIDTH, BAR_HEIGHT, TFT_WHITE);
  
  // Fill bar based on brightness level
  int fill_width = (BAR_WIDTH - 4) * brightness / 100;
  if (fill_width > 0) {
    tft->fillRect(BAR_X + 2, BAR_Y + 2, fill_width, BAR_HEIGHT - 4, TFT_YELLOW);
  }
  
  // Draw instructions
  tft->setTextFont(2);
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextDatum(TC_DATUM); // Top-center alignment
  tft->drawString("Turn encoder to adjust", tft->width() / 2, 190);
  tft->drawString("Press button to save", tft->width() / 2, 210);
}

}  // namespace display
}  // namespace vol_ctrl
}  // namespace esphome
