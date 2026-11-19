#include <gtest/gtest.h>

#include <memory>
#include <vector>
#include "../../examples/companion_radio/ui-homemesh/Persist.h"

using namespace homemesh;

namespace {

std::unique_ptr<Model> freshModel() {
  std::unique_ptr<Model> m(new Model());
  m->clear();
  return m;
}

const uint8_t KEY_A[6] = { 1, 2, 3, 4, 5, 6 };
const uint8_t KEY_B[6] = { 9, 8, 7, 6, 5, 4 };

void fill(Model& m) {
  m.applyDefaults();
  m.large_text = true;
  m.quiet_start_min = 22 * 60 + 30;
  m.quiet_end_min = 6 * 60 + 45;
  m.sound_on = false;
  m.brightness_day = 70;
  m.brightness_night = 10;
  m.advert_interval_h = 6;
  m.last_read_at = 123456789;
  strcpy(m.pin, "Sonntag 12 Uhr Essen");
  strcpy(m.warning, "DWD · Sturmböen");
  m.warning_at = 987654;
  strcpy(m.wifi_ssid, "Heimnetz");
  strcpy(m.wifi_pwd, "geheim 123");

  Appointment a = {};
  a.status = ApptStatus::Ok;
  a.date = { 2026, 11, 24 };
  a.hour = 19;
  a.minute = 5;
  a.weekday_given = 1;
  a.weekday_actual = 1;
  strcpy(a.name, "Anrufen");
  m.appts[0] = a;
  m.appts[0].reminded_start = 29400000LL;
  a.daily = true;
  strcpy(a.name, "Tabletten");
  m.appts[1] = a;
  m.appts[1].reminded_start = -1;
  m.appt_count = 2;

  int i = m.ensureConversation(KEY_A, "Node A", true);
  strcpy(m.convs[i].alias, "Kurz");
  m.convs[i].log.add(100, false, "Hallo äöü");
  m.convs[i].log.add(200, true, "Antwort").status = Delivery::Delivered;
  m.convs[i].log.add(300, true, "Noch unterwegs");
  int j = m.ensureConversation(KEY_B, "Node B", false);
  m.convs[j].log.add(400, false, "Servus");

  Channel& c = m.channels[m.channel_count++];
  strcpy(c.name, "boeblingen");
  c.log.add(500, false, "Funkabend?", "Mike");
  c.log.markRead();
  c.log.add(600, false, "Bin dabei", "Sabine");
}

std::vector<uint8_t> save(const Model& m) {
  std::vector<uint8_t> buf(PERSIST_MAX_BYTES);
  size_t n = serializeState(m, buf.data(), buf.size());
  buf.resize(n);
  return buf;
}

}  // namespace

TEST(HomeMeshPersist, RoundTrip) {
  auto a = freshModel();
  fill(*a);
  auto buf = save(*a);
  ASSERT_GT(buf.size(), 0u);

  auto b = freshModel();
  ASSERT_TRUE(deserializeState(*b, buf.data(), buf.size()));
  EXPECT_TRUE(b->large_text);
  EXPECT_TRUE(b->quiet_enabled);
  EXPECT_FALSE(b->sound_on);
  EXPECT_EQ(22 * 60 + 30, b->quiet_start_min);
  EXPECT_EQ(6 * 60 + 45, b->quiet_end_min);
  EXPECT_EQ(70, b->brightness_day);
  EXPECT_EQ(10, b->brightness_night);
  EXPECT_EQ(6, b->advert_interval_h);
  EXPECT_EQ(123456789u, b->last_read_at);
  EXPECT_STREQ("Sonntag 12 Uhr Essen", b->pin);
  EXPECT_STREQ("DWD · Sturmböen", b->warning);
  EXPECT_EQ(987654u, b->warning_at);
  EXPECT_STREQ("Heimnetz", b->wifi_ssid);
  EXPECT_STREQ("geheim 123", b->wifi_pwd);

  ASSERT_EQ(2, b->appt_count);
  EXPECT_STREQ("Anrufen", b->appts[0].name);
  EXPECT_FALSE(b->appts[0].daily);
  EXPECT_EQ(2026, b->appts[0].date.year);
  EXPECT_EQ(24, b->appts[0].date.day);
  EXPECT_EQ(19, b->appts[0].hour);
  EXPECT_EQ(5, b->appts[0].minute);
  EXPECT_EQ(29400000LL, b->appts[0].reminded_start);
  EXPECT_TRUE(b->appts[1].daily);
  EXPECT_EQ(-1, b->appts[1].reminded_start);

  ASSERT_EQ(2, b->conv_count);
  EXPECT_EQ(0, memcmp(KEY_A, b->convs[0].pubkey_prefix, 6));
  EXPECT_STREQ("Node A", b->convs[0].name);
  EXPECT_STREQ("Kurz", b->convs[0].alias);
  EXPECT_TRUE(b->convs[0].favourite);
  ASSERT_EQ(3, b->convs[0].log.count);
  EXPECT_STREQ("Hallo äöü", b->convs[0].log.msgs[0].text);
  EXPECT_EQ(1, b->convs[0].log.unread);
  EXPECT_EQ(Delivery::Delivered, b->convs[0].log.msgs[1].status);
  EXPECT_FALSE(b->convs[1].favourite);

  ASSERT_EQ(1, b->channel_count);
  EXPECT_STREQ("boeblingen", b->channels[0].name);
  EXPECT_EQ(2, b->channels[0].log.count);
  EXPECT_EQ(1, b->channels[0].log.seen);
  EXPECT_STREQ("Sabine", b->channels[0].log.msgs[1].sender);
}

