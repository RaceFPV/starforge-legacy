#ifndef CONFIG_GLOBALS_H
#define CONFIG_GLOBALS_H

#include <Arduino.h>
#include "config.h"

// RX5808 pin configuration. Defined in main.cpp.
// Overridden at boot when a custom pin config is stored in NVS.
extern uint8_t g_rssi_input_pin;
extern uint8_t g_rx5808_data_pin;
extern uint8_t g_rx5808_clk_pin;
extern uint8_t g_rx5808_sel_pin;

#endif // CONFIG_GLOBALS_H
