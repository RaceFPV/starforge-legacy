// Host tests for the eight-seat engine and the API 36 protocol endpoint.
// No hardware, no Arduino: exactly the code the firmware runs.
#include <stdio.h>
#include <string.h>

#include "multipilot/rh_node.h"
#include "multipilot/seat_engine.h"

using namespace mp;

static int failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static constexpr uint32_t MS = 1000;

namespace {

// Drives the level of one seat from the floor, at 1 ms resolution.
struct Driver {
    SeatEngine& engine;
    unsigned seat;
    uint32_t nowUs;
    void hold(uint32_t ms, uint8_t level) {
        for (uint32_t i = 0; i < ms; ++i) {
            engine.sample(seat, level, nowUs);
            nowUs += MS;
        }
    }
    void ramp(uint32_t ms, uint8_t from, uint8_t to) {
        // Signed maths: an unsigned division here wraps a falling ramp and
        // produces 254-level samples.
        for (uint32_t i = 0; i < ms; ++i) {
            int32_t v = (int32_t)from +
                        ((int32_t)to - (int32_t)from) * (int32_t)i /
                            (int32_t)(ms ? ms : 1);
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            engine.sample(seat, (uint8_t)v, nowUs);
            nowUs += MS;
        }
    }
};

struct Capture {
    uint8_t bytes[256];
    unsigned len = 0;
};
void captureWrite(void* ctx, const uint8_t* data, unsigned len) {
    Capture* cap = (Capture*)ctx;
    for (unsigned i = 0; i < len && cap->len < sizeof cap->bytes; ++i) cap->bytes[cap->len++] = data[i];
}

struct WriteRec {
    unsigned seat = 99;
    uint16_t mhz = 0;
    unsigned calls = 0;
};
void captureFreq(void* ctx, unsigned seat, uint16_t mhz) {
    WriteRec* rec = (WriteRec*)ctx;
    rec->seat = seat;
    rec->mhz = mhz;
    rec->calls++;
}

uint8_t checksum(const uint8_t* data, unsigned len) {
    uint8_t sum = 0;
    for (unsigned i = 0; i < len; ++i) sum = (uint8_t)(sum + data[i]);
    return sum;
}

// The two payload bytes a WRITE_FREQUENCY carries.
void freqPayload(uint16_t mhz, uint8_t out[2]) {
    out[0] = (uint8_t)(mhz >> 8);
    out[1] = (uint8_t)(mhz & 0xFF);
}

struct NodeUnderTest {
    Capture cap;
    WriteRec freq;
    RhNode node;
};

RhNode makeNode(NodeUnderTest* t, unsigned seats = 8) {
    RhNode node;
    node.begin(seats, 120, 100, captureWrite, &t->cap, captureFreq, &t->freq);
    node.setIdentity("2.1.0", "ESP32-C5", "Nov 27 2026", "00:00:00");
    return node;
}

void feedCmd(RhNode& node, uint8_t cmd, uint32_t nowUs) { node.feed(cmd, nowUs); }

void feedWrite(RhNode& node, uint8_t cmd, const uint8_t* payload, unsigned len, uint32_t nowUs) {
    node.feed(cmd, nowUs);
    for (unsigned i = 0; i < len; ++i) node.feed(payload[i], nowUs);
    node.feed(checksum(payload, len), nowUs);
}

}  // namespace

static void testFrequenciesAndSeatRouting() {
    NodeUnderTest t;
    RhNode& node = t.node;
    node = makeNode(&t);

    // Seat 3 gets a frequency; the write must reach the radio callback.
    uint8_t slot3 = 3;
    feedWrite(node, 0x7A, &slot3, 1, 1 * MS);
    // The echo is 2 bytes: slot then checksum.
    CHECK(t.cap.len == 2);
    CHECK(t.cap.bytes[0] == 3);
    CHECK(t.cap.bytes[1] == checksum(t.cap.bytes, 1));
    CHECK(node.selected() == 3);

    t.cap.len = 0;
    uint8_t f[2]; freqPayload(5800, f);
    feedWrite(node, 0x51, f, 2, 2 * MS);
    CHECK(t.freq.seat == 3);
    CHECK(t.freq.mhz == 5800);
    CHECK(node.engine().frequency(3) == 5800);
    CHECK(node.engine().frequency(0) == 0);

    // Reading the frequency returns the selected seat's value only.
    t.cap.len = 0;
    feedCmd(node, 0x03, 3 * MS);
    CHECK(t.cap.len == 3);
    CHECK((t.cap.bytes[0] << 8 | t.cap.bytes[1]) == 5800);

    uint8_t slot0 = 0;
    feedWrite(node, 0x7A, &slot0, 1, 4 * MS);
    t.cap.len = 0;
    feedCmd(node, 0x03, 5 * MS);
    CHECK((t.cap.bytes[0] << 8 | t.cap.bytes[1]) == 0);
}

