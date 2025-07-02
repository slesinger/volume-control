#pragma once

#include <string>
#include <map>

namespace esphome
{
  namespace vol_ctrl
  {

    // Device state struct to track speaker status
    struct DeviceState
    {
      bool is_up = false; // True if device is reachable
      float requested_volume = -1.0f; // Volume requested by user, not yet applied
      bool muted = false;
      float volume = -1.0f;  // -1.0f indicates volume not set
      int standby_countdown = -1;

      bool set_is_up(bool new_is_up);
      float get_requested_volume();
      void set_requested_volume(float new_requested_volume);
      bool set_standby_countdown(int new_standby_countdown);
      float get_volume();
      bool set_volume(float new_volume);
      bool set_mute(bool new_mute);    };

    // Rotating symbol for UI
    extern const char ROT_SYMBOLS[];
    extern const int ROT_SYMBOLS_LEN;

  } // namespace vol_ctrl
} // namespace esphome
