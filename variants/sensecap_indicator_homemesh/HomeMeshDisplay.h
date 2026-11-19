#pragma once

// Display for the HomeMesh UI. Same panel setup as the upstream ESP-NOW
// variant; adds what the LVGL glue needs: the LovyanGFX device for flushing,
// the backlight, and switching from the text sprite to 16-bit drawing.
//
// Panel init: the ST7701 chip select and reset sit on the TCA9535 expander
// (port 0, bits 4 and 5). LovyanGFX does not know the expander encoding -
// to it "4 | IO_EXPANDER" is GPIO 68 - so without help the init sequence
// never reaches the panel (stripes, wrong colours, flicker). begin() holds
// the real CS low through the expander for the whole init, as PR #2568 does.

#include <Wire.h>
#include "../sensecap_indicator-espnow/SCIndicatorDisplay.h"

class HomeMeshDisplay : public SCIndicatorDisplay {
  static const uint8_t EXPANDER_ADDR = 0x20;
  static const uint8_t REG_OUTPUT_P0 = 0x02;
  static const uint8_t REG_CONFIG_P0 = 0x06;

  static void expanderWrite(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(EXPANDER_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
  }

public:
  bool begin() {
    Wire.begin(PIN_BOARD_SDA, PIN_BOARD_SCL, 400000);
    expanderWrite(REG_OUTPUT_P0, 0xFF);   // everything high first
    expanderWrite(REG_CONFIG_P0, 0xCF);   // bits 4 (LCD CS) and 5 (LCD reset) outputs
    // reset pulse, so a soft restart starts from the same state as power-on
    expanderWrite(REG_OUTPUT_P0, 0xDF);   // reset low
    delay(10);
    expanderWrite(REG_OUTPUT_P0, 0xFF);   // reset high
    delay(120);
    expanderWrite(REG_OUTPUT_P0, 0xEF);   // CS low for the init sequence
    delay(5);
    bool ok = SCIndicatorDisplay::begin();
    expanderWrite(REG_OUTPUT_P0, 0xFF);   // CS high: radio SPI shares the lines
    return ok;
  }

  LGFX_Device* device() { return display; }

  // Frees the sprite of the stock text UI and prepares fast 16-bit pushes.
  // Rotation 3 cancels the panel's offset_rotation (1), so LovyanGFX copies
  // whole rows into its frame buffer instead of converting pixel by pixel;
  // LVGL turns the picture by 180 degrees itself (see UITask::lvFlush).
  void startLvgl() {
    buffer.deleteSprite();
    display->setColorDepth(16);
    display->setRotation(3);
  }

  void setBacklight(uint8_t percent) {
    if (percent > 100) percent = 100;
    display->setBrightness((uint8_t)((uint16_t)percent * 255 / 100));
  }
};
