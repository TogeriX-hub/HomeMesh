// HomeMesh buzzer firmware for the RP2040 of the SenseCAP Indicator.
//
// The buzzer of the Indicator is wired to the RP2040, not to the ESP32-S3.
// This small firmware listens on the UART to the ESP32 and plays the tone
// patterns from Rp2040Link.h. The sensors and the SD card stay unused.
//
// Flashing: hold the button on the underside while plugging in USB, then
// copy firmware.uf2 to the RPI-RP2 drive.

#include <Arduino.h>
#include "Rp2040Link.h"

using namespace homemesh;

static const int BUZZER_PIN = 19;
static const int UART_RX_PIN = 17;   // from ESP32 TX (GPIO 19)
static const int UART_TX_PIN = 16;   // to ESP32 RX (GPIO 20)

static CobsReceiver rx;
static TonePlayer player;

static void setTone(uint16_t hz) {
  if (hz) tone(BUZZER_PIN, hz);
  else noTone(BUZZER_PIN);
}

static void handlePacket(const uint8_t* p, size_t n) {
  if (n < 5) return;
  uint32_t para = (uint32_t)p[1] | ((uint32_t)p[2] << 8) | ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 24);
  switch (p[0]) {
    case RP_PKT_BEEP_ON:
      setTone(player.start(para, millis()));
      break;
    case RP_PKT_BEEP_OFF:
      player.stop();
      setTone(0);
      break;
    default:
      break;
  }
}

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  Serial1.setRX(UART_RX_PIN);
  Serial1.setTX(UART_TX_PIN);
  Serial1.begin(RP_BAUD);
}

void loop() {
  while (Serial1.available()) {
    uint8_t pkt[8];
    size_t n = rx.push((uint8_t)Serial1.read(), pkt, sizeof(pkt));
    if (n) handlePacket(pkt, n);
  }
  uint16_t hz;
  if (player.update(millis(), &hz)) setTone(hz);
}
