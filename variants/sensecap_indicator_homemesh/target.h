#pragma once

#include <RadioLib.h>
#include <helpers/ESP32Board.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/SensorManager.h>
#include "HomeMeshSX1262Wrapper.h"

#ifdef DISPLAY_CLASS
  // Display and touch setup is identical to the upstream ESP-NOW variant.
  #include "../sensecap_indicator-espnow/SCIndicatorDisplay.h"
  #include <helpers/ui/MomentaryButton.h>
#endif

class IndicatorBoard : public ESP32Board {
public:
  const char* getManufacturerName() const override { return "SenseCAP Indicator"; }
  // USB powered, no battery: "power off" means display and radio off until
  // the next reset or power cycle.
  void powerOff() override;
};

extern IndicatorBoard board;
extern WRAPPER_CLASS radio_driver;
extern ESP32RTCClock rtc_clock;
extern SensorManager sensors;  // no sensors on the ESP32 side of the D1L

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
