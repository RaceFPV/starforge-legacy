// Drives the eight-seat engine from one hopping radio.
//
// The radio sees one frequency at a time, so this visits the enabled seats in
// turn: retune, settle, one reading, next seat. Everything happens in loop(),
// between serial polls, so the protocol handler is never blocked for longer
// than one hop - there is no second task and no sharing to get wrong on a
// single-core chip.
//
// Tuning knobs (config.h): C5_HOP_SETTLE_US is how long a reading is ignored
// after a retune; kSeatsPerTick is how many seats are visited before the loop
// services the serial port again. Measure both on the bench - see
// docs/ROTORHAZARD_8PILOT.md phase 0.
#ifndef MP_C5_SEAT_SAMPLER_H
#define MP_C5_SEAT_SAMPLER_H

#include <stdint.h>

#include <Arduino.h>  // first: defines CONFIG_IDF_TARGET_* used by config.h
#include "config/config.h"

#if SF_SEAT_COUNT > 1 && SF_RECEIVER_C5_NATIVE

#include "filters/burst_filter.h"
#include "filters/kalman.h"
#include "multipilot/c5_hop_radio.h"
#include "multipilot/rh_node.h"

namespace mp {

class C5SeatSampler {
public:
    // Must be called after RhNode::begin().
    bool begin(RhNode* node);

    // Visit up to kSeatsPerTick enabled seats. Call as often as possible.
    void tick();

    // Seats visited per tick: two hops is about 1-2 ms of loop time, small
    // enough that the host's serial reads stay timely.
    static constexpr unsigned kSeatsPerTick = 2;

private:
    struct SeatFilter {
        BurstFilter<C5_MP_FILTER_OPEN, C5_MP_FILTER_CLOSE> burst;
        KalmanFilter kalman;
        uint16_t freq = 0;
        bool started = false;
    };

    RhNode* node_ = nullptr;
    C5HopRadio radio_;
    SeatFilter filters_[SeatEngine::kMaxSeats];
    unsigned next_ = 0;
};

}  // namespace mp

#endif  // SF_SEAT_COUNT > 1 && SF_RECEIVER_C5_NATIVE
#endif  // MP_C5_SEAT_SAMPLER_H
