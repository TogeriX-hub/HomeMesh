#include <Arduino.h>
#include "target.h"

// SX1262 SPI data pins. They are driven by software SPI in SenseCapHAL so the
// hardware SPI peripheral does not disturb the RGB display bus.
#define LORA_SCLK  41
#define LORA_MISO  47
#define LORA_MOSI  48

// SX1262 control pins on port 0 of the TCA9535 IO expander, encoded as
// (pin | IO_EXPANDER) like the display driver does.
#define LORA_NSS    (0 | IO_EXPANDER)
#define LORA_RESET  (1 | IO_EXPANDER)
#define LORA_BUSY   (2 | IO_EXPANDER)
#define LORA_DIO1   (3 | IO_EXPANDER)

#define EXPANDER_I2C_ADDR 0x20

IndicatorBoard board;

// The SPIClass argument is only a placeholder: SenseCapHAL overrides all SPI methods.
static SenseCapHAL radio_hal(SPI, LORA_SCLK, LORA_MISO, LORA_MOSI, EXPANDER_I2C_ADDR, &Wire);
RADIO_CLASS radio = new Module(&radio_hal, LORA_NSS, LORA_DIO1, LORA_RESET, LORA_BUSY);
WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock rtc_clock;
SensorManager sensors;

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true, true);
#endif

// Shared between the radio HAL (expander) and touch reads, which use the same I2C bus.
static SemaphoreHandle_t i2c_mutex = nullptr;

// The display init can leave the I2C bus with SDA held low. Clock SCL until
// SDA is released, then send a STOP condition.
static void i2c_bus_recovery() {
  pinMode(PIN_BOARD_SCL, OUTPUT_OPEN_DRAIN);
  pinMode(PIN_BOARD_SDA, INPUT_PULLUP);
  digitalWrite(PIN_BOARD_SCL, HIGH);
  delayMicroseconds(10);

  for (int i = 0; i < 9 && digitalRead(PIN_BOARD_SDA) == LOW; i++) {
    digitalWrite(PIN_BOARD_SCL, LOW);
    delayMicroseconds(10);
    digitalWrite(PIN_BOARD_SCL, HIGH);
    delayMicroseconds(10);
  }

  pinMode(PIN_BOARD_SDA, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_BOARD_SDA, LOW);
  delayMicroseconds(10);
  digitalWrite(PIN_BOARD_SCL, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_BOARD_SDA, HIGH);
  delayMicroseconds(10);

  pinMode(PIN_BOARD_SCL, INPUT_PULLUP);
  pinMode(PIN_BOARD_SDA, INPUT_PULLUP);
  delay(5);
}

// Must run after display.begin(): the expander setup keeps the bits the
// display driver configured and locks the display chip-select high.
bool radio_init() {
  i2c_mutex = xSemaphoreCreateMutex();
  radio_hal.setMutex(i2c_mutex);

  xSemaphoreTake(i2c_mutex, portMAX_DELAY);
  i2c_bus_recovery();
  Wire.end();
  delay(5);
  Wire.begin(PIN_BOARD_SDA, PIN_BOARD_SCL, 400000);
  Wire.setTimeOut(15);
  xSemaphoreGive(i2c_mutex);

  if (!radio_hal.scanExpander()) {
    MESH_DEBUG_PRINTLN("radio_init: IO expander not found");
    return false;
  }
  radio_hal.initExpander();

  rtc_clock.begin();
  return radio.std_init();
}

#ifdef DISPLAY_CLASS
bool readTouch(int* x, int* y) {
  if (i2c_mutex && xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return false;
  bool touched = display.getTouch(x, y);
  if (i2c_mutex) xSemaphoreGive(i2c_mutex);
  return touched;
}
#endif

void IndicatorBoard::powerOff() {
#ifdef DISPLAY_CLASS
  display.turnOff();
#endif
  radio_driver.powerOff();
  Serial.flush();
  esp_deep_sleep_start();  // no wakeup source: stays off until reset
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}
