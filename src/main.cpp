#include <Arduino.h>
#include "config/config.h"
#include "config/config_loader.h"
#include "timing_core.h"
#include "node_mode.h"
#include "system/serial_guard.h"
#include <nvs_flash.h>

// RotorHazard reads these exact strings over the node serial protocol.
const char *firmwareVersionString = "FIRMWARE_VERSION: ESP32_" SF_FW_VERSION;
const char *firmwareBuildDateString = "FIRMWARE_BUILDDATE: " __DATE__;
const char *firmwareBuildTimeString = "FIRMWARE_BUILDTIME: " __TIME__;
const char *firmwareProcTypeString = "FIRMWARE_PROCTYPE: ESP32";

uint8_t g_rssi_input_pin = RSSI_INPUT_PIN;
uint8_t g_rx5808_data_pin = RX5808_DATA_PIN;
uint8_t g_rx5808_clk_pin = RX5808_CLK_PIN;
uint8_t g_rx5808_sel_pin = RX5808_SEL_PIN;

TimingCore timing;
NodeMode node;

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

  timing.begin();
  timing.setDebugMode(false);
  node.begin(&timing);
  timing.setActivated(true);
}

void loop() {
  timing.process();
  node.handleSerialInput();
  node.process();
  taskYIELD();
}
