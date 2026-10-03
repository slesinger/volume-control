#pragma once

#include <string>
#include <map>
#include "esphome/core/log.h"
#include <HTTPClient.h>
#include <WiFiClient.h>

namespace esphome {
namespace vol_ctrl {

enum class TransportState {
    STOPPED,
    PAUSED_PLAYBACK,
    PLAYING,
    TRANSITIONING,
    NO_MEDIA_PRESENT,
    UNKNOWN
};

struct TransportInfo {
    TransportState current_transport_state;
    std::string current_transport_status;
    std::string current_speed;
    uint32_t track;
    std::string track_duration;
    std::string track_metadata;
    std::string track_uri;
    std::string rel_time;
    std::string abs_time;
};

class UPnPClient {
public:
    UPnPClient(const std::string& device_ip);
    
    // Initialize and discover service endpoints
    bool init();
    
    // AVTransport control methods (based on wiimplay)
    bool get_transport_info(TransportInfo& info);
    bool play(const std::string& speed = "1");
    bool pause();
    bool stop();
    bool next();
    bool previous();
    
    // Get current playback state
    TransportState get_transport_state();
    
private:
    std::string device_ip_;
    std::string av_transport_control_url_;
    std::string av_transport_event_url_;
    
    // SOAP/HTTP helpers
    bool make_soap_request(const std::string& action, const std::string& soap_body, std::string& response);
    bool discover_services();
    bool parse_device_description(const std::string& location, std::string& av_transport_url);
    
    // XML parsing helpers
    std::string extract_xml_value(const std::string& xml, const std::string& tag);
    TransportState parse_transport_state(const std::string& state_str);
    
    static const char* TAG;
};

}  // namespace vol_ctrl
}  // namespace esphome
