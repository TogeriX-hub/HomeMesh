#pragma once

// Everything the HomeMesh screens show, independent of LVGL and MeshCore.
// The firmware fills it from MyMesh events, the PC simulator fills it with
// sample data. Pure logic, so it can be unit-tested natively.
//
// On the device the model lives in PSRAM (about 70 KB with the limits below).

#include <stdint.h>
#include <string.h>
#include <helpers/UTF8Helpers.h>
#include "Appointment.h"
#include "SignalMeter.h"

namespace homemesh {

static const int MAX_CONVERSATIONS = 6;
static const int MAX_CHANNELS = 6;
static const int MAX_LOG_MESSAGES = 30;
static const int MAX_FAVOURITES = 3;
static const int MAX_APPOINTMENTS = 4;
static const int MAX_REPEATERS = 6;
static const int MAX_WIFI_NETWORKS = 8;
static const int TEXT_LEN = 161;      // MeshCore text payload + terminator
static const int NAME_LEN = 32;

enum class Delivery : uint8_t { None, Pending, Delivered, Failed, Sent };

struct Message {
  uint32_t time;          // unix time, local clock
  bool outgoing;
  Delivery status;        // outgoing only
  char sender[NAME_LEN];  // channel messages: name before ": "
  char text[TEXT_LEN];
};

inline void copyText(char* dst, size_t cap, const char* src) {
  size_t n = mesh::validUtf8PrefixLength(src, cap - 1);
  memcpy(dst, src, n);
  dst[n] = 0;
}

// Fixed-size message history; the oldest message drops out when full.
struct MessageLog {
  Message msgs[MAX_LOG_MESSAGES];
  int count;
  int unread;    // incoming messages not yet read
  int seen;      // number of messages the user has looked at (channel divider)

  void clear() { count = unread = seen = 0; }

  Message& add(uint32_t time, bool outgoing, const char* text, const char* sender = "") {
    if (count == MAX_LOG_MESSAGES) {
      memmove(&msgs[0], &msgs[1], sizeof(Message) * (MAX_LOG_MESSAGES - 1));
      count--;
      if (seen > 0) seen--;
    }
    Message& m = msgs[count++];
    m.time = time;
    m.outgoing = outgoing;
    m.status = outgoing ? Delivery::Pending : Delivery::None;
    copyText(m.sender, sizeof(m.sender), sender);
    copyText(m.text, sizeof(m.text), text);
    if (!outgoing) unread++;
    if (unread > count) unread = count;
    return m;
  }

  const Message* last() const { return count ? &msgs[count - 1] : nullptr; }
  void markRead() { unread = 0; seen = count; }
};

struct Conversation {
  char name[NAME_LEN];   // node name from the contact list
  char alias[NAME_LEN];  // display-only name, empty = use node name
  uint8_t pubkey_prefix[6];
  bool favourite;
  MessageLog log;

  const char* displayName() const { return alias[0] ? alias : name; }
};

struct Channel {
  char name[NAME_LEN];   // without '#'
  MessageLog log;
};

struct HeardRepeater {
  char name[NAME_LEN];
  float snr;
  uint32_t heard_at_s;   // uptime seconds of the last packet
};

struct WifiNetwork {
  char ssid[33];
  int8_t rssi;
  bool secured;
};

enum class TimeSource : uint8_t { None, App, Wifi, Mesh };

struct Model {
  // clock
  bool time_valid;
  CivilDate date;
  int hour, minute;
  TimeSource time_source;
  uint32_t time_set_at_s;   // uptime seconds when the clock was last set

  // status
  uint8_t signal_level;  // 0 = no signal .. 4 = good, from SignalMeter
  bool night;           // quiet hours active (computed from the settings below)
  bool large_text;

  // settings (changed on the device or by remote command, saved by the firmware)
  bool quiet_enabled;
  int quiet_start_min, quiet_end_min;   // minutes of the day, e.g. 22:00 = 1320
  bool sound_on;                        // sound for messages from favourites
  uint8_t brightness_day, brightness_night;   // percent
  uint8_t advert_interval_h;            // zero-hop advert interval, 0 = off

  // home screen extras
  char warning[TEXT_LEN];   // empty = none
  char pin[TEXT_LEN];       // empty = none
  Appointment appts[MAX_APPOINTMENTS];
  int appt_count;

