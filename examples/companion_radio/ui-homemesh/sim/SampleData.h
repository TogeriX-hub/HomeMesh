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
  model.signal_ok = true;

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

  // warning, pinned note and appointment start empty; they appear on events
}

inline void setSampleWarning() { strcpy(model.warning, "DWD · Sturmböen · Landkreis"); }
inline void setSamplePin() { strcpy(model.pin, "Sonntag 12 Uhr Essen"); }
inline void setSampleAppointment() {
  model.has_appointment = true;
  model.appointment = parseAppointment("Funkrunde täglich 19:00", model.date, model.hour, model.minute);
}

}  // namespace homemesh