TEST(HomeMeshPersist, PendingBecomesSentAfterRestart) {
  auto a = freshModel();
  fill(*a);
  auto buf = save(*a);
  auto b = freshModel();
  ASSERT_TRUE(deserializeState(*b, buf.data(), buf.size()));
  EXPECT_EQ(Delivery::Sent, b->convs[0].log.msgs[2].status);
}

TEST(HomeMeshPersist, CorruptionFallsBackToDefaults) {
  auto a = freshModel();
  fill(*a);
  auto buf = save(*a);
  buf[buf.size() / 2] ^= 0x40;
  auto b = freshModel();
  EXPECT_FALSE(deserializeState(*b, buf.data(), buf.size()));
  EXPECT_EQ(0, b->conv_count);
  EXPECT_EQ(0, b->appt_count);
  EXPECT_EQ(0, b->pin[0]);
  EXPECT_EQ(22 * 60, b->quiet_start_min);
  EXPECT_EQ(4, b->advert_interval_h);
}

TEST(HomeMeshPersist, TruncatedOrForeignDataIsRejected) {
  auto a = freshModel();
  fill(*a);
  auto buf = save(*a);
  auto b = freshModel();
  EXPECT_FALSE(deserializeState(*b, buf.data(), buf.size() - 10));
  EXPECT_FALSE(deserializeState(*b, buf.data(), 3));
  std::vector<uint8_t> other(buf);
  other[3] = PERSIST_VERSION + 1;
  EXPECT_FALSE(deserializeState(*b, other.data(), other.size()));
}

TEST(HomeMeshPersist, FullModelFitsTheBuffer) {
  auto a = freshModel();
  a->applyDefaults();
  std::string longtext(TEXT_LEN - 1, 'x');
  std::string longname(NAME_LEN - 1, 'n');
  for (int i = 0; i < MAX_CONVERSATIONS; i++) {
    uint8_t key[6] = { (uint8_t)i, 0, 0, 0, 0, 1 };
    int k = a->ensureConversation(key, longname.c_str(), false);
    strcpy(a->convs[k].alias, longname.c_str());
    for (int j = 0; j < MAX_LOG_MESSAGES; j++) a->convs[k].log.add(j, false, longtext.c_str(), longname.c_str());
  }
  for (int i = 0; i < MAX_CHANNELS; i++) {
    Channel& c = a->channels[a->channel_count++];
    strcpy(c.name, longname.c_str());
    for (int j = 0; j < MAX_LOG_MESSAGES; j++) c.log.add(j, false, longtext.c_str(), longname.c_str());
  }
  strcpy(a->pin, longtext.c_str());
  strcpy(a->warning, longtext.c_str());
  strcpy(a->wifi_ssid, std::string(32, 's').c_str());
  strcpy(a->wifi_pwd, std::string(63, 'p').c_str());
  for (int i = 0; i < MAX_APPOINTMENTS; i++) strcpy(a->appts[a->appt_count++].name, std::string(APPT_NAME_MAX - 1, 'a').c_str());

  std::vector<uint8_t> buf(PERSIST_MAX_BYTES);
  size_t n = serializeState(*a, buf.data(), buf.size());
  EXPECT_GT(n, 0u);
  EXPECT_LE(n, PERSIST_MAX_BYTES);
  auto b = freshModel();
  EXPECT_TRUE(deserializeState(*b, buf.data(), n));
  EXPECT_EQ(MAX_LOG_MESSAGES, b->channels[MAX_CHANNELS - 1].log.count);
  // a buffer that is too small fails cleanly
  EXPECT_EQ(0u, serializeState(*a, buf.data(), n - 1));
}

