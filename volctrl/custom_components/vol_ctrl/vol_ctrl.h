#pragma once

#include "esphome/core/component.h"
#include "esphome/components/spi/spi.h"
#include <map>
#include <string>
#include "device_state.h"

// Forward-declare the TFT_eSPI class instead of including the whole header
class TFT_eSPI;

namespace esphome {
namespace vol_ctrl {

// Main VolCtrl component class
class VolCtrl : public Component, public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, 
                                                       spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_40MHZ> {
 public:
  // Standard ESPHome methods
  void setup() override;
  void loop() override;
  void dump_config() override;

 protected:
  // TFT display instance
  TFT_eSPI *tft_{nullptr};
  
  // UI state tracking
  bool wifi_connected_prev_{false};
  bool muted_prev_{false};
  bool standby_prev_{false};
  float volume_prev_{-999.0f};  // Invalid value to force first update
  int standby_time_prev_{-1};  // Invalid value to force first update
  std::string status_{"Connecting to WiFi"};
  std::string datetime_{"--:-- --- --"};
  std::string datetime_prev_{""};
  bool normal_{false};
  bool normal_prev_{false};
  uint32_t last_redraw_{0};
  uint32_t last_device_check_{0};
  uint32_t last_detail_check_{0};
  bool first_run_{true};
};

}  // namespace vol_ctrl
}  // namespace esphome
