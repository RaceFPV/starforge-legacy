// Host stand-in for an eight-seat StarForge C5 node, on a PTY.
//
// Compiles the exact portable protocol + seat engine the firmware runs, so the
// real RotorHazard server can be tested against it with no hardware:
//
//   ./pty_node --port-file /tmp/rh.pty --state /tmp/rh.state
//
// Writes the PTY slave path to --port-file, then serves until killed. --state
// gets a small JSON summary on every change (programmed frequencies, samples
// per seat, passes emitted) so a test can assert what the server did.
//
// The synthetic signal: every enabled seat sits at the noise floor; seat 0 gets
// one clean pass every two seconds (80 ms rise, 20 ms plateau, 60 ms fall) so a
// running race records laps.
#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "multipilot/rh_node.h"

namespace {

constexpr unsigned kSeats = 8;

uint32_t nowUs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull);
}

struct State {
    int fd = -1;
    const char* path = nullptr;
    mp::RhNode* node = nullptr;
    unsigned programmed = 0;
    unsigned passes = 0;
};

void writeState(State& st) {
    FILE* f = fopen(st.path, "w");
    if (f == nullptr) return;
    fprintf(f, "{\"programmed\":%u,\"passes\":%u,\"seats\":[", st.programmed, st.passes);
    for (unsigned i = 0; i < st.node->seats(); ++i) {
        fprintf(f, "%s%u", i ? "," : "", st.node->engine().frequency(i));
    }
    fprintf(f, "]}\n");
    fclose(f);
}

void onWrite(void* ctx, const uint8_t* data, unsigned len) {
    State* st = (State*)ctx;
    ssize_t written = ::write(st->fd, data, len);
    (void)written;
}

void onFrequency(void* ctx, unsigned seat, uint16_t mhz) {
    State* st = (State*)ctx;
    st->programmed++;
    writeState(*st);
    fprintf(stderr, "seat %u -> %u MHz\n", seat, mhz);
}

}  // namespace

int main(int argc, char** argv) {
    const char* portFile = "/tmp/rh-node.pipe";
    const char* stateFile = "/tmp/rh-node.state";
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--port-file") == 0 && i + 1 < argc) portFile = argv[++i];
        else if (strcmp(argv[i], "--state") == 0 && i + 1 < argc) stateFile = argv[++i];
    }

    int master = -1;
    int slave = -1;
    char slaveName[128] = {};
    if (openpty(&master, &slave, slaveName, nullptr, nullptr) != 0) {
        perror("openpty");
        return 1;
    }
    struct termios tio;
    if (tcgetattr(slave, &tio) == 0) {
        cfmakeraw(&tio);
        tcsetattr(slave, TCSANOW, &tio);
    }
    // Keep the slave open: closing it would make the master read EOF when the
    // server closes its side.
    fcntl(master, F_SETFL, O_NONBLOCK);

    FILE* pf = fopen(portFile, "w");
    if (pf == nullptr) return 1;
    fprintf(pf, "%s\n", slaveName);
    fclose(pf);

    State st;
    st.fd = master;
    st.path = stateFile;

    mp::RhNode node;
    st.node = &node;
    node.begin(kSeats, 120, 100, onWrite, &st, onFrequency, &st);
    node.setIdentity("2.1.0", "ESP32-C5", "Nov 27 2026", "00:00:00");
    writeState(st);
    fprintf(stderr, "node on %s\n", slaveName);

    // Synthetic sampling: one reading per seat every millisecond, round-robin.
    // Seat 0 repeats a clean pass every two seconds (80 ms rise, 20 ms
    // plateau, 60 ms fall) so a running race records laps.
    unsigned seat = 0;
    uint32_t nextSample = nowUs();
    uint32_t cycleStart = nextSample;
    uint32_t lastPhase = 0;

    while (true) {
        // Commands from the host.
        uint8_t buf[256];
        ssize_t n;
        while ((n = ::read(master, buf, sizeof buf)) > 0) {
            for (ssize_t i = 0; i < n; ++i) {
                node.feed(buf[i], nowUs());
            }
        }

        uint32_t t = nowUs();
        if ((int32_t)(t - nextSample) >= 0) {
            nextSample += 1000;

            uint32_t phase = (t - cycleStart) % 2000000u;
            if (phase < lastPhase) {
                st.passes++;
                writeState(st);
            }
            lastPhase = phase;

            uint8_t level = (uint8_t)(20 + (t % 3));  // floor, a little noise
            if (seat == 0) {
                if (phase < 80000u) {
                    level = (uint8_t)(20 + phase / 1000 * 180 / 80);
                } else if (phase < 100000u) {
                    level = 200;
                } else if (phase < 160000u) {
                    level = (uint8_t)(200 - (phase - 100000u) / 1000 * 180 / 60);
                }
            }
            node.sample(seat, level, t);
            seat = (seat + 1) % kSeats;
        }

        usleep(200);
    }
    return 0;
}
