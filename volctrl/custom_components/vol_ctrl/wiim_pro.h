#pragma once

#include <string>
#include <vector>

namespace esphome {
namespace vol_ctrl {

struct WiimDevice {
    std::string name;
    std::string ip_address;
    std::string model_description;
    std::string uuid;
};

class WiimPro {
public:
    WiimPro();
    
    // Initialize with default IP address
    void init();
    
    // Scan for WiiM devices on the network
    std::vector<WiimDevice> scan();
    
    // Get/set the IP address
    const std::string& get_ip_address() const { return ip_address_; }
    void set_ip_address(const std::string& ip) { ip_address_ = ip; }
    
private:
    std::string ip_address_;
    static const std::string DEFAULT_IP;
    
    // Helper functions for UPnP discovery
    bool send_multicast_search();
    std::vector<std::string> parse_ssdp_responses();
    WiimDevice parse_device_description(const std::string& location);
};

}  // namespace vol_ctrl
}  // namespace esphome
