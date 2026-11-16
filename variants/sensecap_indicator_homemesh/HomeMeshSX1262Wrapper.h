#pragma once

// SX1262 wrapper for the SenseCAP Indicator.
//
// The radio's DIO1 line is not wired to the ESP32-S3 directly. It goes through
// the TCA9535 IO expander, whose single /INT output also fires for BUSY and
// touch changes. SenseCapHAL therefore only records a pending interrupt in its
// ISR; the real check (DIO1 high?) needs an I2C read and must run outside the
// ISR.
//
// The dispatcher calls loop() once per iteration, before it checks for a
// finished transmission or a received packet. Dispatching the pending
// interrupt there covers both RX-done and TX-done without touching the
// upstream RadioLibWrapper.

#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include "SenseCapHAL.h"

class HomeMeshSX1262Wrapper : public CustomSX1262Wrapper {
  SenseCapHAL* hal() {
    return static_cast<SenseCapHAL*>(static_cast<CustomSX1262*>(_radio)->getMod()->hal);
  }

public:
  HomeMeshSX1262Wrapper(CustomSX1262& radio, mesh::MainBoard& board)
    : CustomSX1262Wrapper(radio, board) {}

  void loop() override {
    hal()->dispatchPendingIrq();
    CustomSX1262Wrapper::loop();
  }

  // Periodic noise-floor sampling reads RSSI over SPI, and every SPI command
  // toggles BUSY, which in turn fires the shared expander interrupt. Only
  // resample when interference detection actually uses the noise floor.
  void triggerNoiseFloorCalibrate(int threshold) override {
    if (threshold > 0) {
      CustomSX1262Wrapper::triggerNoiseFloorCalibrate(threshold);
    } else {
      _threshold = threshold;
    }
  }
};
