#include <gtest/gtest.h>

#include "../../examples/companion_radio/ui-homemesh/TimeZone.h"

using namespace homemesh;

TEST(HomeMeshTimeZone, LastSundays) {
  CivilDate a = civilFromDays(lastSundayOf(2026, 3));
  EXPECT_EQ(29, a.day);
  CivilDate b = civilFromDays(lastSundayOf(2026, 10));
  EXPECT_EQ(25, b.day);
  CivilDate c = civilFromDays(lastSundayOf(2025, 3));
  EXPECT_EQ(30, c.day);
  CivilDate d = civilFromDays(lastSundayOf(2027, 3));
  EXPECT_EQ(28, d.day);
  CivilDate e = civilFromDays(lastSundayOf(2026, 12));   // year wrap
  EXPECT_EQ(27, e.day);
  EXPECT_EQ(12, e.month);
}

TEST(HomeMeshTimeZone, SummerTimeSwitchesAtOneUtc) {
  const uint32_t start = 1774746000UL;   // 2026-03-29 01:00 UTC
  const uint32_t end = 1792890000UL;     // 2026-10-25 01:00 UTC
  EXPECT_FALSE(isEuSummerTime(start - 1));
  EXPECT_TRUE(isEuSummerTime(start));
  EXPECT_TRUE(isEuSummerTime(end - 1));
  EXPECT_FALSE(isEuSummerTime(end));
}

TEST(HomeMeshTimeZone, LocalWallClock) {
  LocalTime w = splitLocal(utcToLocal(1794940200UL));   // 2026-11-17 18:30 UTC
  EXPECT_EQ(2026, w.date.year);
  EXPECT_EQ(11, w.date.month);
  EXPECT_EQ(17, w.date.day);
  EXPECT_EQ(19, w.hour);
  EXPECT_EQ(30, w.minute);

  LocalTime s = splitLocal(utcToLocal(1782907200UL));   // 2026-07-01 12:00 UTC
  EXPECT_EQ(14, s.hour);
}

TEST(HomeMeshTimeZone, NewYearInLocalTime) {
  // 2026-12-31 23:30 UTC is already 2027 in Germany
  LocalTime w = splitLocal(utcToLocal(1798759800UL));
  EXPECT_EQ(2027, w.date.year);
  EXPECT_EQ(1, w.date.month);
  EXPECT_EQ(1, w.date.day);
  EXPECT_EQ(0, w.hour);
  EXPECT_EQ(30, w.minute);
}

TEST(HomeMeshTimeZone, UnsetClockIsInvalid) {
  EXPECT_FALSE(isValidClock(0));
  EXPECT_FALSE(isValidClock(1715770351UL));   // MeshCore default build time range
  EXPECT_TRUE(isValidClock(1794940200UL));
}
