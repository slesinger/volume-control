#pragma once

#include "esphome/core/component.h"
#include "esphome/components/spi/spi.h"

// Forward-declare the TFT_eSPI class instead of including the whole header.
// This tells the compiler "trust me, a class with this name exists".
class TFT_eSPI;

namespace esphome {
namespace tft_hello_world {

class TFTHelloWorld : public Component, public spi::SPIDevice {
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
