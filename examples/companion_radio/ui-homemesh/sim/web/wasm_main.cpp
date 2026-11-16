// HomeMesh in the browser: the real UI code compiled to WebAssembly.
// index.html draws the framebuffer onto a canvas and forwards mouse input.
// Build: see build.sh

#include <lvgl.h>
#include <stdint.h>
#include <string.h>

#include "../SampleData.h"

using namespace homemesh;

#define EXPORT(name) extern "C" __attribute__((export_name(#name)))

static const int W = 480, H = 480;
static uint32_t rgba[W * H];          // what the page copies onto the canvas
static uint16_t draw_buf[W * 48];
static uint32_t now_ms = 0;
static int ptr_x = 0, ptr_y = 0;
static bool ptr_down = false;
static bool dirty = true;

static HomeMeshUI ui;

// Outgoing direct messages switch from "on the way" to delivered (or failed
// without radio) after a short delay, like a real ACK.
struct PendingAck { int conv, msg; uint32_t due; };
static PendingAck acks[8];
static int ack_count = 0;

class WebActions : public SimActions {
public:
  void sendDirect(int conv, const char* text) override {
    SimActions::sendDirect(conv, text);
    queueAck(conv, model.convs[conv].log.count - 1);
  }
  void retryDirect(int conv, int i) override {
    SimActions::retryDirect(conv, i);
    queueAck(conv, i);
  }
  static void queueAck(int conv, int msg) {
    if (ack_count < 8) acks[ack_count++] = PendingAck{ conv, msg, now_ms + 2500 };
  }
};
static WebActions actions;

static uint32_t tickCb() { return now_ms; }

static void flushCb(lv_display_t* disp, const lv_area_t* a, uint8_t* px) {
  const uint16_t* src = (const uint16_t*)px;
  int w = lv_area_get_width(a);
  for (int y = a->y1; y <= a->y2; y++) {
    for (int x = a->x1; x <= a->x2; x++) {
      uint16_t p = *src++;
      uint32_t r = ((p >> 11) & 0x1F) * 255 / 31, g = ((p >> 5) & 0x3F) * 255 / 63, b = (p & 0x1F) * 255 / 31;
      rgba[y * W + x] = 0xFF000000u | (b << 16) | (g << 8) | r;   // RGBA bytes, little endian
    }
    (void)w;
  }
  dirty = true;
  lv_display_flush_ready(disp);
}

static void pointerCb(lv_indev_t*, lv_indev_data_t* d) {
  d->point.x = ptr_x;
  d->point.y = ptr_y;
  d->state = ptr_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

EXPORT(hm_init) void hm_init(int hour, int minute, int year, int month, int day) {
  lv_init();
  lv_tick_set_cb(tickCb);
  lv_display_t* disp = lv_display_create(W, H);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp, draw_buf, nullptr, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flushCb);
  lv_indev_t* in = lv_indev_create();
  lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(in, pointerCb);

  sampleData();
  model.date = CivilDate{ year, month, day };
  model.hour = hour;
  model.minute = minute;
  model.appointment = parseAppointment("Funkrunde täglich 19:00", model.date, hour, minute);
  ui.begin(&model, &actions);
}

EXPORT(hm_frame) void hm_frame(uint32_t ms) {
  now_ms = ms;
  for (int i = 0; i < ack_count;) {
    if ((int32_t)(now_ms - acks[i].due) >= 0) {
      Message& m = model.convs[acks[i].conv].log.msgs[acks[i].msg];
      m.status = model.signal_ok ? Delivery::Delivered : Delivery::Failed;
      acks[i] = acks[--ack_count];
      if (ui.view() == View::Chat) ui.refresh();
    } else {
      i++;
    }
  }
  ui.tick(now_ms);
  lv_timer_handler();
}

EXPORT(hm_set_time) void hm_set_time(int hour, int minute) {
  if (hour == model.hour && minute == model.minute) return;
  model.hour = hour;
  model.minute = minute;
  if (ui.view() == View::Home) ui.refresh();
}

EXPORT(hm_pointer) void hm_pointer(int x, int y, int down) {
  ptr_x = x;
  ptr_y = y;
  if (down && !ptr_down) ui.onTouch(now_ms);
  ptr_down = down != 0;
}

EXPORT(hm_framebuffer) uint32_t* hm_framebuffer() { return rgba; }
EXPORT(hm_take_dirty) int hm_take_dirty() { bool d = dirty; dirty = false; return d; }
EXPORT(hm_screen_off) int hm_screen_off() { return ui.screenOff(); }
EXPORT(hm_is_night) int hm_is_night() { return model.night; }
EXPORT(hm_is_large) int hm_is_large() { return model.large_text; }
EXPORT(hm_has_signal) int hm_has_signal() { return model.signal_ok; }

static const char* SAMPLE_FAV[] = {
  "Seid ihr zuhause?", "Strom ist bei uns wieder da. Bei euch auch?",
  "Ich komme morgen gegen 10 Uhr vorbei.", "Alles gut bei euch?",
};
static int sample_i = 0;

// Buttons of the control panel next to the device
EXPORT(hm_action) void hm_action(int id) {
  uint32_t t = SimActions::now();
  switch (id) {
    case 1:   // message from the favourite
      model.convs[0].log.add(t, false, SAMPLE_FAV[sample_i++ % 4]);
      ui.onFavouriteMessage(now_ms);
      break;
    case 2:   // message from another contact
      model.convs[1].log.add(t, false, "Seid ihr wohlauf?");
      ui.refresh();
      break;
    case 3:   // channel message
      model.channels[0].log.add(t, false, "Repeater am Goldberg läuft wieder", "Mike");
      ui.refresh();
      break;
    case 4: {  // real WarnBridge alert
      const char* raw = "[!] DWD | Sturmböen | Landkreis | /details";
      model.channels[1].log.add(t, false, raw, "WarnBridge");
      copyText(model.warning, sizeof(model.warning), "DWD · Sturmböen · Landkreis");
      ui.refresh();
      break;
    }
    case 5: model.night = !model.night; ui.onTouch(now_ms); ui.refresh(); break;
    case 6: model.large_text = !model.large_text; ui.refresh(); break;
    case 7: model.signal_ok = !model.signal_ok; ui.refresh(); break;
    case 8: ui.onButtonShort(); break;
    case 9: ui.onButtonLong(); break;
    case 10: ui.tick(now_ms + 61UL * 60 * 1000); break;  // an hour passes
    case 11: model.pin[0] = 0; ui.refresh(); break;
    default: break;
  }
}
