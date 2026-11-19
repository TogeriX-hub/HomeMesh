#include "UITask.h"

#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include <helpers/TxtDataHelpers.h>
#include <helpers/AdvertDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"

#include <WiFi.h>
#include <esp_sntp.h>

#include "Persist.h"
#include "RemoteCommands.h"
#include "Rp2040Link.h"
#include "ShareLink.h"
#include "TimeZone.h"
#include "Warnings.h"

using namespace homemesh;

static const char STATE_FILE[] = "/hm_state.bin";
static const char STATE_TMP[] = "/hm_state.tmp";

static const unsigned long SAVE_CHECK_MS = 30000;       // look for changes to save (each write briefly disturbs the display)
static const unsigned long SYNC_MS = 30000;              // re-read contacts and channels
static const unsigned long ACK_MARGIN_MS = 3000;         // on top of MeshCore's estimate
static const unsigned long APP_ACK_TIMEOUT_MS = 30000;   // app sends: no estimate from the hook
static const int LV_BUF_LINES = 20;
static const int RP_TX_PIN = 19;   // UART to the RP2040, which drives the buzzer
static const int RP_RX_PIN = 20;

static UITask* s_task = nullptr;
static uint8_t* s_save_buf = nullptr;     // PSRAM, PERSIST_MAX_BYTES
static Channel* s_channel_tmp = nullptr;  // PSRAM, for reordering channels

static volatile bool s_ntp_synced = false;
static void onNtpSync(struct timeval* tv) { (void)tv; s_ntp_synced = true; }

// ---------------------------------------------------------------- setup

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  (void)sensors;
  s_task = this;
  _display = display;
  _prefs = node_prefs;
  Serial.printf("[HomeMesh] begin: psram free=%u internal free=%u\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  // heap_caps directly: Arduino's ps_malloc() returns nothing here, because
  // psramFound() stays false although ESP-IDF already put the PSRAM into the heap
  _m = (Model*)heap_caps_calloc(1, sizeof(Model), MALLOC_CAP_SPIRAM);
  s_save_buf = (uint8_t*)heap_caps_malloc(PERSIST_MAX_BYTES, MALLOC_CAP_SPIRAM);
  s_channel_tmp = (Channel*)heap_caps_calloc(MAX_CHANNELS, sizeof(Channel), MALLOC_CAP_SPIRAM);
  if (!_m || !s_save_buf || !s_channel_tmp) {
    Serial.println("[HomeMesh] PSRAM allocation failed");
    return;
  }
  if (!_display) return;   // no screen: plain companion without UI
  _m->clear();
  loadState();

  // After a power cut the ESP32 clock restarts at a fixed date; only a clock
  // that was set (and survived a soft restart) counts as valid.
  _clock_set = rtc_clock.getCurrentTime() > (uint32_t)RTC_TIME_MIN + millis() / 1000 + 60;

  copyText(_m->fw_version, sizeof(_m->fw_version), HOMEMESH_VERSION);
  copyText(_m->node_name, sizeof(_m->node_name), _prefs->node_name);
  _m->ble_pin = the_mesh.getBLEPin();

  // contact link for the QR code (scanned with the MeshCore app)
  buildContactLink(_prefs->node_name, the_mesh.self_id.pub_key, CONTACT_TYPE_COMPANION,
                   _m->share_uri, sizeof(_m->share_uri));

  // Wi-Fi is only used for the clock (and the update hotspot). It is set up
  // here instead of the companion's Wi-Fi interface, so no app port is
  // opened on the home network. The network is kept in the HomeMesh state.
  if (_m->wifi_ssid[0]) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(_m->wifi_ssid, _m->wifi_pwd);
  }

  Serial1.begin(RP_BAUD, SERIAL_8N1, RP_RX_PIN, RP_TX_PIN);
  user_btn.begin();
  initLvgl();
  _ui.begin(_m, this);
  _ui_ready = true;

  syncContacts();
  syncChannels();
  updateClock();
  applyBrightness();
  Serial.println("[HomeMesh] begin done");

  _last_millis = millis();
  _next_save_check_ms = millis() + SAVE_CHECK_MS;
}

