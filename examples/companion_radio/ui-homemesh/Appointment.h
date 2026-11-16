#pragma once

// Appointments sent with the remote command "!termin <name> <day> <time>".
//
// The words of the argument are scanned for a time, a day and the keyword
// "täglich" (daily); everything else forms the name. The command is German
// because the device interface is German.
//
//   time:  19:00 | 19 Uhr | 19:00 Uhr | 19Uhr      (only ":" or "Uhr", never "19.30")
//   day:   heute | morgen | übermorgen | Mo..So | Montag..Sonntag
//          | 24.12. | 24.12 | 24.12.2026
//   daily: täglich
//
// Rules:
//   - time only: today, or tomorrow if that time has passed
//   - weekday: next such day, today only if the time has not passed
//   - date without year: this year; up to 60 days in the past is an error,
//     further back means next year (e.g. "05.01." written in December)
//   - weekday together with a date: the date counts, the weekday is a check;
//     a mismatch is an error, nothing is guessed
//   - "um" before a time and "am" before a day are filler words and dropped
//
// Pure logic without Arduino dependencies, so it can be unit-tested natively.

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <helpers/UTF8Helpers.h>

namespace homemesh {

static const int APPT_NAME_MAX = 32;          // bytes including terminator
static const int APPT_PAST_ERROR_DAYS = 60;

struct CivilDate {
  int year, month, day;
};

enum class ApptStatus : uint8_t {
  Ok,
  NoTime,          // no time given
  BadTime,         // e.g. 25:00
  BadDate,         // e.g. 31.02. or 19.30
  InPast,          // date/time already passed
  WeekdayMismatch, // "Mo, 12.11." but 12.11. is a Thursday
  Ambiguous,       // two times, two days, or "täglich" together with a day
};

struct Appointment {
  ApptStatus status;
  bool daily;
  CivilDate date;       // valid when !daily
  int hour, minute;
  char name[APPT_NAME_MAX];
  int weekday_given;    // 0=Mo..6=So, -1 if none (for error messages)
  int weekday_actual;   // weekday of the resolved date
};

// ---- date arithmetic (proleptic Gregorian, days since 1970-01-01) ----

inline int64_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

inline CivilDate civilFromDays(int64_t z) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int y = (int)yoe + (int)(era * 400);
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  return CivilDate{ y + (m <= 2), (int)m, (int)d };
}

// 0 = Monday .. 6 = Sunday
inline int weekdayOf(const CivilDate& d) {
  int64_t days = daysFromCivil(d.year, d.month, d.day);  // 1970-01-01 was a Thursday
  int w = (int)((days + 3) % 7);
  return w < 0 ? w + 7 : w;
}

inline bool isValidDate(int y, int m, int d) {
  if (m < 1 || m > 12 || d < 1) return false;
  static const int len[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  int max = len[m - 1];
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) max = 29;
  return d <= max;
}

inline const char* weekdayShort(int w) {
  static const char* names[7] = { "Mo", "Di", "Mi", "Do", "Fr", "Sa", "So" };
  return (w >= 0 && w < 7) ? names[w] : "?";
}

// ---- token helpers ----

