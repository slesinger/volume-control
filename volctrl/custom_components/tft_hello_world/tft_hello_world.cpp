#include "tft_hello_world.h"
#include "esphome/core/log.h"
#include <TFT_eSPI.h> // <-- The problematic include is now safely here.

namespace esphome {
namespace tft_hello_world {

static const char *const TAG = "tft_hello_world";

void TFTHelloWorld::setup() {
  ESP_LOGCONFIG(TAG, "Setting up TFT Hello World...");

  this->tft_ = new TFT_eSPI();
  this->tft_->init();
  this->tft_->setRotation(2);
  this->tft_->fillScreen(TFT_BLACK);
  this->tft_->setTextColor(TFT_WHITE, TFT_BLACK);
  this->tft_->setTextSize(3);
  this->tft_->setTextDatum(MC_DATUM);
  this->tft_->drawString("Hello World!", this->tft_->width() / 2, this->tft_->height() / 2);
}

void TFTHelloWorld::loop() {
  // Nothing to do in a loop for a static display.
}

void TFTHelloWorld::dump_config() {
  ESP_LOGCONFIG(TAG, "TFT Hello World component configured.");
}

}  // namespace tft_hello_world
}  // namespace esphome