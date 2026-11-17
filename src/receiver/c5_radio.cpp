#include "receiver/c5_radio.h"

#if SF_RECEIVER_C5_NATIVE

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_timer.h>
#include "filters/burst_filter.h"

// ---------------------------------------------------------------------------
// ESP32-C5 PHY library entry points.
//
// These are exported by Espressif's libphy.a (Apache-2.0) but are not in a
// public header. Signatures were read from the library itself
// (arduino-esp32 3.3.x / ESP-IDF 5.5) and must be re-checked when the
// framework is upgraded: see docs/ESP32_C5.md.
//
//   phy_check_sigrssi_en(en)   puts the baseband RSSI register into
//                              continuous signal-RSSI mode
//   phy_get_sigrssi()          that register as a signed dBm byte
//   RFChannelSel(mhz, bw)      full PHY channel change; above channel 14 the
//                              PHY takes the frequency in MHz
//   phy_param_track_tot(w, b)  one PLL/temperature tracking pass, the same
//                              call ESP-IDF's periodic PHY tracking makes
//   phy_11p_set(en, mode)      802.11p (10 MHz) receive settings
// ---------------------------------------------------------------------------
extern "C" {
void phy_check_sigrssi_en(bool enable);
int8_t phy_get_sigrssi(void);
void RFChannelSel(int chan, uint8_t bw);
void phy_param_track_tot(bool wifi_track, bool ble_track);
void phy_11p_set(int enable, int mode);
}

namespace {

// 5 GHz Wi-Fi channels the C5's driver accepts. Centre = 5000 + 5 * ch MHz.
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

// One filter instance; only the sampler touches it.
BurstFilter<C5_FILTER_OPEN_SAMPLES, C5_FILTER_CLOSE_SAMPLES> s_filter;

}  // namespace

bool C5Radio::begin(uint16_t freq_mhz) {
  // Default event loop: the Wi-Fi driver posts events to it. It may already
  // exist; that is fine.
  esp_err_t err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  cfg.nvs_enable = 0;  // StarForge's NVS is left alone; nothing to store.
  if (esp_wifi_init(&cfg) != ESP_OK) return false;
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return false;

  // Allow every 5 GHz channel. This device only receives: it never
  // connects, scans or transmits.
  wifi_country_t country = {};
  country.cc[0] = '0';
  country.cc[1] = '1';
  country.cc[2] = ' ';
  country.schan = 1;
  country.nchan = 13;
  country.max_tx_power = 0;
  country.policy = WIFI_COUNTRY_POLICY_MANUAL;
  country.wifi_5g_channel_mask = 0x1FFFFFFE;  // bits 1-28: channels 36-177
  esp_wifi_set_country(&country);

  if (esp_wifi_start() != ESP_OK) return false;
  esp_wifi_set_band_mode(WIFI_BAND_MODE_5G_ONLY);
  esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
  esp_wifi_set_ps(WIFI_PS_NONE);  // keep the receiver powered

  // Promiscuous mode keeps the receive chain running. No callback is
  // installed and only management frames are passed up, so the CPU cost is
  // negligible.
  wifi_promiscuous_filter_t filt = {};
  filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&filt);
  if (esp_wifi_set_promiscuous(true) != ESP_OK) return false;

  ok_ = true;
  tune(freq_mhz);

  esp_timer_create_args_t args = {};
  args.callback = &C5Radio::sampleCallback;
  args.arg = this;
  args.dispatch_method = ESP_TIMER_TASK;
  args.name = "c5rssi";
  args.skip_unhandled_events = true;
  esp_timer_handle_t handle = nullptr;
  if (esp_timer_create(&args, &handle) != ESP_OK) {
    ok_ = false;
    return false;
  }
  timer_ = handle;
  esp_timer_start_periodic(handle, 1000000 / C5_SAMPLE_RATE_HZ);
  return true;
}

uint16_t C5Radio::tune(uint16_t freq_mhz) {
  if (!ok_) return freq_mhz;

  tuning_ = true;

  // Park the Wi-Fi driver on the nearest channel first, so its own state is
  // a 5 GHz channel next to the target.
  int16_t offset = 0;
  uint8_t ch = nearestWifiChannel(freq_mhz, &offset);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

  // Then move the radio onto the exact frequency if the channel centre is
  // not close enough (R8 and E6-E8 have no Wi-Fi channel at all; R3 is
  // 12 MHz from one).
  if (abs(offset) > C5_WIFI_TUNE_TOLERANCE_MHZ) {
    RFChannelSel((int)freq_mhz, 0);
    phy_param_track_tot(true, false);
  }

#if C5_ENABLE_11P
  phy_11p_set(freq_mhz >= C5_11P_FROM_MHZ ? 1 : 0, 0);
#endif

  // A channel change reprograms the baseband; re-enable signal-RSSI mode
  // every time rather than relying on it surviving.
  phy_check_sigrssi_en(true);

  freq_mhz_ = freq_mhz;
  needs_reset_ = true;
  settle_until_us_ = (uint32_t)esp_timer_get_time() + C5_SETTLE_MS * 1000UL;
  tuning_ = false;
  return freq_mhz;
}

void C5Radio::waitSettled(uint32_t timeout_ms) {
  uint32_t start = millis();
  while ((tuning_ || needs_reset_) && ok_ && (millis() - start) < timeout_ms) {
    vTaskDelay(1);
  }
}

void C5Radio::sampleCallback(void* arg) {
  static_cast<C5Radio*>(arg)->sample();
}

void C5Radio::sample() {
  if (tuning_) return;
  if ((int32_t)((uint32_t)esp_timer_get_time() - settle_until_us_) < 0) return;

  int16_t raw = phy_get_sigrssi();

  if (needs_reset_) {
    // First reading on the new channel: start the filter from it, so the
    // previous channel's level does not leak through the filter windows.
    s_filter.reset(raw);
    needs_reset_ = false;
  }

  int16_t filtered = s_filter.push(raw);
  dbm_ = filtered;
  rssi_ = dbmToRssi(filtered, C5_RSSI_DBM_FLOOR, C5_RSSI_DBM_CEIL);
}

#endif  // SF_RECEIVER_C5_NATIVE
