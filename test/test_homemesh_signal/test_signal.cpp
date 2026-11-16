#include <gtest/gtest.h>

#include "../../examples/companion_radio/ui-homemesh/SignalMeter.h"

using namespace homemesh;

TEST(HomeMeshSignal, NothingHeardIsNoSignal) {
  SignalMeter m;
  EXPECT_EQ(0, m.level(1000));
}

TEST(HomeMeshSignal, LevelsFollowSnr) {
  SignalMeter good, ok, weak;
  good.onPacket(7.5f, 0);
  ok.onPacket(-2.0f, 0);
  weak.onPacket(-9.0f, 0);
  EXPECT_EQ(4, good.level(1000));
  EXPECT_EQ(2, ok.level(1000));
  EXPECT_EQ(1, weak.level(1000));
}

TEST(HomeMeshSignal, AverageSmoothsSingleOutliers) {
  SignalMeter m;
  for (int i = 0; i < 10; i++) m.onPacket(6.0f, i * 1000);
  m.onPacket(-12.0f, 11000);          // one bad packet
  EXPECT_EQ(3, m.level(12000));       // 6*0.8 - 12*0.2 = 2.4 dB -> still 3 bars
}

TEST(HomeMeshSignal, LongSilenceMeansNoSignal) {
  SignalMeter m;
  m.onPacket(6.0f, 1000);
  EXPECT_EQ(4, m.level(1000 + SignalMeter::SILENCE_MS));
  EXPECT_EQ(0, m.level(1000 + SignalMeter::SILENCE_MS + 1));
}

TEST(HomeMeshSignal, Texts) {
  EXPECT_STREQ("Kein Funkempfang", signalText(0));
  EXPECT_STREQ("Funknetz schwach", signalText(1));
  EXPECT_STREQ("Funknetz ok", signalText(2));
  EXPECT_STREQ("Funknetz gut", signalText(4));
}