void UITask::initLvgl() {
  display.startLvgl();
  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });

  lv_display_t* disp = lv_display_create(display.device()->width(), display.device()->height());
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  // Small draw buffer in internal RAM: drawing there is much faster than in
  // PSRAM, and the panel's DMA keeps the PSRAM bandwidth for the frame buffer.
  // (~19 KB; Bluetooth and Wi-Fi need the rest of the internal RAM.)
  size_t buf_bytes = (size_t)display.device()->width() * LV_BUF_LINES * 2;
  void* buf = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf) buf = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, buf_bytes, MALLOC_CAP_SPIRAM);
  lv_display_set_buffers(disp, buf, nullptr, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, lvFlush);
  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_180);   // see lvFlush
  lv_display_add_event_cb(disp, lvRenderEvent, LV_EVENT_RENDER_START, nullptr);
  lv_display_add_event_cb(disp, lvRenderEvent, LV_EVENT_RENDER_READY, nullptr);

  lv_indev_t* touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, lvTouch);
}

// The panel's frame buffer holds big-endian RGB565, turned by 180 degrees
// against the picture. One pass over the block in fast internal RAM does
// both: reversing the pixel order turns a block by 180 degrees, and each
// pixel gets its bytes swapped. LovyanGFX can then copy whole rows (its
// per-pixel path for rotated or converted pixels was the slow part).
// frame timing for the diagnostics
static uint32_t s_frame_start = 0, s_flush_sum = 0;

void UITask::lvRenderEvent(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_RENDER_START) {
    s_frame_start = millis();
    s_flush_sum = 0;
  } else if (s_task && s_task->_m) {
    s_task->_m->diag.noteFrame(millis() - s_frame_start, s_flush_sum, millis());
  }
}

void UITask::lvFlush(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
  uint32_t t0 = millis();
  int32_t w = lv_area_get_width(area), h = lv_area_get_height(area);
  uint16_t* p = (uint16_t*)px;
  size_t n = (size_t)w * (size_t)h;
  for (size_t i = 0, j = n - 1; i < j; i++, j--) {
    uint16_t a = p[i], b = p[j];
    p[i] = (uint16_t)((b >> 8) | (b << 8));
    p[j] = (uint16_t)((a >> 8) | (a << 8));
  }
  if (n & 1) {
    uint16_t m = p[n / 2];
    p[n / 2] = (uint16_t)((m >> 8) | (m << 8));
  }
  int32_t hor = lv_display_get_physical_horizontal_resolution(disp);
  int32_t ver = lv_display_get_physical_vertical_resolution(disp);
  LGFX_Device* dev = display.device();
  dev->startWrite();
  dev->pushImage(hor - 1 - area->x2, ver - 1 - area->y2, w, h, (const lgfx::swap565_t*)p);
  dev->endWrite();
  s_flush_sum += millis() - t0;
  lv_display_flush_ready(disp);
}

