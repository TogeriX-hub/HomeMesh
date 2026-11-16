// HomeMesh in the browser: the real UI code compiled to WebAssembly.
// index.html draws the framebuffer onto a canvas and forwards mouse input.
// Build: see build.sh

#include <lvgl.h>
#include <stdint.h>
#include <string.h>

#include "../SampleData.h"
#include "../../RemoteCommands.h"

using namespace homemesh;

#define EXPORT(name) extern "C" __attribute__((export_name(#name)))

static const int W = 480, H = 480;
static uint32_t rgba[W * H];          // what the page copies onto the canvas
static uint16_t draw_buf[W * 48];
static uint32_t now_ms = 0;
static uint32_t silence_offset_ms = 0;   // "3 hours of silence" without waiting
static int ptr_x = 0, ptr_y = 0;
static bool ptr_down = false;
static bool dirty = true;

static HomeMeshUI ui;
static SignalMeter meter;
static float sim_snr = 6.0f;
static bool sim_silent = false;
static uint32_t next_packet_ms = 0;

// Outgoing direct messages switch from "on the way" to delivered (or failed
// without radio) after a short delay, like a real ACK.
struct PendingAck { int conv, msg; uint32_t due; };
static PendingAck acks[8];
static int ack_count = 0;

static uint32_t wifi_connect_due = 0;
static int brightness = 80;

class WebActions : public SimActions {
public:
  void saveSettings() override {}
  void wifiScan() override { sampleNetworks(); }
  void wifiConnect(const char* ssid, const char*) override {
    copyText(model.wifi_ssid, sizeof(model.wifi_ssid), ssid);
    model.wifi_connected = false;
    wifi_connect_due = now_ms + 2000;
  }
  void wifiForget() override { model.wifi_ssid[0] = 0; model.wifi_connected = false; }
  void startUpdate() override { model.update_running = true; }
  void stopUpdate() override { model.update_running = false; }
  void setBrightness(uint8_t p) override { brightness = p; }
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
  ui.begin(&model, &actions);
}

EXPORT(hm_frame) void hm_frame(uint32_t ms) {
  now_ms = ms;
  for (int i = 0; i < ack_count;) {
    if ((int32_t)(now_ms - acks[i].due) >= 0) {
      Message& m = model.convs[acks[i].conv].log.msgs[acks[i].msg];
      m.status = model.signal_level ? Delivery::Delivered : Delivery::Failed;
      acks[i] = acks[--ack_count];
      if (ui.view() == View::Chat) ui.refresh();
    } else {
      i++;
    }
  }
  model.uptime_s = 3 * 86400 + now_ms / 1000;
  if (wifi_connect_due && (int32_t)(now_ms - wifi_connect_due) >= 0) {
    wifi_connect_due = 0;
    model.wifi_connected = true;
    model.time_source = TimeSource::Wifi;
    model.time_set_at_s = model.uptime_s;
    if (ui.view() == View::Wifi || ui.view() == View::Settings) ui.refresh();
  }
  // fake mesh traffic: a packet every 2 s with the SNR from the slider
  if (!sim_silent && (int32_t)(now_ms - next_packet_ms) >= 0) {
    float snr = sim_snr + ((int)(now_ms / 2000) % 5 - 2) * 0.5f;   // a little jitter
    meter.onPacket(snr, now_ms);
    model.heard_any = true;
    model.last_snr = snr;
    model.last_packet_at_s = model.uptime_s;
    model.repeaters[0].snr = snr;
    model.repeaters[0].heard_at_s = model.uptime_s;
    next_packet_ms = now_ms + 2000;
    if (ui.view() == View::Placement) ui.refresh();
  }
  uint8_t level = meter.level(now_ms + silence_offset_ms);
  if (level != model.signal_level) {
    model.signal_level = level;
    if (ui.view() == View::Home) ui.refresh();
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
EXPORT(hm_has_signal) int hm_has_signal() { return model.signal_level; }
EXPORT(hm_set_snr) void hm_set_snr(float snr) { sim_snr = snr; }
EXPORT(hm_is_silent) int hm_is_silent() { return sim_silent; }
EXPORT(hm_brightness) int hm_brightness() { return model.night ? model.brightness_night : model.brightness_day; }

// Remote command from the favourite: the page writes UTF-8 into this buffer,
// calls hm_command() and reads the reply back from the same buffer.
static char cmd_buf[TEXT_LEN];
EXPORT(hm_cmd_buf) char* hm_cmd_buf() { return cmd_buf; }
EXPORT(hm_command) void hm_command() {
  if (!isRemoteCommand(cmd_buf)) {   // plain text: an ordinary message
    model.convs[0].log.add(SimActions::now(), false, cmd_buf);
    ui.onFavouriteMessage(now_ms);
    cmd_buf[0] = 0;
    return;
  }
  CommandResult r;
  handleRemoteCommand(model, cmd_buf, r);
  if (r.ring) ui.onRing();
  else if (r.update) { model.update_running = true; ui.onUpdateStarted(); }
  else ui.refresh();
  copyText(cmd_buf, sizeof(cmd_buf), r.reply);
}

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
      setSampleWarning();
      ui.refresh();
      break;
    }
    case 5: model.night = !model.night; ui.onTouch(now_ms); ui.refresh(); break;
    case 6: model.large_text = !model.large_text; ui.refresh(); break;
    case 7:   // radio silence: no packets, and 3 h pass at once
      sim_silent = !sim_silent;
      silence_offset_ms = sim_silent ? SignalMeter::SILENCE_MS + 1000 : 0;
      if (!sim_silent) next_packet_ms = now_ms;
      break;
    case 8: ui.onButtonShort(); break;
    case 9: ui.onButtonLong(); break;
    case 10: ui.tick(now_ms + 61UL * 60 * 1000); break;  // an hour passes
    case 11: if (model.pin[0]) model.pin[0] = 0; else setSamplePin(); ui.refresh(); break;
    case 12: if (model.appt_count) model.appt_count = 0; else setSampleAppointment(); ui.refresh(); break;
    case 13: {  // appointment in 5 minutes -> reminder right away
      int m = model.minuteOfDay() + 5;
      char cmd[48];
      snprintf(cmd, sizeof(cmd), "Anrufen heute %02d:%02d", (m / 60) % 24, m % 60);
      model.addAppointment(parseAppointment(cmd, model.date, model.hour, model.minute));
      break;
    }
    case 14: ui.onRing(); break;
    default: break;
  }
}