static void testMultinodeAndRegister() {
    NodeUnderTest t;
    RhNode& node = t.node;
    node = makeNode(&t);

    t.cap.len = 0;
    feedCmd(node, 0x39, 1 * MS);
    CHECK(t.cap.len == 2);
    CHECK(t.cap.bytes[0] == 8);

    t.cap.len = 0;
    feedCmd(node, 0x22, 2 * MS);
    CHECK(t.cap.len == 3);
    CHECK(t.cap.bytes[0] == 0x25);
    CHECK(t.cap.bytes[1] == RhNode::kApiLevel);

    // Register test: off seat answers 0, a tuned seat answers 1, an
    // unreachable frequency answers 0.
    t.cap.len = 0;
    feedCmd(node, 0x04, 3 * MS);
    CHECK(t.cap.bytes[0] == 0);

    uint8_t f[2]; freqPayload(5800, f);
    feedWrite(node, 0x51, f, 2, 4 * MS);
    t.cap.len = 0;
    feedCmd(node, 0x04, 5 * MS);
    CHECK(t.cap.bytes[0] == 1);

    uint8_t bad[2]; freqPayload(5000, bad);
    feedWrite(node, 0x51, bad, 2, 6 * MS);
    t.cap.len = 0;
    feedCmd(node, 0x04, 7 * MS);
    CHECK(t.cap.bytes[0] == 0);

    // 1111 disables the seat and reports frequency 0.
    uint8_t off[2]; freqPayload(1111, off);
    feedWrite(node, 0x51, off, 2, 8 * MS);
    CHECK(!node.engine().enabled(0));
    CHECK(node.engine().frequency(0) == 0);
}

static void testBadChecksumIsIgnored() {
    NodeUnderTest t;
    RhNode& node = t.node;
    node = makeNode(&t);
    uint8_t f[2]; freqPayload(5800, f);
    node.feed(0x51, 1 * MS);
    node.feed(f[0], 1 * MS);
    node.feed(f[1], 1 * MS);
    node.feed(0x00, 1 * MS);  // wrong checksum
    CHECK(node.engine().frequency(0) == 0);
}

static void testPassTiming() {
    SeatEngine engine;
    engine.begin(8, 120, 100);
    engine.setFrequency(0, 5800, 0);
    Driver d{engine, 0, 200 * MS};  // past the 100 ms settle

    d.hold(300, 20);            // quiet floor
    d.ramp(80, 20, 220);        // a quad flying in, not a jump
    uint32_t plateauStart = d.nowUs;
    d.hold(20, 220);            // peak plateau
    uint32_t plateauEnd = d.nowUs;
    d.ramp(60, 220, 20);        // gone
    d.hold(400, 20);            // the pass ends 300 ms after it fell under ExitAt

    SeatView v = engine.view(0, d.nowUs);
    CHECK(v.lap == 1);
    CHECK(v.passPeak >= 219);
    // The pass ended 300 ms after the level fell; the 250 ms crossing hold is
    // still running at this point.
    CHECK(v.crossing);

    // Timed at the middle of the peak plateau.
    uint32_t passUs = plateauStart + (plateauEnd - plateauStart) / 2;
    uint32_t expectedMs = (d.nowUs - passUs) / 1000;
    uint32_t delta = v.msSincePass > expectedMs ? v.msSincePass - expectedMs
                                                : expectedMs - v.msSincePass;
    CHECK(delta <= 2);

    d.hold(250, 20);
    CHECK(!engine.view(0, d.nowUs).crossing);
}

static void testCrossingFlagHold() {
    SeatEngine engine;
    engine.begin(8, 120, 100);
    engine.setFrequency(0, 5800, 0);
    Driver d{engine, 0, 200 * MS};

    d.hold(300, 20);
    d.ramp(80, 20, 220);
    d.hold(20, 220);
    d.ramp(60, 220, 20);
    uint32_t droppedUs = d.nowUs;

    // The level stayed under ExitAt: the pass ends one merge later, and the
    // crossing flag stays up for 250 ms after that, so a 50 Hz poll sees it.
    d.hold(320, 20);
    CHECK(engine.view(0, droppedUs + 320 * MS).crossing);
    CHECK(engine.view(0, droppedUs + 320 * MS).lap == 1);
    d.hold(300, 20);
    CHECK(!engine.view(0, droppedUs + 620 * MS).crossing);
}

static void testMultipathDipIsOnePass() {
    SeatEngine engine;
    engine.begin(8, 120, 100);
    engine.setFrequency(0, 5800, 0);
    Driver d{engine, 0, 200 * MS};

    d.hold(300, 20);
    d.ramp(60, 20, 200);
    d.hold(20, 200);
    d.hold(150, 90);   // dips under exit for 150 ms < merge 300 ms
    d.hold(20, 200);   // back up: same pass
    d.hold(40, 200);
    d.hold(400, 20);   // stays down: the pass ends
    d.hold(50, 20);

    CHECK(engine.view(0, d.nowUs).lap == 1);
}

