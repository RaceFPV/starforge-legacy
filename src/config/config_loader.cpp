#include "config_loader.h"
#include <Preferences.h>

const char* ConfigLoader::NVS_NAMESPACE = "sfos_pins";

bool ConfigLoader::loadCustomConfig(CustomPinConfig* config, bool allowSerialOutput) {
    if (!config) {
        return false;
    }

    Preferences prefs;

    // Read-write open avoids an error when the namespace does not exist yet.
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        if (allowSerialOutput) {
            Serial.println("ConfigLoader: Failed to open NVS - using config.h defaults");
        }
        return false;
    }

    uint8_t enabled = prefs.getUChar("pin_enabled", 0);
    if (enabled == 0) {
        if (allowSerialOutput) {
            Serial.println("ConfigLoader: Custom pins disabled or not found - using config.h defaults");
        }
        prefs.end();
        return false;
    }

    config->enabled = true;
    config->rssi_input_pin = prefs.getUChar("pin_rssi_input", 0);
    config->rx5808_data_pin = prefs.getUChar("pin_rx5808_data", 0);
    config->rx5808_clk_pin = prefs.getUChar("pin_rx5808_clk", 0);
    config->rx5808_sel_pin = prefs.getUChar("pin_rx5808_sel", 0);

    prefs.end();

    if (allowSerialOutput) {
        Serial.println("ConfigLoader: Custom pin configuration loaded from NVS");
        Serial.printf("  RSSI Input: GPIO%d\n", config->rssi_input_pin);
        Serial.printf("  RX5808 Data: GPIO%d\n", config->rx5808_data_pin);
        Serial.printf("  RX5808 CLK: GPIO%d\n", config->rx5808_clk_pin);
        Serial.printf("  RX5808 SEL: GPIO%d\n", config->rx5808_sel_pin);
    }

    return true;
}

bool ConfigLoader::saveCustomConfig(const CustomPinConfig* config) {
    if (!config) {
        return false;
    }

    Preferences prefs;

    if (!prefs.begin(NVS_NAMESPACE, false)) {
        Serial.println("ConfigLoader: Failed to open NVS for writing - cannot save config");
        return false;
    }

    prefs.putUChar("pin_enabled", config->enabled ? 1 : 0);

    if (config->enabled) {
        prefs.putUChar("pin_rssi_input", config->rssi_input_pin);
        prefs.putUChar("pin_rx5808_data", config->rx5808_data_pin);
        prefs.putUChar("pin_rx5808_clk", config->rx5808_clk_pin);
        prefs.putUChar("pin_rx5808_sel", config->rx5808_sel_pin);
    }

    prefs.end();
    Serial.println("ConfigLoader: Configuration saved to NVS");
    return true;
}

bool ConfigLoader::hasCustomConfig() {
    Preferences prefs;

    if (!prefs.begin(NVS_NAMESPACE, false)) {
        return false;
    }

    uint8_t enabled = prefs.getUChar("pin_enabled", 0);
    prefs.end();

    return (enabled != 0);
}
