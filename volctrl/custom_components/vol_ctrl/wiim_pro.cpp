#include "wiim_pro.h"
#include "utils.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>

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
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create UDP socket: %s", strerror(errno));
        return false;
    }
    
    // Set socket to broadcast
    int broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        ESP_LOGE(TAG, "Failed to set broadcast option: %s", strerror(errno));
        close(sock);
        return false;
    }
    
    // Set timeout
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    // Prepare multicast address
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &addr.sin_addr);
    
    // Prepare M-SEARCH message
    const char* msearch = 
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "ST: urn:schemas-upnp-org:device:MediaRenderer:1\r\n"
        "MX: 3\r\n\r\n";
    
    // Send the search request
    int sent = sendto(sock, msearch, strlen(msearch), 0, (struct sockaddr*)&addr, sizeof(addr));
    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send M-SEARCH: %s", strerror(errno));
        close(sock);
        return false;
    }
    
    ESP_LOGD(TAG, "Sent M-SEARCH message, waiting for responses...");
    close(sock);
    return true;
}

std::vector<std::string> WiimPro::parse_ssdp_responses() {
    std::vector<std::string> locations;
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create UDP socket for receiving: %s", strerror(errno));
        return locations;
    }
    
    // Bind to listen for responses
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0; // Let system choose port
    
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket: %s", strerror(errno));
        close(sock);
        return locations;
    }
    
    // Set timeout for receiving
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    char buffer[2048];
    uint32_t start_time = millis();
    
    while (millis() - start_time < 3000) { // 3 second timeout
        memset(buffer, 0, sizeof(buffer));
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        
        int bytes = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&from, &fromlen);
        if (bytes > 0) {
            std::string response(buffer, bytes);
            
            // Look for LOCATION header
            size_t loc_pos = response.find("LOCATION:");
            if (loc_pos == std::string::npos) {
                loc_pos = response.find("Location:");
            }
            
            if (loc_pos != std::string::npos) {
                size_t start = response.find("http", loc_pos);
                if (start != std::string::npos) {
                    size_t end = response.find("\r\n", start);
                    if (end != std::string::npos) {
                        std::string location = response.substr(start, end - start);
                        locations.push_back(location);
                        ESP_LOGD(TAG, "Found device location: %s", location.c_str());
                    }
                }
            }
        }
    }
    
    close(sock);
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

}  // namespace vol_ctrl
}  // namespace esphome
