#pragma once

#include <stdint.h>

// Sampler progress, readable over the node link with the bench command 0x7E so
// the sampling rate can be measured without a host setting frequencies. Nothing
// in RotorHazard sends 0x7E, and the counters cost two increments per tick.
namespace mp {

extern volatile uint32_t g_sampler_passes;
extern volatile uint32_t g_sampler_samples;

// Last unfiltered reading per seat, with the frequency it was taken on and a
// running count, so a host can watch the raw RSSI stream and see what a retune
// does to the first samples. Bench only; nothing reads these in a normal build.
constexpr int kBenchSeats = 8;
extern volatile int16_t g_last_raw[kBenchSeats];
extern volatile uint16_t g_last_freq[kBenchSeats];
extern volatile uint32_t g_last_count[kBenchSeats];

}  // namespace mp