TEST(HomeMeshModel, ConversationsMakeRoomForNewContacts) {
  auto m = freshModel();
  uint8_t fav[6] = { 0xF0, 0, 0, 0, 0, 0 };
  m->ensureConversation(fav, "Fav", true);
  for (int i = 1; i < MAX_CONVERSATIONS; i++) {
    uint8_t key[6] = { (uint8_t)i, 0, 0, 0, 0, 0 };
    int k = m->ensureConversation(key, "Other", false);
    m->convs[k].log.add(1000 + i, false, "x");
  }
  ASSERT_EQ(MAX_CONVERSATIONS, m->conv_count);
  uint8_t fresh[6] = { 0xAA, 0, 0, 0, 0, 0 };
  int k = m->ensureConversation(fresh, "New", false);
  ASSERT_GE(k, 0);
  EXPECT_EQ(MAX_CONVERSATIONS, m->conv_count);
  uint8_t gone[6] = { 1, 0, 0, 0, 0, 0 };   // quiet the longest
  EXPECT_EQ(-1, m->findConversation(gone));
  EXPECT_GE(m->findConversation(fav), 0);
  // same contact again finds the existing entry and updates the flags
  EXPECT_EQ(k, m->ensureConversation(fresh, "Renamed", true));
  EXPECT_STREQ("Renamed", m->convs[k].name);
  EXPECT_TRUE(m->convs[k].favourite);
}

TEST(HomeMeshModel, WarningExpiresAfterSixHours) {
  auto m = freshModel();
  m->time_valid = true;
  m->date = { 2026, 11, 17 };
  m->hour = 10; m->minute = 0;
  m->setWarning("DWD · Sturm");
  m->hour = 15; m->minute = 59;
  m->expireWarning();
  EXPECT_STREQ("DWD · Sturm", m->warning);
  m->hour = 16; m->minute = 0;
  m->expireWarning();
  EXPECT_EQ(0, m->warning[0]);
}

TEST(HomeMeshModel, RepeaterListKeepsRecentOnes) {
  auto m = freshModel();
  char name[16];
  for (int i = 0; i < MAX_REPEATERS; i++) {
    snprintf(name, sizeof(name), "R%d", i);
    m->noteRepeater(name, (float)i, 100 + i);
  }
  m->noteRepeater("R3", 9.5f, 500);           // known: updated in place
  EXPECT_EQ(MAX_REPEATERS, m->repeater_count);
  EXPECT_FLOAT_EQ(9.5f, m->repeaters[3].snr);
  m->noteRepeater("New", -3.0f, 600);         // replaces R0 (heard longest ago)
  EXPECT_EQ(MAX_REPEATERS, m->repeater_count);
  EXPECT_STREQ("New", m->repeaters[0].name);
  EXPECT_EQ(600u, m->repeaters[0].heard_at_s);
}

TEST(HomeMeshModel, SoundRules) {
  auto m = freshModel();
  m->applyDefaults();   // sound on, quiet hours 22-7
  m->time_valid = true;
  m->date = { 2026, 11, 17 };
  m->hour = 12; m->minute = 0;
  EXPECT_TRUE(m->soundAllowed(Sound::Message));
  m->hour = 23;
  EXPECT_FALSE(m->soundAllowed(Sound::Message));
  EXPECT_TRUE(m->soundAllowed(Sound::Reminder));
  EXPECT_TRUE(m->soundAllowed(Sound::Ring));
  m->sound_on = false;
  EXPECT_FALSE(m->soundAllowed(Sound::Reminder));
  EXPECT_FALSE(m->soundAllowed(Sound::Ring));
}

TEST(HomeMeshModel, NoClockNoRemindersAndLateWarningStamp) {
  auto m = freshModel();
  Appointment a = {};
  a.status = ApptStatus::Ok;
  a.daily = true;
  a.hour = 0; a.minute = 3;
  strcpy(a.name, "Tabletten");
  m->appts[m->appt_count++] = a;
  EXPECT_EQ(-1, m->takeDueReminder());   // clock unknown: date {0,0,0}

  m->setWarning("DWD · Sturm");
  EXPECT_EQ(0u, m->warning_at);
  m->expireWarning();                    // still no clock: kept, not stamped
  EXPECT_STREQ("DWD · Sturm", m->warning);
  m->time_valid = true;
  m->date = { 2026, 11, 19 };
  m->hour = 14; m->minute = 0;
  m->expireWarning();                    // clock known: stamped now
  EXPECT_EQ(m->nowUnix(), m->warning_at);
  m->hour = 19; m->minute = 59;
  m->expireWarning();
  EXPECT_STREQ("DWD · Sturm", m->warning);
  m->hour = 20; m->minute = 0;
  m->expireWarning();
  EXPECT_EQ(0, m->warning[0]);
}
