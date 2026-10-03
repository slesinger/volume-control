#include "wiim.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace esphome {
namespace vol_ctrl {
namespace wiim {

static const char *const TAG = "vol_ctrl.wiim";

namespace {

constexpr uint32_t POLL_INTERVAL_MS = 5000;
constexpr uint32_t RETRY_INTERVAL_MS = 30000;  // while the WiiM is unreachable
constexpr uint32_t HTTP_TIMEOUT_MS = 2000;
constexpr size_t MAX_QUEUE = 4;

enum class CommandType { TOGGLE_PLAY, NEXT, PREVIOUS, CYCLE_INPUT, SET_INPUT };

struct Command {
  CommandType type;
  std::string arg;
};

// The order cycle_input() steps through; the strings are the names exposed to Home Assistant
struct InputInfo {
  const char *name;
  const char *wiim_mode;  // argument of setPlayerCmd:switchmode:
};
const InputInfo INPUTS[] = {
    {"Network", "wifi"}, {"Bluetooth", "bluetooth"}, {"Optical", "optical"}, {"Line-In", "line-in"}};

std::mutex mtx;
std::string ip;
bool enabled = false;
Status status;
std::deque<Command> queue;
std::atomic<bool> online{false};
TaskHandle_t worker = nullptr;

void enqueue(CommandType type, const std::string &arg = "") {
  if (!enabled) return;
  {
    std::lock_guard<std::mutex> lock(mtx);
    if (queue.size() >= MAX_QUEUE) queue.pop_front();  // drop the oldest if the WiiM is not keeping up
    queue.push_back({type, arg});
  }
  if (worker != nullptr) xTaskNotifyGive(worker);
}

// getPlayerStatus "mode" -> input name (see "Switch mode" in the WiiM API doc)
const char *input_for_mode(int mode) {
  switch (mode) {
    case 40: return "Line-In";
    case 41: return "Bluetooth";
    case 43: return "Optical";
    default: return "Network";  // wifi streaming, AirPlay (1), Spotify (31), TIDAL (32), Qobuz (36), ...
  }
}

// Extract a JSON string value: "key":"value" (the WiiM replies are flat, so a plain search is enough)
bool json_string(const std::string &json, const char *key, std::string &out) {
  std::string pattern = std::string("\"") + key + "\"";
  size_t pos = json.find(pattern);
  if (pos == std::string::npos) return false;
  pos = json.find('"', json.find(':', pos + pattern.size()));
  if (pos == std::string::npos) return false;
  size_t end = json.find('"', pos + 1);
  if (end == std::string::npos) return false;
  out = json.substr(pos + 1, end - pos - 1);
  return true;
}

bool http_get(const std::string &host, const std::string &path, std::string &response) {
  WiFiClientSecure client;
  client.setInsecure();  // the WiiM uses a self-signed certificate
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, ("https://" + host + path).c_str())) return false;
  int code = http.GET();
  bool ok = code == 200;
  if (ok) response = http.getString().c_str();
  else ESP_LOGD(TAG, "GET %s -> %d", path.c_str(), code);
  http.end();
  return ok;
}

// Find a WiiM via SSDP: M-SEARCH for UPnP media renderers, then check each device description for "WiiM".
bool discover(std::string &found_ip) {
  WiFiUDP udp;
  if (!udp.begin(0)) return false;
  static const char *const request =
      "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\n"
      "ST: urn:schemas-upnp-org:device:MediaRenderer:1\r\n\r\n";
  udp.beginPacket(IPAddress(239, 255, 255, 250), 1900);
  udp.write(reinterpret_cast<const uint8_t *>(request), strlen(request));
  udp.endPacket();

  std::vector<std::string> locations;
  uint32_t start = millis();
  while (millis() - start < 3000) {
    int size = udp.parsePacket();
    if (size <= 0) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    std::string reply(size, '\0');
    udp.read(&reply[0], size);
    std::string lower = reply;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    size_t pos = lower.find("location:");
    if (pos == std::string::npos) continue;
    size_t end = reply.find("\r\n", pos);
    std::string location = reply.substr(pos + 9, end == std::string::npos ? std::string::npos : end - pos - 9);
    location.erase(0, location.find_first_not_of(' '));
    if (std::find(locations.begin(), locations.end(), location) == locations.end()) locations.push_back(location);
  }
  udp.stop();

  for (const auto &location : locations) {
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    if (!http.begin(location.c_str())) continue;
    int code = http.GET();
    String body = code == 200 ? http.getString() : String();
    http.end();
    if (body.indexOf("WiiM") < 0) continue;
    // http://192.168.1.243:49152/description.xml -> 192.168.1.243
    size_t host_start = location.find("//");
    if (host_start == std::string::npos) continue;
    host_start += 2;
    size_t host_end = location.find_first_of(":/", host_start);
    found_ip = location.substr(host_start, host_end == std::string::npos ? std::string::npos : host_end - host_start);
    return true;
  }
  return false;
}

void set_available(bool available) {
  std::lock_guard<std::mutex> lock(mtx);
  if (status.available != available)
    ESP_LOGI(TAG, "WiiM %s", available ? "online" : "offline");
  status.available = available;
  status.checked = true;
}

// Refresh the cached status (and track metadata while playing). Returns false if the WiiM did not answer.
bool poll(const std::string &host) {
  std::string body;
  if (!http_get(host, "/httpapi.asp?command=getPlayerStatus", body)) return false;

  Status next;
  next.available = true;
  next.checked = true;
  std::string value;
  next.input = input_for_mode(json_string(body, "mode", value) ? atoi(value.c_str()) : 10);
  next.playing = json_string(body, "status", value) && value == "play";

  if (next.playing && http_get(host, "/httpapi.asp?command=getMetaInfo", body)) {
    json_string(body, "title", next.title);
    json_string(body, "artist", next.artist);
    json_string(body, "album", next.album);
    if (next.title == "unknow") next.title.clear();
    if (next.artist == "unknow") next.artist.clear();
    if (next.album == "unknow") next.album.clear();
  }

  std::lock_guard<std::mutex> lock(mtx);
  status = next;
  return true;
}

bool execute(const std::string &host, const Command &cmd) {
  std::string path = "/httpapi.asp?command=setPlayerCmd:";
  switch (cmd.type) {
    case CommandType::TOGGLE_PLAY: path += "onepause"; break;
    case CommandType::NEXT: path += "next"; break;
    case CommandType::PREVIOUS: path += "prev"; break;
    case CommandType::CYCLE_INPUT:
    case CommandType::SET_INPUT: {
      std::string wanted = cmd.arg;
      if (cmd.type == CommandType::CYCLE_INPUT) {
        std::string current;
        {
          std::lock_guard<std::mutex> lock(mtx);
          current = status.input;
        }
        size_t index = 0;
        for (size_t i = 0; i < sizeof(INPUTS) / sizeof(INPUTS[0]); i++)
          if (current == INPUTS[i].name) index = i;
        wanted = INPUTS[(index + 1) % (sizeof(INPUTS) / sizeof(INPUTS[0]))].name;
      }
      std::string lower = wanted;
      std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
      const char *mode = nullptr;
      if (lower == "network" || lower == "wifi") mode = "wifi";
      else if (lower == "bluetooth" || lower == "bt") mode = "bluetooth";
      else if (lower == "optical") mode = "optical";
      else if (lower == "line-in" || lower == "aux" || lower == "aux-in") mode = "line-in";
      if (mode == nullptr) {
        ESP_LOGW(TAG, "Unknown input '%s'", wanted.c_str());
        return true;  // not a connectivity problem
      }
      ESP_LOGI(TAG, "Switching input to %s", mode);
      path += std::string("switchmode:") + mode;
      break;
    }
  }
  std::string response;
  return http_get(host, path, response);
}

void worker_task(void *) {
  uint32_t next_poll = 0;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));
    if (!online.load()) continue;

    std::string host;
    {
      std::lock_guard<std::mutex> lock(mtx);
      host = ip;
    }
    if (host.empty()) {
      std::string found;
      if (discover(found)) {
        ESP_LOGI(TAG, "Found WiiM at %s", found.c_str());
        std::lock_guard<std::mutex> lock(mtx);
        ip = found;
      } else {
        ESP_LOGD(TAG, "No WiiM found via SSDP");
        set_available(false);
        vTaskDelay(pdMS_TO_TICKS(RETRY_INTERVAL_MS));
      }
      continue;
    }

    for (;;) {
      Command cmd;
      {
        std::lock_guard<std::mutex> lock(mtx);
        if (queue.empty()) break;
        cmd = queue.front();
        queue.pop_front();
      }
      if (!get_status().available) {
        ESP_LOGD(TAG, "WiiM offline, dropping command");
        continue;
      }
      if (execute(host, cmd)) {
        next_poll = millis();  // refresh input/play state right away
      } else {
        set_available(false);
        next_poll = millis() + RETRY_INTERVAL_MS;
      }
    }

    if (static_cast<int32_t>(millis() - next_poll) >= 0) {
      if (poll(host)) {
        next_poll = millis() + POLL_INTERVAL_MS;
      } else {
        set_available(false);
        next_poll = millis() + RETRY_INTERVAL_MS;
      }
    }
  }
}

}  // namespace

void init(const std::string &address) {
  std::lock_guard<std::mutex> lock(mtx);
  ip = address;
  enabled = true;
}

void start() {
  if (!enabled || worker != nullptr) return;
  xTaskCreate(worker_task, "vol_wiim", 10240, nullptr, 1, &worker);
}

void set_online(bool value) { online.store(value); }

void toggle_play() { enqueue(CommandType::TOGGLE_PLAY); }
void next() { enqueue(CommandType::NEXT); }
void previous() { enqueue(CommandType::PREVIOUS); }
void cycle_input() { enqueue(CommandType::CYCLE_INPUT); }
void set_input(const std::string &input) { enqueue(CommandType::SET_INPUT, input); }

Status get_status() {
  std::lock_guard<std::mutex> lock(mtx);
  return status;
}

bool is_enabled() {
  std::lock_guard<std::mutex> lock(mtx);
  return enabled;
}

}  // namespace wiim
}  // namespace vol_ctrl
}  // namespace esphome