namespace detail {

// Lower-case ASCII and the German capital umlauts, so "Täglich" == "täglich".
inline void lowerInto(const char* src, size_t len, char* dst, size_t cap) {
  size_t o = 0;
  for (size_t i = 0; i < len && o + 1 < cap; i++) {
    unsigned char c = (unsigned char)src[i];
    if (c >= 'A' && c <= 'Z') { dst[o++] = (char)(c + 32); continue; }
    if (c == 0xC3 && i + 1 < len && o + 2 < cap) {
      unsigned char n = (unsigned char)src[i + 1];
      if (n == 0x84 || n == 0x96 || n == 0x9C) n += 0x20;  // Ä Ö Ü -> ä ö ü
      dst[o++] = (char)c;
      dst[o++] = (char)n;
      i++;
      continue;
    }
    dst[o++] = (char)c;
  }
  dst[o] = 0;
}

inline bool allDigits(const char* s, size_t len) {
  if (len == 0) return false;
  for (size_t i = 0; i < len; i++) if (s[i] < '0' || s[i] > '9') return false;
  return true;
}

inline int toInt(const char* s, size_t len) {
  int v = 0;
  for (size_t i = 0; i < len; i++) v = v * 10 + (s[i] - '0');
  return v;
}

// "19:00" (also "7:30"). Returns 1 = time, 0 = not a time, -1 = looks like a time but invalid.
inline int parseClock(const char* s, int& h, int& m) {
  const char* colon = strchr(s, ':');
  if (!colon) return 0;
  size_t hl = colon - s, ml = strlen(colon + 1);
  if (!allDigits(s, hl) || hl > 2 || !allDigits(colon + 1, ml) || ml != 2) return 0;
  h = toInt(s, hl);
  m = toInt(colon + 1, ml);
  return (h <= 23 && m <= 59) ? 1 : -1;
}

// "24.12.", "24.12", "24.12.2026". Returns 1 = date, 0 = not a date, -1 = date-like but invalid.
inline int parseDate(const char* s, int& d, int& m, int& y) {
  const char* p1 = strchr(s, '.');
  if (!p1) return 0;
  size_t dl = p1 - s;
  if (!allDigits(s, dl) || dl > 2) return 0;
  const char* p2 = strchr(p1 + 1, '.');
  size_t ml = p2 ? (size_t)(p2 - (p1 + 1)) : strlen(p1 + 1);
  if (!allDigits(p1 + 1, ml) || ml > 2) return 0;
  d = toInt(s, dl);
  m = toInt(p1 + 1, ml);
  y = 0;
  if (p2 && p2[1]) {
    size_t yl = strlen(p2 + 1);
    if (!allDigits(p2 + 1, yl) || yl != 4) return -1;
    y = toInt(p2 + 1, yl);
  }
  return 1;
}

inline int parseWeekday(const char* lower) {
  static const char* shortN[7] = { "mo", "di", "mi", "do", "fr", "sa", "so" };
  static const char* longN[7]  = { "montag", "dienstag", "mittwoch", "donnerstag", "freitag", "samstag", "sonntag" };
  for (int i = 0; i < 7; i++) {
    if (strcmp(lower, shortN[i]) == 0 || strcmp(lower, longN[i]) == 0) return i;
  }
  return -1;
}

}  // namespace detail

