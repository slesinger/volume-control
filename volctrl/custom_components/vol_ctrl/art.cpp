#include "art.h"
#include "esphome/core/log.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <TJpg_Decoder.h>
#include <atomic>
#include <mutex>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace esphome {
namespace vol_ctrl {
namespace art {

static const char *const TAG = "vol_ctrl.art";

namespace {

constexpr int SIZE = 120;
constexpr size_t MAX_JPEG_BYTES = 90 * 1024;

std::mutex mtx;
std::string wanted;
std::string loaded;
bool running = false;
std::atomic<bool> is_ready{false};
std::atomic<uint32_t> ver{0};
uint16_t *pixels = nullptr;

// Decoder context (only touched by the art task)
int src_w = 0, src_h = 0, src_scale = 1;

bool block_cb(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  const int dw = (src_w + src_scale - 1) / src_scale;
  const int dh = (src_h + src_scale - 1) / src_scale;
  for (int j = 0; j < h; j++) {
    const int ty = (y + j) * SIZE / dh;
    if (ty >= SIZE) break;
    for (int i = 0; i < w; i++) {
      const int tx = (x + i) * SIZE / dw;
      if (tx < SIZE) pixels[ty * SIZE + tx] = bitmap[j * w + i];
    }
  }
  return true;
}

void dim() {
  for (int i = 0; i < SIZE * SIZE; i++) {
    uint16_t p = pixels[i];
    pixels[i] = ((p >> 1) & 0x7BEF) + ((p >> 3) & 0x1863);  // about 62%
  }
}

bool load(const std::string &url) {
  if (pixels == nullptr)
    pixels = static_cast<uint16_t *>(heap_caps_malloc(SIZE * SIZE * sizeof(uint16_t), MALLOC_CAP_8BIT));
  if (pixels == nullptr) {
    ESP_LOGW(TAG, "No memory for the album art");
    return false;
  }

  WiFiClientSecure secure;
  secure.setInsecure();
  WiFiClient plain;
  HTTPClient http;
  http.setTimeout(6000);
  http.setConnectTimeout(4000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  bool begun = url.rfind("https://", 0) == 0 ? http.begin(secure, url.c_str()) : http.begin(plain, url.c_str());
  if (!begun)
    return false;
  int code = http.GET();
  int size = http.getSize();
  if (code != 200 || size > static_cast<int>(MAX_JPEG_BYTES)) {
    ESP_LOGW(TAG, "Album art not usable (HTTP %d, %d bytes)", code, size);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();
  if (body.length() == 0 || body.length() > MAX_JPEG_BYTES)
    return false;

  const uint8_t *data = reinterpret_cast<const uint8_t *>(body.c_str());
  uint16_t w = 0, h = 0;
  if (TJpgDec.getJpgSize(&w, &h, data, body.length()) != 0 || w == 0 || h == 0) {
    ESP_LOGW(TAG, "Album art is not a baseline JPEG");
    return false;
  }
  src_w = w;
  src_h = h;
  src_scale = 1;
  while (src_scale < 8 && w / (src_scale * 2) >= SIZE && h / (src_scale * 2) >= SIZE)
    src_scale *= 2;
  TJpgDec.setJpgScale(src_scale);
  TJpgDec.setSwapBytes(false);
  TJpgDec.setCallback(block_cb);
  is_ready.store(false);
  if (TJpgDec.drawJpg(0, 0, data, body.length()) != 0) {
    ESP_LOGW(TAG, "Album art decoding failed");
    return false;
  }
  dim();
  ESP_LOGI(TAG, "Album art %ux%u decoded (1/%d), free heap %u", w, h, src_scale, ESP.getFreeHeap());
  return true;
}

void art_task(void *) {
  for (;;) {
    std::string url;
    {
      std::lock_guard<std::mutex> lock(mtx);
      if (wanted == loaded || wanted.empty()) {
        running = false;
        break;
      }
      url = wanted;
    }
    ESP_LOGI(TAG, "Fetching album art %s", url.c_str());
    bool ok = load(url);
    {
      std::lock_guard<std::mutex> lock(mtx);
      loaded = url;  // also after a failure: do not retry the same picture forever
    }
    is_ready.store(ok);
    ver.fetch_add(1);
  }
  vTaskDelete(nullptr);
}

inline void unpack(uint16_t p, int &r, int &g, int &b) {
  r = (p >> 11) & 0x1F;
  g = (p >> 5) & 0x3F;
  b = p & 0x1F;
}

// (3*a + b) / 4 per channel
inline uint16_t mix(uint16_t a, uint16_t b) {
  int ar, ag, ab, br, bg, bb;
  unpack(a, ar, ag, ab);
  unpack(b, br, bg, bb);
  return static_cast<uint16_t>((((3 * ar + br) >> 2) << 11) | (((3 * ag + bg) >> 2) << 5) | ((3 * ab + bb) >> 2));
}

}  // namespace

void request(const std::string &url) {
  std::lock_guard<std::mutex> lock(mtx);
  if (url == wanted)
    return;
  wanted = url;
  if (url.empty()) {
    if (is_ready.exchange(false)) ver.fetch_add(1);
    loaded.clear();
    return;
  }
  if (!running) {
    running = true;
    if (xTaskCreate(art_task, "vol_art", 16384, nullptr, 1, nullptr) != pdPASS)
      running = false;
  }
}

bool ready() { return is_ready.load(); }
uint32_t version() { return ver.load(); }

void row(int y, int x, int w, uint16_t *out) {
  const uint16_t *p = pixels;
  const int sy = std::min(y >> 1, SIZE - 1);
  const int ny = std::min(std::max((y & 1) ? sy + 1 : sy - 1, 0), SIZE - 1);
  const uint16_t *r0 = p + sy * SIZE;
  const uint16_t *r1 = p + ny * SIZE;
  for (int i = 0; i < w; i++) {
    const int X = x + i;
    const int sx = std::min(X >> 1, SIZE - 1);
    const int nx = std::min(std::max((X & 1) ? sx + 1 : sx - 1, 0), SIZE - 1);
    out[i] = mix(mix(r0[sx], r0[nx]), mix(r1[sx], r1[nx]));
  }
}

}  // namespace art
}  // namespace vol_ctrl
}  // namespace esphome
