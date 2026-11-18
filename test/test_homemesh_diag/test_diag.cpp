#include <gtest/gtest.h>

#include <memory>
#include <string>
#include "../../examples/companion_radio/ui-homemesh/RemoteCommands.h"

using namespace homemesh;

TEST(HomeMeshDiag, PeakWindowKeepsRecentAndAllTimeMaximum) {
  PeakWindow p{};
  p.add(30, 1000);
  p.add(80, 2000);
  p.add(20, 3000);
  EXPECT_EQ(80u, p.recent());
  EXPECT_EQ(80u, p.all);
  // next window: the old peak is still "recent"
  p.add(10, 1000 + DIAG_WINDOW_MS);
  EXPECT_EQ(80u, p.recent());
  // one more window later it is gone, the all-time maximum stays
  p.add(15, 1000 + 2 * DIAG_WINDOW_MS);
  EXPECT_EQ(15u, p.recent());
  EXPECT_EQ(80u, p.all);
}

TEST(HomeMeshDiag, LongSilenceForgetsTheOldWindow) {
  PeakWindow p{};
  p.add(500, 0);
  p.add(5, 5 * DIAG_WINDOW_MS);
  EXPECT_EQ(5u, p.recent());
}

TEST(HomeMeshDiag, PeakWindowAcrossMillisWrap) {
  PeakWindow p{};
  uint32_t t = 0xFFFFFF00u;
  p.add(40, t);
  p.add(20, t + 0x200);   // wrapped, same window
  EXPECT_EQ(40u, p.recent());
}

TEST(HomeMeshDiag, SlowLoopsAreCounted) {
  Diagnostics d{};
  d.noteLoop(50, 10);
  d.noteLoop(101, 20);
  d.noteLoop(250, 30);
  EXPECT_EQ(2u, d.slow_loops);
  EXPECT_EQ(250u, d.loop_ms.all);
}

TEST(HomeMeshDiag, LineFitsAMeshMessage) {
  Diagnostics d{};
  d.noteLoop(1234, 1);
  d.render_ms.add(456, 1);
  d.save_ms.add(789, 1);
  d.saves = 12345;
  d.save_bytes = 76543;
  d.slow_loops = 99999;
  d.heap_free = 123 * 1024; d.heap_min = 45 * 1024; d.heap_largest = 67 * 1024;
  d.psram_free = 7800 * 1024;
  d.rx = 123456; d.rx_errors = 789; d.tx = 45678;
  char line[TEXT_LEN];
  int n = d.format(line, sizeof(line));
  EXPECT_LT(n, TEXT_LEN) << line;
}

TEST(HomeMeshDiag, RemoteCommand) {
  std::unique_ptr<Model> m(new Model());
  m->clear();
  m->diag.noteLoop(42, 1);
  CommandResult r = {};
  handleRemoteCommand(*m, "!diag", r);
  EXPECT_EQ(0, strncmp(r.reply, "Loop 42/42ms", 12)) << r.reply;
}
