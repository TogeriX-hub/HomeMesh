#pragma once

// Remote maintenance: direct messages from a favourite that start with '!'
// are commands, not chat. They change the model and produce a short reply.
// Actions that need the hardware (advert, restart, ring, Wi-Fi update) are
// returned as flags for the firmware to carry out.
//
// Commands are German because the device interface is German:
//   !status                      radio, repeaters, time source, quiet hours, last read
//   !pin <text> / !unpin         pinned note on the home screen
//   !termin <name> <day> <time>  appointment (see Appointment.h)
//   !termine                     list appointments
//   !termin löschen <n>|alle     remove appointments
//   !nachtruhe 22-7 | 22:30-6:45 | an | aus
//   !ton an|aus                  sound for messages from favourites
//   !klingeln                    ring until someone taps the screen
//   !advert                      send a zero-hop advert now
//   !update                      open the Wi-Fi update hotspot
//   !neustart                    restart
//
// Pure logic, unit-tested natively.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Model.h"

namespace homemesh {

struct CommandResult {
  char reply[TEXT_LEN];
  bool advert, reboot, ring, update, settings_changed;
};

namespace detail {

inline bool startsWithWord(const char* s, const char* word, const char** rest) {
  size_t n = strlen(word);
  if (strncmp(s, word, n) != 0) return false;
  if (s[n] != 0 && s[n] != ' ') return false;
  const char* r = s + n;
  while (*r == ' ') r++;
  *rest = r;
  return true;
}

// "22", "22:30", "6:45" -> minutes of the day
inline bool parseClockMinutes(const char* s, size_t len, int& out) {
  int h = 0, m = 0, i = 0, digits = 0;
  while (i < (int)len && s[i] >= '0' && s[i] <= '9') { h = h * 10 + (s[i++] - '0'); digits++; }
  if (digits == 0 || digits > 2) return false;
  if (i < (int)len) {
    if (s[i] != ':' || len - i != 3) return false;
    if (s[i + 1] < '0' || s[i + 1] > '5' || s[i + 2] < '0' || s[i + 2] > '9') return false;
    m = (s[i + 1] - '0') * 10 + (s[i + 2] - '0');
  }
  if (h > 23) return false;
  out = h * 60 + m;
  return true;
}

inline void fmtClockMinutes(int min, char* out, size_t cap) {
  if (min % 60) snprintf(out, cap, "%d:%02d", min / 60, min % 60);
  else snprintf(out, cap, "%d", min / 60);
}

inline void fmtDuration(uint32_t s, char* out, size_t cap) {
  if (s >= 86400) snprintf(out, cap, "%uT %uh", (unsigned)(s / 86400), (unsigned)(s % 86400 / 3600));
  else if (s >= 3600) snprintf(out, cap, "%uh %um", (unsigned)(s / 3600), (unsigned)(s % 3600 / 60));
  else snprintf(out, cap, "%um", (unsigned)(s / 60));
}

}  // namespace detail

inline bool isRemoteCommand(const char* text) { return text && text[0] == '!'; }

inline void handleRemoteCommand(Model& m, const char* text, CommandResult& r) {
  using namespace detail;
  memset(&r, 0, sizeof(r));
  const char* cmd = text + 1;
  const char* arg;

  if (startsWithWord(cmd, "status", &arg)) {
    char up[16], quiet[24], read[24], rx[40];
    fmtDuration(m.uptime_s, up, sizeof(up));
    if (m.quiet_enabled) {
      char a[8], b[8];
      fmtClockMinutes(m.quiet_start_min, a, sizeof(a));
      fmtClockMinutes(m.quiet_end_min, b, sizeof(b));
      snprintf(quiet, sizeof(quiet), "Nachtruhe %s-%s", a, b);
    } else {
      snprintf(quiet, sizeof(quiet), "Nachtruhe aus");
    }
    if (m.last_read_at) snprintf(read, sizeof(read), "gelesen %02u:%02u", (unsigned)(m.last_read_at / 3600 % 24), (unsigned)(m.last_read_at / 60 % 60));
    else snprintf(read, sizeof(read), "noch nichts gelesen");
    if (m.heard_any) {
      char ago[16];
      uint32_t age = m.uptime_s - m.last_packet_at_s;
      if (age < 60) snprintf(ago, sizeof(ago), "gerade");
      else { char d[12]; fmtDuration(age, d, sizeof(d)); snprintf(ago, sizeof(ago), "vor %s", d); }
      snprintf(rx, sizeof(rx), "%s (%.1f dB, %s)", signalText(m.signal_level), m.last_snr, ago);
    } else {
      snprintf(rx, sizeof(rx), "%s", signalText(0));
    }
    static const char* SRC[] = { "keine", "App", "WLAN", "Mesh" };
    snprintf(r.reply, sizeof(r.reply), "OK · läuft %s · %s · %d Rptr · Zeit %s · %s · %s · %s",
             up, rx, m.repeater_count, SRC[(int)m.time_source], quiet, read, m.fw_version);
    return;
  }

  if (startsWithWord(cmd, "pin", &arg)) {
    if (!*arg) { snprintf(r.reply, sizeof(r.reply), "FEHLER · Text fehlt (!pin <Text>)"); return; }
    copyText(m.pin, sizeof(m.pin), arg);
    snprintf(r.reply, sizeof(r.reply), "OK · angeheftet");
    return;
  }
  if (startsWithWord(cmd, "unpin", &arg)) {
    m.pin[0] = 0;
    snprintf(r.reply, sizeof(r.reply), "OK · entfernt");
    return;
  }

  if (startsWithWord(cmd, "termine", &arg)) {
    m.removeExpiredAppointments();
    if (m.appt_count == 0) { snprintf(r.reply, sizeof(r.reply), "OK · keine Termine"); return; }
    size_t o = (size_t)snprintf(r.reply, sizeof(r.reply), "OK");
    for (int i = 0; i < m.appt_count && o < sizeof(r.reply); i++) {
      char when[40];
      formatWhen(m.appts[i], m.date.year, when, sizeof(when));
      o += (size_t)snprintf(r.reply + o, sizeof(r.reply) - o, " · %d %s %s", i + 1, m.appts[i].name, when);
    }
    return;
  }
  if (startsWithWord(cmd, "termin", &arg)) {
    const char* which;
    if (startsWithWord(arg, "löschen", &which) || startsWithWord(arg, "loeschen", &which)) {
      if (strcmp(which, "alle") == 0) {
        m.appt_count = 0;
        snprintf(r.reply, sizeof(r.reply), "OK · alle Termine gelöscht");
        return;
      }
      int n = atoi(which);
      if (n < 1 || n > m.appt_count) { snprintf(r.reply, sizeof(r.reply), "FEHLER · Nummer aus !termine angeben"); return; }
      m.removeAppointment(n - 1);
      snprintf(r.reply, sizeof(r.reply), "OK · Termin %d gelöscht", n);
      return;
    }
    if (!m.time_valid) {
      snprintf(r.reply, sizeof(r.reply), "FEHLER · Uhrzeit am Gerät nicht gestellt");
      return;
    }
    Appointment a = parseAppointment(arg, m.date, m.hour, m.minute);
    if (a.status == ApptStatus::Ok && !m.addAppointment(a)) {
      snprintf(r.reply, sizeof(r.reply), "FEHLER · schon %d Termine, erst einen löschen", MAX_APPOINTMENTS);
      return;
    }
    formatAppointmentReply(a, m.date.year, r.reply, sizeof(r.reply));
    return;
  }

  if (startsWithWord(cmd, "nachtruhe", &arg)) {
    if (strcmp(arg, "aus") == 0) {
      m.quiet_enabled = false;
    } else if (strcmp(arg, "an") == 0) {
      m.quiet_enabled = true;
    } else {
      const char* dash = strchr(arg, '-');
      int a, b;
      if (!dash || !parseClockMinutes(arg, dash - arg, a) || !parseClockMinutes(dash + 1, strlen(dash + 1), b) || a == b) {
        snprintf(r.reply, sizeof(r.reply), "FEHLER · z. B. !nachtruhe 22-7 oder !nachtruhe aus");
        return;
      }
      m.quiet_enabled = true;
      m.quiet_start_min = a;
      m.quiet_end_min = b;
    }
    r.settings_changed = true;
    if (m.quiet_enabled) {
      char a[8], b[8];
      fmtClockMinutes(m.quiet_start_min, a, sizeof(a));
      fmtClockMinutes(m.quiet_end_min, b, sizeof(b));
      snprintf(r.reply, sizeof(r.reply), "OK · Nachtruhe %s-%s", a, b);
    } else {
      snprintf(r.reply, sizeof(r.reply), "OK · Nachtruhe aus");
    }
    return;
  }

  if (startsWithWord(cmd, "ton", &arg)) {
    if (strcmp(arg, "an") == 0) m.sound_on = true;
    else if (strcmp(arg, "aus") == 0) m.sound_on = false;
    else { snprintf(r.reply, sizeof(r.reply), "FEHLER · !ton an oder !ton aus"); return; }
    r.settings_changed = true;
    snprintf(r.reply, sizeof(r.reply), "OK · Ton %s", m.sound_on ? "an" : "aus");
    return;
  }

  if (startsWithWord(cmd, "klingeln", &arg)) { r.ring = true; snprintf(r.reply, sizeof(r.reply), "OK · klingelt"); return; }
  if (startsWithWord(cmd, "advert", &arg)) { r.advert = true; snprintf(r.reply, sizeof(r.reply), "OK · Advert gesendet"); return; }
  if (startsWithWord(cmd, "update", &arg)) {
    r.update = true;
    snprintf(r.reply, sizeof(r.reply), "OK · WLAN MeshCore-OTA offen, Seite 192.168.4.1/update");
    return;
  }
  if (startsWithWord(cmd, "diag", &arg)) {
    char line[TEXT_LEN];
    m.diag.format(line, sizeof(line));
    snprintf(r.reply, sizeof(r.reply), "%s", line);
    return;
  }
  if (startsWithWord(cmd, "neustart", &arg)) { r.reboot = true; snprintf(r.reply, sizeof(r.reply), "OK · startet neu"); return; }

  snprintf(r.reply, sizeof(r.reply), "Unbekannt · !status !pin !unpin !termin !termine !nachtruhe !ton !klingeln !advert !update !neustart !diag");
}

}  // namespace homemesh