void UITask::lvTouch(lv_indev_t* indev, lv_indev_data_t* data) {
  (void)indev;
  UITask* t = s_task;
  int x, y;
  bool down = readTouch(&x, &y);
  bool was_down = t->_touch_down;
  t->_touch_down = down;
  if (down && !was_down && t->_ui.screenOff()) {
    // the first touch only wakes the screen
    t->_ui.onButtonShort();
    t->_swallow_touch = true;
  }
  if (t->_swallow_touch) {
    if (!down) t->_swallow_touch = false;
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  if (down) {
    if (!was_down) t->_ui.onTouch(millis());
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// ---------------------------------------------------------------- state file

void UITask::loadState() {
  bool ok = false;
  for (const char* path : { STATE_FILE, STATE_TMP }) {
    if (!SPIFFS.exists(path)) continue;
    File f = SPIFFS.open(path, "r");
    if (!f) continue;
    size_t size = f.size();
    size_t n = size <= PERSIST_MAX_BYTES ? f.read(s_save_buf, size) : 0;
    f.close();
    ok = n && deserializeState(*_m, s_save_buf, n);
    Serial.printf("[HomeMesh] load %s: size %u, read %u, %s\n", path, (unsigned)size, (unsigned)n, ok ? "ok" : "unreadable");
    if (ok) break;
  }
  if (!ok) Serial.println("[HomeMesh] no saved state, defaults");
  if (!ok) {
    _m->applyDefaults();
    _saved_crc = 0;
  } else {
    size_t n = serializeState(*_m, s_save_buf, PERSIST_MAX_BYTES);
    _saved_crc = recordChecksum(s_save_buf, n);
  }
}

// Writes the state when it changed since the last save. The new record goes
// to a temporary file first; the loader also accepts that file in case the
// power fails between removing the old file and renaming the new one.
void UITask::saveState(bool force) {
  if (!_m || !s_save_buf) return;
  size_t n = serializeState(*_m, s_save_buf, PERSIST_MAX_BYTES);
  if (n == 0) return;
  uint32_t crc = recordChecksum(s_save_buf, n);
  if (!force && crc == _saved_crc) return;

  unsigned long t0 = millis();
  File f = SPIFFS.open(STATE_TMP, "w");
  if (!f) { Serial.println("[HomeMesh] save: cannot open file"); return; }
  size_t written = f.write(s_save_buf, n);
  f.close();
  if (written != n) {
    Serial.printf("[HomeMesh] save: wrote %u of %u bytes\n", (unsigned)written, (unsigned)n);
    SPIFFS.remove(STATE_TMP);
    return;
  }
  SPIFFS.remove(STATE_FILE);
  bool renamed = SPIFFS.rename(STATE_TMP, STATE_FILE);
  Serial.printf("[HomeMesh] saved %u bytes in %lu ms%s\n", (unsigned)n, millis() - t0, renamed ? "" : " (rename failed)");
  _saved_crc = crc;
  _m->diag.save_ms.add(millis() - t0, millis());
  _m->diag.saves++;
  _m->diag.save_bytes = n;
}

// ---------------------------------------------------------------- main loop

void UITask::refreshUi() {
  if (_ui_ready) _ui.refresh();
}

void UITask::loop() {
  if (!_ui_ready) return;
  unsigned long now = millis();
  if (_last_loop_ms) _m->diag.noteLoop(now - _last_loop_ms, now);
  _last_loop_ms = now;
  _uptime_ms += (unsigned long)(now - _last_millis);
  _last_millis = now;
  _m->uptime_s = (uint32_t)(_uptime_ms / 1000);

  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) _ui.onButtonShort();
  else if (ev == BUTTON_EVENT_LONG_PRESS) _ui.onButtonLong();

  if ((long)(now - _next_clock_ms) >= 0) {
    _next_clock_ms = now + 1000;
    updateClock();
    maybeAdvert();
    updateDiagnostics();
  }
  if ((long)(now - _next_sync_ms) >= 0) {
    _next_sync_ms = now + SYNC_MS;
    syncContacts();
    syncChannels();
  }
  updateSignal();
  checkAckTimeouts();
  sendQueuedReply();
  updateWifi();

  _ui.tick(now);
  applyBrightness();
  unsigned long t0 = millis();
  lv_timer_handler();
  _m->diag.render_ms.add(millis() - t0, millis());

  if (_save_due_ms && (long)(now - _save_due_ms) >= 0) {
    _save_due_ms = 0;
    saveState(false);
  } else if ((long)(now - _next_save_check_ms) >= 0) {
    _next_save_check_ms = now + SAVE_CHECK_MS;
    saveState(false);
  }

  // Restart once the reply and its ACK are out and MeshCore has saved the
  // contacts - or after 15 s at the latest.
  if (_reboot_at_ms && (long)(now - _reboot_at_ms) >= 0 && !_reply.active &&
      (!the_mesh.hasPendingWork() || (long)(now - _reboot_deadline_ms) >= 0)) {
    saveState(true);
    _board->reboot();
  }
}

// ---------------------------------------------------------------- clock

uint32_t UITask::nowLocal() const {
  uint32_t utc = rtc_clock.getCurrentTime();
  return _clock_set && isValidClock(utc) ? utcToLocal(utc) : 0;
}

void UITask::updateClock() {
  uint32_t utc = rtc_clock.getCurrentTime();

  // Who set the clock? A jump we did not cause comes from Wi-Fi time when
  // the time server just answered, otherwise from the app.
  if (_last_utc != 0) {
    int32_t jump = (int32_t)(utc - (_last_utc + 1));
    if (jump > 30 || jump < -30) {
      _m->time_source = s_ntp_synced ? TimeSource::Wifi : TimeSource::App;
      _m->time_set_at_s = _m->uptime_s;
      _clock_set = true;
    }
  }
  if (s_ntp_synced) {
    s_ntp_synced = false;
    _m->time_source = TimeSource::Wifi;
    _m->time_set_at_s = _m->uptime_s;
    _clock_set = true;
  }
  _last_utc = utc;

  bool valid = _clock_set && isValidClock(utc);
  _m->time_valid = valid;
  if (valid) {
    LocalTime t = splitLocal(utcToLocal(utc));
    _m->date = t.date;
    _m->hour = t.hour;
    _m->minute = t.minute;
  }

  bool night = _m->inQuietHours();
  _m->expireWarning();
  if (night != _last_night) {
    _last_night = night;
    _m->night = night;
    refreshUi();
  } else if (valid && _m->minute != _last_minute && _ui.view() == View::Home) {
    // the clock on the home screen: just the digits, unless something else
    // shown there changed with the time (date, next appointment, warning)
    uint32_t sig = (uint32_t)_m->date.day | ((uint32_t)(_m->nextAppointment() + 1) << 8) |
                   (_m->warning[0] ? 1u << 16 : 0) | (_m->pin[0] ? 1u << 17 : 0);
    if (sig != _home_sig || !_ui.updateClockText()) refreshUi();
    _home_sig = sig;
  }
  _last_minute = _m->minute;
}

// ---------------------------------------------------------------- diagnostics

void UITask::updateDiagnostics() {
  Diagnostics& d = _m->diag;
  d.heap_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  d.heap_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  d.heap_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  d.psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  d.rx = radio_driver.getPacketsRecv();
  d.rx_errors = radio_driver.getPacketsRecvErrors();
  d.tx = radio_driver.getPacketsSent();
#ifdef HOMEMESH_DIAG_LOG
  if ((long)(millis() - _next_diag_log_ms) >= 0) {
    _next_diag_log_ms = millis() + 60000;
    char line[TEXT_LEN];
    d.format(line, sizeof(line));
    Serial.printf("[HomeMesh] %s\n", line);
  }
#endif
}

// ---------------------------------------------------------------- radio status

void UITask::updateSignal() {
  uint32_t n = radio_driver.getPacketsRecv();
  if (n != _last_packets) {
    _last_packets = n;
    float snr = radio_driver.getLastSNR();
    _signal.onPacket(snr, millis());
    _m->heard_any = true;
    _m->last_snr = snr;
    _m->last_packet_at_s = _m->uptime_s;
    // only the placement helper is live; the technical page shows a snapshot
    if (_ui.view() == View::Placement && (long)(millis() - _next_signal_refresh_ms) >= 0) {
      _next_signal_refresh_ms = millis() + 1000;
      refreshUi();
    }
  }
  uint8_t level = _signal.level(millis());
  _m->signal_level = level;
  if (level != _last_level) {
    _last_level = level;
    if (_ui.view() == View::Home || _ui.view() == View::Placement) refreshUi();   // the bars are shown there
  }
}

void UITask::onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  (void)is_new; (void)path;
  if (!_ui_ready) return;
  copyText(_m->last_from, sizeof(_m->last_from), contact.name);
  if (contact.type == ADV_TYPE_REPEATER && (path_len & 63) == 0) {
    _m->noteRepeater(contact.name, radio_driver.getLastSNR(), _m->uptime_s);
  }
}

void UITask::maybeAdvert() {
  if (_m->advert_interval_h == 0 || _m->uptime_s < _next_advert_s) return;
  if (the_mesh.advert()) _m->last_advert_at_s = _m->uptime_s;
  _next_advert_s = _m->uptime_s + (uint32_t)_m->advert_interval_h * 3600;
}

// ---------------------------------------------------------------- contacts and channels

void UITask::syncContacts() {
  bool changed = false;
  ContactsIterator it = the_mesh.startContactsIterator();
  ContactInfo c;
  while (it.hasNext(&the_mesh, c)) {
    if (c.type != ADV_TYPE_CHAT) continue;
    int i = _m->findConversation(c.id.pub_key);
    if (i >= 0) {
      if (_m->convs[i].favourite != c.isFav() || strcmp(_m->convs[i].name, c.name) != 0) changed = true;
      _m->convs[i].favourite = c.isFav();
      copyText(_m->convs[i].name, sizeof(_m->convs[i].name), c.name);
    } else if (c.isFav()) {
      if (_m->ensureConversation(c.id.pub_key, c.name, true) >= 0) changed = true;
    }
  }
  if (changed) refreshUi();
}

void UITask::syncChannels() {
  int n = 0;
  bool changed = false;
  int8_t idx[MAX_CHANNELS];
  for (int i = 0; i < MAX_GROUP_CHANNELS && n < MAX_CHANNELS; i++) {
    ChannelDetails cd;
    if (!the_mesh.getChannel(i, cd) || cd.name[0] == 0) continue;
    const char* name = plainChannelName(cd.name);
    int old = _m->findChannel(name);
    if (old >= 0) {
      s_channel_tmp[n] = _m->channels[old];
      if (old != n) changed = true;
    } else {
      memset(&s_channel_tmp[n], 0, sizeof(Channel));
      copyText(s_channel_tmp[n].name, sizeof(s_channel_tmp[n].name), name);
      changed = true;
    }
    idx[n++] = (int8_t)i;
  }
  if (n != _m->channel_count) changed = true;
  memcpy(_channel_idx, idx, sizeof(idx));
  if (!changed) return;
  memcpy(_m->channels, s_channel_tmp, sizeof(Channel) * n);
  _m->channel_count = n;
  refreshUi();
}

// ---------------------------------------------------------------- incoming

bool UITask::alreadySeen(const ContactInfo& from, uint32_t sender_timestamp, const char* text) {
  uint32_t crc = crc32((const uint8_t*)text, strlen(text));
  for (int i = 0; i < SEEN_SIZE; i++) {
    const SeenMsg& s = _seen[i];
    if (s.timestamp == sender_timestamp && s.text_crc == crc && memcmp(s.prefix, from.id.pub_key, 6) == 0) return true;
  }
  SeenMsg& s = _seen[_seen_next];
  _seen_next = (uint8_t)((_seen_next + 1) % SEEN_SIZE);
  memcpy(s.prefix, from.id.pub_key, 6);
  s.timestamp = sender_timestamp;
  s.text_crc = crc;
  return false;
}

bool UITask::interceptMessage(const ContactInfo& from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) {
  if (!_ui_ready || txt_type != TXT_TYPE_PLAIN || !from.isFav() || !isRemoteCommand(text)) return false;

  if (alreadySeen(from, sender_timestamp, text)) {
    // the sender missed our ACK and repeats the command: answer again, do not run it twice
    if (memcmp(_last_reply.prefix, from.id.pub_key, 6) == 0 && _last_reply.text[0]) {
      _reply = _last_reply;
      _reply.active = true;
    }
    return true;
  }

  CommandResult r;
  handleRemoteCommand(*_m, text, r);
  memcpy(_reply.prefix, from.id.pub_key, 6);
  copyText(_reply.text, sizeof(_reply.text), r.reply);
  _reply.active = true;
  _last_reply = _reply;

  if (r.advert && the_mesh.advert()) _m->last_advert_at_s = _m->uptime_s;
  if (r.ring) _ui.onRing();
  if (r.update) startUpdate();
  if (r.reboot) requestReboot(3000);
  if (!_m->sound_on) stopSound();
  _m->night = _m->inQuietHours();
  _last_night = _m->night;
  saveSettings();
  refreshUi();
  return true;   // handled: not shown in the chat, not passed to the app
}

void UITask::onMessageRecv(mesh::Packet* pkt, const ContactInfo& from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) {
  (void)pkt;
  if (!_ui_ready || (txt_type != TXT_TYPE_PLAIN && txt_type != TXT_TYPE_SIGNED_PLAIN)) return;
  if (alreadySeen(from, sender_timestamp, text)) return;   // repeat after a lost ACK

  // Fallback clock: take the sender's time while ours is not set. Only from
  // favourites, and not the start date of an ESP32 node whose clock was
  // never set: a wrong clock could not be turned back by the app later.
  if (!_clock_set && from.isFav() && sender_timestamp > (uint32_t)RTC_TIME_MIN + 86400) {
    rtc_clock.setCurrentTime(sender_timestamp);
    _clock_set = true;
    _last_utc = sender_timestamp;
    _m->time_source = TimeSource::Mesh;
    _m->time_set_at_s = _m->uptime_s;
    updateClock();
  }

  int i = _m->ensureConversation(from.id.pub_key, from.name, from.isFav());
  if (i < 0) return;
  _m->convs[i].log.add(nowLocal(), false, text);
  if (from.isFav()) _ui.onFavouriteMessage(millis());
  else refreshUi();
}

void UITask::onChannelMessageRecv(mesh::Packet* pkt, ChannelDetails& channel_details, const char* text) {
  (void)pkt;
  if (!_ui_ready) return;
  const char* name = plainChannelName(channel_details.name);
  int ch = _m->findChannel(name);
  if (ch < 0) {
    syncChannels();
    ch = _m->findChannel(name);
    if (ch < 0) return;
  }

  // "Sender: text" - the sender is everything before the first ": "
  char sender[NAME_LEN] = "";
  const char* body = text;
  const char* sep = strstr(text, ": ");
  if (sep && sep - text < NAME_LEN) {
    memcpy(sender, text, sep - text);
    sender[sep - text] = 0;
    body = sep + 2;
  }

  if (isWarningChannel(name)) {
    char warning[TEXT_LEN];
    if (extractWarning(body, warning, sizeof(warning))) _m->setWarning(warning);
  }
  _m->channels[ch].log.add(nowLocal(), false, body, sender);
  refreshUi();
}

// ---------------------------------------------------------------- outgoing

void UITask::trackAck(const uint8_t prefix[6], const Message& m, uint32_t ack, uint32_t timeout_ms) {
  int slot = 0;
  for (int i = 0; i < MAX_PENDING; i++) {
    if (!_pending[i].active) { slot = i; break; }
    if ((long)(_pending[i].deadline - _pending[slot].deadline) < 0) slot = i;   // full: reuse the oldest
  }
  PendingAck& p = _pending[slot];
  memcpy(p.prefix, prefix, 6);
  p.msg_time = m.time;
  p.text_crc = crc32((const uint8_t*)m.text, strlen(m.text));
  p.ack = ack;
  p.deadline = millis() + timeout_ms;
  p.active = true;
}

Message* UITask::findOutgoing(const PendingAck& p, int* conv_out) {
  int c = _m->findConversation(p.prefix);
  if (c < 0) return nullptr;
  MessageLog& log = _m->convs[c].log;
  for (int i = log.count - 1; i >= 0; i--) {
    Message& m = log.msgs[i];
    if (m.outgoing && m.time == p.msg_time && crc32((const uint8_t*)m.text, strlen(m.text)) == p.text_crc) {
      if (conv_out) *conv_out = c;
      return &m;
    }
  }
  return nullptr;
}

bool UITask::sendToContact(int conv, Message& msg, bool flood) {
  ContactInfo* ci = the_mesh.lookupContactByPubKey(_m->convs[conv].pubkey_prefix, 6);
  if (!ci) { msg.status = Delivery::Failed; return false; }
  if (flood) the_mesh.resetPathTo(*ci);
  uint32_t ack = 0, timeout = 0;
  int res = the_mesh.sendMessage(*ci, rtc_clock.getCurrentTimeUnique(), 0, msg.text, ack, timeout);
  if (res == MSG_SEND_FAILED) { msg.status = Delivery::Failed; return false; }
  msg.status = Delivery::Pending;
  trackAck(_m->convs[conv].pubkey_prefix, msg, ack, timeout + ACK_MARGIN_MS);
  return true;
}

void UITask::sendDirect(int conv, const char* text) {
  if (conv < 0 || conv >= _m->conv_count || !text[0]) return;
  Message& m = _m->convs[conv].log.add(nowLocal(), true, text);
  sendToContact(conv, m, false);
  refreshUi();
}

// A manual retry goes out as flood: the stored path evidently did not work.
void UITask::retryDirect(int conv, int msg_index) {
  if (conv < 0 || conv >= _m->conv_count) return;
  MessageLog& log = _m->convs[conv].log;
  if (msg_index < 0 || msg_index >= log.count || !log.msgs[msg_index].outgoing) return;
  sendToContact(conv, log.msgs[msg_index], true);
  refreshUi();
}

void UITask::sendChannel(int channel, const char* text_in) {
  if (channel < 0 || channel >= _m->channel_count || !text_in[0]) return;
  // MeshCore would cut longer text in the middle of a character; cut it here
  // at a character boundary, so the log shows what was really sent
  char text[TEXT_LEN];
  copyText(text, channelTextBudget(_prefs->node_name) + 1, text_in);
  ChannelDetails cd;
  Message& m = _m->channels[channel].log.add(nowLocal(), true, text, _prefs->node_name);
  if (the_mesh.getChannel(_channel_idx[channel], cd) &&
      the_mesh.sendGroupMessage(rtc_clock.getCurrentTimeUnique(), cd.channel, _prefs->node_name, text, strlen(text))) {
    m.status = Delivery::Sent;
  } else {
    m.status = Delivery::Failed;
  }
  refreshUi();
}

void UITask::onACKRecv(uint32_t ack_crc) {
  if (!_ui_ready) return;
  for (int i = 0; i < MAX_PENDING; i++) {
    PendingAck& p = _pending[i];
    if (!p.active || p.ack != ack_crc) continue;
    p.active = false;
    Message* m = findOutgoing(p, nullptr);
    if (m) {
      m->status = Delivery::Delivered;
      refreshUi();
    }
  }
}

void UITask::checkAckTimeouts() {
  for (int i = 0; i < MAX_PENDING; i++) {
    PendingAck& p = _pending[i];
    if (!p.active || (long)(millis() - p.deadline) < 0) continue;
    p.active = false;
    bool other_attempt = false;   // the app's next attempt for the same message
    for (int k = 0; k < MAX_PENDING; k++) {
      const PendingAck& q = _pending[k];
      if (q.active && q.msg_time == p.msg_time && q.text_crc == p.text_crc && memcmp(q.prefix, p.prefix, 6) == 0) other_attempt = true;
    }
    if (other_attempt) continue;
    Message* m = findOutgoing(p, nullptr);
    if (m && m->status == Delivery::Pending) {
      m->status = Delivery::Failed;
      refreshUi();
    }
  }
}

// Messages written in the MeshCore app also appear on the device. When the
// app retries (attempt > 0), the existing entry is reused.
void UITask::onAppMessageSent(const ContactInfo& to, uint8_t attempt, const char* text, uint32_t expected_ack, uint32_t est_timeout) {
  if (!_ui_ready) return;
  int c = _m->ensureConversation(to.id.pub_key, to.name, to.isFav());
  if (c < 0) return;
  MessageLog& log = _m->convs[c].log;
  Message* msg = nullptr;
  if (attempt > 0) {
    for (int i = log.count - 1; i >= 0 && !msg; i--) {
      if (log.msgs[i].outgoing && strcmp(log.msgs[i].text, text) == 0) msg = &log.msgs[i];
    }
  }
  if (!msg) msg = &log.add(nowLocal(), true, text);
  msg->status = Delivery::Pending;
  if (expected_ack) trackAck(to.id.pub_key, *msg, expected_ack, est_timeout ? est_timeout + ACK_MARGIN_MS : APP_ACK_TIMEOUT_MS);
  else msg->status = Delivery::Sent;
  refreshUi();
}

void UITask::onAppChannelMessageSent(uint8_t channel_idx, const char* text, int len) {
  if (!_ui_ready || len <= 0) return;
  for (int ch = 0; ch < _m->channel_count; ch++) {
    if (_channel_idx[ch] != (int8_t)channel_idx) continue;
    char buf[TEXT_LEN];
    int n = len < TEXT_LEN - 1 ? len : TEXT_LEN - 1;
    memcpy(buf, text, n);
    buf[n] = 0;
    _m->channels[ch].log.add(nowLocal(), true, buf, _prefs->node_name).status = Delivery::Sent;
    refreshUi();
    return;
  }
}

void UITask::sendQueuedReply() {
  if (!_reply.active) return;
  _reply.active = false;
  ContactInfo* ci = the_mesh.lookupContactByPubKey(_reply.prefix, 6);
  if (!ci) return;
  uint32_t ack, timeout;
  the_mesh.sendMessage(*ci, rtc_clock.getCurrentTimeUnique(), 0, _reply.text, ack, timeout);
}

// ---------------------------------------------------------------- display

void UITask::setBrightness(uint8_t percent) {
  display.setBacklight(percent);
}

// Follows the model only when the wanted value changes, so the settings
// slider can preview a value with setBrightness().
void UITask::applyBrightness() {
  uint8_t want = _ui.backlight(millis());
  if (want == _want_brightness) return;
  _want_brightness = want;
  setBrightness(want);
}

// ---------------------------------------------------------------- sound

// The buzzer hangs on the RP2040; it needs the HomeMesh RP2040 firmware
// (examples/homemesh_rp2040). Without it the commands are simply ignored.
void UITask::sendToRp2040(uint8_t type, uint32_t para) {
  uint8_t frame[8];
  size_t n = buildRpCommand(type, para, frame);
  Serial1.write(frame, n);
}

void UITask::playSound(Sound s) { sendToRp2040(RP_PKT_BEEP_ON, (uint32_t)s); }

void UITask::stopSound() { sendToRp2040(RP_PKT_BEEP_OFF, 0); }

// ---------------------------------------------------------------- Wi-Fi and update

void UITask::updateWifi() {
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != _m->wifi_connected) {
    _m->wifi_connected = connected;
    if (_ui.view() == View::Wifi || _ui.view() == View::Settings) refreshUi();
  }
  if (connected && !_ntp_started) {
    _ntp_started = true;
    sntp_set_time_sync_notification_cb(onNtpSync);
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  }
  if (_wifi_scanning) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    _wifi_scanning = false;
    _m->network_count = 0;
    for (int i = 0; i < n && _m->network_count < MAX_WIFI_NETWORKS; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;
      WifiNetwork& w = _m->networks[_m->network_count++];
      copyText(w.ssid, sizeof(w.ssid), ssid.c_str());
      w.rssi = (int8_t)WiFi.RSSI(i);
      w.secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
    if (_ui.view() == View::Wifi) refreshUi();
  }
}

