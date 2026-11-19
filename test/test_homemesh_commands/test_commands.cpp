#include <gtest/gtest.h>

#include "../../examples/companion_radio/ui-homemesh/RemoteCommands.h"

using namespace homemesh;

namespace {

Model& freshModel() {
  static Model m;
  m.clear();
  m.time_valid = true;
  m.date = CivilDate{ 2026, 11, 16 };   // Monday
  m.hour = 14;
  m.minute = 15;
  m.quiet_enabled = true;
  m.quiet_start_min = 22 * 60;
  m.quiet_end_min = 7 * 60;
  strcpy(m.fw_version, "0.1.0");
  return m;
}

std::string run(Model& m, const char* text, CommandResult* out = nullptr) {
  CommandResult r;
  handleRemoteCommand(m, text, r);
  if (out) *out = r;
  return r.reply;
}

}  // namespace

TEST(HomeMeshCommands, Recognises) {
  EXPECT_TRUE(isRemoteCommand("!status"));
  EXPECT_FALSE(isRemoteCommand("Hallo !status"));
}

TEST(HomeMeshCommands, PinAndUnpin) {
  Model& m = freshModel();
  EXPECT_EQ("OK · angeheftet", run(m, "!pin Sonntag 12 Uhr Essen"));
  EXPECT_STREQ("Sonntag 12 Uhr Essen", m.pin);
  EXPECT_EQ("OK · entfernt", run(m, "!unpin"));
  EXPECT_STREQ("", m.pin);
  EXPECT_EQ("FEHLER · Text fehlt (!pin <Text>)", run(m, "!pin"));
}

TEST(HomeMeshCommands, AppointmentsAddListDelete) {
  Model& m = freshModel();
  EXPECT_EQ("OK · Anrufen · Di 17.11. 19:00", run(m, "!termin Anrufen morgen 19:00"));
  EXPECT_EQ("OK · Funkrunde · täglich 19:00", run(m, "!termin Funkrunde täglich 19:00"));
  EXPECT_EQ(2, m.appt_count);
  EXPECT_EQ("OK · 1 Anrufen Di 17.11. 19:00 · 2 Funkrunde täglich 19:00", run(m, "!termine"));
  EXPECT_EQ("OK · Termin 1 gelöscht", run(m, "!termin löschen 1"));
  EXPECT_EQ(1, m.appt_count);
  EXPECT_EQ("FEHLER · Nummer aus !termine angeben", run(m, "!termin löschen 5"));
  EXPECT_EQ("OK · alle Termine gelöscht", run(m, "!termin löschen alle"));
  EXPECT_EQ("OK · keine Termine", run(m, "!termine"));
}

TEST(HomeMeshCommands, AppointmentErrorsAreNotStored) {
  Model& m = freshModel();
  EXPECT_EQ("FEHLER · Uhrzeit fehlt (z. B. 19:00)", run(m, "!termin Anrufen morgen"));
  EXPECT_EQ(0, m.appt_count);
}

TEST(HomeMeshCommands, AppointmentLimit) {
  Model& m = freshModel();
  for (int i = 0; i < MAX_APPOINTMENTS; i++) run(m, "!termin Test morgen 10:00");
  EXPECT_EQ("FEHLER · schon 4 Termine, erst einen löschen", run(m, "!termin Noch einer morgen 11:00"));
}

TEST(HomeMeshCommands, QuietHours) {
  Model& m = freshModel();
  CommandResult r;
  EXPECT_EQ("OK · Nachtruhe 22:30-6:45", run(m, "!nachtruhe 22:30-6:45", &r));
  EXPECT_TRUE(r.settings_changed);
  EXPECT_EQ(22 * 60 + 30, m.quiet_start_min);
  EXPECT_EQ("OK · Nachtruhe aus", run(m, "!nachtruhe aus"));
  EXPECT_FALSE(m.quiet_enabled);
  EXPECT_EQ("OK · Nachtruhe 22:30-6:45", run(m, "!nachtruhe an"));
  EXPECT_EQ("FEHLER · z. B. !nachtruhe 22-7 oder !nachtruhe aus", run(m, "!nachtruhe 25-7"));
  EXPECT_EQ("FEHLER · z. B. !nachtruhe 22-7 oder !nachtruhe aus", run(m, "!nachtruhe abends"));
}

TEST(HomeMeshCommands, QuietHoursAcrossMidnight) {
  Model& m = freshModel();
  m.hour = 23; m.minute = 0;
  EXPECT_TRUE(m.inQuietHours());
  m.hour = 6; m.minute = 59;
  EXPECT_TRUE(m.inQuietHours());
  m.hour = 7; m.minute = 0;
  EXPECT_FALSE(m.inQuietHours());
}

TEST(HomeMeshCommands, SoundAndFlags) {
  Model& m = freshModel();
  CommandResult r;
  EXPECT_EQ("OK · Ton aus", run(m, "!ton aus"));
  EXPECT_FALSE(m.sound_on);
  run(m, "!klingeln", &r);   EXPECT_TRUE(r.ring);
  run(m, "!advert", &r);     EXPECT_TRUE(r.advert);
  run(m, "!update", &r);     EXPECT_TRUE(r.update);
  run(m, "!neustart", &r);   EXPECT_TRUE(r.reboot);
  EXPECT_EQ(0u, run(m, "!foo").find("Unbekannt"));
}

TEST(HomeMeshCommands, StatusFitsOneMessage) {
  Model& m = freshModel();
  m.uptime_s = 3 * 86400 + 4 * 3600;
  m.heard_any = true;
  m.signal_level = 4;
  m.last_snr = 6.25f;
  m.last_packet_at_s = m.uptime_s - 120;
  m.repeater_count = 3;
  m.time_source = TimeSource::App;
  m.last_read_at = 18 * 3600 + 50 * 60;
  std::string s = run(m, "!status");
  EXPECT_EQ("OK · läuft 3T 4h · Funknetz gut (6.2 dB, vor 2m) · 3 Rptr · Zeit App · Nachtruhe 22-7 · gelesen 18:50 · 0.1.0", s);
  EXPECT_LT(s.size(), (size_t)TEXT_LEN);
}

TEST(HomeMeshSchedule, NextAppointmentAndReminder) {
  Model& m = freshModel();
  run(m, "!termin Funkrunde täglich 19:00");
  run(m, "!termin Anrufen heute 15:00");
  EXPECT_EQ(1, m.nextAppointment());            // 15:00 today comes first
  EXPECT_EQ(-1, m.takeDueReminder());
  m.hour = 14; m.minute = 55;
  EXPECT_EQ(1, m.takeDueReminder());            // 5 minutes before
  EXPECT_EQ(-1, m.takeDueReminder());           // only once
  m.hour = 15; m.minute = 31;
  m.removeExpiredAppointments();
  EXPECT_EQ(1, m.appt_count);                   // one-time appointment gone
  m.hour = 18; m.minute = 55;
  EXPECT_EQ(0, m.takeDueReminder());            // daily one
  m.date = CivilDate{ 2026, 11, 17 };
  EXPECT_EQ(0, m.takeDueReminder());            // again the next day
}

TEST(HomeMeshCommands, AppointmentsNeedTheClock) {
  Model& m = freshModel();
  m.time_valid = false;
  EXPECT_EQ("FEHLER · Uhrzeit am Gerät nicht gestellt", run(m, "!termin Arzt morgen 10:00"));
  EXPECT_EQ(0, m.appt_count);
}
