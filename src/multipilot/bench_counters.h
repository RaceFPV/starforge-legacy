#pragma once

#include <stdint.h>

// Sampler progress, readable over the node link with the bench command 0x7E so
// the sampling rate can be measured without a host setting frequencies. Nothing
// in RotorHazard sends 0x7E, and the counters cost two increments per tick.
namespace mp {

extern volatile uint32_t g_sampler_passes;
extern volatile uint32_t g_sampler_samples;

}  // namespace mp
