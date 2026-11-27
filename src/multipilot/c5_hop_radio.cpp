#include <Arduino.h>  // first: defines CONFIG_IDF_TARGET_* used by config.h
#include "multipilot/c5_hop_radio.h"
#include <string.h>

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
// Fast retune: moves the synthesizer without the full channel-change sequence
// RFChannelSel performs. This is the difference between ~140 Hz and ~1 kHz per
// seat - RFChannelSel costs ~700 us, this is tens of microseconds.
int phy_set_rf_freq_offset(uint8_t xtalSel, uint32_t mhz, int16_t trim);
extern uint8_t phy_param[];
// The stages phy_set_rf_freq_offset runs internally. Splitting them lets the
// sampler pay only for the ones a reading needs: the PLL set and the SDM write
// retune the receiver, the 5G clock calibration and the frequency-memory update
// are what make a full retune cost ~400 us.
void phy_rfpll_set_freq(uint32_t mhz, uint8_t xtalSel, int16_t trim, void* sdm);
void phy_write_rfpll_sdm(void* sdm);
void phy_set_freq_i2c_new(uint32_t mhz);
void phy_restart_cal(void);
void phy_i2c_sdm_en(uint8_t is5g);
void phy_ckgen_5g_cal(uint32_t mhz);
void phy_freq_mem_change_5g_(void* sdm);
void phy_param_track_tot(bool wifi_track, bool ble_track);
}

namespace mp {

namespace {
const uint8_t kChannels5G[] = {
    36, 40, 44, 48, 52, 56, 60, 64,
    100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
    149, 153, 157, 161, 165, 169, 173, 177,
};

// The PHY's own xtal selector byte (phy_chip_set_chan forwards the same byte to
// the PLL setter). Reading it beats hardcoding a board assumption.
constexpr int kParamXtalSel = 49;
constexpr int kParamTrim = 30;   // int16 crystal trim phy_chip_set_chan forwards
constexpr int kParamMhz = 714;   // uint16 slot phy_set_rfpll_freq stores MHz in
constexpr int kParam5g = 42;     // 5G-band flag

uint8_t xtalSelector() { return phy_param[kParamXtalSel]; }

int16_t freqTrim() {
    int16_t trim = 0;
    memcpy(&trim, phy_param + kParamTrim, sizeof trim);
    return trim;
}

// Retune in stages. `skip` bit 0 drops the 5G clock calibration, bit 1 the
// frequency-memory update. Skipping them is what makes ~1 kHz per seat
// possible; the receiver can show a brief spike just after such a hop, which is
// the trade the burst filter is for.
void hopStaged(uint32_t mhz, uint8_t skip) {
    uint32_t sdm[3] = {0, 0, 0};
    uint16_t mhz16 = (uint16_t)mhz;
    memcpy(phy_param + kParamMhz, &mhz16, sizeof mhz16);

    phy_rfpll_set_freq(mhz, xtalSelector(), freqTrim(), sdm);
    phy_write_rfpll_sdm(sdm);
    phy_set_freq_i2c_new(mhz);
    phy_restart_cal();
    const bool is5g = phy_param[kParam5g] != 0;
    phy_i2c_sdm_en(is5g ? 1 : 0);
    if (is5g && !(skip & 1)) phy_ckgen_5g_cal(mhz);
    if ((is5g || mhz > 2482) && !(skip & 2)) phy_freq_mem_change_5g_(sdm);
}

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
#if !SF_BENCH_NO_PROMISC
    if (esp_wifi_set_promiscuous(true) != ESP_OK) return false;
#endif

    phy_check_sigrssi_en(true);
    lastWifiChannel_ = -1;
    ready_ = true;
    return true;
}

bool C5HopRadio::hop(uint16_t mhz) {
    if (!ready_) return false;
    int16_t offset = 0;
    uint8_t ch = nearestWifiChannel(mhz, &offset);

#if !C5_FAST_HOP
    // Driver-first path, kept for A/B measurement: esp_wifi_set_channel costs
    // roughly a millisecond, which is the whole per-seat budget at 1 kHz.
    if ((int)ch != lastWifiChannel_) {
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        lastWifiChannel_ = ch;
    }
#endif
    (void)offset;
    (void)ch;

#if C5_FAST_HOP == 3
    hopStaged((uint32_t)mhz, (uint8_t)C5_HOP_SKIP);
#elif C5_FAST_HOP == 2
    // Fastest path: synthesizer only (phy_set_rf_freq_offset). RFChannelSel
    // runs the full channel-change sequence and costs ~700 us, which caps the
    // sampler near 140 Hz/seat.
    phy_set_rf_freq_offset(xtalSelector(), (uint32_t)mhz, 0);
#else
    // Move the PHY onto the exact race frequency. The RX chain follows the PHY,
    // so this is the only retune the sampler needs per seat.
    RFChannelSel((int)mhz, 0);
#endif

#if C5_SIGRSSI_EACH_HOP
    // Only needed when the driver moved the channel (or after a driver reset).
    phy_check_sigrssi_en(true);
#endif
    return true;
}

int16_t C5HopRadio::readDbm() const { return ready_ ? phy_get_sigrssi() : -128; }

uint8_t C5HopRadio::toRssi(int16_t dbm) {
    return dbmToRssi(dbm, C5_RSSI_DBM_FLOOR, C5_RSSI_DBM_CEIL);
}

}  // namespace mp

#endif  // SF_RECEIVER_C5_NATIVE
