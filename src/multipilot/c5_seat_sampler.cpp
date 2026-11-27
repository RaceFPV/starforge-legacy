#include <Arduino.h>  // first: defines CONFIG_IDF_TARGET_* used by config.h
#include "multipilot/c5_seat_sampler.h"

#if SF_SEAT_COUNT > 1 && SF_RECEIVER_C5_NATIVE
#include <esp_timer.h>
#include <math.h>

namespace mp {

bool C5SeatSampler::begin(RhNode* node) {
    node_ = node;
    if (!radio_.begin()) {
        // Keep the protocol alive: the seats will report the noise floor and
        // the register test still answers, so the host sees a node that is
        // present but deaf instead of a dead port.
        return false;
    }
    return true;
}

void C5SeatSampler::tick() {
    if (node_ == nullptr) return;
    const unsigned seats = node_->seats();
    if (seats == 0) return;

    unsigned sampled = 0;
    unsigned scanned = 0;
    while (sampled < kSeatsPerTick && scanned < seats) {
        unsigned seat = next_;
        next_ = (next_ + 1 >= seats) ? 0 : next_ + 1;
        scanned++;

        uint16_t freq = node_->engine().frequency(seat);
        SeatFilter& filter = filters_[seat];
        if (freq == 0) {
            filter.started = false;
            continue;
        }
        if (freq != filter.freq) {
            filter.freq = freq;
            filter.started = false;
        }

        radio_.hop(freq);
        // A retune reprograms the baseband; give the PLL and the RSSI register
        // time to settle before the reading is trusted.
        delayMicroseconds(C5_HOP_SETTLE_US);
        int16_t dbm = radio_.readDbm();

        if (!filter.started) {
            filter.burst.reset(dbm);
            filter.kalman = KalmanFilter();
            filter.started = true;
        }
        int16_t filtered = filter.burst.push(dbm);
        // The Kalman filter works on unsigned values; shift dBm into range.
        float smoothed = filter.kalman.filter((uint16_t)(filtered + 128));
        int16_t level = (int16_t)lroundf(smoothed) - 128;

        node_->sample(seat, C5HopRadio::toRssi(level), (uint32_t)esp_timer_get_time());
        sampled++;
    }

    if (sampled == 0) {
        // No seat is enabled: do not spin the CPU.
        vTaskDelay(1);
    }
}

}  // namespace mp

#endif  // SF_SEAT_COUNT > 1 && SF_RECEIVER_C5_NATIVE
