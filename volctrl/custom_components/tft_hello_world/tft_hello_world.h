#pragma once

#include "esphome/core/component.h"
#include "esphome/components/spi/spi.h"

// Forward-declare the TFT_eSPI class instead of including the whole header.
// This tells the compiler "trust me, a class with this name exists".
class TFT_eSPI;

namespace esphome {
namespace tft_hello_world {

// Correctly use SPIDevice with template parameters for SPI configuration
class TFTHelloWorld : public Component, public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, 
                                                           spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_40MHZ> {
 public:
  // --- Standard ESPHome methods ---
  void setup() override;
  void loop() override;
  void dump_config() override;

 protected:
  // Use a pointer to the forward-declared class.
  TFT_eSPI *tft_{nullptr};
};

}  // namespace tft_hello_world
}  // namespace esphome
