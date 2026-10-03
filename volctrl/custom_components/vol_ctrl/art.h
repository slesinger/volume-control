#pragma once
#include <cstdint>
#include <string>

namespace esphome {
namespace vol_ctrl {
namespace art {

// Album art used as screen background. The picture is downloaded and decoded on its own short-lived task,
// stored dimmed at 120x120 (RGB565) and upscaled to 240x240 on the fly while the background is painted.

// url: image to show; empty = no art (stone background). Cheap to call repeatedly with the same url.
void request(const std::string &url);
bool ready();
uint32_t version();  // changes whenever art appears, changes or goes away
// Fills out[0..w) with the background pixels of screen row y, starting at column x (only when ready())
void row(int y, int x, int w, uint16_t *out);

}  // namespace art
}  // namespace vol_ctrl
}  // namespace esphome
