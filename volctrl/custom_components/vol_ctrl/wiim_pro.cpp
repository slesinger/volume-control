#include "wiim_pro.h"
#include "utils.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/application.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <cstring>
#include <algorithm>

namespace esphome {
namespace vol_ctrl {

static const char *const TAG = "vol_ctrl.wiim_pro";

const std::string WiimPro::DEFAULT_IP = "192.168.1.243";

WiimPro::WiimPro() : ip_address_(DEFAULT_IP), upnp_client_(nullptr), is_available_(false), last_retry_time_(0) {
}

bool WiimPro::init() {
    ESP_LOGI(TAG, "WiimPro::init() called with default IP: %s", ip_address_.c_str());
    
    // For initial startup, use more aggressive timeouts to avoid blocking
    // the system startup sequence
    if (!init_upnp_client()) {
        ESP_LOGW(TAG, "WiiM Pro initialization failed - device may be offline, features disabled until reconnection");
        is_available_ = false;
        last_retry_time_ = millis() - RETRY_INTERVAL_MS + 2000; // Start retry timer
        return false;
    }
    ESP_LOGI(TAG, "WiiM Pro initialized successfully - all features enabled");
    is_available_ = true;
    return true;
}

void WiimPro::try_reconnect() {
    uint32_t now = millis();
    
    // Only try to reconnect if device is not available and enough time has passed
    if (is_available_ || (now - last_retry_time_) < RETRY_INTERVAL_MS) {
        return;
    }
    
    ESP_LOGI(TAG, "WiiM device offline - attempting reconnection to %s", ip_address_.c_str());
    
    if (init_upnp_client()) {
        ESP_LOGI(TAG, "✓ WiiM device back online - all features restored");
        is_available_ = true;
    } else {
        ESP_LOGD(TAG, "WiiM device still offline, will retry in %d seconds (features disabled)", RETRY_INTERVAL_MS / 1000);
        last_retry_time_ = now; // Reset retry timer
    }
}

bool WiimPro::init_upnp_client() {
    ESP_LOGI(TAG, "Initializing UPnP client for IP: %s", ip_address_.c_str());
    
    // First test basic HTTP connectivity to WiiM device using its HTTP API
    std::string test_url = "https://" + ip_address_ + "/httpapi.asp?command=getPlayerStatus";
    std::string test_response;
    
    ESP_LOGD(TAG, "Testing basic connectivity to WiiM device...");
    if (!make_http_request(test_url, test_response)) {
        ESP_LOGW(TAG, "WiiM device at %s is not responding to HTTP requests (device may be offline)", ip_address_.c_str());
        is_available_ = false;
        return false;
    }
    
    ESP_LOGD(TAG, "WiiM device responds to HTTP, attempting UPnP initialization...");
    
    upnp_client_.reset(new UPnPClient(ip_address_));
    
    if (!upnp_client_->init()) {
        ESP_LOGW(TAG, "UPnP client initialization failed for %s, but device is reachable via HTTP", ip_address_.c_str());
        ESP_LOGW(TAG, "UPnP features will be limited, but basic controls should work");
        upnp_client_.reset();
        // Still mark as available since HTTP API works
        is_available_ = true;
        return true; // Return true since we can still use HTTP API
    }
    
    ESP_LOGI(TAG, "UPnP client initialized successfully");
    is_available_ = true;
    return true;
}

std::vector<WiimDevice> WiimPro::scan() {
    ESP_LOGI(TAG, "WiimPro::scan() - Starting UPnP discovery for WiiM devices");
    std::vector<WiimDevice> devices;
    
    // Send multicast search for MediaRenderer devices
    if (!send_multicast_search()) {
        ESP_LOGE(TAG, "Failed to send multicast search");
        return devices;
    }
    
    // Parse SSDP responses and get device locations
    std::vector<std::string> locations = parse_ssdp_responses();
    
    // For each location, fetch device description and check if it's a WiiM device
    for (const auto& location : locations) {
        WiimDevice device = parse_device_description(location);
        if (!device.name.empty() && device.model_description.find("WiiM") != std::string::npos) {
            devices.push_back(device);
            ESP_LOGI(TAG, "Found WiiM device: %s at %s", device.name.c_str(), device.ip_address.c_str());
        }
    }
    
    ESP_LOGI(TAG, "UPnP discovery completed, found %d WiiM devices", devices.size());
    return devices;
}

bool WiimPro::send_multicast_search() {
    ESP_LOGD(TAG, "UPnP discovery not implemented in this version");
    return false;
}

std::vector<std::string> WiimPro::parse_ssdp_responses() {
    std::vector<std::string> locations;
    ESP_LOGD(TAG, "UPnP response parsing not implemented in this version");
    return locations;
}

WiimDevice WiimPro::parse_device_description(const std::string& location) {
    WiimDevice device;
    
    // Extract IP address from location URL
    size_t start = location.find("://") + 3;
    size_t end = location.find(":", start);
    if (end == std::string::npos) {
        end = location.find("/", start);
    }
    
    if (start != std::string::npos && end != std::string::npos && end > start) {
        device.ip_address = location.substr(start, end - start);
    }
    
    // For now, we'll use a simple HTTP client to fetch the device description
    // In a real implementation, you would parse the XML response to extract:
    // - friendlyName
    // - modelDescription 
    // - UDN (UUID)
    
    // Placeholder implementation - in practice you'd make HTTP GET request to location
    // and parse the XML response
    device.name = "WiiM Device"; // Would be extracted from XML
    device.model_description = "WiiM Pro"; // Would be extracted from XML
    device.uuid = "unknown"; // Would be extracted from XML
    
    ESP_LOGD(TAG, "Parsed device at %s", device.ip_address.c_str());
    
    return device;
}

bool WiimPro::make_http_request(const std::string& url, std::string& response) {
    HTTPClient http;
    http.begin(url.c_str());
    http.setTimeout(500); // Reduce timeout to 500ms to avoid watchdog issues
    http.setConnectTimeout(300); // Reduce connection timeout to 300ms
    
    int httpResponseCode = http.GET();
    
    if (httpResponseCode > 0) {
        response = http.getString().c_str();
        ESP_LOGV(TAG, "HTTP Response: %s", response.c_str()); // Changed to LOGV to reduce spam
        http.end();
        return httpResponseCode == 200;
    } else {
        ESP_LOGD(TAG, "HTTP Error: %d", httpResponseCode); // Changed to LOGD
        http.end();
        return false;
    }
}

bool WiimPro::pause_play_toggle() {
    if (!is_available_) {
        ESP_LOGD(TAG, "WiiM device not available, skipping pause/play toggle");
        return false;
    }
    
    ESP_LOGI(TAG, "Toggling play/pause on WiiM device at %s", ip_address_.c_str());
    
    // Get current transport state
    TransportState current_state = upnp_client_->get_transport_state();
    
    ESP_LOGI(TAG, "Current transport state: %d", static_cast<int>(current_state));
    
    // Decide action based on current state (following wiimplay approach)
    switch (current_state) {
        case TransportState::PLAYING:
        case TransportState::TRANSITIONING:
            ESP_LOGI(TAG, "Currently playing, sending pause command");
            return upnp_client_->pause();
            
        case TransportState::PAUSED_PLAYBACK:
        case TransportState::STOPPED:
            ESP_LOGI(TAG, "Currently paused/stopped, sending play command");
            return upnp_client_->play();
            
        case TransportState::NO_MEDIA_PRESENT:
            ESP_LOGW(TAG, "No media present, cannot toggle playback");
            return false;
            
        case TransportState::UNKNOWN:
        default:
            ESP_LOGW(TAG, "Unknown transport state, attempting play command");
            return upnp_client_->play();
    }
}

bool WiimPro::play() {
    if (!is_available_) {
        ESP_LOGD(TAG, "WiiM device not available, skipping play command");
        return false;
    }
    
    ESP_LOGI(TAG, "Sending play command to WiiM device at %s", ip_address_.c_str());
    
    return upnp_client_->play();
}

bool WiimPro::pause() {
    if (!is_available_) {
        ESP_LOGD(TAG, "WiiM device not available, skipping pause command");
        return false;
    }
    
    ESP_LOGI(TAG, "Sending pause command to WiiM device at %s", ip_address_.c_str());
    
    return upnp_client_->pause();
}

bool WiimPro::stop() {
    if (!is_available_) {
        ESP_LOGD(TAG, "WiiM device not available, skipping stop command");
        return false;
    }
    
    ESP_LOGI(TAG, "Sending stop command to WiiM device at %s", ip_address_.c_str());
    
    return upnp_client_->stop();
}

bool WiimPro::previous() {
    if (!is_available_) {
        ESP_LOGD(TAG, "WiiM device not available, skipping previous command");
        return false;
    }
    
    ESP_LOGI(TAG, "Sending previous command to WiiM device at %s", ip_address_.c_str());
    
    return upnp_client_->previous();
}

TransportState WiimPro::get_transport_state() {
    if (!is_available_) {
        ESP_LOGD(TAG, "WiiM device not available, returning unknown transport state");
        return TransportState::UNKNOWN;
    }
    
    return upnp_client_->get_transport_state();
}

bool WiimPro::get_transport_info(TransportInfo& info) {
    if (!is_available_) {
        ESP_LOGD(TAG, "WiiM device not available, cannot get transport info");
        return false;
    }
    
    return upnp_client_->get_transport_info(info);
}

bool WiimPro::next() {
    if (!is_available_) {
        ESP_LOGD(TAG, "WiiM device not available, skipping next command");
        return false;
    }
    
    ESP_LOGI(TAG, "Sending next command to WiiM device at %s", ip_address_.c_str());
    
    return upnp_client_->next();
}

bool WiimPro::cycle_input() {
    if (!is_available_) {
        ESP_LOGD(TAG, "WiiM device not available, skipping input cycle");
        return false;
    }
    
    ESP_LOGI(TAG, "Cycling input on WiiM device at %s", ip_address_.c_str());
    
    // WiiM doesn't have a direct "cycle input" command, so we'll need to:
    // 1. Get current input mode
    // 2. Switch to the next available input
    
    // Available inputs: wifi, bluetooth, optical, line-in, udisk
    static const std::vector<std::string> inputs = {"wifi", "bluetooth", "optical", "line-in"};
    static int current_input_index = 0; // TODO make this class variable and read real input from wiim
    
    // Cycle to next input
    current_input_index = (current_input_index + 1) % inputs.size();
    
    return set_input(inputs[current_input_index]);
}

bool WiimPro::set_input(const std::string& input) {
    if (!is_available_) {
        ESP_LOGD(TAG, "WiiM device not available, skipping set input");
        return false;
    }
    
    ESP_LOGI(TAG, "Setting input to '%s' on WiiM device at %s", input.c_str(), ip_address_.c_str());
    
    // Validate input name and map to WiiM API format
    std::string wiim_input;
    std::string input_lower = input;
    std::transform(input_lower.begin(), input_lower.end(), input_lower.begin(), ::tolower);

    if (input_lower == "wifi" || input_lower == "network") {
        wiim_input = "wifi";
    } else if (input_lower == "bluetooth" || input_lower == "bt") {
        wiim_input = "bluetooth";
    } else if (input_lower == "optical") {
        wiim_input = "optical";
    } else if (input_lower == "line-in" || input_lower == "aux" || input_lower == "aux-in") {
        wiim_input = "line-in";
    } else {
        ESP_LOGE(TAG, "Unknown input: %s", input.c_str());
        return false;
    }
    
    std::string url = "https://" + ip_address_ + "/httpapi.asp?command=setPlayerCmd:switchmode:" + wiim_input;
    std::string response;
    
    if (make_http_request(url, response)) {
        ESP_LOGD(TAG, "Set input command sent successfully, response: %s", response.c_str());
        return response.find("OK") != std::string::npos;
    } else {
        // Mark as unavailable if HTTP request fails
        ESP_LOGW(TAG, "Failed to send set input command, marking device as unavailable");
        is_available_ = false;
        last_retry_time_ = millis();
    }
    
    ESP_LOGE(TAG, "Failed to send set input command");
    return false;
}

std::string WiimPro::get_current_input() {
    if (!is_available_) {
        ESP_LOGD(TAG, "WiiM device not available, returning default input");
        return "Network"; // Default fallback
    }
    
    std::string url = "https://" + ip_address_ + "/httpapi.asp?command=getPlayerStatus";
    std::string response;
    
    if (make_http_request(url, response)) {
        ESP_LOGD(TAG, "Get player status response: %s", response.c_str());
        
        // Parse the JSON response to find the current input mode
        // Look for "mode":"XX" where XX is a numeric value
        size_t mode_pos = response.find("\"mode\":\"");
        if (mode_pos != std::string::npos) {
            size_t start = mode_pos + 8; // Skip "mode":""
            size_t end = response.find("\"", start);
            
            if (end != std::string::npos && end > start) {
                std::string mode_str = response.substr(start, end - start);
                int mode_value = std::atoi(mode_str.c_str());
                
                // Convert numeric mode to our standard input names according to WiiM API documentation
                switch (mode_value) {
                    case 10:
                    case 11:
                    case 16:
                        return "Network";  // WiFi/Network modes (10-19)
                    case 40:
                        return "Line-In";  // AUX-In
                    case 41:
                        return "Bluetooth"; // BT
                    case 43:
                        return "Optical";  // Optical-In
                    case 1:
                        return "Network";  // AirPlay (treat as Network)
                    case 31:
                        return "Network";  // Spotify Connect (treat as Network)
                    case 32:
                        return "Network";  // TIDAL Connect (treat as Network)
                    case 36:
                        return "Network";  // Qobuz Connect (treat as Network)
                    default:
                        ESP_LOGW(TAG, "Unknown input mode value: %d, defaulting to Network", mode_value);
                        return "Network";
                }
            }
        }
        
        ESP_LOGW(TAG, "Could not parse mode from response: %s", response.c_str());
    } else {
        ESP_LOGE(TAG, "Failed to get player status");
        // Mark as unavailable if HTTP request fails
        is_available_ = false;
        last_retry_time_ = millis();
    }
    
    return "Network"; // Default fallback
}

}  // namespace vol_ctrl
}  // namespace esphome
