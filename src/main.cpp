#include <Arduino.h>
#include "config/config.h"
#include "config/config_loader.h"
#include "system/serial_guard.h"
#include <nvs_flash.h>

#if SF_SEAT_COUNT > 1
// Eight seats on one board: the portable protocol core plus the hopping
// sampler. The single-seat TimingCore/NodeMode path is not compiled in.
#include <esp_timer.h>
#include "hardware/status_led.h"
#include "multipilot/c5_seat_sampler.h"
#include "multipilot/rh_node.h"
mp::RhNode multiNode;
mp::C5SeatSampler sampler;
#if defined(STATUS_LED_PIN)
StatusLed statusLed;
#endif
#else
#include "timing_core.h"
#include "node_mode.h"
TimingCore timing;
NodeMode node;
#endif

// RotorHazard reads these exact strings over the node serial protocol.
const char *firmwareVersionString = "FIRMWARE_VERSION: ESP32_" SF_FW_VERSION;
const char *firmwareBuildDateString = "FIRMWARE_BUILDDATE: " __DATE__;
const char *firmwareBuildTimeString = "FIRMWARE_BUILDTIME: " __TIME__;
const char *firmwareProcTypeString = "FIRMWARE_PROCTYPE: ESP32";

uint8_t g_rssi_input_pin = RSSI_INPUT_PIN;
uint8_t g_rx5808_data_pin = RX5808_DATA_PIN;
uint8_t g_rx5808_clk_pin = RX5808_CLK_PIN;
uint8_t g_rx5808_sel_pin = RX5808_SEL_PIN;

#if SF_SEAT_COUNT > 1
// Protocol replies go straight out; nothing else may write to this port.
static void serialWrite(void *, const uint8_t *data, unsigned len) {
  Serial.write(data, len);
  Serial.flush();
}

// The sampler reads the engine's frequencies on every tick, so a frequency
// change needs no push here.
static void seatFrequencyChanged(void *, unsigned, uint16_t) {}
#endif

void setup() {
  Serial.begin(UART_BAUD_RATE);
  // Keep ESP-IDF and driver log text off the RotorHazard link.
  silenceSystemOutput();
  delay(200);

  // Drop bootloader text so it is not parsed as a node command.
  while (Serial.available()) {
    Serial.read();
  }

  esp_err_t nvs_ret = nvs_flash_init();
  if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_ret = nvs_flash_init();
  }
  (void)nvs_ret;

  // Custom RX5808 pins stored in NVS. No serial logging: the RotorHazard
  // link is a binary protocol.
  CustomPinConfig customConfig;
  if (ConfigLoader::loadCustomConfig(&customConfig, false)) {
    g_rssi_input_pin = customConfig.rssi_input_pin;
    g_rx5808_data_pin = customConfig.rx5808_data_pin;
    g_rx5808_clk_pin = customConfig.rx5808_clk_pin;
    g_rx5808_sel_pin = customConfig.rx5808_sel_pin;
  }

#if SF_SEAT_COUNT > 1
  multiNode.begin(SF_SEAT_COUNT, ENTER_RSSI, EXIT_RSSI, serialWrite, nullptr,
                  seatFrequencyChanged, nullptr);
  multiNode.setIdentity(SF_FW_VERSION, "ESP32-C5", __DATE__, __TIME__);
  sampler.begin(&multiNode);

  // Give every seat a Raceband channel at start-up. RotorHazard's node init
  // reads READ_FREQUENCY and refuses a node that answers 0 ("Unable to read
  // frequency value"), so a board that has never been configured would never
  // enumerate at all. It also means a board with no host attached still reports
  // live levels on sensible channels. The host overwrites these from its
  // profile as soon as it starts.
  {
    static const uint16_t kRaceband[8] = {5658, 5695, 5732, 5769,
                                          5806, 5843, 5880, 5917};
    uint32_t now = (uint32_t)esp_timer_get_time();
    for (unsigned i = 0; i < SF_SEAT_COUNT && i < 8; ++i) {
#if SF_BENCH_SELF_TUNE == 2
      // Same frequency on every seat: separates "cost per retune" from
      // "cost per frequency change".
      multiNode.engine().setFrequency(i, kRaceband[0], now);
#elif SF_BENCH_SELF_TUNE
      // Bench: seat 0 carries the frequency under test, the rest stay on
      // Raceband so cross-channel bleed shows up on seats that should be deaf.
      uint16_t freq = (i == 0) ? (uint16_t)SF_BENCH_SEAT0_MHZ : kRaceband[i];
      multiNode.engine().setFrequency(i, freq, now);
#else
      multiNode.engine().setFrequency(i, kRaceband[i], now);
#endif
    }
  }

#if defined(STATUS_LED_PIN)
  statusLed.init(STATUS_LED_PIN, STATUS_LED_INVERTED);
#if defined(STATUS_LED_WS2812) && STATUS_LED_WS2812
  statusLed.setColor(0, 20, 0);  // dim green: node mode
#endif
  statusLed.blink(300, 1700);
#endif
#else
  timing.begin();
  timing.setDebugMode(false);
  node.begin(&timing);
  timing.setActivated(true);
#endif
}

void loop() {
#if SF_SEAT_COUNT > 1
  // Bound the work per pass so the sampler always gets its turn, even if the
  // host is streaming commands.
  for (int i = 0; i < 64 && Serial.available() > 0; ++i) {
    multiNode.feed((uint8_t)Serial.read(), (uint32_t)esp_timer_get_time());
  }
  sampler.tick();
#if defined(STATUS_LED_PIN)
  statusLed.update(millis());
#endif
#else
  timing.process();
  node.handleSerialInput();
  node.process();
#endif
  taskYIELD();
}