  Conversation convs[MAX_CONVERSATIONS];
  int conv_count;
  Channel channels[MAX_CHANNELS];
  int channel_count;

  // technical page
  HeardRepeater repeaters[MAX_REPEATERS];
  int repeater_count;
  bool heard_any;
  float last_snr;                // last received packet, for the placement helper
  char last_from[NAME_LEN];
  uint32_t last_packet_at_s;
  uint32_t uptime_s;
  uint32_t last_advert_at_s;
  char fw_version[16];
  char node_name[NAME_LEN];
  uint32_t ble_pin;
  char share_uri[200];           // contact link for the QR code (filled by the firmware)

  // Wi-Fi
  char wifi_ssid[33];            // empty = not configured
  bool wifi_connected;
  WifiNetwork networks[MAX_WIFI_NETWORKS];
  int network_count;
  bool update_running;           // OTA hotspot open
  uint32_t last_read_at;         // unix time a favourite's messages were last read on the device

  void clear() { memset(this, 0, sizeof(*this)); }

  // favourites in contact-list order, up to MAX_FAVOURITES
  int favourites(int out[MAX_FAVOURITES]) const {
    int n = 0;
    for (int i = 0; i < conv_count && n < MAX_FAVOURITES; i++) {
      if (convs[i].favourite) out[n++] = i;
    }
    return n;
  }

  int firstFavourite() const {
    for (int i = 0; i < conv_count; i++) if (convs[i].favourite) return i;
    return -1;
  }

  int unreadFrom(bool favourite) const {
    int sum = 0;
    for (int i = 0; i < conv_count; i++) {
      if (convs[i].favourite == favourite) sum += convs[i].log.unread;
    }
    return sum;
  }

  int minuteOfDay() const { return hour * 60 + minute; }
  uint32_t nowUnix() const { return (uint32_t)(nowMinutes() * 60); }
  int64_t nowMinutes() const { return daysFromCivil(date.year, date.month, date.day) * 1440 + minuteOfDay(); }

  // Quiet hours may span midnight (22:00-07:00).
  bool inQuietHours() const {
    if (!quiet_enabled || !time_valid) return false;
    int m = minuteOfDay();
    if (quiet_start_min <= quiet_end_min) return m >= quiet_start_min && m < quiet_end_min;
    return m >= quiet_start_min || m < quiet_end_min;
  }

  // ---- appointments ----

  bool addAppointment(const Appointment& a) {
    removeExpiredAppointments();
    if (appt_count >= MAX_APPOINTMENTS) return false;
    appts[appt_count] = a;
    appts[appt_count].reminded_start = -1;
    appt_count++;
    return true;
  }

  void removeAppointment(int i) {
    if (i < 0 || i >= appt_count) return;
    memmove(&appts[i], &appts[i + 1], sizeof(Appointment) * (appt_count - i - 1));
    appt_count--;
  }

  // One-time appointments stay visible until 30 minutes after their start.
  void removeExpiredAppointments() {
    int64_t now = nowMinutes();
    for (int i = appt_count - 1; i >= 0; i--) {
      if (!appts[i].daily && now > appointmentStart(appts[i], now) + APPT_SHOW_AFTER_MIN) removeAppointment(i);
    }
  }

  // The appointment that starts next (or started less than 30 minutes ago); -1 if none.
  int nextAppointment() const {
    int64_t now = nowMinutes();
    int best = -1;
    int64_t best_start = 0;
    for (int i = 0; i < appt_count; i++) {
      int64_t st = appointmentStart(appts[i], now);
      if (now > st + APPT_SHOW_AFTER_MIN) continue;
      if (best < 0 || st < best_start) { best = i; best_start = st; }
    }
    return best;
  }

  // Appointment whose reminder is due now (5 minutes before the start), marked as reminded.
  int takeDueReminder() {
    int64_t now = nowMinutes();
    for (int i = 0; i < appt_count; i++) {
      int64_t st = appointmentStart(appts[i], now);
      if (now >= st - APPT_REMIND_BEFORE_MIN && now < st + APPT_SHOW_AFTER_MIN && appts[i].reminded_start != st) {
        appts[i].reminded_start = st;
        return i;
      }
    }
    return -1;
  }
};

}  // namespace homemesh
