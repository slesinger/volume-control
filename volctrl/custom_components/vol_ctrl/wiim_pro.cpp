#include "wiim_pro.h"
#include "utils.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <cstring>
#include <algorithm>

namespace esphome {
namespace vol_ctrl {

static const char *const TAG = "vol_ctrl.wiim_pro";

const std::string WiimPro::DEFAULT_IP = "192.168.1.245";

WiimPro::WiimPro() : ip_address_(DEFAULT_IP) {
}

void WiimPro::init() {
    ESP_LOGI(TAG, "WiimPro::init() called with default IP: %s", ip_address_.c_str());
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
    http.setTimeout(3000); // 3 second timeout
    
    int httpResponseCode = http.GET();
    
    if (httpResponseCode > 0) {
        response = http.getString().c_str();
        ESP_LOGD(TAG, "HTTP Response: %s", response.c_str());
        http.end();
        return httpResponseCode == 200;
    } else {
        ESP_LOGE(TAG, "HTTP Error: %d", httpResponseCode);
        http.end();
        return false;
    }
}

bool WiimPro::pause() {
    ESP_LOGI(TAG, "Sending pause command to WiiM device at %s", ip_address_.c_str());
    
    std::string url = "https://" + ip_address_ + "/httpapi.asp?command=setPlayerCmd:pause";
    std::string response;
    
    if (make_http_request(url, response)) {
        ESP_LOGD(TAG, "Pause command sent successfully, response: %s", response.c_str());
        return response.find("OK") != std::string::npos;
    }
    
    ESP_LOGE(TAG, "Failed to send pause command");
    return false;
}

bool WiimPro::next() {
    ESP_LOGI(TAG, "Sending next command to WiiM device at %s", ip_address_.c_str());
    
    std::string url = "https://" + ip_address_ + "/httpapi.asp?command=setPlayerCmd:next";
    std::string response;
    
    if (make_http_request(url, response)) {
        ESP_LOGD(TAG, "Next command sent successfully, response: %s", response.c_str());
        return response.find("OK") != std::string::npos;
    }
    
    ESP_LOGE(TAG, "Failed to send next command");
    return false;
}

bool WiimPro::cycle_input() {
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
    }
    
    ESP_LOGE(TAG, "Failed to send set input command");
    return false;
}

}  // namespace vol_ctrl
}  // namespace esphome
