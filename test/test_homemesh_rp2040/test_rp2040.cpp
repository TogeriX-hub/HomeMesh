#include <gtest/gtest.h>

#include <vector>
#include "../../examples/companion_radio/ui-homemesh/Rp2040Link.h"

using namespace homemesh;

static std::vector<uint8_t> enc(std::vector<uint8_t> in) {
  std::vector<uint8_t> out(in.size() + 2);
  out.resize(cobsEncode(in.data(), in.size(), out.data()));
  return out;
}

TEST(HomeMeshRp2040, CobsKnownVectors) {
  EXPECT_EQ((std::vector<uint8_t>{ 0x01, 0x01, 0x00 }), enc({ 0x00 }));
  EXPECT_EQ((std::vector<uint8_t>{ 0x01, 0x01, 0x01, 0x00 }), enc({ 0x00, 0x00 }));
  EXPECT_EQ((std::vector<uint8_t>{ 0x03, 0x11, 0x22, 0x02, 0x33, 0x00 }), enc({ 0x11, 0x22, 0x00, 0x33 }));
  EXPECT_EQ((std::vector<uint8_t>{ 0x05, 0x11, 0x22, 0x33, 0x44, 0x00 }), enc({ 0x11, 0x22, 0x33, 0x44 }));
  EXPECT_EQ((std::vector<uint8_t>{ 0x01, 0x00 }), enc({}));
}

TEST(HomeMeshRp2040, CommandRoundTripThroughReceiver) {
  uint8_t frame[8];
  size_t n = buildRpCommand(RP_PKT_BEEP_ON, (uint32_t)SoundPattern::Ring, frame);
  EXPECT_EQ(7u, n);
  EXPECT_EQ(0, frame[n - 1]);

  CobsReceiver rx;
  uint8_t out[8];
  size_t got = 0;
  rx.push(0, out, sizeof(out));   // stray delimiter before the frame: ignored
  for (size_t i = 0; i < n; i++) got = rx.push(frame[i], out, sizeof(out));
  ASSERT_EQ(5u, got);
  EXPECT_EQ(RP_PKT_BEEP_ON, out[0]);
  EXPECT_EQ(3, out[1]);
  EXPECT_EQ(0, out[2]);
  EXPECT_EQ(0, out[4]);
}

TEST(HomeMeshRp2040, MalformedFramesAreDropped) {
  uint8_t out[8];
  const uint8_t bad[] = { 0x05, 0x11 };   // code points past the end
  EXPECT_EQ(0u, cobsDecode(bad, sizeof(bad), out, sizeof(out)));
  CobsReceiver rx;
  for (int i = 0; i < 40; i++) rx.push(0x22, out, sizeof(out));   // too long
  EXPECT_EQ(0u, rx.push(0, out, sizeof(out)));
  // the next frame works again
  uint8_t frame[8];
  size_t n = buildRpCommand(RP_PKT_BEEP_OFF, 0, frame);
  size_t got = 0;
  for (size_t i = 0; i < n; i++) got = rx.push(frame[i], out, sizeof(out));
  EXPECT_EQ(5u, got);
  EXPECT_EQ(RP_PKT_BEEP_OFF, out[0]);
}

TEST(HomeMeshRp2040, MessagePatternPlaysOnce) {
  TonePlayer p;
  EXPECT_EQ(BUZZ_HZ, p.start((uint32_t)SoundPattern::Message, 1000));
  uint16_t hz = 1;
  EXPECT_FALSE(p.update(1119, &hz));
  EXPECT_TRUE(p.update(1120, &hz));
  EXPECT_EQ(0, hz);
  EXPECT_TRUE(p.update(1220, &hz));
  EXPECT_EQ(BUZZ_HZ, hz);
  EXPECT_TRUE(p.update(1340, &hz));
  EXPECT_EQ(0, hz);
  EXPECT_FALSE(p.playing());
}

TEST(HomeMeshRp2040, RingRepeatsUntilLimit) {
  TonePlayer p;
  uint32_t t = 0xFFFFF000u;   // across the millis() wrap
  p.start((uint32_t)SoundPattern::Ring, t);
  uint16_t hz;
  int changes = 0;
  for (uint32_t ms = 0; ms < RING_MAX_MS + 5000; ms += 10) {
    if (p.update(t + ms, &hz)) changes++;
    if (!p.playing()) break;
  }
  EXPECT_FALSE(p.playing());
  EXPECT_GT(changes, 100);   // ~2.5 s per round, 4 changes each, for 2 minutes
}

TEST(HomeMeshRp2040, UnknownPatternIsSilent) {
  TonePlayer p;
  EXPECT_EQ(0, p.start(99, 0));
  EXPECT_FALSE(p.playing());
}
