#pragma once

#include <string>
#include "device_state.h"

namespace esphome {
namespace vol_ctrl {
namespace network {

// Network-related functions
bool send_ssc_command(const std::string &ipv6, const std::string &command, std::string &response);
bool is_device_up(const std::string &ipv6);
bool get_device_volume(const std::string &ipv6, float &volume);
bool get_device_mute_status(const std::string &ipv6, bool &muted);
bool get_device_standby_time(const std::string &ipv6, int &standby_time);

// Register device for monitoring
void register_device(const std::string &name, const std::string &ipv6);

// Get device state map reference
const std::map<std::string, DeviceState>& get_device_states();

// Initialize network subsystem
void init();

}  // namespace network
}  // namespace vol_ctrl
}  // namespace esphome
