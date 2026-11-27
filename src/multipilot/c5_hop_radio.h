// Fast PLL-only retune for the eight-seat sweep.
//
// The single-seat C5 receiver (receiver/c5_radio.*) takes the slow path: park
// the Wi-Fi driver on the nearest channel, then RFChannelSel + one tracking
// pass, and throw away 15 ms of readings. That is fine at one frequency and
// impossible at eight: 8 x 15 ms is eight samples a second per seat.
//
// A multi-node board has to hop: one PLL retune, a few hundred microseconds of
// settling, one reading, next seat. phy_set_rf_freq_offset() is the libphy call
// that does exactly that retune (the same one Espressif's periodic tracking
// uses). It is not public API, which is the same risk class as the four
// functions already used in c5_radio.cpp - see docs/ESP32_C5.md, and re-check
// the symbol when the framework pin moves:
//
//   L=~/.platformio/packages/framework-arduinoespressif32-libs/esp32c5/ld/libphy.a
//   riscv32-esp-elf-nm -g --defined-only $L | grep -E "rf_freq_offset|sigrssi"
//
// Facts taken from the reference implementation's published notes (settle
// window, that the PLL is settled when the call returns); no code from it is
// used, and none of it is copied here.
#ifndef MP_C5_HOP_RADIO_H
#define MP_C5_HOP_RADIO_H

#include <stdint.h>

#include <Arduino.h>  // first: defines CONFIG_IDF_TARGET_* used by config.h
#include "config/config.h"

#if SF_RECEIVER_C5_NATIVE

namespace mp {

class C5HopRadio {
public:
    // Wi-Fi bring-up once (receive only: station mode that never connects,
    // 5 GHz, promiscuous so the receive chain stays on). Returns false when
    // the radio cannot start; readings then stay at the noise floor.
    bool begin();

    // PLL-only retune. Returns false when the radio is not up.
    bool hop(uint16_t mhz);

    // One raw reading in dBm at the current frequency.
    int16_t readDbm() const;

    // RotorHazard's 0-255 scale, linear in dB.
    static uint8_t toRssi(int16_t dbm);

    // Time the PLL needs after a hop before a reading means anything.
    // Measured on hardware; see docs/ROTORHAZARD_8PILOT.md.
    static constexpr uint32_t kHopSettleUs = C5_HOP_SETTLE_US;

private:
    bool ready_ = false;
};

}  // namespace mp

#endif  // SF_RECEIVER_C5_NATIVE
#endif  // MP_C5_HOP_RADIO_H
