#pragma once

// Display for the HomeMesh UI. Same panel setup as the upstream ESP-NOW
// variant; adds what the LVGL glue needs: the LovyanGFX device for flushing,
// the backlight, and switching from the text sprite to 16-bit drawing.

#include "../sensecap_indicator-espnow/SCIndicatorDisplay.h"

class HomeMeshDisplay : public SCIndicatorDisplay {
public:
  LGFX_Device* device() { return display; }

  // Frees the sprite of the stock text UI and prepares 16-bit pushes.
  void startLvgl() {
    buffer.deleteSprite();
    display->setColorDepth(16);
  }

  void setBacklight(uint8_t percent) {
    if (percent > 100) percent = 100;
    display->setBrightness((uint8_t)((uint16_t)percent * 255 / 100));
  }
};
