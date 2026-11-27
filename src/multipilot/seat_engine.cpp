#include "multipilot/seat_engine.h"

namespace mp {

namespace {
bool isOff(uint16_t mhz) { return mhz == 0 || mhz == 1111; }
uint8_t clampLevel(int level) {
    if (level < 1) return 1;
    if (level > 254) return 254;
    return (uint8_t)level;
}
}  // namespace

void SeatEngine::begin(unsigned seatCount, uint8_t defaultEnter, uint8_t defaultExit) {
    count_ = seatCount == 0 ? 1 : (seatCount > kMaxSeats ? kMaxSeats : seatCount);
    programmed_ = 0;
    for (unsigned i = 0; i < kMaxSeats; ++i) {
        seats_[i] = Seat();
        seats_[i].enter = defaultEnter;
        seats_[i].exit = defaultExit;
    }
}

void SeatEngine::setFrequency(unsigned seat, uint16_t mhz, uint32_t nowUs) {
    if (seat >= count_) return;
    Seat& s = seats_[seat];
    s.frequency = isOff(mhz) ? 0 : mhz;
    if (s.frequency != 0) programmed_++;
    resetDetection(s, nowUs);
}

void SeatEngine::setThresholds(unsigned seat, uint8_t enter, uint8_t exit) {
    if (seat >= count_) return;
    seats_[seat].enter = enter;
    seats_[seat].exit = exit;
}

void SeatEngine::forceEndCrossing(unsigned seat) {
    if (seat >= count_) return;
    Seat& s = seats_[seat];
    s.crossing = false;
    s.ending = false;
    s.shownCrossing = false;
    s.provisional = false;
}

uint16_t SeatEngine::frequency(unsigned seat) const {
    return seat < count_ ? seats_[seat].frequency : 0;
}

bool SeatEngine::enabled(unsigned seat) const {
    return seat < count_ && seats_[seat].frequency != 0;
}

uint8_t SeatEngine::enter(unsigned seat) const {
    return seat < count_ ? seats_[seat].enter : 0;
}

uint8_t SeatEngine::exit(unsigned seat) const {
    return seat < count_ ? seats_[seat].exit : 0;
}

void SeatEngine::resetDetection(Seat& s, uint32_t nowUs) {
    s.crossing = false;
    s.ending = false;
    s.shownCrossing = false;
    s.peakFirstUs = s.peakLastUs = s.passUs = 0;
    s.quiet = false;
    s.quietSinceUs = 0;
    s.floorLastUs = 0;
    s.provisional = false;
    s.provisionalUntilUs = 0;
    s.settleUntilUs = nowUs + kFrequencySettleUs;
    s.passPeak = 1;
    s.passNadir = 254;
    for (unsigned b = 0; b < kShowBuckets; ++b) {
        s.bucketUsed[b] = false;
        s.bucketMax[b] = 0;
        s.bucketId[b] = 0;
    }
    s.pendingPeak = ExtremumRec();
    s.pendingNadir = ExtremumRec();
}

void SeatEngine::showLevel(Seat& s, uint8_t level, uint32_t nowUs) {
    uint32_t id = nowUs / kShowBucketUs;
    unsigned slot = id % kShowBuckets;
    if (!s.bucketUsed[slot] || s.bucketId[slot] != id) {
        s.bucketUsed[slot] = true;
        s.bucketId[slot] = id;
        s.bucketMax[slot] = level;
    } else if (level > s.bucketMax[slot]) {
        s.bucketMax[slot] = level;
    }
}

uint8_t SeatEngine::shownLevel(const Seat& s, uint32_t nowUs) const {
    uint32_t newest = nowUs / kShowBucketUs;
    uint8_t best = 1;
    for (unsigned b = 0; b < kShowBuckets; ++b) {
        if (!s.bucketUsed[b]) continue;
        // Only buckets inside the last 250 ms count.
        uint32_t id = s.bucketId[b];
        if ((newest - id) > kShowBuckets) continue;
        if (s.bucketMax[b] > best) best = s.bucketMax[b];
    }
    return best;
}

void SeatEngine::recordExtrema(Seat& s, uint8_t level, uint32_t nowUs) {
    // Node peak/nadir since reset.
    if (level > s.peak) s.peak = level;
    if (level < s.nadir) s.nadir = level;

    // Extremums pending since the host's last read: track max and min, and the
    // span each was held.
    if (!s.pendingPeak.used || level > s.pendingPeak.rssi) {
        s.pendingPeak.used = true;
        s.pendingPeak.rssi = level;
        s.pendingPeak.firstUs = nowUs;
        s.pendingPeak.lastUs = nowUs;
    } else if (level == s.pendingPeak.rssi) {
        s.pendingPeak.lastUs = nowUs;
    }
    if (!s.pendingNadir.used || level < s.pendingNadir.rssi) {
        s.pendingNadir.used = true;
        s.pendingNadir.rssi = level;
        s.pendingNadir.firstUs = nowUs;
        s.pendingNadir.lastUs = nowUs;
    } else if (level == s.pendingNadir.rssi) {
        s.pendingNadir.lastUs = nowUs;
    }
    if (!s.crossing && s.frequency != 0) {
        // Pass nadir: the lowest level since the last pass ended.
        if (level < s.passNadir) s.passNadir = level;
    }
}

void SeatEngine::sample(unsigned seat, uint8_t level, uint32_t nowUs) {
    if (seat >= count_) return;
    Seat& s = seats_[seat];
    if (s.frequency == 0) return;

    if (s.haveSample) {
        uint32_t delta = nowUs - s.lastSampleUs;
        s.loopUs = delta > 65535 ? 65535 : (uint16_t)delta;
    }
    s.haveSample = true;
    s.lastSampleUs = nowUs;

    // The radio is not settled yet: keep the bookkeeping, decide nothing.
    if ((int32_t)(nowUs - s.settleUntilUs) < 0) {
        return;
    }

    level = clampLevel(level);

    // Floor tracking: a pass that crosses EnterAt within Reject::riseUs of
    // leaving the floor, after at least Reject::quietUs at the floor, is a
    // suspect VTX switch-on. It is still detected and timed normally, but if
    // the level collapses before Reject::holdUs it is dropped as a lap.
    bool atFloor = level <= (uint16_t)s.nadir + reject_.margin;
    if (atFloor) {
        if (!s.quiet) {
            s.quiet = true;
            s.quietSinceUs = nowUs;
        }
        s.floorLastUs = nowUs;
    } else {
        s.quiet = false;
    }

    if (s.provisional) {
        if (atFloor) {
            // Collapsed inside the window: a VTX switched on, not a pass.
            s.provisional = false;
            s.crossing = false;
            s.ending = false;
            s.passPeak = 1;
            s.passNadir = 254;
            return;
        }
        if ((int32_t)(nowUs - s.provisionalUntilUs) >= 0) {
            // It lasted: a real signal after all.
            s.provisional = false;
        }
    }

    showLevel(s, level, nowUs);
    s.rssi = shownLevel(s, nowUs);
    recordExtrema(s, level, nowUs);
    detect(s, level, nowUs);
}

void SeatEngine::detect(Seat& s, uint8_t level, uint32_t nowUs) {
    if (!s.crossing) {
        if (level >= s.enter) {
            s.crossing = true;
            s.ending = false;
            s.passPeak = level;
            s.passNadir = 254;
            s.peakFirstUs = nowUs;
            s.peakLastUs = nowUs;
            bool fastRise = (nowUs - s.floorLastUs) <= reject_.riseUs;
            bool longQuiet = s.floorLastUs != 0 &&
                             (s.floorLastUs - s.quietSinceUs) >= reject_.quietUs;
            s.provisional = fastRise && longQuiet;
            s.provisionalUntilUs = nowUs + reject_.holdUs;
        }
        return;
    }

    // Track the peak plateau: a new highest sample starts a new plateau, and
    // samples within one count of the peak extend it.
    if (level > s.passPeak) {
        s.passPeak = level;
        s.peakFirstUs = nowUs;
        s.peakLastUs = nowUs;
    } else if (level + 1 >= s.passPeak) {
        s.peakLastUs = nowUs;
    }

    if (level <= s.exit) {
        if (!s.ending) {
            s.ending = true;
            s.endingUs = nowUs;
        } else if ((nowUs - s.endingUs) >= detect_.mergeUs) {
            endPass(s, nowUs);
        }
    } else {
        // Back over the exit threshold: the same pass, multipath dip over.
        s.ending = false;
    }
}

void SeatEngine::endPass(Seat& s, uint32_t nowUs) {
    if (s.provisional) {
        // A VTX blink that crossed EnterAt and fell back inside Reject::holdUs.
        s.provisional = false;
        s.crossing = false;
        s.ending = false;
        s.passPeak = 1;
        s.passNadir = 254;
        return;
    }
    // Timed at the middle of the peak plateau, on the node's clock. The host
    // applies its own round-trip correction, so no poll delay is added here.
    s.passUs = s.peakFirstUs + (s.peakLastUs - s.peakFirstUs) / 2;
    s.lap = (uint8_t)(s.lap + 1);
    s.crossing = false;
    s.ending = false;
    s.shownCrossing = true;
    s.shownUntilUs = nowUs + kCrossingHoldUs;
    // The next pass's nadir starts from here.
    s.passNadir = 254;
}

SeatView SeatEngine::view(unsigned seat, uint32_t nowUs) const {
    SeatView v;
    if (seat >= count_) return v;
    const Seat& s = seats_[seat];
    v.frequency = s.frequency;
    v.enter = s.enter;
    v.exit = s.exit;
    v.rssi = s.frequency == 0 ? 1 : shownLevel(s, nowUs);
    if (v.rssi == 0) v.rssi = 1;
    v.peak = s.peak;
    v.nadir = s.nadir;
    v.passPeak = s.passPeak;
    v.passNadir = s.passNadir;
    v.lap = s.lap;
    // A live crossing, or the 250 ms hold that follows a pass so a 50 Hz
    // poll cannot miss it.
    v.crossing = s.crossing ||
                 (s.shownCrossing && (int32_t)(s.shownUntilUs - nowUs) > 0);
    v.enabled = s.frequency != 0;
    if (s.passUs != 0) {
        uint32_t since = (nowUs - s.passUs) / 1000;
        v.msSincePass = since > 65535 ? 65535 : (uint16_t)since;
    }
    v.loopUs = s.loopUs;
    return v;
}

bool SeatEngine::popExtremum(unsigned seat, Extremum* out) {
    if (seat >= count_ || out == nullptr) return false;
    Seat& s = seats_[seat];
    bool havePeak = s.pendingPeak.used;
    bool haveNadir = s.pendingNadir.used;
    if (!havePeak && !haveNadir) return false;

    bool takePeak;
    if (havePeak && haveNadir) {
        // Oldest first: the host draws its history in order.
        takePeak = s.pendingPeak.firstUs <= s.pendingNadir.firstUs;
    } else {
        takePeak = havePeak;
    }

    const ExtremumRec& rec = takePeak ? s.pendingPeak : s.pendingNadir;
    out->rssi = rec.rssi;
    out->firstUs = rec.firstUs;
    out->lastUs = rec.lastUs;
    out->isPeak = takePeak;
    (takePeak ? s.pendingPeak : s.pendingNadir) = ExtremumRec();
    return true;
}

}  // namespace mp
