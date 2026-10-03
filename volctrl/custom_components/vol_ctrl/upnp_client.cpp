#include "upnp_client.h"
#include "esphome/core/hal.h"
#include "esphome/core/application.h"
#include <WiFiUdp.h>

namespace esphome {
namespace vol_ctrl {

const char* UPnPClient::TAG = "vol_ctrl.upnp_client";

UPnPClient::UPnPClient(const std::string& device_ip) : device_ip_(device_ip) {
}

bool UPnPClient::init() {
    ESP_LOGI(TAG, "Initializing UPnP client for device: %s", device_ip_.c_str());
    return discover_services();
}

bool UPnPClient::discover_services() {
    // Try common WiiM UPnP endpoints first
    // Based on wiimplay, WiiM devices typically use these paths
    std::string device_desc_url = "http://" + device_ip_ + ":49152/description.xml";
    
    ESP_LOGD(TAG, "Fetching device description from: %s", device_desc_url.c_str());
    
    HTTPClient http;
    http.begin(device_desc_url.c_str());
    http.setTimeout(1000);  // Reduce timeout to 1 second to avoid watchdog issues
    http.setConnectTimeout(500); // Reduce connection timeout to 500ms
    
    int httpResponseCode = http.GET();
    
    if (httpResponseCode == 200) {
        std::string response = http.getString().c_str();
        http.end();
        
        ESP_LOGD(TAG, "Device description received: %s", response.c_str());
        
        if (parse_device_description(response, av_transport_control_url_)) {
            ESP_LOGI(TAG, "Found AVTransport service at: %s", av_transport_control_url_.c_str());
            return true;
        }
    } else {
        ESP_LOGD(TAG, "Failed to get device description from port 49152, HTTP code: %d", httpResponseCode);
        http.end();
    }
    
    // Fallback: try common alternative ports (but with shorter timeout)
    std::vector<int> ports = {49153, 49154, 8080};
    for (int port : ports) {
        device_desc_url = "http://" + device_ip_ + ":" + std::to_string(port) + "/description.xml";
        ESP_LOGD(TAG, "Trying alternative URL: %s", device_desc_url.c_str());
        
        http.begin(device_desc_url.c_str());
        http.setTimeout(500);  // Reduce fallback timeout to 500ms to avoid watchdog issues
        http.setConnectTimeout(500); // Add connection timeout
        httpResponseCode = http.GET();
        
        if (httpResponseCode == 200) {
            std::string response = http.getString().c_str();
            http.end();
            
            if (parse_device_description(response, av_transport_control_url_)) {
                ESP_LOGI(TAG, "Found AVTransport service at: %s", av_transport_control_url_.c_str());
                return true;
            }
        } else {
            http.end();
        }
    }
    
    ESP_LOGW(TAG, "Could not discover UPnP services for device %s (device may be offline)", device_ip_.c_str());
    return false;
}

bool UPnPClient::parse_device_description(const std::string& xml, std::string& av_transport_url) {
    // Look for AVTransport service in the device description
    // This is a simplified parser - in production, you'd want a proper XML parser
    
    // Find AVTransport service
    size_t service_start = xml.find("<service>");
    while (service_start != std::string::npos) {
        size_t service_end = xml.find("</service>", service_start);
        if (service_end == std::string::npos) break;
        
        std::string service_block = xml.substr(service_start, service_end - service_start);
        
        // Check if this is AVTransport service
        if (service_block.find("AVTransport") != std::string::npos) {
            // Extract control URL
            std::string control_url = extract_xml_value(service_block, "controlURL");
            if (!control_url.empty()) {
                // Make absolute URL
                if (control_url[0] == '/') {
                    av_transport_url = "http://" + device_ip_ + ":49152" + control_url;
                } else {
                    av_transport_url = "http://" + device_ip_ + ":49152/" + control_url;
                }
                return true;
            }
        }
        
        service_start = xml.find("<service>", service_end);
    }
    
    return false;
}

std::string UPnPClient::extract_xml_value(const std::string& xml, const std::string& tag) {
    std::string start_tag = "<" + tag + ">";
    std::string end_tag = "</" + tag + ">";
    
    size_t start = xml.find(start_tag);
    if (start == std::string::npos) return "";
    
    start += start_tag.length();
    size_t end = xml.find(end_tag, start);
    if (end == std::string::npos) return "";
    
    return xml.substr(start, end - start);
}

bool UPnPClient::get_transport_info(TransportInfo& info) {
    if (av_transport_control_url_.empty()) {
        ESP_LOGE(TAG, "AVTransport control URL not initialized");
        return false;
    }
    
    // SOAP request for GetTransportInfo
    std::string soap_body = 
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body>"
        "<u:GetTransportInfo xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
        "<InstanceID>0</InstanceID>"
        "</u:GetTransportInfo>"
        "</s:Body>"
        "</s:Envelope>";
    
    std::string response;
    if (!make_soap_request("GetTransportInfo", soap_body, response)) {
        return false;
    }
    
    // Parse response
    info.current_transport_state = parse_transport_state(extract_xml_value(response, "CurrentTransportState"));
    info.current_transport_status = extract_xml_value(response, "CurrentTransportStatus");
    info.current_speed = extract_xml_value(response, "CurrentSpeed");
    
    ESP_LOGD(TAG, "Transport state: %d, status: %s", 
             static_cast<int>(info.current_transport_state), 
             info.current_transport_status.c_str());
    
    return true;
}

TransportState UPnPClient::parse_transport_state(const std::string& state_str) {
    if (state_str == "PLAYING") return TransportState::PLAYING;
    if (state_str == "PAUSED_PLAYBACK") return TransportState::PAUSED_PLAYBACK;
    if (state_str == "STOPPED") return TransportState::STOPPED;
    if (state_str == "TRANSITIONING") return TransportState::TRANSITIONING;
    if (state_str == "NO_MEDIA_PRESENT") return TransportState::NO_MEDIA_PRESENT;
    return TransportState::UNKNOWN;
}

TransportState UPnPClient::get_transport_state() {
    TransportInfo info;
    if (get_transport_info(info)) {
        return info.current_transport_state;
    }
    return TransportState::UNKNOWN;
}

bool UPnPClient::play(const std::string& speed) {
    if (av_transport_control_url_.empty()) {
        ESP_LOGE(TAG, "AVTransport control URL not initialized");
        return false;
    }
    
    std::string soap_body = 
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body>"
        "<u:Play xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
        "<InstanceID>0</InstanceID>"
        "<Speed>" + speed + "</Speed>"
        "</u:Play>"
        "</s:Body>"
        "</s:Envelope>";
    
    std::string response;
    return make_soap_request("Play", soap_body, response);
}

bool UPnPClient::pause() {
    if (av_transport_control_url_.empty()) {
        ESP_LOGE(TAG, "AVTransport control URL not initialized");
        return false;
    }
    
    std::string soap_body = 
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body>"
        "<u:Pause xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
        "<InstanceID>0</InstanceID>"
        "</u:Pause>"
        "</s:Body>"
        "</s:Envelope>";
    
    std::string response;
    return make_soap_request("Pause", soap_body, response);
}

bool UPnPClient::stop() {
    if (av_transport_control_url_.empty()) {
        ESP_LOGE(TAG, "AVTransport control URL not initialized");
        return false;
    }
    
    std::string soap_body = 
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body>"
        "<u:Stop xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
        "<InstanceID>0</InstanceID>"
        "</u:Stop>"
        "</s:Body>"
        "</s:Envelope>";
    
    std::string response;
    return make_soap_request("Stop", soap_body, response);
}

bool UPnPClient::next() {
    if (av_transport_control_url_.empty()) {
        ESP_LOGE(TAG, "AVTransport control URL not initialized");
        return false;
    }
    
    std::string soap_body = 
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body>"
        "<u:Next xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
        "<InstanceID>0</InstanceID>"
        "</u:Next>"
        "</s:Body>"
        "</s:Envelope>";
    
    std::string response;
    return make_soap_request("Next", soap_body, response);
}

bool UPnPClient::previous() {
    if (av_transport_control_url_.empty()) {
        ESP_LOGE(TAG, "AVTransport control URL not initialized");
        return false;
    }
    
    std::string soap_body = 
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body>"
        "<u:Previous xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
        "<InstanceID>0</InstanceID>"
        "</u:Previous>"
        "</s:Body>"
        "</s:Envelope>";
    
    std::string response;
    return make_soap_request("Previous", soap_body, response);
}

bool UPnPClient::make_soap_request(const std::string& action, const std::string& soap_body, std::string& response) {
    HTTPClient http;
    http.begin(av_transport_control_url_.c_str());
    http.setTimeout(1500);  // Increase timeout to 1.5 seconds for better reliability
    http.setConnectTimeout(500); // Add connection timeout
    
    // Set SOAP headers
    http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
    String soap_action = "\"urn:schemas-upnp-org:service:AVTransport:1#" + String(action.c_str()) + "\"";
    http.addHeader("SOAPAction", soap_action);
    
    int httpResponseCode = http.POST(soap_body.c_str());
    
    if (httpResponseCode == 200) {
        response = http.getString().c_str();
        ESP_LOGD(TAG, "SOAP %s successful, response: %s", action.c_str(), response.c_str());
        http.end();
        return true;
    } else {
        ESP_LOGE(TAG, "SOAP %s failed, HTTP code: %d", action.c_str(), httpResponseCode);
        http.end();
        return false;
    }
}

}  // namespace vol_ctrl
}  // namespace esphome
