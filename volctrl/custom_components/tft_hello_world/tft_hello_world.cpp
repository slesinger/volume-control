#include "esphome/core/log.h"
#include <TFT_eSPI.h>
#include "tft_hello_world.h"
#include <map>
#include <string>
#include "esphome/components/wifi/wifi_component.h"
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include "esphome/core/time.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include <time.h>

namespace esphome {
namespace tft_hello_world {

static const char *const TAG = "tft_hello_world";

static const std::map<std::string, std::string> DEVICE_MAP = {
  {"Left-6473470117", "2a00:1028:8390:75ee:2a36:38ff:fe61:25b9"},
  {"Right-6194478038", "2a00:1028:8390:75ee:2a36:38ff:fe61:279e"}
};

bool ran_query = false;

// Device status cache
enum DeviceStatus { UNKNOWN, UP, DOWN };
struct DeviceState {
  DeviceStatus status = UNKNOWN;
  uint32_t last_check = 0;
};
static std::map<std::string, DeviceState> device_states;

// Rotating symbol state
static std::map<std::string, int> device_rot;
const char ROT_SYMBOLS[] = {'|', '/', '-', '\\'};
const int ROT_SYMBOLS_LEN = 4;

bool is_device_up(const std::string &ipv6) {
  uint32_t now = millis();
  auto &state = device_states[ipv6];
  // Check every 10 seconds
  if (state.status != UNKNOWN && now - state.last_check < 10000) {
    return state.status == UP;
  }
  state.last_check = now;
  // Rotate symbol for this device
  device_rot[ipv6] = (device_rot[ipv6] + 1) % ROT_SYMBOLS_LEN;
  // Try to open a UDP socket to the IPv6 address, port 12345
  int sock = socket(AF_INET6, SOCK_DGRAM, 0);
  if (sock < 0) {
    state.status = DOWN;
    return false;
  }
  struct sockaddr_in6 sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin6_family = AF_INET6;
  sa.sin6_port = htons(12345); // arbitrary port
  if (inet_pton(AF_INET6, ipv6.c_str(), &sa.sin6_addr) != 1) {
    close(sock);
    state.status = DOWN;
    return false;
  }
  int res = connect(sock, (struct sockaddr *)&sa, sizeof(sa));
  close(sock);
  if (res == 0) {
    state.status = UP;
    return true;
  } else {
    state.status = DOWN;
    return false;
  }
}

// Find and use the time component to access RTC
static ESPTime last_time;

void TFTHelloWorld::setup() {
  ESP_LOGCONFIG(TAG, "Setting up TFT Hello World...");
  this->tft_ = new TFT_eSPI();
  this->tft_->init();
  this->tft_->setRotation(2);
  this->tft_->fillScreen(TFT_BLACK);
  this->tft_->setTextColor(TFT_WHITE, TFT_BLACK);
  this->tft_->setTextSize(3);
  this->tft_->setTextDatum(MC_DATUM);
  this->tft_->drawString("Ahoj Medliku", this->tft_->width() / 2, this->tft_->height() / 2);
  // Show connecting to wifi message below main text
  this->tft_->setTextSize(2);
  int y = this->tft_->height() / 2 + 40;
  this->tft_->drawString("Connecting to wifi", this->tft_->width() / 2, y);
}

// Helper to format date/time string
std::string get_datetime_string() {
  time_t now = ::time(nullptr);
  if (now != 0) {
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char buf[32];
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int month = timeinfo.tm_mon;
    snprintf(buf, sizeof(buf), "%02d:%02d %s %02d", 
             timeinfo.tm_hour, timeinfo.tm_min,
             (month >= 0 && month < 12) ? months[month] : "---",
             timeinfo.tm_mday);
    return std::string(buf);
  }
  return "--:-- --- --";
}

void draw_wifi_icon(TFT_eSPI *tft, int x, int y, bool connected) {
  uint16_t color = connected ? TFT_GREEN : TFT_RED;
  tft->fillCircle(x, y, 8, color);
  tft->drawCircle(x, y, 8, TFT_WHITE);
  tft->drawCircle(x, y, 5, TFT_WHITE);
  tft->drawCircle(x, y, 2, TFT_WHITE);
}

void draw_speaker_dots(TFT_eSPI *tft, int x, int y, const std::map<std::string, DeviceState> &states) {
  int dx = 18;
  int i = 0;
  for (const auto &entry : DEVICE_MAP) {
    const std::string &ipv6 = entry.second;
    bool up = (states.count(ipv6) && states.at(ipv6).status == UP);
    uint16_t color = up ? TFT_GREEN : 0x8410; // gray
    tft->fillCircle(x + i * dx, y, 7, color);
    tft->drawCircle(x + i * dx, y, 7, TFT_WHITE);
    i++;
  }
}

void draw_forbidden_icon(TFT_eSPI *tft, int x, int y) {
  tft->drawCircle(x, y, 24, TFT_RED);
  tft->drawLine(x-17, y-17, x+17, y+17, TFT_RED);
}

void draw_zzz_icon(TFT_eSPI *tft, int x, int y) {
  tft->setTextColor(TFT_CYAN, TFT_BLACK);
  tft->setTextSize(2);
  tft->drawString("Zzz", x, y);
}

void draw_top_line(TFT_eSPI *tft, bool wifi_connected, const std::map<std::string, DeviceState> &states, int standby_time, const std::string &datetime) {
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextSize(1);
  int y = 10;
  // Standby time (left)
  char buf[16];
  snprintf(buf, sizeof(buf), "%dm", standby_time);
  tft->drawString(buf, 5, y);
  // WiFi icon (left of datetime)
  draw_wifi_icon(tft, 60, y+5, wifi_connected);
  // Speaker dots (left)
  draw_speaker_dots(tft, 90, y+5, states);
  // Date/time (right)
  tft->setTextDatum(TR_DATUM);
  tft->drawString(datetime.c_str(), tft->width()-5, y);
  tft->setTextDatum(MC_DATUM);
}

void draw_bottom_line(TFT_eSPI *tft, const std::string &status, bool normal) {
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextSize(2);
  int y = tft->height() - 24;
  if (normal) {
    tft->drawString("Press button to enter menu", tft->width()/2, y);
  } else {
    tft->drawString(status.c_str(), tft->width()/2, y);
  }
}

void draw_middle_area(TFT_eSPI *tft, float volume, bool muted, bool standby) {
  int y = tft->height()/2;
  tft->setTextColor(TFT_YELLOW, TFT_BLACK);
  tft->setTextSize(6);
  char buf[8];
  snprintf(buf, sizeof(buf), "%.0f", volume);
  tft->drawString(buf, tft->width()/2, y);
  if (muted) draw_forbidden_icon(tft, tft->width()/2, y);
  if (standby) draw_zzz_icon(tft, tft->width()/2, y-40);
}

void TFTHelloWorld::loop() {
  // Example state variables (replace with real logic)
  static bool wifi_connected_prev = false;
  static bool muted = false;
  static bool standby = false;
  static float volume = 40.0;
  static int standby_time = 90;
  static std::string status = "Connecting to WiFi";
  static std::string datetime = "12:34 Jun 24";
  static std::string datetime_prev = "";
  static bool normal = false;
  static bool normal_prev = false;
  static uint32_t last_redraw = 0;
  static uint32_t last_device_check = 0;
  static bool first_run = true;
  
  // Check for active devices every 2 seconds
  uint32_t now = millis();
  if (now - last_device_check > 2000 || first_run) {
    last_device_check = now;
    for (const auto &entry : DEVICE_MAP) {
      is_device_up(entry.second);
    }
  }
  
  // Only update screen every 100ms to avoid flicker
  if (now - last_redraw < 100 && !first_run) return;
  last_redraw = now;
  first_run = false;

  bool wifi_connected = wifi::global_wifi_component->is_connected();
  
  // Update status based on connection state
  if (!wifi_connected) {
    status = "Connecting to WiFi";
    normal = false;
  } else if (device_states.empty()) {
    status = "No speakers connected";
    normal = false;
  } else {
    // Check if any device is UP
    bool any_device_up = false;
    for (const auto& pair : device_states) {
      if (pair.second.status == UP) {
        any_device_up = true;
        break;
      }
    }
    
    if (!any_device_up) {
      status = "No speakers connected";
      normal = false;
    } else {
      normal = true;
    }
  }
  
  // TODO: fetch muted, standby, volume, standby_time values from speakers
  
  // Always update datetime from RTC
  datetime = get_datetime_string();
  
  // Only redraw if something changed
  if (wifi_connected != wifi_connected_prev || 
      datetime != datetime_prev ||
      normal != normal_prev ||
      first_run) {
    
    this->tft_->fillScreen(TFT_BLACK);
    draw_top_line(this->tft_, wifi_connected, device_states, standby_time, datetime);
    draw_middle_area(this->tft_, volume, muted, standby);
    draw_bottom_line(this->tft_, status, normal);
    
    // Update previous values
    wifi_connected_prev = wifi_connected;
    datetime_prev = datetime;
    normal_prev = normal;
  }
}

void TFTHelloWorld::dump_config() {
  ESP_LOGCONFIG(TAG, "TFT Hello World component configured.");
}

}  // namespace tft_hello_world
}  // namespace esphome