// Parses the arguments of "!termin" relative to the device's local date and time.
inline Appointment parseAppointment(const char* args, const CivilDate& today, int nowHour, int nowMinute) {
  using namespace detail;

  Appointment a;
  memset(&a, 0, sizeof(a));
  a.status = ApptStatus::Ok;
  a.weekday_given = -1;
  a.weekday_actual = -1;

  enum Kind : uint8_t { Name, Time, Uhr, Rel, Weekday, Date, Daily, Filler };
  struct Tok { const char* p; size_t len; Kind kind; int v1, v2, v3; };
  Tok toks[24];
  int n = 0;

  // split on spaces; a trailing comma belongs to nothing ("Mo," -> "Mo")
  for (const char* s = args; *s && n < 24;) {
    while (*s == ' ') s++;
    if (!*s) break;
    const char* e = s;
    while (*e && *e != ' ') e++;
    size_t len = e - s;
    while (len > 0 && s[len - 1] == ',') len--;
    if (len > 0) toks[n++] = Tok{ s, len, Name, 0, 0, 0 };
    s = e;
  }

  int times = 0, days = 0;
  bool badTime = false, badDate = false;

  for (int i = 0; i < n; i++) {
    char low[40];
    lowerInto(toks[i].p, toks[i].len, low, sizeof(low));
    size_t ll = strlen(low);
    int h, m, d, mo, y;

    // "19uhr"
    if (ll > 3 && strcmp(low + ll - 3, "uhr") == 0 && allDigits(low, ll - 3) && ll - 3 <= 2) {
      h = toInt(low, ll - 3);
      if (h <= 23) { toks[i].kind = Time; toks[i].v1 = h; toks[i].v2 = 0; times++; }
      else badTime = true;
      continue;
    }
    if (strcmp(low, "uhr") == 0) { toks[i].kind = Uhr; continue; }

    int c = parseClock(low, h, m);
    if (c == 1) { toks[i].kind = Time; toks[i].v1 = h; toks[i].v2 = m; times++; continue; }
    if (c == -1) { badTime = true; toks[i].kind = Filler; continue; }

    // bare number followed by "Uhr"
    if (allDigits(low, ll) && ll <= 2 && i + 1 < n) {
      char next[8];
      lowerInto(toks[i + 1].p, toks[i + 1].len, next, sizeof(next));
      if (strcmp(next, "uhr") == 0) {
        h = toInt(low, ll);
        if (h <= 23) { toks[i].kind = Time; toks[i].v1 = h; toks[i].v2 = 0; times++; }
        else { badTime = true; toks[i].kind = Filler; }
        continue;
      }
    }

    int dt = parseDate(low, d, mo, y);
    if (dt != 0) {
      if (dt == -1 || mo < 1 || mo > 12 || d < 1 || d > 31) { badDate = true; toks[i].kind = Filler; continue; }
      toks[i].kind = Date; toks[i].v1 = d; toks[i].v2 = mo; toks[i].v3 = y; days++;
      continue;
    }

    if (strcmp(low, "heute") == 0) { toks[i].kind = Rel; toks[i].v1 = 0; days++; continue; }
    if (strcmp(low, "morgen") == 0) { toks[i].kind = Rel; toks[i].v1 = 1; days++; continue; }
    if (strcmp(low, "übermorgen") == 0 || strcmp(low, "uebermorgen") == 0) { toks[i].kind = Rel; toks[i].v1 = 2; days++; continue; }
    if (strcmp(low, "täglich") == 0 || strcmp(low, "taeglich") == 0) { toks[i].kind = Daily; continue; }

    int w = parseWeekday(low);
    if (w >= 0) { toks[i].kind = Weekday; toks[i].v1 = w; continue; }  // counted below
  }

  // filler words: "um" before a time, "am" before a day
  for (int i = 0; i + 1 < n; i++) {
    if (toks[i].kind != Name) continue;
    char low[8];
    lowerInto(toks[i].p, toks[i].len, low, sizeof(low));
    Kind next = toks[i + 1].kind;
    if (strcmp(low, "um") == 0 && next == Time) toks[i].kind = Filler;
    if (strcmp(low, "am") == 0 && (next == Date || next == Weekday)) toks[i].kind = Filler;
  }

  // name = remaining words in order
  char name[APPT_NAME_MAX * 2] = { 0 };
  size_t nl = 0;
  for (int i = 0; i < n; i++) {
    if (toks[i].kind != Name) continue;
    if (nl > 0 && nl + 1 < sizeof(name)) name[nl++] = ' ';
    size_t copy = toks[i].len;
    if (nl + copy >= sizeof(name)) copy = sizeof(name) - 1 - nl;
    memcpy(name + nl, toks[i].p, copy);
    nl += copy;
    name[nl] = 0;
  }
  if (nl == 0) strcpy(name, "Funktermin");
  size_t keep = mesh::validUtf8PrefixLength(name, APPT_NAME_MAX - 1);
  memcpy(a.name, name, keep);
  a.name[keep] = 0;

  // collect the parts
  int weekdays = 0, dateIdx = -1, relIdx = -1, timeIdx = -1;
  bool daily = false;
  for (int i = 0; i < n; i++) {
    switch (toks[i].kind) {
      case Weekday: weekdays++; a.weekday_given = toks[i].v1; break;
      case Date: dateIdx = i; break;
      case Rel: relIdx = i; break;
      case Time: timeIdx = i; break;
      case Daily: daily = true; break;
      default: break;
    }
  }

  if (badTime) { a.status = ApptStatus::BadTime; return a; }
  if (badDate) { a.status = ApptStatus::BadDate; return a; }
  if (times == 0) { a.status = ApptStatus::NoTime; return a; }
  if (times > 1 || days > 1 || weekdays > 1 || (relIdx >= 0 && weekdays > 0)
      || (daily && (days > 0 || weekdays > 0))) {
    a.status = ApptStatus::Ambiguous;
    return a;
  }

  a.hour = toks[timeIdx].v1;
  a.minute = toks[timeIdx].v2;
  if (daily) { a.daily = true; return a; }

  const int64_t todayDays = daysFromCivil(today.year, today.month, today.day);
  const int nowMin = nowHour * 60 + nowMinute;
  const int apptMin = a.hour * 60 + a.minute;
  int64_t target;

  if (dateIdx >= 0) {
    int d = toks[dateIdx].v1, mo = toks[dateIdx].v2, y = toks[dateIdx].v3;
    bool explicitYear = y != 0;
    if (!explicitYear) y = today.year;
    if (!isValidDate(y, mo, d)) { a.status = ApptStatus::BadDate; return a; }
    target = daysFromCivil(y, mo, d);
    bool past = target < todayDays || (target == todayDays && apptMin <= nowMin);
    if (past && !explicitYear && todayDays - target > APPT_PAST_ERROR_DAYS) {
      y++;
      if (!isValidDate(y, mo, d)) { a.status = ApptStatus::BadDate; return a; }  // 29.02.
      target = daysFromCivil(y, mo, d);
      past = false;
    }
    a.date = civilFromDays(target);
    a.weekday_actual = weekdayOf(a.date);
    bool mismatch = a.weekday_given >= 0 && a.weekday_given != a.weekday_actual;
    if (mismatch) { a.status = past ? ApptStatus::InPast : ApptStatus::WeekdayMismatch; return a; }
    if (past) { a.status = ApptStatus::InPast; return a; }
  } else if (relIdx >= 0) {
    target = todayDays + toks[relIdx].v1;
    if (target == todayDays && apptMin <= nowMin) {
      a.date = today;
      a.weekday_actual = weekdayOf(today);
      a.status = ApptStatus::InPast;
      return a;
    }
  } else if (weekdays > 0) {
    int ahead = (a.weekday_given - weekdayOf(today) + 7) % 7;
    if (ahead == 0 && apptMin <= nowMin) ahead = 7;
    target = todayDays + ahead;
  } else {
    target = todayDays + (apptMin <= nowMin ? 1 : 0);
  }

  a.date = civilFromDays(target);
  a.weekday_actual = weekdayOf(a.date);
  return a;
}

