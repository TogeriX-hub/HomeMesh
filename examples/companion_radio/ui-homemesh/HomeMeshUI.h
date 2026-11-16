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
};

enum class View : uint8_t {
  Home,
  NewMessages,   // full screen for unread messages from favourites
  Chat,
  Compose,
  Messages,      // list of all direct conversations
  Channels,
};

class HomeMeshUI {
public:
  void begin(Model* model, UiActions* actions);

  // Call after the model changed; redraws the current view.
  void refresh();

  void show(View v);
  View view() const { return _view; }

  // A direct message from a favourite arrived (already added to the model).
  void onFavouriteMessage(uint32_t now_ms);

  // Hardware button
  void onButtonShort();
  void onButtonLong();

  // Call regularly: closes the full-screen notice after an hour and drops
  // to the night clock when idle.
  void tick(uint32_t now_ms);
  void onTouch(uint32_t now_ms) { _last_touch_ms = now_ms; _idle = false; }

  bool screenOff() const { return _screen_off; }

private:
  Model* _m = nullptr;
  UiActions* _act = nullptr;
  View _view = View::Home;
  View _back[6];
  int _back_n = 0;

  int _chat_conv = 0;          // conversation shown in Chat/Compose
  int _compose_channel = -1;   // >=0: composing into a channel
  int _channel = 0;            // channel shown in Channels
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

  void openChat(int conv);
  void openChannel(int ch);
  void sendDraft();

public:
  // LVGL event callbacks (used by the widget helpers in HomeMeshUI.cpp)
  static void onClick(lv_event_t* e);
  static void onBubbleLongPress(lv_event_t* e);
  static void onKeyboard(lv_event_t* e);
};

}  // namespace homemesh
