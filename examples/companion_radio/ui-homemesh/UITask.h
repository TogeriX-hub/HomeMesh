#pragma once

// HomeMesh on the companion firmware: connects MeshCore (MyMesh listener
// events, contacts, channels, radio statistics) to the HomeMesh model and
// runs the LVGL screens on the SenseCAP Indicator panel.
//
// Everything runs in the Arduino loop: LVGL rendering, touch and the radio
// share one thread, so no locking is needed between the model and the mesh.

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/SensorManager.h>
#include <Arduino.h>

#include "../AbstractUITask.h"
#include "../NodePrefs.h"

#include "Model.h"
#include "SignalMeter.h"
#include "HomeMeshUI.h"

#ifndef HOMEMESH_VERSION
  #define HOMEMESH_VERSION "0.1.0"
#endif

class UITask : public AbstractUITask, public homemesh::UiActions {
public:
  UITask(mesh::MainBoard* board, MultiSerialInterface* serial) : AbstractUITask(board, serial) {}

  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  // AbstractUITask
  void notify(UIEventType t = UIEventType::none) override { (void)t; }
  void loop() override;

  // MyMesh::Listener
  void onMessageRecv(mesh::Packet* pkt, const ContactInfo& from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) override;
  void onChannelMessageRecv(mesh::Packet* pkt, ChannelDetails& channel_details, const char* text) override;
  void onQueueSizeChanged(int msgcount) override { (void)msgcount; }
  void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) override;
  void onACKRecv(uint32_t ack_crc) override;
  bool interceptMessage(const ContactInfo& from, uint8_t txt_type, const char* text) override;
  void onAppMessageSent(const ContactInfo& to, uint8_t attempt, const char* text, uint32_t expected_ack, uint32_t est_timeout) override;
  void onAppChannelMessageSent(uint8_t channel_idx, const char* text, int len) override;

  // homemesh::UiActions
  void sendDirect(int conv, const char* text) override;
  void sendChannel(int channel, const char* text) override;
  void retryDirect(int conv, int msg_index) override;
  void saveSettings() override { _save_due_ms = millis(); }
  void wifiConnect(const char* ssid, const char* password) override;
  void wifiForget() override;
  void wifiScan() override;
  void startUpdate() override;
  void stopUpdate() override;
  void ringAcknowledged() override {}
  void setBrightness(uint8_t percent) override;
  void playSound(homemesh::Sound s) override;
  void stopSound() override;

private:
  static const int MAX_PENDING = 8;

  // An outgoing direct message waiting for its ACK.
  struct PendingAck {
    uint8_t prefix[6];
    uint32_t msg_time;
    uint32_t text_crc;
    uint32_t ack;
    unsigned long deadline;
    bool active;
  };

  // A reply to a remote command, sent from loop() (not from inside the
  // receive callback).
  struct QueuedReply {
    uint8_t prefix[6];
    char text[homemesh::TEXT_LEN];
    bool active;
  };

  DisplayDriver* _display = nullptr;
  NodePrefs* _prefs = nullptr;
  homemesh::Model* _m = nullptr;
  homemesh::HomeMeshUI _ui;
  homemesh::SignalMeter _signal;

  PendingAck _pending[MAX_PENDING] = {};
  QueuedReply _reply = {};
  int8_t _channel_idx[homemesh::MAX_CHANNELS] = {};   // model channel -> MeshCore channel index

  uint32_t _last_packets = 0;
  uint64_t _uptime_ms = 0;
  unsigned long _last_millis = 0;
  unsigned long _next_clock_ms = 0, _next_sync_ms = 0, _next_signal_refresh_ms = 0;
  unsigned long _save_due_ms = 0, _next_save_check_ms = 0;
  unsigned long _reboot_at_ms = 0;
  unsigned long _last_loop_ms = 0, _next_diag_log_ms = 0;
  uint32_t _saved_crc = 0;
  uint32_t _last_utc = 0;
  uint32_t _next_advert_s = 60;
  int _last_minute = -1;
  uint8_t _last_level = 0xFF;
  bool _last_night = false;
  bool _swallow_touch = false;
  bool _touch_down = false;
  bool _clock_set = false;
  bool _ui_ready = false;
  uint8_t _want_brightness = 0xFF;

  void initLvgl();
  void updateDiagnostics();
  void sendToRp2040(uint8_t type, uint32_t para);
  void refreshUi();
  void loadState();
  void saveState(bool force);

  void updateClock();
  void updateSignal();
  void syncContacts();
  void syncChannels();
  void applyBrightness();
  void sendQueuedReply();
  void checkAckTimeouts();
  void maybeAdvert();

  uint32_t nowLocal() const;
  void trackAck(const uint8_t prefix[6], const homemesh::Message& m, uint32_t ack, uint32_t timeout_ms);
  homemesh::Message* findOutgoing(const PendingAck& p, int* conv_out);
  bool sendToContact(int conv, homemesh::Message& msg, bool flood);

  bool _wifi_scanning = false;
  bool _ntp_started = false;
  void updateWifi();

  static void lvFlush(lv_display_t* disp, const lv_area_t* area, uint8_t* px);
  static void lvTouch(lv_indev_t* indev, lv_indev_data_t* data);
};
