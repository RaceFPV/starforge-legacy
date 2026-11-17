#ifndef C5_RADIO_H
#define C5_RADIO_H

// Native ESP32-C5 receiver: the C5's own 5 GHz radio replaces the RX5808.
//
// The radio is started receive-only (Wi-Fi station, never connecting or
// scanning, 5 GHz only, promiscuous so the receiver stays on), tuned to the
// requested FPV frequency, and its live signal-strength reading is sampled
// at 1 kHz, filtered (filters/burst_filter.h) and scaled to 0-255.
//
// Only compiled into the ESP32-C5 build with SF_RECEIVER_C5_NATIVE=1.

#include <Arduino.h>  // first: defines CONFIG_IDF_TARGET_* used by config.h
#include <stdint.h>
#include "config/config.h"

#if SF_RECEIVER_C5_NATIVE

class C5Radio {
public:
  // Start Wi-Fi in receive-only mode and the 1 kHz sampler. Returns false if
  // the radio could not be started; rssi() then reads 0.
  bool begin(uint16_t freq_mhz);

  // Tune to any frequency in MHz. Returns the frequency now tuned.
  uint16_t tune(uint16_t freq_mhz);

  // Block until the post-tune settling time has passed (at most timeout_ms).
  void waitSettled(uint32_t timeout_ms);

  // Latest filtered reading on RotorHazard's 0-255 scale.
  uint8_t rssi() const { return rssi_; }

  // Latest filtered reading in dBm.
  int16_t dbm() const { return dbm_; }

  bool ok() const { return ok_; }

private:
  static void sampleCallback(void* arg);
  void sample();

  volatile bool ok_ = false;
  volatile bool tuning_ = false;
  volatile bool needs_reset_ = true;
  volatile uint32_t settle_until_us_ = 0;
  volatile uint8_t rssi_ = 0;
  volatile int16_t dbm_ = -128;
  uint16_t freq_mhz_ = 0;
  void* timer_ = nullptr;
};

#endif  // SF_RECEIVER_C5_NATIVE
#endif  // C5_RADIO_H
