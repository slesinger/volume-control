#pragma once

#include <map>
#include <string>
#include <vector>
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

            // Result of one background poll of a speaker
            struct PollUpdate
            {
                std::string ipv6;
                bool is_up = false;
                DeviceVolStdbyData data; // only valid when is_up
            };

            // All socket I/O runs on a dedicated FreeRTOS task, so none of the calls below block the caller.

            // Register device for monitoring (call before start())
            void register_device(const std::string &name, const std::string &ipv6);

            // Spawn the worker task. It polls every registered speaker periodically and sends queued writes.
            void start();

            // The worker idles while WiFi is down
            void set_online(bool online);

            // Queue a write. Writes are coalesced: only the latest pending volume / mute per speaker is sent.
            void request_volume(const std::string &ipv6, float volume);
            void request_mute(const std::string &ipv6, bool mute);

            // Move the poll results gathered since the last call into `out` (latest per speaker).
            // Returns false when there is nothing new. Results made stale by a write issued meanwhile are dropped.
            bool take_updates(std::vector<PollUpdate> &out);

            // Get device state map reference (mutable: owned and updated by the UI thread only)
            std::map<std::string, DeviceState> &get_device_states();

            // Initialize network subsystem
            void init();

        } // namespace network
    } // namespace vol_ctrl
} // namespace esphome
