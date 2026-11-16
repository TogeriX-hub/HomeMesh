// HomeMesh PC simulator: renders the screens with sample data into PNG files.
//
//   make LVGL_DIR=/path/to/lvgl && ./homemesh_sim out/
//
// Runs headless (no window), so it also works in CI or a sandbox. The UI code
// is the same that runs on the device.

#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <zlib.h>

#include "../HomeMeshUI.h"

using namespace homemesh;

static const int W = 480, H = 480;
static uint16_t framebuffer[W * H];
static uint32_t fake_ms = 0;

static uint32_t tickCb() { return fake_ms; }

static void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
  int w = lv_area_get_width(area);
  for (int y = area->y1; y <= area->y2; y++) {
    memcpy(&framebuffer[y * W + area->x1], px + (y - area->y1) * w * 2, w * 2);
  }
  lv_display_flush_ready(disp);
}

// ---- minimal PNG writer (RGB, zlib) ----
static void put32(std::string& s, uint32_t v) {
  s += (char)(v >> 24); s += (char)(v >> 16); s += (char)(v >> 8); s += (char)v;
}
static void chunk(FILE* f, const char* type, const std::string& data) {
  std::string c(type, 4);
  c += data;
  std::string len;
  put32(len, (uint32_t)data.size());
  std::string crc;
  put32(crc, (uint32_t)crc32(0, (const Bytef*)c.data(), (uInt)c.size()));
  fwrite(len.data(), 1, 4, f);
  fwrite(c.data(), 1, c.size(), f);
  fwrite(crc.data(), 1, 4, f);
}
static bool writePng(const char* path) {
  std::string raw;
  raw.reserve(H * (1 + W * 3));
  for (int y = 0; y < H; y++) {
    raw += (char)0;
    for (int x = 0; x < W; x++) {
      uint16_t p = framebuffer[y * W + x];
      raw += (char)(((p >> 11) & 0x1F) * 255 / 31);
      raw += (char)(((p >> 5) & 0x3F) * 255 / 63);
      raw += (char)((p & 0x1F) * 255 / 31);
    }
  }
  uLongf zlen = compressBound(raw.size());
  std::string z(zlen, 0);
  compress2((Bytef*)&z[0], &zlen, (const Bytef*)raw.data(), raw.size(), 9);
  z.resize(zlen);
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
  std::string ihdr;
  put32(ihdr, W); put32(ihdr, H);
  ihdr += (char)8; ihdr += (char)2; ihdr += (char)0; ihdr += (char)0; ihdr += (char)0;
  chunk(f, "IHDR", ihdr);
  chunk(f, "IDAT", z);
  chunk(f, "IEND", "");
  fclose(f);
  return true;
}

// ---- sample data ----
static Model model;

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

static uint32_t at(int day_offset, int h, int m) {
  return (uint32_t)((daysFromCivil(model.date.year, model.date.month, model.date.day) + day_offset) * 86400
                    + h * 3600 + m * 60);
}

static void sampleData() {
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

  strcpy(model.warning, "DWD · Sturmböen · Landkreis");
  strcpy(model.pin, "Sonntag 12 Uhr Essen");
  model.has_appointment = true;
  model.appointment = parseAppointment("Funkrunde täglich 19:00", model.date, 18, 42);
}

// ---- rendering ----
static HomeMeshUI ui;
static SimActions actions;

static void settle() {
  for (int i = 0; i < 5; i++) {
    fake_ms += 10;
    lv_timer_handler();
  }
  lv_refr_now(nullptr);
}

static void shot(const std::string& dir, const char* name) {
  settle();
  std::string path = dir + "/" + name + ".png";
  if (writePng(path.c_str())) printf("wrote %s\n", path.c_str());
}

// Clicks the first clickable object whose label (or a child label) contains text.
static lv_obj_t* findByText(lv_obj_t* parent, const char* text) {
  uint32_t n = lv_obj_get_child_count(parent);
  for (uint32_t i = 0; i < n; i++) {
    lv_obj_t* c = lv_obj_get_child(parent, i);
    if (lv_obj_check_type(c, &lv_label_class) && strstr(lv_label_get_text(c), text)) return c;
    lv_obj_t* r = findByText(c, text);
    if (r) return r;
  }
  return nullptr;
}

static void click(const char* text) {
  settle();   // the UI rebuilds screens asynchronously
  lv_obj_t* o = findByText(lv_screen_active(), text);
  while (o && !lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) o = lv_obj_get_parent(o);
  if (!o) { printf("not found: %s\n", text); return; }
  lv_obj_send_event(o, LV_EVENT_CLICKED, nullptr);
  settle();
}

int main(int argc, char** argv) {
  std::string out = argc > 1 ? argv[1] : ".";

  lv_init();
  lv_tick_set_cb(tickCb);
  lv_display_t* disp = lv_display_create(W, H);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  static uint16_t buf[W * 60];
  lv_display_set_buffers(disp, buf, nullptr, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flushCb);

  sampleData();
  ui.begin(&model, &actions);
  shot(out, "01_home");

  // a new message from the favourite
  model.convs[0].log.add(SimActions::now(), false, "Strom ist bei uns wieder da. Bei euch auch?");
  ui.onFavouriteMessage(fake_ms);
  shot(out, "02_new_message");

  model.convs[0].log.add(SimActions::now(), false, "Wir kommen morgen gegen 10 Uhr vorbei.");
  ui.onFavouriteMessage(fake_ms);
  shot(out, "03_new_messages_two");

  click("Antworten");
  shot(out, "04_compose");

  click("Ruf mich an");
  click("Senden");
  shot(out, "05_chat_after_send");

  ui.show(View::Home);
  shot(out, "06_home_after_read");

  click("Nachrichten");
  shot(out, "07_messages");

  ui.show(View::Home);
  click("Kanäle");
  shot(out, "08_channels");

  model.large_text = true;
  ui.show(View::Home);
  shot(out, "09_home_large_text");
  model.large_text = false;

  model.night = true;
  ui.show(View::Home);
  shot(out, "10_home_night");

  fake_ms += 31000;
  ui.tick(fake_ms);
  shot(out, "11_night_clock");

  model.convs[0].log.add(SimActions::now(), false, "Schlaft gut!");
  ui.onFavouriteMessage(fake_ms);
  shot(out, "12_night_clock_unread");
  return 0;
}