void UITask::wifiScan() {
  if (_wifi_scanning) return;
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
  _wifi_scanning = WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING;
}

void UITask::wifiConnect(const char* ssid, const char* password) {
  copyText(_m->wifi_ssid, sizeof(_m->wifi_ssid), ssid);
  strncpy(_m->wifi_pwd, password, sizeof(_m->wifi_pwd) - 1);
  _m->wifi_pwd[sizeof(_m->wifi_pwd) - 1] = 0;
  saveSettings();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(_m->wifi_ssid, _m->wifi_pwd);
}

void UITask::wifiForget() {
  _m->wifi_ssid[0] = 0;
  _m->wifi_pwd[0] = 0;
  _m->wifi_connected = false;
  saveSettings();
  WiFi.disconnect(true, true);
}

void UITask::startUpdate() {
  if (!_m->update_running) {
    char reply[160];
    _m->update_running = _board->startOTAUpdate(_prefs->node_name, reply);
  }
  _ui.onUpdateStarted();
}

// There is no way to close the update hotspot again; a restart does it.
void UITask::stopUpdate() {
  if (_m->update_running) requestReboot(500);
}

void UITask::requestReboot(unsigned long delay_ms) {
  _reboot_at_ms = millis() + delay_ms;
  _reboot_deadline_ms = millis() + 15000;
}
