#pragma once

// Sample data and fake actions shared by the PC simulator and the browser
// version. Names, places and texts are placeholders.

#include <string.h>
#include "../Model.h"
#include "../HomeMeshUI.h"

namespace homemesh {

inline Model model;

class SimActions : public UiActions {
public:
  void sendDirect(int conv, const char* text) override {
    Message& m = model.convs[conv].log.add(now(), true, text);
    m.status = Delivery::Pending;
  }
  void sendChannel(int ch, const char* text) override {
    Message& m = model.channels[ch].log.add(now(), true, text);
    m.status = Delivery::Sent;
  }
  void retryDirect(int conv, int i) override { model.convs[conv].log.msgs[i].status = Delivery::Pending; }
  static uint32_t now() {
    return (uint32_t)(daysFromCivil(model.date.year, model.date.month, model.date.day) * 86400
                      + model.hour * 3600 + model.minute * 60);
  }
};

inline uint32_t at(int day_offset, int h, int m) {
  return (uint32_t)((daysFromCivil(model.date.year, model.date.month, model.date.day) + day_offset) * 86400
                    + h * 3600 + m * 60);
}

inline void sampleData() {
  model.clear();
  model.time_valid = true;
  model.date = CivilDate{ 2026, 11, 16 };
  model.hour = 18; model.minute = 42;
  model.signal_level = 4;

  Conversation& fav = model.convs[model.conv_count++];
  strcpy(fav.name, "Juno48 Tracker");
  strcpy(fav.alias, "Max");
  fav.favourite = true;
  fav.log.add(at(-1, 20, 15), false, "Gute Nacht ihr beiden!");
  fav.log.add(at(0, 18, 40), false, "Kommt ihr Sonntag zum Essen?");
  Message& r1 = fav.log.add(at(0, 18, 42), true, "Ja, passt");
  r1.status = Delivery::Delivered;
  Message& r2 = fav.log.add(at(0, 18, 43), true, "Bring bitte die Leiter mit");
  r2.status = Delivery::Failed;
  fav.log.markRead();

  Conversation& other = model.convs[model.conv_count++];
  strcpy(other.name, "Nachbar Node");
  other.log.add(at(-1, 9, 5), false, "Hallo, habt ihr auch Empfang?");

  Channel& c1 = model.channels[model.channel_count++];
  strcpy(c1.name, "boeblingen");
  c1.log.add(at(0, 17, 58), false, "Wieder online nach Wartung", "Repeater-Nord");
  c1.log.seen = 1;
  c1.log.add(at(0, 18, 20), false, "Jemand heute Abend beim Funkabend?", "Mike");
  c1.log.add(at(0, 18, 31), false, "Bin dabei, komme um 19 Uhr", "Sabine");
  Channel& c2 = model.channels[model.channel_count++];
  strcpy(c2.name, "warnings");
  c2.log.add(at(0, 16, 2), false, "DAB+:OK/18dB NINA:OK 5m Mesh:OK Up:3d Warn:1", "WarnBridge");

  // settings
  model.quiet_enabled = true;
  model.quiet_start_min = 22 * 60;
  model.quiet_end_min = 7 * 60;
  model.sound_on = true;
  model.brightness_day = 80;
  model.brightness_night = 15;
  model.advert_interval_h = 4;

  // device and technical page
  strcpy(model.fw_version, "0.1.0");
  strcpy(model.node_name, "HomeMesh Tisch");
  model.ble_pin = 123456;
  // placeholder: the firmware fills in the real MeshCore contact link
  strcpy(model.share_uri, "meshcore://contact/add?name=HomeMesh+Tisch&public_key=0000000000000000000000000000000000000000000000000000000000000000&type=1");
  model.time_source = TimeSource::App;
  model.uptime_s = 3 * 86400 + 4 * 3600;
  model.time_set_at_s = model.uptime_s - 2 * 86400;
  model.last_advert_at_s = model.uptime_s - 2 * 3600;
  const char* rpt[3] = { "Repeater-Nord", "Repeater-Berg", "Repeater-Ost" };
  float snr[3] = { 8.5f, 2.0f, -6.5f };
  uint32_t ago[3] = { 120, 14 * 60, 3 * 3600 };
  for (int i = 0; i < 3; i++) {
    strcpy(model.repeaters[i].name, rpt[i]);
    model.repeaters[i].snr = snr[i];
    model.repeaters[i].heard_at_s = model.uptime_s - ago[i];
  }
  model.repeater_count = 3;
  model.heard_any = true;
  model.last_snr = 8.5f;
  strcpy(model.last_from, "Repeater-Nord");
  model.last_packet_at_s = model.uptime_s - 2;

  // warning, pinned note and appointment start empty; they appear on events
}

inline void sampleNetworks() {
  const char* names[4] = { "FRITZ!Box 7590 XY", "Nachbar-WLAN", "Telekom-ABC123", "Gast" };
  int8_t rssi[4] = { -48, -71, -80, -63 };
  for (int i = 0; i < 4; i++) {
    strcpy(model.networks[i].ssid, names[i]);
    model.networks[i].rssi = rssi[i];
    model.networks[i].secured = i != 3;
  }
  model.network_count = 4;
}

inline void setSampleWarning() { strcpy(model.warning, "DWD · Sturmböen · Landkreis"); }
inline void setSamplePin() { strcpy(model.pin, "Sonntag 12 Uhr Essen"); }
inline void setSampleAppointment() {
  model.addAppointment(parseAppointment("Funkrunde täglich 19:00", model.date, model.hour, model.minute));
}

}  // namespace homemesh
