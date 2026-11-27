#include <Arduino.h>  // first: defines CONFIG_IDF_TARGET_* used by config.h
#include "multipilot/c5_hop_radio.h"

#if SF_RECEIVER_C5_NATIVE
#include <esp_event.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#include "filters/burst_filter.h"

// libphy entry points (not public API; the same family c5_radio.cpp uses and
// docs/ESP32_C5.md documents re-checking).
extern "C" {
void phy_check_sigrssi_en(bool enable);
int8_t phy_get_sigrssi(void);
void RFChannelSel(int chan, uint8_t bw);
void phy_param_track_tot(bool wifi_track, bool ble_track);
}

namespace mp {

namespace {
const uint8_t kChannels5G[] = {
    36, 40, 44, 48, 52, 56, 60, 64,
    100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
    149, 153, 157, 161, 165, 169, 173, 177,
};

uint8_t nearestWifiChannel(uint16_t freq_mhz, int16_t* offset_mhz) {
    uint8_t best = kChannels5G[0];
    int16_t bestOff = 32767;
    for (uint8_t ch : kChannels5G) {
        int16_t off = (int16_t)freq_mhz - (int16_t)(5000 + 5 * ch);
        if (abs(off) < abs(bestOff)) {
            best = ch;
            bestOff = off;
        }
    }
    *offset_mhz = bestOff;
    return best;
}
}  // namespace

bool C5HopRadio::begin() {
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = 0;
    if (esp_wifi_init(&cfg) != ESP_OK) return false;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return false;

    wifi_country_t country = {};
    country.cc[0] = '0';
    country.cc[1] = '1';
    country.cc[2] = ' ';
    country.schan = 1;
    country.nchan = 13;
    country.max_tx_power = 0;
    country.policy = WIFI_COUNTRY_POLICY_MANUAL;
    country.wifi_5g_channel_mask = 0x1FFFFFFE;
    esp_wifi_set_country(&country);

    if (esp_wifi_start() != ESP_OK) return false;
    esp_wifi_set_band_mode(WIFI_BAND_MODE_5G_ONLY);
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
    esp_wifi_set_ps(WIFI_PS_NONE);

    wifi_promiscuous_filter_t filt = {};
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    esp_wifi_set_promiscuous_filter(&filt);
    if (esp_wifi_set_promiscuous(true) != ESP_OK) return false;

    phy_check_sigrssi_en(true);
    lastWifiChannel_ = -1;
    ready_ = true;
    return true;
}

bool C5HopRadio::hop(uint16_t mhz) {
    if (!ready_) return false;
    // Park the driver's own idea of the channel near the target, then move the
    // PHY onto the exact frequency. The RX path follows the PHY, so the driver
    // call is only needed when the nearest channel actually changes - two seats
    // often share one, and at a 150 us budget a driver call per hop is the
    // difference between ~1 kHz per seat and half of it.
    int16_t offset = 0;
    uint8_t ch = nearestWifiChannel(mhz, &offset);
    if ((int)ch != lastWifiChannel_) {
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        lastWifiChannel_ = ch;
    }
    if (abs(offset) > C5_WIFI_TUNE_TOLERANCE_MHZ) {
        RFChannelSel((int)mhz, 0);
    }
    // A channel change reprograms the baseband: signal-RSSI mode has to be
    // re-enabled every time (docs/ESP32_C5.md).
    phy_check_sigrssi_en(true);
    return true;
}

int16_t C5HopRadio::readDbm() const { return ready_ ? phy_get_sigrssi() : -128; }

uint8_t C5HopRadio::toRssi(int16_t dbm) {
    return dbmToRssi(dbm, C5_RSSI_DBM_FLOOR, C5_RSSI_DBM_CEIL);
}

}  // namespace mp

#endif  // SF_RECEIVER_C5_NATIVE
