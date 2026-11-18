#pragma once

// Link between the ESP32-S3 (HomeMesh) and the RP2040 of the SenseCAP
// Indicator, which drives the buzzer. Shared by both firmwares.
//
// UART at 115200 baud (ESP32 TX 19 / RX 20, RP2040 RX 17 / TX 16). Packets
// follow Seeed's scheme - one type byte, then a little-endian uint32 - and
// are COBS-encoded with a 0x00 delimiter (as Arduino PacketSerial does).
//
// Pure logic without Arduino dependencies, unit-tested natively.

#include <stddef.h>
#include <stdint.h>

namespace homemesh {

static const uint32_t RP_BAUD = 115200;
static const uint8_t RP_PKT_BEEP_ON = 0xA1;    // para = SoundPattern
static const uint8_t RP_PKT_BEEP_OFF = 0xA2;   // para unused

enum class SoundPattern : uint8_t { Message = 1, Reminder = 2, Ring = 3 };

// ---- COBS ----

// Encodes len bytes and appends the 0x00 delimiter. out needs len + 2 bytes
// (len <= 254). Returns the number of bytes written.
inline size_t cobsEncode(const uint8_t* in, size_t len, uint8_t* out) {
  size_t code_pos = 0, o = 1;
  uint8_t code = 1;
  for (size_t i = 0; i < len; i++) {
    if (in[i] == 0) {
      out[code_pos] = code;
      code_pos = o++;
      code = 1;
    } else {
      out[o++] = in[i];
      code++;
    }
  }
  out[code_pos] = code;
  out[o++] = 0;
  return o;
}

// Decodes one frame without its delimiter. Returns the decoded length, or 0
// for a malformed frame.
inline size_t cobsDecode(const uint8_t* in, size_t len, uint8_t* out, size_t cap) {
  size_t i = 0, o = 0;
  while (i < len) {
    uint8_t code = in[i++];
    if (code == 0 || i + code - 1 > len) return 0;
    for (uint8_t k = 1; k < code; k++) {
      if (o >= cap) return 0;
      out[o++] = in[i++];
    }
    if (code != 0xFF && i < len) {
      if (o >= cap) return 0;
      out[o++] = 0;
    }
  }
  return o;
}

// One command frame, ready to write to the UART (max 7 bytes).
inline size_t buildRpCommand(uint8_t type, uint32_t para, uint8_t out[8]) {
  uint8_t raw[5] = { type, (uint8_t)para, (uint8_t)(para >> 8), (uint8_t)(para >> 16), (uint8_t)(para >> 24) };
  return cobsEncode(raw, sizeof(raw), out);
}

// Collects UART bytes into frames.
struct CobsReceiver {
  uint8_t buf[32];
  size_t len = 0;
  bool overflow = false;

  // Returns the decoded length when a frame is complete, else 0.
  size_t push(uint8_t b, uint8_t* out, size_t cap) {
    if (b != 0) {
      if (len < sizeof(buf)) buf[len++] = b;
      else overflow = true;
      return 0;
    }
    size_t n = overflow ? 0 : cobsDecode(buf, len, out, cap);
    len = 0;
    overflow = false;
    return n;
  }
};

// ---- tone patterns ----

struct ToneStep {
  uint16_t hz;   // 0 = silence
  uint16_t ms;
};

static const uint16_t BUZZ_HZ = 2700;   // to be tuned on the device

// New message from a favourite: two short beeps.
static const ToneStep PATTERN_MESSAGE[] = {
  { BUZZ_HZ, 120 }, { 0, 100 }, { BUZZ_HZ, 120 },
};
// Appointment reminder: three beeps, twice.
static const ToneStep PATTERN_REMINDER[] = {
  { BUZZ_HZ, 150 }, { 0, 100 }, { BUZZ_HZ, 150 }, { 0, 100 }, { BUZZ_HZ, 150 }, { 0, 600 },
  { BUZZ_HZ, 150 }, { 0, 100 }, { BUZZ_HZ, 150 }, { 0, 100 }, { BUZZ_HZ, 150 },
};
// Ring: repeats until it is stopped or RING_MAX_MS has passed.
static const ToneStep PATTERN_RING[] = {
  { BUZZ_HZ, 400 }, { 0, 200 }, { BUZZ_HZ, 400 }, { 0, 1500 },
};
static const uint32_t RING_MAX_MS = 2UL * 60 * 1000;

struct PatternInfo {
  const ToneStep* steps;
  uint8_t count;
  bool repeat;
};

inline PatternInfo patternFor(uint32_t id) {
  switch (id) {
    case (uint32_t)SoundPattern::Message: return { PATTERN_MESSAGE, sizeof(PATTERN_MESSAGE) / sizeof(ToneStep), false };
    case (uint32_t)SoundPattern::Reminder: return { PATTERN_REMINDER, sizeof(PATTERN_REMINDER) / sizeof(ToneStep), false };
    case (uint32_t)SoundPattern::Ring: return { PATTERN_RING, sizeof(PATTERN_RING) / sizeof(ToneStep), true };
    default: return { nullptr, 0, false };
  }
}

// Steps through a pattern; the caller turns the tone on and off. Time in ms
// (wrapping millis() is fine).
struct TonePlayer {
  PatternInfo p = { nullptr, 0, false };
  uint8_t step = 0;
  uint32_t step_end = 0;
  uint32_t started = 0;

  bool playing() const { return p.steps != nullptr; }

  // Returns the frequency to sound now (0 = silent).
  uint16_t start(uint32_t id, uint32_t now) {
    p = patternFor(id);
    step = 0;
    started = now;
    if (!playing()) return 0;
    step_end = now + p.steps[0].ms;
    return p.steps[0].hz;
  }

  void stop() { p = { nullptr, 0, false }; }

  // Call often. Returns true when the tone must change; *hz is the new one.
  bool update(uint32_t now, uint16_t* hz) {
    if (!playing() || (int32_t)(now - step_end) < 0) return false;
    if (++step >= p.count) {
      if (!p.repeat || now - started >= RING_MAX_MS) {
        stop();
        *hz = 0;
        return true;
      }
      step = 0;
    }
    step_end = now + p.steps[step].ms;
    *hz = p.steps[step].hz;
    return true;
  }
};

}  // namespace homemesh
