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

// This function does not loop fully. It is rather a linear progression where there is an internal loop at the end.
void TFTHelloWorld::loop() {
  // Connect to wifi
  static bool wifi_connected = false;
  if (!wifi::global_wifi_component->is_connected()) {
    // Show connecting to wifi if not connected
    this->tft_->fillScreen(TFT_BLACK);
    this->tft_->setTextColor(TFT_WHITE, TFT_BLACK);
    this->tft_->setTextSize(3);
    this->tft_->setTextDatum(MC_DATUM);
    this->tft_->drawString("Ahoj Medliku", this->tft_->width() / 2, this->tft_->height() / 2);
    this->tft_->setTextSize(2);
    int y = this->tft_->height() / 2 + 40;
    this->tft_->drawString("Connecting to wifi", this->tft_->width() / 2, y);
    wifi_connected = false;
    return;
  }
  if (!wifi_connected) {
    // Just connected, clear screen
    this->tft_->fillScreen(TFT_BLACK);
    wifi_connected = true;
  }
  static uint32_t last_update = 0;
  const uint32_t interval = 2000; // ms
  if (millis() - last_update < interval) return;
  last_update = millis();
  this->tft_->fillScreen(TFT_BLACK);
  this->tft_->setTextSize(2);
  int y = 30;
  for (const auto &entry : DEVICE_MAP) {
    const std::string &name = entry.first;
    const std::string &ipv6 = entry.second;
    bool up = (device_states[ipv6].status == UP);
    uint16_t color = up ? TFT_GREEN : 0x8410; // gray: RGB565 0x8410
    this->tft_->setTextColor(color, TFT_BLACK);
    // Rotating symbol
    char rot = ROT_SYMBOLS[device_rot[ipv6] % ROT_SYMBOLS_LEN];
    std::string display = name + "  ";
    display += rot;
    this->tft_->drawString(display.c_str(), this->tft_->width() / 2, y);
    y += 40;
    // Only check device if status is not UP
    if (!up) is_device_up(ipv6);
  }
}

void TFTHelloWorld::dump_config() {
  ESP_LOGCONFIG(TAG, "TFT Hello World component configured.");
}

}  // namespace tft_hello_world
}  // namespace esphome