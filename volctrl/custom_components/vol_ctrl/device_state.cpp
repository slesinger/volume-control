#include "device_state.h"
#include <cmath>

namespace esphome
{
    namespace vol_ctrl
    {

        // Define the rotation symbols
        const char ROT_SYMBOLS[] = {'|', '/', '-', '\\'};
        const int ROT_SYMBOLS_LEN = 4;

        bool DeviceState::set_is_up(bool new_is_up)
        {
            if (this->is_up != new_is_up)
            {
                this->is_up = new_is_up;
                return true;
            }
            return false;
        }

        bool DeviceState::set_standby_countdown(int new_standby_countdown)
        {
            bool standby_countdown_change = false;
            if (this->standby_countdown != new_standby_countdown)
            {
                this->standby_countdown = new_standby_countdown;
                return true;
            }
            return false;
        }

        float DeviceState::get_requested_volume()
        {
            return this->requested_volume;
        }

        void DeviceState::set_requested_volume(float new_requested_volume)
        {
            if (fabs(this->requested_volume - new_requested_volume) > 1e-4)
            {
                this->requested_volume = new_requested_volume;
            }
        }

        float DeviceState::get_volume()
        {
            return this->volume;
        }

        bool DeviceState::set_volume(float new_volume)
        {
            if (fabs(this->volume - new_volume) > 1e-4)
            {
                this->volume = new_volume;
                return true;
            }
            return false;
        }

        bool DeviceState::set_mute(bool new_mute)
        {
            if (this->muted != new_mute)
            {
                this->muted = new_mute;
                return true;
            }
            return false;
        }
    } // namespace vol_ctrl
} // namespace esphome
