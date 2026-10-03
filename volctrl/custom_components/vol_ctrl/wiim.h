#pragma once

#include <string>

namespace esphome {
namespace vol_ctrl {
namespace wiim {

// Optional WiiM streamer next to the speakers (https://<ip>/httpapi.asp, see docs/HTTP_API_for_WiiM_Products.md).
// Like network.cpp, all HTTP traffic runs on its own FreeRTOS task: the calls below never block the caller
// and the getters return the values cached by the last poll.

struct Status {
  bool available = false;  // last request succeeded
  bool checked = false;    // the WiiM has been looked for at least once (until then: still searching)
  std::string input;       // "Network", "Bluetooth", "Line-In", "Optical" ("" until the first poll)
  bool playing = false;
  std::string title;
  std::string artist;
  std::string album;
};

// ip: address of the WiiM; empty = find it via SSDP. Call once before start().
void init(const std::string &ip);
void start();
void set_online(bool online);  // idles while WiFi is down

// Commands are queued and executed in order on the worker task
void toggle_play();
void next();
void previous();
void cycle_input();
void set_input(const std::string &input);  // Network | Bluetooth | Line-In | Optical (also wifi, bt, aux, ...)

Status get_status();
bool is_enabled();  // false when WiiM support is switched off in yaml

}  // namespace wiim
}  // namespace vol_ctrl
}  // namespace esphome
