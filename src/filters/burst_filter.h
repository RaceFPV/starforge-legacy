#ifndef BURST_FILTER_H
#define BURST_FILTER_H

// Streaming morphological filter for radio RSSI in dBm.
//
// Used by the native ESP32-C5 receiver. The C5's live signal reading has two
// kinds of short artefact that an RX5808 does not show:
//   - Background Wi-Fi packets: short upward spikes, a few ms long.
//   - The VTX's own reading: short downward dips, a few ms long.
//
// Two standard grey-scale morphology steps remove both:
//   1. Opening  (running min, then running max) over OPEN_W samples removes
//      upward pulses shorter than OPEN_W samples. Longer signals pass.
//   2. Closing  (running max, then running min) over CLOSE_W samples fills
//      downward gaps shorter than CLOSE_W samples. Longer drops pass.
//
// Both steps delay rising and falling edges by the same amount
// ((OPEN_W - 1) + (CLOSE_W - 1) samples), so a gate pass keeps its shape and
// its peak is shifted by a constant, which cancels out of lap times.
//
// Plain C++ with no hardware dependencies, so it is unit-tested on a PC
// (test/unit/test_burst_filter.cpp).

#include <stdint.h>

template <uint8_t N>
class RunningExtreme {
public:
  void reset(int16_t fill) {
    for (uint8_t i = 0; i < N; i++) buf_[i] = fill;
    idx_ = 0;
  }
  // Push a sample; return the min (wantMax = false) or max of the last N.
  int16_t push(int16_t v, bool wantMax) {
    buf_[idx_] = v;
    idx_ = (uint8_t)((idx_ + 1) % N);
    int16_t r = buf_[0];
    for (uint8_t i = 1; i < N; i++) {
      if (wantMax ? (buf_[i] > r) : (buf_[i] < r)) r = buf_[i];
    }
    return r;
  }

private:
  int16_t buf_[N] = {};
  uint8_t idx_ = 0;
};

template <uint8_t OPEN_W, uint8_t CLOSE_W>
class BurstFilter {
  static_assert(OPEN_W >= 1 && CLOSE_W >= 1, "window must be at least 1");

public:
  // Total delay of an edge through the filter, in samples.
  static constexpr uint8_t kDelaySamples = (OPEN_W - 1) + (CLOSE_W - 1);

  BurstFilter() { reset(-128); }

  // Start again from a steady level (after a retune, for example).
  void reset(int16_t level) {
    openMin_.reset(level);
    openMax_.reset(level);
    closeMax_.reset(level);
    closeMin_.reset(level);
    out_ = level;
  }

  int16_t push(int16_t dbm) {
    int16_t opened = openMax_.push(openMin_.push(dbm, false), true);
    out_ = closeMin_.push(closeMax_.push(opened, true), false);
    return out_;
  }

  int16_t value() const { return out_; }

private:
  RunningExtreme<OPEN_W> openMin_;
  RunningExtreme<OPEN_W> openMax_;
  RunningExtreme<CLOSE_W> closeMax_;
  RunningExtreme<CLOSE_W> closeMin_;
  int16_t out_;
};

// Map a level in dBm to the 0-255 RSSI scale RotorHazard expects. Linear in
// dB, the same shape as an RX5808's log-detector output.
static inline uint8_t dbmToRssi(int16_t dbm, int16_t floorDbm, int16_t ceilDbm) {
  if (ceilDbm <= floorDbm) return 0;
  if (dbm <= floorDbm) return 0;
  if (dbm >= ceilDbm) return 255;
  int32_t span = ceilDbm - floorDbm;
  return (uint8_t)(((int32_t)(dbm - floorDbm) * 255 + span / 2) / span);
}

#endif  // BURST_FILTER_H
