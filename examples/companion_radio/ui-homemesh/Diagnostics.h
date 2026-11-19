#pragma once

// Runtime measurements for the first tests on the device: how long the main
// loop stalls (the radio interrupt is only handled from the loop), how long
// drawing and saving take, free memory and packet counts.
// Shown on the "Technik" page, answered by !diag and logged on the serial
// port. Pure logic, unit-tested natively.

#include <stdint.h>
#include <stdio.h>

namespace homemesh {

static const uint32_t DIAG_WINDOW_MS = 10UL * 60 * 1000;
static const uint32_t DIAG_SLOW_LOOP_MS = 100;

// Largest value in the current and the previous 10-minute window, and since
// the start. Plain data (zeroed with the model), so no member initialisers.
struct PeakWindow {
  uint32_t cur, prev, all;
  uint32_t window_start;

  void roll(uint32_t now_ms) {
    if (now_ms - window_start < DIAG_WINDOW_MS) return;
    // a whole window passed without a call: nothing recent to remember
    prev = (now_ms - window_start < 2 * DIAG_WINDOW_MS) ? cur : 0;
    cur = 0;
    window_start = now_ms;
  }
  void add(uint32_t v, uint32_t now_ms) {
    roll(now_ms);
    if (v > cur) cur = v;
    if (v > all) all = v;
  }
  uint32_t recent() const { return cur > prev ? cur : prev; }
};

struct Diagnostics {
  PeakWindow loop_ms;     // time between two UI loop calls (whole Arduino loop)
  PeakWindow render_ms;   // lv_timer_handler() as a whole
  PeakWindow frame_ms;    // drawing one picture (render + flush)
  PeakWindow flush_ms;    // of that: copying into the panel's frame buffer
  PeakWindow build_ms;    // building a screen from the model
  uint32_t frame_avg_x8;  // running average of frame_ms, times 8
  PeakWindow save_ms;     // writing the state file
  uint32_t slow_loops;
  uint32_t saves;
  uint32_t save_bytes;

  uint32_t heap_free;      // internal RAM, bytes
  uint32_t heap_min;       // lowest free internal RAM since start
  uint32_t heap_largest;   // largest free internal block
  uint32_t psram_free;

  uint32_t rx, rx_errors, tx;

  void noteFrame(uint32_t ms, uint32_t flush, uint32_t now_ms) {
    frame_ms.add(ms, now_ms);
    flush_ms.add(flush, now_ms);
    frame_avg_x8 = frame_avg_x8 ? frame_avg_x8 - frame_avg_x8 / 8 + ms : ms * 8;
  }
  uint32_t frameAvg() const { return (frame_avg_x8 + 4) / 8; }

  void noteLoop(uint32_t gap_ms, uint32_t now_ms) {
    loop_ms.add(gap_ms, now_ms);
    if (gap_ms > DIAG_SLOW_LOOP_MS) slow_loops++;
  }

  // One line for !diag and the serial log (fits a mesh message).
  int format(char* out, size_t cap) const {
    return snprintf(out, cap,
                    "Loop %lu/%lums %lux>%lu | Frm %lu/%lu/%lu Bld %lu | Save %lums %lux %luB | RAM %luK min%lu blk%lu | "
                    "PS %luK | RX %lu/%lu TX %lu",
                    (unsigned long)loop_ms.recent(), (unsigned long)loop_ms.all, (unsigned long)slow_loops,
                    (unsigned long)DIAG_SLOW_LOOP_MS, (unsigned long)frame_ms.recent(), (unsigned long)frameAvg(),
                    (unsigned long)flush_ms.recent(), (unsigned long)build_ms.recent(), (unsigned long)save_ms.all,
                    (unsigned long)saves, (unsigned long)save_bytes, (unsigned long)(heap_free / 1024),
                    (unsigned long)(heap_min / 1024), (unsigned long)(heap_largest / 1024),
                    (unsigned long)(psram_free / 1024), (unsigned long)rx, (unsigned long)rx_errors, (unsigned long)tx);
  }
};

}  // namespace homemesh
