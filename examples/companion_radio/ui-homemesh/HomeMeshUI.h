#pragma once

// HomeMesh screens, built with LVGL 9 for a 480x480 touch panel.
//
// The UI only reads the Model and reports user actions through UiActions; it
// knows nothing about MeshCore. The same code runs on the device and in the
// PC simulator (ui-homemesh/sim).
//
// Screens are rebuilt from the model whenever something changes. At 480x480
// that is cheap and keeps the code free of stale widget state.

#include <lvgl.h>
#include "Model.h"

namespace homemesh {

class UiActions {
public:
  virtual ~UiActions() {}
  virtual void sendDirect(int conv, const char* text) = 0;
  virtual void sendChannel(int channel, const char* text) = 0;
  virtual void retryDirect(int conv, int msg_index) = 0;
  virtual void saveSettings() {}                       // settings or aliases changed
  virtual void wifiConnect(const char* ssid, const char* password) { (void)ssid; (void)password; }
  virtual void wifiForget() {}
  virtual void wifiScan() {}
  virtual void startUpdate() {}                        // open the OTA hotspot
  virtual void stopUpdate() {}
  virtual void ringAcknowledged() {}                   // someone tapped the ringing screen
  virtual void setBrightness(uint8_t percent) { (void)percent; }
  virtual void playSound(Sound s) { (void)s; }         // only called when Model::soundAllowed()
  virtual void stopSound() {}
};

enum class View : uint8_t {
  Home,
  NewMessages,   // full screen for unread messages from favourites
  Chat,
  Compose,
  Messages,      // list of all direct conversations
  Channels,
  Settings,      // long press on the hardware button
  QuietHours,
  Wifi,
  Technical,
  Placement,     // live signal while carrying the device around
  Share,         // QR code of the device's contact
  Update,        // Wi-Fi update hotspot with QR codes
  Reminder,      // appointment starts in 5 minutes
  Ring,          // remote "!klingeln"
};

enum class ComposeMode : uint8_t { Direct, Channel, Alias, WifiPassword };

class HomeMeshUI {
public:
  void begin(Model* model, UiActions* actions);

  // Call after the model changed; redraws the current view.
  void refresh();

  void show(View v);
  View view() const { return _view; }

  // A direct message from a favourite arrived (already added to the model).
  void onFavouriteMessage(uint32_t now_ms);

  // Remote "!klingeln": full screen until someone taps it.
  void onRing();
  bool ringing() const { return _view == View::Ring; }

  // The OTA hotspot was opened (from settings or by "!update").
  void onUpdateStarted() { show(View::Update); }

  // Hardware button
  void onButtonShort();
  void onButtonLong();

  // Call regularly: closes the full-screen notice after an hour and drops
  // to the night clock when idle.
  void tick(uint32_t now_ms);
  void onTouch(uint32_t now_ms) { _last_touch_ms = now_ms; _idle = false; }

  bool screenOff() const { return _screen_off; }
  bool updateClockText();   // false: the screen needs a rebuild
  void sound(Sound s) { if (_m->soundAllowed(s)) _act->playSound(s); }

  // A full-screen message notice that nobody looked at for 10 minutes is
  // dimmed to the night brightness until it is touched or a new one arrives.
  static const uint32_t DIM_AFTER_MS = 10UL * 60 * 1000;
  bool dimmed(uint32_t now_ms) const {
    uint32_t a = now_ms - _newmsg_since_ms, b = now_ms - _last_touch_ms;   // wrap-safe
    return _view == View::NewMessages && (a < b ? a : b) > DIM_AFTER_MS;
  }

  // Backlight in percent for the current state.
  uint8_t backlight(uint32_t now_ms) const {
    if (_screen_off) return 0;
    return (_m->night || dimmed(now_ms)) ? _m->brightness_night : _m->brightness_day;
  }

private:
  Model* _m = nullptr;
  UiActions* _act = nullptr;
  View _view = View::Home;
  View _built_view = View::Home;   // screen currently on display
  lv_obj_t* _clock_label = nullptr;
  View _back[6];
  int _back_n = 0;

  int _chat_conv = 0;          // conversation shown in Chat/Compose
  ComposeMode _compose_mode = ComposeMode::Direct;
  int _compose_channel = 0;    // channel for ComposeMode::Channel
  char _wifi_ssid[33] = {0};   // network for ComposeMode::WifiPassword
  int _reminder_appt = -1;
  int64_t _snooze_until_min = -1;
  int _channel = 0;            // channel shown in Channels
  // The model lists can be reordered while a screen is open (a quiet
  // conversation makes room, channels change in the app). The selection is
  // kept by key and looked up again before every build and send.
  uint8_t _chat_key[6] = {0};
  char _compose_channel_key[NAME_LEN] = {0};
  char _channel_key[NAME_LEN] = {0};
  void selectConv(int conv);
  void selectComposeChannel(int ch);
  void selectChannel(int ch);
  bool resolveSelection();
  int _channel_divider = -1;   // index where "new since last visit" starts
  char _draft[TEXT_LEN] = {0};

  uint32_t _newmsg_since_ms = 0;
  uint32_t _last_touch_ms = 0;
  bool _idle = false;          // night clock shown
  bool _screen_off = false;

  lv_obj_t* _textarea = nullptr;

  void go(View v);
  void back();
  bool _build_pending = false;
  void build();
  void buildNow();
  static void buildAsync(void* self);

  void buildHome(lv_obj_t* scr);
  void buildNightClock(lv_obj_t* scr);
  void buildNewMessages(lv_obj_t* scr);
  void buildChat(lv_obj_t* scr);
  void buildCompose(lv_obj_t* scr);
  void buildMessages(lv_obj_t* scr);
  void buildChannels(lv_obj_t* scr);
  void buildSettings(lv_obj_t* scr);
  void buildQuietHours(lv_obj_t* scr);
  void buildWifi(lv_obj_t* scr);
  void buildTechnical(lv_obj_t* scr);
  void buildPlacement(lv_obj_t* scr);
  void buildShare(lv_obj_t* scr);
  void buildUpdate(lv_obj_t* scr);
  void buildReminder(lv_obj_t* scr);
  void buildRing(lv_obj_t* scr);

  void openChat(int conv);
  void openCompose(ComposeMode mode, int arg);
  void openChannel(int ch);
  void sendDraft();

public:
  // LVGL event callbacks (used by the widget helpers in HomeMeshUI.cpp)
  static void onClick(lv_event_t* e);
  static void onBubbleLongPress(lv_event_t* e);
  static void onKeyboard(lv_event_t* e);
  static void onSwitch(lv_event_t* e);
  static void onSlider(lv_event_t* e);
};

}  // namespace homemesh
