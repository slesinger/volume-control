#pragma once

#include <string>
#include <vector>
#include <memory>
#include "upnp_client.h"

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
    void set_ip_address(const std::string& ip);
    
    // Playback control functions (following wiimplay approach)
    bool pause_play_toggle();
    bool play();
    bool pause();
    bool next();
    bool previous();
    bool stop();
    
    // Input control (still using HTTP API as UPnP doesn't support this)
    bool cycle_input();
    bool set_input(const std::string& input);
    
    // State inquiry
    TransportState get_transport_state();
    bool get_transport_info(TransportInfo& info);
    
private:
    std::string ip_address_;
    std::unique_ptr<UPnPClient> upnp_client_;
    static const std::string DEFAULT_IP;
    
    // Helper functions for UPnP discovery
    bool send_multicast_search();
    std::vector<std::string> parse_ssdp_responses();
    WiimDevice parse_device_description(const std::string& location);
    
    // Helper function for HTTP requests (for input switching)
    bool make_http_request(const std::string& url, std::string& response);
    
    // Initialize UPnP client
    bool init_upnp_client();
};

}  // namespace vol_ctrl
}  // namespace esphome
