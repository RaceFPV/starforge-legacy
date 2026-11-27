// Eight-seat RotorHazard timing engine for one ESP32-C5.
//
// Portable: no Arduino, no ESP-IDF, no radio. The radio side hands in one
// filtered RSSI level per seat; this decides what RotorHazard reads. That is
// what lets the host tests and the pty conformance harness drive the exact
// code the firmware runs.
//
// The detection rules are the behaviour RotorHazard itself expects from a
// multi-node board (and what C5MK documents): a pass ends only after the level
// has stayed under the exit threshold for a while (multipath), it is timed at
// the middle of its peak plateau, the crossing flag stays visible for 250 ms so
// a 50 Hz poll cannot miss a short pass, and a VTX switching on - a jump off the
// noise floor in a few ms - is not a pass.
#ifndef MP_SEAT_ENGINE_H
#define MP_SEAT_ENGINE_H

#include <stdint.h>

namespace mp {

// A VTX powering on or changing channel jumps off the floor in a few ms; a
// quad flying in needs hundreds. A crossing that starts with such a jump after
// at least quietUs at the floor is hidden for holdUs and dropped if it ends
// sooner. "The floor" is within margin of the seat's nadir.
struct Reject {
    uint32_t riseUs = 30000;
    uint32_t holdUs = 90000;
    uint32_t quietUs = 100000;
    uint8_t margin = 16;
};

// A pass in a small room dips under the exit threshold and back (multipath).
// A crossing therefore ends only once the level has stayed under it for
// mergeUs; back over the enter threshold sooner is the same pass.
struct Detect {
    uint32_t mergeUs = 300000;
};

// The 8-byte READ_LAP_PASS_STATS block and what feeds it.
struct SeatView {
    uint16_t frequency = 0;
    uint8_t enter = 120;
    uint8_t exit = 100;
    uint8_t rssi = 1;      // highest level of about the last 250 ms
    uint8_t peak = 1;      // node peak since reset (jumps excluded)
    uint8_t nadir = 254;   // node nadir since reset
    uint8_t passPeak = 1;  // peak of the last pass
    uint8_t passNadir = 254;
    uint8_t lap = 0;       // wraps at 256
    bool crossing = false;
    bool enabled = false;
    uint16_t msSincePass = 0;
    uint16_t loopUs = 0;   // interval since this seat's previous sample
};

class SeatEngine {
public:
    static constexpr unsigned kMaxSeats = 8;
    // The crossing flag stays set this long after a pass ends.
    static constexpr uint32_t kCrossingHoldUs = 250000;
    // A frequency change restarts the filters: no crossing starts for this long.
    static constexpr uint32_t kFrequencySettleUs = 100000;
    // Shown level: the highest sample of each 25 ms bucket over 250 ms.
    static constexpr unsigned kShowBuckets = 10;
    static constexpr uint32_t kShowBucketUs = 25000;

    void begin(unsigned seatCount, uint8_t defaultEnter, uint8_t defaultExit);

    unsigned seats() const { return count_; }

    // -- settings (protocol writes) --------------------------------------
    // 0 or 1111 disables the seat. Any change clears detection state, because
    // the first samples after a retune are meaningless.
    void setFrequency(unsigned seat, uint16_t mhz, uint32_t nowUs);
    void setThresholds(unsigned seat, uint8_t enter, uint8_t exit);
    void forceEndCrossing(unsigned seat);

    uint16_t frequency(unsigned seat) const;
    bool enabled(unsigned seat) const;
    uint8_t enter(unsigned seat) const;
    uint8_t exit(unsigned seat) const;

    // -- sampling ---------------------------------------------------------
    void sample(unsigned seat, uint8_t level, uint32_t nowUs);

    // -- reads ------------------------------------------------------------
    SeatView view(unsigned seat, uint32_t nowUs) const;

    // Oldest extremum since the last pop, or false when none is pending.
    struct Extremum {
        uint8_t rssi = 0;
        uint32_t firstUs = 0;   // ms since it started, computed by the caller
        uint32_t lastUs = 0;
        bool isPeak = false;
    };
    bool popExtremum(unsigned seat, Extremum* out);

    // Test hook: how many seats were given a frequency since begin().
    unsigned programmedSeats() const { return programmed_; }

private:
    struct ExtremumRec {
        bool used = false;
        uint8_t rssi = 0;
        uint32_t firstUs = 0;
        uint32_t lastUs = 0;
    };

    struct Seat {
        uint16_t frequency = 0;
        uint8_t enter = 120;
        uint8_t exit = 100;

        // shown-level buckets
        uint8_t bucketMax[kShowBuckets] = {};
        uint32_t bucketId[kShowBuckets] = {};
        bool bucketUsed[kShowBuckets] = {};

        uint8_t rssi = 1;
        uint8_t peak = 1;
        uint8_t nadir = 254;
        uint8_t passPeak = 1;
        uint8_t passNadir = 254;
        uint8_t lap = 0;

        bool crossing = false;
        uint32_t peakFirstUs = 0;
        uint32_t peakLastUs = 0;
        uint32_t passUs = 0;
        bool ending = false;
        uint32_t endingUs = 0;
        bool shownCrossing = false;
        uint32_t shownUntilUs = 0;

        uint32_t settleUntilUs = 0;
        uint32_t lastSampleUs = 0;
        bool haveSample = false;
        uint16_t loopUs = 0;

        // Floor tracking and the provisional-pass window (see Reject).
        bool quiet = false;
        uint32_t quietSinceUs = 0;
        uint32_t floorLastUs = 0;
        bool provisional = false;
        uint32_t provisionalUntilUs = 0;

        ExtremumRec pendingPeak;
        ExtremumRec pendingNadir;
    };

    Seat seats_[kMaxSeats];
    unsigned count_ = 1;
    unsigned programmed_ = 0;
    Reject reject_;
    Detect detect_;

    void resetDetection(Seat& seat, uint32_t nowUs);
    void recordExtrema(Seat& seat, uint8_t level, uint32_t nowUs);
    void showLevel(Seat& seat, uint8_t level, uint32_t nowUs);
    uint8_t shownLevel(const Seat& seat, uint32_t nowUs) const;
    void detect(Seat& seat, uint8_t level, uint32_t nowUs);
    void endPass(Seat& seat, uint32_t nowUs);
};

}  // namespace mp

#endif  // MP_SEAT_ENGINE_H
