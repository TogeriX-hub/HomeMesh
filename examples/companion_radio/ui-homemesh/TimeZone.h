#pragma once

// Converts the mesh clock (UTC unix time) into Central European local time.
//
// Summer time follows the EU rule: it starts on the last Sunday of March at
// 01:00 UTC and ends on the last Sunday of October at 01:00 UTC.
// Standard time is UTC+1, summer time UTC+2.
//
// The model keeps "local epoch" seconds: local wall-clock time counted as if
// it were UTC. That keeps date arithmetic simple (daysFromCivil etc.).

#include <stdint.h>
#include "Appointment.h"

namespace homemesh {

static const int32_t TZ_STD_OFFSET_S = 3600;
static const int32_t TZ_DST_OFFSET_S = 7200;

// Clock values before this are treated as "not set" (2025-01-01 00:00 UTC).
static const uint32_t TZ_MIN_VALID_UTC = 1735689600UL;

// Day number (days since 1970-01-01) of the last Sunday of a month.
inline int64_t lastSundayOf(int year, int month) {
  int next_y = month == 12 ? year + 1 : year;
  int next_m = month == 12 ? 1 : month + 1;
  int64_t last = daysFromCivil(next_y, next_m, 1) - 1;
  int wd = weekdayOf(civilFromDays(last));   // 0 = Monday .. 6 = Sunday
  return last - ((wd + 1) % 7);
}

inline bool isEuSummerTime(uint32_t utc) {
  int64_t days = (int64_t)(utc / 86400);
  int year = civilFromDays(days).year;
  int64_t start = lastSundayOf(year, 3) * 86400 + 3600;
  int64_t end = lastSundayOf(year, 10) * 86400 + 3600;
  return (int64_t)utc >= start && (int64_t)utc < end;
}

inline int32_t localOffsetFor(uint32_t utc) {
  return isEuSummerTime(utc) ? TZ_DST_OFFSET_S : TZ_STD_OFFSET_S;
}

inline uint32_t utcToLocal(uint32_t utc) { return utc + (uint32_t)localOffsetFor(utc); }

inline bool isValidClock(uint32_t utc) { return utc >= TZ_MIN_VALID_UTC; }

struct LocalTime {
  CivilDate date;
  int hour, minute, second;
};

inline LocalTime splitLocal(uint32_t local) {
  LocalTime t;
  t.date = civilFromDays((int64_t)(local / 86400));
  uint32_t sod = local % 86400;
  t.hour = (int)(sod / 3600);
  t.minute = (int)((sod / 60) % 60);
  t.second = (int)(sod % 60);
  return t;
}

}  // namespace homemesh
