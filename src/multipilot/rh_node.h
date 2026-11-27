// RotorHazard node endpoint for the eight-seat C5 build.
//
// Portable: no Arduino, no ESP-IDF. The firmware, the host unit tests and the
// pty conformance harness all drive this same code. Framing and command
// semantics follow RotorHazard's src/interface/RHInterface.py and the Rust
// port's rust/server/src/serial.rs.
//
// API 36 adds TEST_RX_REGISTER (0x04): the host writes a frequency and then
// asks whether the receiver really holds it. A seat that is off, or tuned to a
// frequency this radio cannot reach, answers 0 and the server warns instead of
// pretending the seat is ready.
#ifndef MP_RH_NODE_H
#define MP_RH_NODE_H

#include <stdint.h>

#include "multipilot/seat_engine.h"

namespace mp {

class RhNode {
public:
    static constexpr uint8_t kApiLevel = 36;
    static constexpr uint8_t kVerifyByte = 0x25;
    // Frequencies the C5 receiver can tune (the RotorHazard band plan range).
    static constexpr uint16_t kMinMhz = 5645;
    static constexpr uint16_t kMaxMhz = 5945;

    // Bytes for the host, and radio retune notifications (0 = seat off).
    using WriteFn = void (*)(void* ctx, const uint8_t* data, unsigned len);
    using FreqFn = void (*)(void* ctx, unsigned seat, uint16_t mhz);

    void begin(unsigned seatCount, uint8_t defaultEnter, uint8_t defaultExit,
               WriteFn write, void* writeCtx, FreqFn freq, void* freqCtx);
    void setIdentity(const char* version, const char* proctype,
                     const char* buildDate, const char* buildTime);

    // One byte from the host.
    void feed(uint8_t byte, uint32_t nowUs);

    // One filtered RSSI level per seat, from the radio side.
    void sample(unsigned seat, uint8_t level, uint32_t nowUs) {
        engine_.sample(seat, level, nowUs);
    }

    SeatEngine& engine() { return engine_; }
    const SeatEngine& engine() const { return engine_; }
    unsigned selected() const { return selected_; }
    unsigned seats() const { return engine_.seats(); }

private:
    void handleRead(uint32_t nowUs);
    void handleWrite(uint32_t nowUs);
    void send(const uint8_t* payload, unsigned len);
    void sendByte(uint8_t value);
    void sendText16(const char* text);

    SeatEngine engine_;

    WriteFn write_ = nullptr;
    void* writeCtx_ = nullptr;
    FreqFn freq_ = nullptr;
    void* freqCtx_ = nullptr;

    // framing
    uint8_t command_ = 0;
    uint8_t payload_[3] = {};
    uint8_t needed_ = 0;
    uint8_t received_ = 0;
    bool collecting_ = false;

    unsigned selected_ = 0;

    char version_[17] = "2.0.4";
    char proctype_[17] = "ESP32-C5";
    char buildDate_[17] = "";
    char buildTime_[17] = "";
};

}  // namespace mp

#endif  // MP_RH_NODE_H
