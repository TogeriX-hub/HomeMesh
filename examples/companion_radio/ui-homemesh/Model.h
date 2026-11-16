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

struct Model {
  // clock
  bool time_valid;
  CivilDate date;
  int hour, minute;

  // status
  uint8_t signal_level;  // 0 = no signal .. 4 = good, from SignalMeter
  bool night;           // quiet hours active
  bool large_text;

  // home screen extras
  char warning[TEXT_LEN];   // empty = none
  char pin[TEXT_LEN];       // empty = none
  bool has_appointment;
  Appointment appointment;

  Conversation convs[MAX_CONVERSATIONS];
  int conv_count;
  Channel channels[MAX_CHANNELS];
  int channel_count;

  void clear() { memset(this, 0, sizeof(*this)); }

  // favourites in contact-list order, up to MAX_FAVOURITES
  int favourites(int out[MAX_FAVOURITES]) const {
    int n = 0;
    for (int i = 0; i < conv_count && n < MAX_FAVOURITES; i++) {
      if (convs[i].favourite) out[n++] = i;
    }
    return n;
  }

  int unreadFrom(bool favourite) const {
    int sum = 0;
    for (int i = 0; i < conv_count; i++) {
      if (convs[i].favourite == favourite) sum += convs[i].log.unread;
    }
    return sum;
  }
};

}  // namespace homemesh