static void testVtxSwitchOnIsNotAPass() {
    SeatEngine engine;
    engine.begin(8, 120, 100);
    engine.setFrequency(0, 5800, 0);
    Driver d{engine, 0, 200 * MS};

    d.hold(300, 20);   // quiet floor, long enough to be a floor
    d.hold(40, 200);   // VTX switches on and off: a jump, not a pass
    d.hold(400, 20);

    CHECK(engine.view(0, d.nowUs).lap == 0);

    // A real pass afterwards still counts.
    d.ramp(80, 20, 220);
    d.hold(20, 220);
    d.ramp(60, 220, 20);
    d.hold(400, 20);
    CHECK(engine.view(0, d.nowUs).lap == 1);
}

static void testShortPassStillCounts() {
    SeatEngine engine;
    engine.begin(8, 120, 100);
    engine.setFrequency(0, 5800, 0);
    Driver d{engine, 0, 200 * MS};

    d.hold(300, 20);
    d.ramp(120, 20, 200);  // a real rise, well past Reject::riseUs
    d.hold(2, 200);        // a 2 ms pass
    d.hold(400, 20);      // past the merge window

    CHECK(engine.view(0, d.nowUs).lap == 1);
}

static void testDisabledSeatIsSilent() {
    SeatEngine engine;
    engine.begin(8, 120, 100);
    Driver d{engine, 2, 0};

    d.hold(50, 20);
    SeatView v = engine.view(2, d.nowUs);
    CHECK(!v.enabled);
    CHECK(v.rssi == 1);
    CHECK(v.lap == 0);
}

static void testExtremumOrder() {
    SeatEngine engine;
    engine.begin(8, 120, 100);
    engine.setFrequency(0, 5800, 0);
    Driver d{engine, 0, 200 * MS};

    d.hold(10, 30);    // nadir first
    d.hold(10, 200);   // then a peak

    SeatEngine::Extremum ext;
    CHECK(engine.popExtremum(0, &ext));
    CHECK(!ext.isPeak);
    CHECK(ext.rssi == 30);
    CHECK(engine.popExtremum(0, &ext));
    CHECK(ext.isPeak);
    CHECK(ext.rssi == 200);
    CHECK(!engine.popExtremum(0, &ext));
}

static void testPassStatsLayout() {
    NodeUnderTest t;
    RhNode& node = t.node;
    node = makeNode(&t);
    uint8_t f[2]; freqPayload(5800, f);
    feedWrite(node, 0x51, f, 2, 1 * MS);

    // Drive a pass through the node's sample path, seat 0 selected.
    uint32_t nowUs = 200 * MS;
    for (int i = 0; i < 300; ++i) { node.sample(0, 20, nowUs); nowUs += MS; }
    for (int i = 0; i < 60; ++i) { node.sample(0, 20 + i * 3, nowUs); nowUs += MS; }
    for (int i = 0; i < 20; ++i) { node.sample(0, 220, nowUs); nowUs += MS; }
    for (int i = 0; i < 60; ++i) { node.sample(0, 220 - i * 3, nowUs); nowUs += MS; }
    for (int i = 0; i < 400; ++i) { node.sample(0, 20, nowUs); nowUs += MS; }

    t.cap.len = 0;
    feedCmd(node, 0x0D, nowUs);
    CHECK(t.cap.len == 9);            // 8 bytes + checksum
    CHECK(t.cap.bytes[0] == 1);       // lap id
    CHECK(t.cap.bytes[3] >= 1);       // current RSSI must never be 0
    CHECK(t.cap.bytes[5] >= 200);     // pass peak
    CHECK(checksum(t.cap.bytes, 8) == t.cap.bytes[8]);

    t.cap.len = 0;
    feedCmd(node, 0x0E, nowUs);
    CHECK(t.cap.len == 9);
    CHECK(checksum(t.cap.bytes, 8) == t.cap.bytes[8]);
}

int main() {
    testFrequenciesAndSeatRouting();
    testMultinodeAndRegister();
    testBadChecksumIsIgnored();
    testPassTiming();
    testCrossingFlagHold();
    testMultipathDipIsOnePass();
    testVtxSwitchOnIsNotAPass();
    testShortPassStillCounts();
    testDisabledSeatIsSilent();
    testExtremumOrder();
    testPassStatsLayout();

    if (failures == 0) {
        printf("PASS: seat engine and API 36 endpoint\n");
        return 0;
    }
    printf("%d check(s) failed\n", failures);
    return 1;
}
