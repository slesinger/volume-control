#pragma once

#include <map>
#include <string>
#include "device_state.h"

namespace esphome
{
    namespace vol_ctrl
    {
        namespace network
        {

            struct DeviceVolStdbyData
            {
                int standby_countdown = 0;
                float volume = 0.0f;
                bool mute = false;
            };

            // Network-related functions
            bool send_ssc_command(const std::string &ipv6, const std::string &command, std::string &response);
            // On failure `data` is left untouched, so callers must check the return value.
            bool get_device_data(const std::string &ipv6, DeviceVolStdbyData &data);
            bool set_device_volume(const std::string &ipv6, float volume);
            bool set_device_mute(const std::string &ipv6, bool mute);

            // Register device for monitoring
            void register_device(const std::string &name, const std::string &ipv6);

            // Get device state map reference (mutable: the UI layer updates state in place)
            std::map<std::string, DeviceState> &get_device_states();

            // Initialize network subsystem
            void init();

        } // namespace network
    } // namespace vol_ctrl
} // namespace esphome
