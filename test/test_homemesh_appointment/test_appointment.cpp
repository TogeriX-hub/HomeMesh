#include <gtest/gtest.h>

#include "../../examples/companion_radio/ui-homemesh/Appointment.h"

using namespace homemesh;

namespace {

// Reference "now" for most tests: Monday 16.11.2026, 14:15
const CivilDate kToday{ 2026, 11, 16 };

std::string reply(const char* args, CivilDate today = kToday, int h = 14, int m = 15) {
  Appointment a = parseAppointment(args, today, h, m);
  char out[160];
  formatAppointmentReply(a, today.year, out, sizeof(out));
  return out;
}

}  // namespace

TEST(HomeMeshDate, WeekdayAndRoundTrip) {
  EXPECT_EQ(0, weekdayOf(CivilDate{ 2026, 11, 16 }));  // Monday
  EXPECT_EQ(3, weekdayOf(CivilDate{ 2026, 11, 12 }));  // Thursday
  EXPECT_EQ(3, weekdayOf(CivilDate{ 2026, 12, 24 }));  // Thursday
  EXPECT_EQ(1, weekdayOf(CivilDate{ 2027, 1, 5 }));    // Tuesday
  CivilDate d = civilFromDays(daysFromCivil(2028, 2, 29));
  EXPECT_EQ(2028, d.year);
  EXPECT_EQ(2, d.month);
  EXPECT_EQ(29, d.day);
}

// The cases agreed in the plan
TEST(HomeMeshAppointment, Tomorrow) {
  EXPECT_EQ("OK · Anrufen · Di 17.11. 19:00", reply("Anrufen morgen 19:00"));
}

TEST(HomeMeshAppointment, WeekdayAhead) {
  EXPECT_EQ("OK · Funken · Fr 20.11. 18:30", reply("Funken Fr 18:30"));
}

TEST(HomeMeshAppointment, TimeOnlyToday) {
  EXPECT_EQ("OK · Funktermin · Mo 16.11. 20:00", reply("20:00"));
}

TEST(HomeMeshAppointment, SameWeekdayTimePassedMeansNextWeek) {
  EXPECT_EQ("OK · Besuch · Mo 23.11. 10:00", reply("Besuch Mo 10:00"));
}

TEST(HomeMeshAppointment, DateWithUhr) {
  EXPECT_EQ("OK · Kaffee · Do 24.12. 15:00", reply("Kaffee 24.12. 15 Uhr"));
}

TEST(HomeMeshAppointment, Daily) {
  EXPECT_EQ("OK · Funkrunde · täglich 19:00", reply("Funkrunde täglich 19:00"));
}

TEST(HomeMeshAppointment, RecentPastDateWithWrongWeekday) {
  EXPECT_EQ("FEHLER · 12.11. ist ein Do und schon vorbei", reply("Essen Mo, 12.11 12:00"));
}

TEST(HomeMeshAppointment, MissingTime) {
  EXPECT_EQ("FEHLER · Uhrzeit fehlt (z. B. 19:00)", reply("Anrufen morgen"));
}

TEST(HomeMeshAppointment, DateAcrossNewYear) {
  EXPECT_EQ("OK · Treffen · Di 05.01.2027 10:00", reply("Treffen 05.01. 10:00", CivilDate{ 2026, 12, 20 }));
}

// Further edge cases
TEST(HomeMeshAppointment, TimeOnlyPassedMeansTomorrow) {
  EXPECT_EQ("OK · Funktermin · Di 17.11. 09:00", reply("09:00"));
}

TEST(HomeMeshAppointment, WeekdayMismatchInFuture) {
  EXPECT_EQ("FEHLER · 23.11. ist ein Mo, nicht Di", reply("Essen Di 23.11. 12:00"));
}

TEST(HomeMeshAppointment, MatchingWeekdayAndDate) {
  EXPECT_EQ("OK · Essen · Mo 23.11. 12:00", reply("Essen Mo, 23.11. 12:00"));
}

TEST(HomeMeshAppointment, TodayAlreadyPassed) {
  EXPECT_EQ("FEHLER · 16.11. 10:00 ist schon vorbei", reply("Anrufen heute 10:00"));
}

TEST(HomeMeshAppointment, ExplicitPastYear) {
  EXPECT_EQ("FEHLER · 01.03. 10:00 ist schon vorbei", reply("Test 01.03.2026 10:00"));
}

TEST(HomeMeshAppointment, OldDateWithoutYearMeansNextYear) {
  EXPECT_EQ("OK · Test · Mo 01.03.2027 10:00", reply("Test 01.03. 10:00"));
}

TEST(HomeMeshAppointment, DotTimeIsRejected) {
  EXPECT_EQ("FEHLER · Datum ungültig (z. B. 24.12., Uhrzeit mit :)", reply("Anrufen morgen 19.30"));
}

TEST(HomeMeshAppointment, InvalidTime) {
  EXPECT_EQ("FEHLER · Uhrzeit ungültig (z. B. 19:00)", reply("Anrufen morgen 25:00"));
}

TEST(HomeMeshAppointment, InvalidDate) {
  EXPECT_EQ("FEHLER · Datum ungültig (z. B. 24.12., Uhrzeit mit :)", reply("Test 31.02. 10:00"));
}

TEST(HomeMeshAppointment, FillerWordsDropped) {
  EXPECT_EQ("OK · Anrufen · Di 17.11. 19:00", reply("Anrufen morgen um 19 Uhr"));
  EXPECT_EQ("OK · Essen · So 22.11. 12:00", reply("Essen am Sonntag 12:00"));
}

TEST(HomeMeshAppointment, UhrGluedAndCapitalised) {
  EXPECT_EQ("OK · Funken · Di 17.11. 19:00", reply("Funken Morgen 19Uhr"));
  EXPECT_EQ("OK · Funktermin · täglich 07:30", reply("Täglich 7:30"));
}

TEST(HomeMeshAppointment, WordContainingWeekdayIsName) {
  EXPECT_EQ("OK · Sonntagsessen · So 22.11. 12:00", reply("Sonntagsessen So 12:00"));
}

TEST(HomeMeshAppointment, ConflictingDays) {
  EXPECT_EQ("FEHLER · Tag oder Uhrzeit doppelt angegeben", reply("Test morgen Fr 10:00"));
  EXPECT_EQ("FEHLER · Tag oder Uhrzeit doppelt angegeben", reply("Test 10:00 11:00"));
  EXPECT_EQ("FEHLER · Tag oder Uhrzeit doppelt angegeben", reply("Test täglich morgen 10:00"));
}

TEST(HomeMeshAppointment, LongNameIsCutOnCharacterBoundary) {
  Appointment a = parseAppointment("Großes Familientreffen im Gemeindehaus morgen 19:00", kToday, 14, 15);
  EXPECT_EQ(ApptStatus::Ok, a.status);
  EXPECT_LT(strlen(a.name), (size_t)APPT_NAME_MAX);
  EXPECT_EQ(strlen(a.name), mesh::validUtf8PrefixLength(a.name, strlen(a.name)));
  EXPECT_EQ(0, strncmp(a.name, "Großes Familientreffen", 23));
}
