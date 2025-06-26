#pragma once

#include <string>
#include <map>

namespace esphome {
namespace vol_ctrl {

// Device status enum
enum DeviceStatus { UNKNOWN, UP, DOWN };

// Device state struct to track speaker status
struct DeviceState {
  DeviceStatus status = UNKNOWN;
  uint32_t last_check = 0;
  bool standby = false;
  bool muted = false;
  float volume = 0.0f;
  int standby_time = 90;
};

// Rotating symbol for UI
extern const char ROT_SYMBOLS[];
extern const int ROT_SYMBOLS_LEN;

}  // namespace vol_ctrl
}  // namespace esphome
