#pragma once

// Saves the parts of the model that must survive a power cut: settings,
// the Wi-Fi network, display names, appointments, the pinned note, the
// warning line and the message history.
//
// The format is a small versioned binary record with a CRC32 at the end.
// The firmware writes it to a temporary file and renames it over the old
// one, so a power cut while saving leaves the previous state intact.
// Pure logic, no file system, so it can be unit-tested natively.
//
// Conversations are stored with the contact's public key prefix and matched
// to the contact list again after loading; channels are matched by name.

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "Model.h"

namespace homemesh {

static const uint8_t PERSIST_MAGIC[3] = { 'H', 'M', 'S' };
static const uint8_t PERSIST_VERSION = 1;

namespace detail {
  // per message: time, flags, sender, text (strings with a length byte)
  constexpr size_t MSG_MAX = 4 + 1 + (1 + NAME_LEN) + (1 + TEXT_LEN);
  constexpr size_t LOG_MAX = 3 + MAX_LOG_MESSAGES * MSG_MAX;
  constexpr size_t APPT_MAX = 1 + 2 + 1 + 1 + 1 + 1 + 1 + 1 + (1 + APPT_NAME_MAX) + 8;
  constexpr size_t SETTINGS_MAX = 1 + 2 + 2 + 1 + 1 + 1;
}

// Upper bound of a saved record, for the save buffer.
constexpr size_t PERSIST_MAX_BYTES =
    4 + detail::SETTINGS_MAX + 4 + (1 + 33) + (1 + 64) +
    2 * (1 + TEXT_LEN) + 4 +
    1 + MAX_APPOINTMENTS * detail::APPT_MAX +
    1 + MAX_CONVERSATIONS * (6 + 2 * (1 + NAME_LEN) + 1 + detail::LOG_MAX) +
    1 + MAX_CHANNELS * ((1 + NAME_LEN) + detail::LOG_MAX) +
    4;

inline uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc = 0) {
  crc = ~crc;
  while (n--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1)));
  }
  return ~crc;
}

namespace detail {

struct Writer {
  uint8_t* buf;
  size_t cap, len;
  bool ok;

  void bytes(const void* p, size_t n) {
    if (!ok || len + n > cap) { ok = false; return; }
    memcpy(buf + len, p, n);
    len += n;
  }
  void u8(uint8_t v) { bytes(&v, 1); }
  void u16(uint16_t v) { uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; bytes(b, 2); }
  void u32(uint32_t v) { uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; bytes(b, 4); }
  void i64(int64_t v) { u32((uint32_t)(uint64_t)v); u32((uint32_t)((uint64_t)v >> 32)); }
  void str(const char* s, size_t field) {
    size_t n = strnlen(s, field - 1);
    if (n > 255) n = 255;
    u8((uint8_t)n);
    bytes(s, n);
  }
};

struct Reader {
  const uint8_t* buf;
  size_t len, pos;
  bool ok;

  void bytes(void* p, size_t n) {
    if (!ok || pos + n > len) { ok = false; memset(p, 0, n); return; }
    memcpy(p, buf + pos, n);
    pos += n;
  }
  uint8_t u8() { uint8_t v; bytes(&v, 1); return v; }
  uint16_t u16() { uint8_t b[2]; bytes(b, 2); return (uint16_t)(b[0] | (b[1] << 8)); }
  uint32_t u32() { uint8_t b[4]; bytes(b, 4); return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24); }
  int64_t i64() { uint64_t lo = u32(); uint64_t hi = u32(); return (int64_t)(lo | (hi << 32)); }
  void str(char* out, size_t field) {
    size_t n = u8();
    if (n >= field) { ok = false; out[0] = 0; return; }
    bytes(out, n);
    out[n] = 0;
    out[mesh::validUtf8PrefixLength(out, n)] = 0;   // drop a broken UTF-8 tail
  }
  // count that must not exceed max
  int count(int max) {
    int n = u8();
    if (n > max) ok = false;
    return ok ? n : 0;
  }
};

