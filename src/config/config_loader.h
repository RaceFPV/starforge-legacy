#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include <Arduino.h>

// Custom RX5808 pin configuration.
// These override the defaults from config.h when enabled.
struct CustomPinConfig {
    bool enabled = false;

    uint8_t rssi_input_pin = 0;         // RSSI input from RX5808 (ADC)
    uint8_t rx5808_data_pin = 0;        // SPI MOSI to RX5808
    uint8_t rx5808_clk_pin = 0;         // SPI SCK to RX5808
    uint8_t rx5808_sel_pin = 0;         // SPI CS to RX5808
};

class ConfigLoader {
public:
    // Load configuration from NVS.
    // Returns true when a custom config is stored and enabled.
    static bool loadCustomConfig(CustomPinConfig* config, bool allowSerialOutput = true);

    // Save configuration to NVS.
    static bool saveCustomConfig(const CustomPinConfig* config);

    // Check if custom config exists and is enabled.
    static bool hasCustomConfig();

private:
    static const char* NVS_NAMESPACE;
};

#endif // CONFIG_LOADER_H
