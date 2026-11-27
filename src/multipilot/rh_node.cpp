#include "multipilot/bench_counters.h"
#include "multipilot/rh_node.h"

#include <stdio.h>
#include <string.h>

namespace mp {

namespace {

constexpr uint8_t READ_ADDRESS = 0x00;
constexpr uint8_t READ_FREQUENCY = 0x03;
constexpr uint8_t TEST_RX_REGISTER = 0x04;
constexpr uint8_t READ_LAP_STATS = 0x05;
constexpr uint8_t READ_LAP_PASS_STATS = 0x0D;
constexpr uint8_t READ_LAP_EXTREMUMS = 0x0E;
constexpr uint8_t READ_RHFEAT_FLAGS = 0x11;
constexpr uint8_t READ_REVISION_CODE = 0x22;
constexpr uint8_t READ_NODE_RSSI_PEAK = 0x23;
constexpr uint8_t READ_NODE_RSSI_NADIR = 0x24;
constexpr uint8_t READ_ENTER_AT_LEVEL = 0x31;
constexpr uint8_t READ_EXIT_AT_LEVEL = 0x32;
constexpr uint8_t READ_TIME_MILLIS = 0x33;
constexpr uint8_t READ_MULTINODE_COUNT = 0x39;
constexpr uint8_t READ_CURNODE_INDEX = 0x3A;
constexpr uint8_t READ_NODE_SLOTIDX = 0x3C;
constexpr uint8_t READ_FW_VERSION = 0x3D;
constexpr uint8_t READ_FW_BUILDDATE = 0x3E;
constexpr uint8_t READ_FW_BUILDTIME = 0x3F;
constexpr uint8_t READ_FW_PROCTYPE = 0x40;

constexpr uint8_t WRITE_FREQUENCY = 0x51;
constexpr uint8_t WRITE_ENTER_AT_LEVEL = 0x71;
constexpr uint8_t WRITE_EXIT_AT_LEVEL = 0x72;
constexpr uint8_t SEND_STATUS_MESSAGE = 0x75;
constexpr uint8_t FORCE_END_CROSSING = 0x78;
constexpr uint8_t WRITE_CURNODE_INDEX = 0x7A;
// Bench only: sampler progress (passes, samples). No RotorHazard host
// sends this, so it cannot collide with the standard protocol.
constexpr uint8_t READ_BENCH_COUNTERS = 0x7E;
// Bench only: one seat's last raw reading. Reply: int16 dBm, uint16 MHz,
// uint32 sample count. The seat index arrives as the command's data byte.
constexpr uint8_t READ_BENCH_SAMPLE = 0x7F;

// Payload bytes the host sends with a write (checksum excluded).
int writePayloadSize(uint8_t command) {
    switch (command) {
        case WRITE_FREQUENCY: return 2;
        case WRITE_ENTER_AT_LEVEL: return 1;
        case WRITE_EXIT_AT_LEVEL: return 1;
        case SEND_STATUS_MESSAGE: return 2;
        case FORCE_END_CROSSING: return 1;
        case WRITE_CURNODE_INDEX: return 1;
        default: return -1;
    }
}

bool isReadCommand(uint8_t command) {
    switch (command) {
        case READ_ADDRESS:
        case READ_FREQUENCY:
        case TEST_RX_REGISTER:
        case READ_LAP_STATS:
        case READ_LAP_PASS_STATS:
        case READ_LAP_EXTREMUMS:
        case READ_RHFEAT_FLAGS:
        case READ_REVISION_CODE:
        case READ_NODE_RSSI_PEAK:
        case READ_NODE_RSSI_NADIR:
        case READ_ENTER_AT_LEVEL:
        case READ_EXIT_AT_LEVEL:
        case READ_TIME_MILLIS:
        case READ_MULTINODE_COUNT:
        case READ_CURNODE_INDEX:
        case READ_NODE_SLOTIDX:
        case READ_FW_VERSION:
        case READ_FW_BUILDDATE:
        case READ_FW_BUILDTIME:
        case READ_FW_PROCTYPE:
        case READ_BENCH_COUNTERS:
        case READ_BENCH_SAMPLE:
            return true;
        default:
            return false;
    }
}

}  // namespace

void RhNode::begin(unsigned seatCount, uint8_t defaultEnter, uint8_t defaultExit,
                   WriteFn write, void* writeCtx, FreqFn freq, void* freqCtx) {
    engine_.begin(seatCount, defaultEnter, defaultExit);
    write_ = write;
    writeCtx_ = writeCtx;
    freq_ = freq;
    freqCtx_ = freqCtx;
    selected_ = 0;
    command_ = 0;
    needed_ = 0;
    received_ = 0;
    collecting_ = false;
    memset(payload_, 0, sizeof payload_);
}

void RhNode::setIdentity(const char* version, const char* proctype,
                         const char* buildDate, const char* buildTime) {
    snprintf(version_, sizeof version_, "%s", version ? version : "");
    snprintf(proctype_, sizeof proctype_, "%s", proctype ? proctype : "");
    snprintf(buildDate_, sizeof buildDate_, "%s", buildDate ? buildDate : "");
    snprintf(buildTime_, sizeof buildTime_, "%s", buildTime ? buildTime : "");
}

void RhNode::feed(uint8_t byte, uint32_t nowUs) {
    if (!collecting_) {
        command_ = byte;
        int payloadSize = writePayloadSize(byte);
        if (payloadSize >= 0) {
            // A write: payload then checksum.
            needed_ = (uint8_t)payloadSize;
            received_ = 0;
            collecting_ = true;
            return;
        }
        if (isReadCommand(byte)) {
            handleRead(nowUs);
            return;
        }
        // Unknown byte (bad baud, stray output on the port): ignore it.
        command_ = 0;
        return;
    }

    if (received_ < needed_) {
        payload_[received_++] = byte;
        return;
    }

    // Checksum byte.
    uint8_t sum = 0;
    for (uint8_t i = 0; i < needed_; ++i) sum = (uint8_t)(sum + payload_[i]);
    collecting_ = false;
    received_ = 0;
    if (sum == byte) {
        handleWrite(nowUs);
    }
    command_ = 0;
}

void RhNode::send(const uint8_t* payload, unsigned len) {
    if (write_ == nullptr) return;
    uint8_t frame[24];
    if (len + 1 > sizeof frame) return;
    uint8_t sum = 0;
    for (unsigned i = 0; i < len; ++i) {
        frame[i] = payload[i];
        sum = (uint8_t)(sum + payload[i]);
    }
    frame[len] = sum;
    write_(writeCtx_, frame, len + 1);
}

void RhNode::sendByte(uint8_t value) { send(&value, 1); }

void RhNode::sendText16(const char* text) {
    uint8_t block[16] = {};
    size_t len = text ? strlen(text) : 0;
    if (len > 16) len = 16;
    if (len > 0) memcpy(block, text, len);
    send(block, 16);
}

void RhNode::handleRead(uint32_t nowUs) {
    SeatView view = engine_.view(selected_, nowUs);
    switch (command_) {
        case READ_ADDRESS:
            sendByte(0x08);
            break;

        case READ_FREQUENCY: {
            uint8_t b[2] = {(uint8_t)(view.frequency >> 8), (uint8_t)view.frequency};
            send(b, 2);
            break;
        }

        case TEST_RX_REGISTER: {
            // The seat holds its frequency when it is on, in the band plan and
            // the sampler has actually seen it. An off seat answers 0.
            bool ok = view.enabled && view.frequency >= kMinMhz && view.frequency <= kMaxMhz;
            sendByte(ok ? 1 : 0);
            break;
        }

        case READ_LAP_PASS_STATS: {
            uint8_t b[8];
            b[0] = view.lap;
            b[1] = (uint8_t)(view.msSincePass >> 8);
            b[2] = (uint8_t)view.msSincePass;
            b[3] = view.rssi;
            b[4] = view.peak;
            b[5] = view.passPeak;
            b[6] = (uint8_t)(view.loopUs >> 8);
            b[7] = (uint8_t)view.loopUs;
            send(b, 8);
            break;
        }

        case READ_LAP_EXTREMUMS: {
            uint8_t b[8] = {};
            SeatEngine::Extremum ext;
            bool have = engine_.popExtremum(selected_, &ext);
            uint8_t flags = 0;
            if (view.crossing) flags |= 0x01;
            if (have && ext.isPeak) flags |= 0x02;
            b[0] = flags;
            b[1] = view.passNadir;
            b[2] = view.nadir;
            if (have) {
                uint32_t since = (nowUs - ext.firstUs) / 1000;
                uint32_t held = (ext.lastUs >= ext.firstUs) ? (ext.lastUs - ext.firstUs) / 1000 : 0;
                if (since > 65535) since = 65535;
                if (held > 65535) held = 65535;
                b[3] = ext.rssi;
                b[4] = (uint8_t)(since >> 8);
                b[5] = (uint8_t)since;
                b[6] = (uint8_t)(held >> 8);
                b[7] = (uint8_t)held;
            }
            send(b, 8);
            break;
        }

        case READ_RHFEAT_FLAGS: {
            uint8_t b[2] = {0, 0};
            send(b, 2);
            break;
        }

        case READ_REVISION_CODE: {
            uint8_t b[2] = {kVerifyByte, kApiLevel};
            send(b, 2);
            break;
        }

        case READ_NODE_RSSI_PEAK:
            sendByte(view.peak);
            break;

        case READ_NODE_RSSI_NADIR:
            sendByte(view.nadir);
            break;

        case READ_ENTER_AT_LEVEL:
            sendByte(view.enter);
            break;

        case READ_EXIT_AT_LEVEL:
            sendByte(view.exit);
            break;

        case READ_TIME_MILLIS: {
            uint32_t ms = nowUs / 1000;
            uint8_t b[4] = {(uint8_t)(ms >> 24), (uint8_t)(ms >> 16), (uint8_t)(ms >> 8), (uint8_t)ms};
            send(b, 4);
            break;
        }

        case READ_BENCH_SAMPLE: {
            unsigned seat = selected_;
            if (seat >= (unsigned)mp::kBenchSeats) seat = 0;
            int16_t dbm = mp::g_last_raw[seat];
            uint16_t mhz = mp::g_last_freq[seat];
            uint32_t count = mp::g_last_count[seat];
            uint8_t b[8] = {
                (uint8_t)((uint16_t)dbm >> 8), (uint8_t)dbm,
                (uint8_t)(mhz >> 8), (uint8_t)mhz,
                (uint8_t)(count >> 24), (uint8_t)(count >> 16),
                (uint8_t)(count >> 8), (uint8_t)count};
            send(b, 8);
            break;
        }

        case READ_BENCH_COUNTERS: {
            uint32_t passes = mp::g_sampler_passes;
            uint32_t samples = mp::g_sampler_samples;
            uint8_t b[8] = {
                (uint8_t)(passes >> 24), (uint8_t)(passes >> 16),
                (uint8_t)(passes >> 8), (uint8_t)passes,
                (uint8_t)(samples >> 24), (uint8_t)(samples >> 16),
                (uint8_t)(samples >> 8), (uint8_t)samples};
            send(b, 8);
            break;
        }

        case READ_MULTINODE_COUNT:
            sendByte((uint8_t)engine_.seats());
            break;

        case READ_CURNODE_INDEX:
            sendByte((uint8_t)selected_);
            break;

        case READ_NODE_SLOTIDX:
            // One processor holds every seat, so its own slot index is 0.
            sendByte(0);
            break;

        case READ_FW_VERSION:
            sendText16(version_);
            break;

        case READ_FW_BUILDDATE:
            sendText16(buildDate_);
            break;

        case READ_FW_BUILDTIME:
            sendText16(buildTime_);
            break;

        case READ_FW_PROCTYPE:
            sendText16(proctype_);
            break;

        default:
            // READ_LAP_STATS (0x05) and anything else: no reply at API 36.
            break;
    }
}

void RhNode::handleWrite(uint32_t nowUs) {
    switch (command_) {
        case WRITE_CURNODE_INDEX: {
            uint8_t slot = payload_[0];
            if (slot < engine_.seats()) selected_ = slot;
            // No reply. RotorHazard's write_block never reads one - it writes
            // and moves on - so answering here leaves two bytes in the host's
            // input buffer and every later read comes back shifted (the Python
            // server then fails with "Unable to read frequency value"). The
            // host confirms the switch with READ_CURNODE_INDEX instead, and an
            // out-of-range slot answers with the current one so it retries
            // rather than silently reading the wrong seat.
            break;
        }

        case WRITE_FREQUENCY: {
            uint16_t mhz = (uint16_t)((payload_[0] << 8) | payload_[1]);
            engine_.setFrequency(selected_, mhz, nowUs);
            if (freq_) freq_(freqCtx_, selected_, engine_.frequency(selected_));
            break;
        }

        case WRITE_ENTER_AT_LEVEL:
            engine_.setThresholds(selected_, payload_[0], engine_.exit(selected_));
            break;

        case WRITE_EXIT_AT_LEVEL:
            engine_.setThresholds(selected_, engine_.enter(selected_), payload_[0]);
            break;

        case FORCE_END_CROSSING:
            engine_.forceEndCrossing(selected_);
            break;

        case SEND_STATUS_MESSAGE:
        default:
            break;
    }
}

}  // namespace mp