inline void writeLog(Writer& w, const MessageLog& log) {
  w.u8((uint8_t)log.count);
  w.u8((uint8_t)log.unread);
  w.u8((uint8_t)log.seen);
  for (int i = 0; i < log.count; i++) {
    const Message& m = log.msgs[i];
    w.u32(m.time);
    w.u8((uint8_t)((m.outgoing ? 1 : 0) | ((uint8_t)m.status << 1)));
    w.str(m.sender, sizeof(m.sender));
    w.str(m.text, sizeof(m.text));
  }
}

inline void readLog(Reader& r, MessageLog& log) {
  log.clear();
  int n = r.count(MAX_LOG_MESSAGES);
  int unread = r.u8();
  int seen = r.u8();
  for (int i = 0; i < n && r.ok; i++) {
    Message& m = log.msgs[i];
    m.time = r.u32();
    uint8_t f = r.u8();
    m.outgoing = f & 1;
    uint8_t st = (uint8_t)(f >> 1);
    m.status = st <= (uint8_t)Delivery::Sent ? (Delivery)st : Delivery::None;
    // An ACK can no longer arrive after a restart: "on its way" becomes "sent".
    if (m.status == Delivery::Pending) m.status = Delivery::Sent;
    r.str(m.sender, sizeof(m.sender));
    r.str(m.text, sizeof(m.text));
  }
  log.count = r.ok ? n : 0;
  log.unread = unread > log.count ? log.count : unread;
  log.seen = seen > log.count ? log.count : seen;
}

}  // namespace detail

// The CRC stored at the end of a record identifies its content. (A CRC taken
// over the whole record including that trailer is the same constant for
// every record - the CRC-32 residue - and says nothing.)
inline uint32_t recordChecksum(const uint8_t* buf, size_t len) {
  if (len < 4) return 0;
  return (uint32_t)buf[len - 4] | ((uint32_t)buf[len - 3] << 8) | ((uint32_t)buf[len - 2] << 16) | ((uint32_t)buf[len - 1] << 24);
}

// Returns the record size, or 0 if buf is too small.
inline size_t serializeState(const Model& m, uint8_t* buf, size_t cap) {
  detail::Writer w{ buf, cap, 0, true };
  w.bytes(PERSIST_MAGIC, 3);
  w.u8(PERSIST_VERSION);

  // settings
  w.u8((uint8_t)((m.large_text ? 1 : 0) | (m.quiet_enabled ? 2 : 0) | (m.sound_on ? 4 : 0)));
  w.u16((uint16_t)m.quiet_start_min);
  w.u16((uint16_t)m.quiet_end_min);
  w.u8(m.brightness_day);
  w.u8(m.brightness_night);
  w.u8(m.advert_interval_h);
  w.u32(m.last_read_at);
  w.str(m.wifi_ssid, sizeof(m.wifi_ssid));
  w.str(m.wifi_pwd, sizeof(m.wifi_pwd));

  // home screen
  w.str(m.pin, sizeof(m.pin));
  w.str(m.warning, sizeof(m.warning));
  w.u32(m.warning_at);

  w.u8((uint8_t)m.appt_count);
  for (int i = 0; i < m.appt_count; i++) {
    const Appointment& a = m.appts[i];
    w.u8(a.daily ? 1 : 0);
    w.u16((uint16_t)a.date.year);
    w.u8((uint8_t)a.date.month);
    w.u8((uint8_t)a.date.day);
    w.u8((uint8_t)a.hour);
    w.u8((uint8_t)a.minute);
    w.u8((uint8_t)(int8_t)a.weekday_given);
    w.u8((uint8_t)(int8_t)a.weekday_actual);
    w.str(a.name, sizeof(a.name));
    w.i64(a.reminded_start);
  }

  w.u8((uint8_t)m.conv_count);
  for (int i = 0; i < m.conv_count; i++) {
    const Conversation& c = m.convs[i];
    w.bytes(c.pubkey_prefix, 6);
    w.str(c.name, sizeof(c.name));
    w.str(c.alias, sizeof(c.alias));
    w.u8(c.favourite ? 1 : 0);
    detail::writeLog(w, c.log);
  }

  w.u8((uint8_t)m.channel_count);
  for (int i = 0; i < m.channel_count; i++) {
    w.str(m.channels[i].name, sizeof(m.channels[i].name));
    detail::writeLog(w, m.channels[i].log);
  }

  if (!w.ok) return 0;
  w.u32(crc32(buf, w.len));
  return w.ok ? w.len : 0;
}

