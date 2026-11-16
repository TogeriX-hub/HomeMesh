#pragma once

// Radio status for the home screen, from received packets only (no probing).
//
// Every received packet carries the SNR of its last hop, i.e. the link from
// the nearest repeater to this device. The meter keeps a moving average of
// those values. Quiet hours are normal in a mesh, so "no signal" is only
// shown after a long silence.
//
// Pure logic, unit-tested natively.

#include <stdint.h>

namespace homemesh {

class SignalMeter {
public:
  static const uint32_t SILENCE_MS = 3UL * 60 * 60 * 1000;   // 3 h without any packet

  void onPacket(float snr, uint32_t now_ms) {
    _avg = _any ? _avg * 0.8f + snr * 0.2f : snr;
    _any = true;
    _last_ms = now_ms;
  }

  // 0 = no signal, 1 = weak, 2 = ok, 3..4 = good
  uint8_t level(uint32_t now_ms) const {
    if (!_any || now_ms - _last_ms > SILENCE_MS) return 0;
    if (_avg >= 5.0f) return 4;
    if (_avg >= 0.0f) return 3;
    if (_avg >= -5.0f) return 2;
    return 1;
  }

  bool heardAnything() const { return _any; }
  float averageSnr() const { return _avg; }
  uint32_t lastHeardMs() const { return _last_ms; }

private:
  bool _any = false;
  uint32_t _last_ms = 0;
  float _avg = 0;
};

inline const char* signalText(uint8_t level) {
  switch (level) {
    case 0: return "Kein Funkempfang";
    case 1: return "Funknetz schwach";
    case 2: return "Funknetz ok";
    default: return "Funknetz gut";
  }
}

}  // namespace homemesh