// "Di 17.11. 19:00", "Di 05.01.2027 10:00" (year only if not the current one), "täglich 19:00"
inline void formatWhen(const Appointment& a, int currentYear, char* out, size_t cap) {
  if (a.daily) {
    snprintf(out, cap, "täglich %02d:%02d", a.hour, a.minute);
  } else if (a.date.year == currentYear) {
    snprintf(out, cap, "%s %02d.%02d. %02d:%02d", weekdayShort(a.weekday_actual),
             a.date.day, a.date.month, a.hour, a.minute);
  } else {
    snprintf(out, cap, "%s %02d.%02d.%04d %02d:%02d", weekdayShort(a.weekday_actual),
             a.date.day, a.date.month, a.date.year, a.hour, a.minute);
  }
}

// Reply sent back to the person who issued "!termin".
inline void formatAppointmentReply(const Appointment& a, int currentYear, char* out, size_t cap) {
  char when[40];
  switch (a.status) {
    case ApptStatus::Ok:
      formatWhen(a, currentYear, when, sizeof(when));
      snprintf(out, cap, "OK · %s · %s", a.name, when);
      break;
    case ApptStatus::NoTime:
      snprintf(out, cap, "FEHLER · Uhrzeit fehlt (z. B. 19:00)");
      break;
    case ApptStatus::BadTime:
      snprintf(out, cap, "FEHLER · Uhrzeit ungültig (z. B. 19:00)");
      break;
    case ApptStatus::BadDate:
      snprintf(out, cap, "FEHLER · Datum ungültig (z. B. 24.12., Uhrzeit mit :)");
      break;
    case ApptStatus::InPast:
      if (a.weekday_given >= 0 && a.weekday_actual >= 0 && a.weekday_given != a.weekday_actual) {
        snprintf(out, cap, "FEHLER · %02d.%02d. ist ein %s und schon vorbei",
                 a.date.day, a.date.month, weekdayShort(a.weekday_actual));
      } else {
        snprintf(out, cap, "FEHLER · %02d.%02d. %02d:%02d ist schon vorbei",
                 a.date.day, a.date.month, a.hour, a.minute);
      }
      break;
    case ApptStatus::WeekdayMismatch:
      snprintf(out, cap, "FEHLER · %02d.%02d. ist ein %s, nicht %s",
               a.date.day, a.date.month, weekdayShort(a.weekday_actual), weekdayShort(a.weekday_given));
      break;
    case ApptStatus::Ambiguous:
      snprintf(out, cap, "FEHLER · Tag oder Uhrzeit doppelt angegeben");
      break;
  }
}

}  // namespace homemesh