// Loads a record into a freshly cleared model. On any error the model gets
// the default settings and an empty history, and false is returned.
inline bool deserializeState(Model& m, const uint8_t* buf, size_t len) {
  auto fail = [&m]() {
    m.pin[0] = m.warning[0] = 0;
    m.warning_at = 0;
    m.appt_count = m.conv_count = m.channel_count = 0;
    m.last_read_at = 0;
    m.wifi_ssid[0] = m.wifi_pwd[0] = 0;
    m.applyDefaults();
    return false;
  };
  if (len < 8 || memcmp(buf, PERSIST_MAGIC, 3) != 0 || buf[3] != PERSIST_VERSION) return fail();
  uint32_t stored = (uint32_t)buf[len - 4] | ((uint32_t)buf[len - 3] << 8) | ((uint32_t)buf[len - 2] << 16) | ((uint32_t)buf[len - 1] << 24);
  if (crc32(buf, len - 4) != stored) return fail();

  detail::Reader r{ buf, len - 4, 4, true };

  uint8_t flags = r.u8();
  m.large_text = flags & 1;
  m.quiet_enabled = flags & 2;
  m.sound_on = flags & 4;
  m.quiet_start_min = r.u16() % 1440;
  m.quiet_end_min = r.u16() % 1440;
  m.brightness_day = r.u8();
  m.brightness_night = r.u8();
  m.advert_interval_h = r.u8();
  m.last_read_at = r.u32();
  r.str(m.wifi_ssid, sizeof(m.wifi_ssid));
  r.str(m.wifi_pwd, sizeof(m.wifi_pwd));

  r.str(m.pin, sizeof(m.pin));
  r.str(m.warning, sizeof(m.warning));
  m.warning_at = r.u32();

  m.appt_count = r.count(MAX_APPOINTMENTS);
  for (int i = 0; i < m.appt_count && r.ok; i++) {
    Appointment& a = m.appts[i];
    memset(&a, 0, sizeof(a));
    a.status = ApptStatus::Ok;
    a.daily = r.u8() != 0;
    a.date.year = r.u16();
    a.date.month = r.u8();
    a.date.day = r.u8();
    a.hour = r.u8() % 24;
    a.minute = r.u8() % 60;
    a.weekday_given = (int8_t)r.u8();
    a.weekday_actual = (int8_t)r.u8();
    r.str(a.name, sizeof(a.name));
    a.reminded_start = r.i64();
  }

  m.conv_count = r.count(MAX_CONVERSATIONS);
  for (int i = 0; i < m.conv_count && r.ok; i++) {
    Conversation& c = m.convs[i];
    memset(&c, 0, sizeof(c));
    r.bytes(c.pubkey_prefix, 6);
    r.str(c.name, sizeof(c.name));
    r.str(c.alias, sizeof(c.alias));
    c.favourite = r.u8() != 0;
    detail::readLog(r, c.log);
  }

  m.channel_count = r.count(MAX_CHANNELS);
  for (int i = 0; i < m.channel_count && r.ok; i++) {
    memset(&m.channels[i], 0, sizeof(Channel));
    r.str(m.channels[i].name, sizeof(m.channels[i].name));
    detail::readLog(r, m.channels[i].log);
  }

  if (!r.ok || r.pos != r.len) return fail();
  return true;
}

}  // namespace homemesh
