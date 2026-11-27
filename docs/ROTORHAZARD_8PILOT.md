# 8-pilot mode: one ESP32-C5 as eight RotorHazard nodes

Status: **implemented and host-tested; bench validation outstanding.** Target
firmware: StarForge Legacy 2.1.0 (C5 native builds). Reference server:
RotorHazard Rust `rh-server-rust-v4.6.0-dev.5` (the protocol is identical in the
Python server).

What exists now:

| Piece | Where |
|---|---|
| Seat state, detection, pass timing, extremums | `src/multipilot/seat_engine.{h,cpp}` |
| Framing + the API 36 protocol surface | `src/multipilot/rh_node.{h,cpp}` |
| Hopping receiver (fast retune, per-seat filters) | `src/multipilot/c5_hop_radio.*`, `c5_seat_sampler.*` |
| Firmware wiring (multi build only) | `src/main.cpp` (`SF_SEAT_COUNT > 1`) |
| Host unit tests | `test/unit/test_seat_engine.cpp` (`bash test/unit/run.sh`) |
| Conformance against the real server | `test/tools/test_rust_server_8seats.sh` |

The single-seat targets are untouched: `SF_SEAT_COUNT` defaults to 1 and the
`TimingCore`/`NodeMode` path still compiles and runs for every non-C5 board.
A C5 built with `-DSF_SEAT_COUNT=1` keeps the old single-seat behaviour.

## 1. Goal

A single ESP32-C5 running this firmware must appear to RotorHazard as **eight
independent timing nodes on one USB serial port**, so a race with eight pilots
needs one board, one cable and no plugin. RotorHazard must be able to:

1. discover eight seats on the port at boot,
2. give each seat its own frequency and enter/exit levels,
3. read each seat's RSSI, peak/nadir history and pass timing,
4. record laps from all eight seats in the same race.

Non-goals: changing the RX5808 targets (they stay one seat per board), adding a
dashboard, or supporting more than eight seats. Eight is RotorHazard's practical
limit and what the reference C5MK firmware exposes.

## 2. Where the code stands (verified against this tree)

| Piece | Today | Needed |
|---|---|---|
| `src/node_mode.cpp` | single seat; API **35**; `READ_MULTINODE_COUNT` returns 1; `READ_CURNODE_INDEX`/`READ_NODE_SLOTIDX` return 0; `WRITE_CURNODE_INDEX` unhandled; no `TEST_RX_REGISTER` (0x04) | N seats, slot routing, API 36, 0x04 |
| `READ_LAP_PASS_STATS` (0x0D) + `READ_LAP_EXTREMUMS` (0x0E) | already API-32 shaped: 8 + 8 bytes, byte-for-byte what the Rust decoder expects | unchanged per seat, cached |
| `src/timing_core.*` | one `TimingState`, one frequency, one pair of extremum buffers, 1 kHz sampler | per-seat state (×8) |
| `src/receiver/c5_radio.*` | tunes one frequency; `C5_SETTLE_MS` 15 ms after a retune; 1 kHz sampling | hop schedule across enabled seats |
| `src/main.cpp` | `timing.process(); node.handleSerialInput();` | seat bank + sampler task |
| tests | host unit tests (`test/unit/run.sh`), mock server (`test/tools/mock_rotorhazard.py`) | protocol conformance against the real Rust server |

The two existing block layouts are already correct and **must not change**:

```
READ_LAP_PASS_STATS (8 bytes, API >= 32)
  0     lap id (u8, wraps at 256)
  1-2   ms since last pass (u16, clamped at 65535)
  3     current RSSI (u8, 1..254 - 0 makes the server drop the whole read)
  4     node peak RSSI since reset
  5     last pass peak RSSI
  6-7   sample interval / loop time (u16, microseconds)

READ_LAP_EXTREMUMS (8 bytes, API >= 32)
  0     flags: bit0 = crossing active, bit1 = this reply carries a peak
  1     pass nadir (RSSI since the last pass ended)
  2     node nadir (RSSI since reset)
  3     extremum RSSI
  4-5   ms since the extremum started (u16)
  6-7   duration the extremum was held (u16; server derives start-duration)
```

## 3. Protocol contract

Everything below is what the Rust server does today
(`rust/server/src/serial.rs`, `hardware.rs`), which matches Python's
`RHInterface.py`. Numbers are decimal unless prefixed `0x`.

### 3.1 Frame format

Request (host → node): `[command]` for reads, `[command, payload…, checksum]`
for writes. Checksum = low byte of the sum of the payload bytes. Response
(node → host): `[payload…, checksum]`, same rule, `payload` length fixed per
command. The node must never emit unsolicited bytes on this port.

### 3.2 Identification

| Read | Reply |
|---|---|
| `0x22 READ_REVISION_CODE` | two bytes: `0x25`, then API level. **API 36.** |
| `0x3D READ_FW_VERSION` | 16 bytes, ASCII, NUL padded ("2.1.0") |
| `0x3E READ_FW_BUILDDATE` | 16 bytes, e.g. "Nov 27 2026" |
| `0x3F READ_FW_BUILDTIME` | 16 bytes, e.g. "12:00:00" |
| `0x40 READ_FW_PROCTYPE` | 16 bytes, "ESP32-C5" |
| `0x11 READ_RHFEAT_FLAGS` | two bytes; `0x0000` is correct for this node |
| `0x00 READ_ADDRESS` | one byte (retained for old mock tools; 0x08 today) |

### 3.3 Multi-node

| Command | Direction | Contract |
|---|---|---|
| `0x39 READ_MULTINODE_COUNT` | read | **8** on C5 native builds, 1 on every other target |
| `0x3A READ_CURNODE_INDEX` | read | currently selected slot |
| `0x3C READ_NODE_SLOTIDX` | read | this processor's slot index (0 on single-seat builds) |
| `0x7A WRITE_CURNODE_INDEX` | write, 1 byte + checksum | select the slot for **all subsequent reads and writes**, and reply with **two bytes**: the new slot, then the checksum of that byte. The server writes this before every block access and retries if the echo does not match. |

Slot selection is a *protocol* concept, not a radio one: it changes which seat's
cached state the next reads return and which seat a write applies to. The radio
sampler keeps cycling through all enabled seats regardless of the selection.

If the requested slot is greater than or equal to the seat count, keep the
current slot and echo that one, so the server's comparison fails and it retries
rather than silently reading the wrong seat.

### 3.4 Per-slot settings

| Command | Reply / effect |
|---|---|
| `0x51 WRITE_FREQUENCY` (2 bytes) | apply to the selected seat. `1111` or `0` disables the seat (stop sampling it, report RSSI 1, no crossings). 5645–5945 MHz: accept anything the radio can tune (see §3.6). |
| `0x03 READ_FREQUENCY` | the selected seat's frequency (0 when disabled) |
| `0x71 WRITE_ENTER_AT_LEVEL` (1 byte) | selected seat's enter threshold |
| `0x72 WRITE_EXIT_AT_LEVEL` (1 byte) | selected seat's exit threshold |
| `0x31 READ_ENTER_AT_LEVEL` | 1 byte |
| `0x32 READ_EXIT_AT_LEVEL` | 1 byte |
| `0x04 TEST_RX_REGISTER` | **new in API 36.** One byte: **non-zero** when the selected seat is enabled and its radio is actually parked on its frequency (and that frequency is tunable), **0** otherwise. |

`0x04` must also be added to `Message::isValidCommand()`; an unknown command is
dropped silently, which would look like a timeout.

### 3.5 Status and timing reads

| Command | Reply |
|---|---|
| `0x0D READ_LAP_PASS_STATS` | 8 bytes as in §2, for the selected seat, from cached state |
| `0x0E READ_LAP_EXTREMUMS` | 8 bytes as in §2, oldest pending extremum first |
| `0x23 READ_NODE_RSSI_PEAK` | 1 byte, node peak since reset |
| `0x24 READ_NODE_RSSI_NADIR` | 1 byte, node nadir since reset (today hardcoded 30) |
| `0x33 READ_TIME_MILLIS` | 4 bytes, free-running node clock |
| `0x05 READ_LAP_STATS` | not required at API ≥ 32 (the server uses 0x0D + 0x0E) |
| `0x78 FORCE_END_CROSSING` | clear the selected seat's crossing flag and post-race hold at once |
| `0x75 SEND_STATUS_MESSAGE` | accept and ignore (2-byte payload) |
| `0x7E JUMP_TO_BOOTLOADER` | unchanged |

### 3.6 Frequencies

The existing C5 receiver tunes exactly: it parks the Wi-Fi driver on the nearest
5 GHz channel and, if the target is more than `C5_WIFI_TUNE_TOLERANCE_MHZ` away,
moves the PHY with `RFChannelSel(mhz, 0)`. Every RotorHazard frequency
(5645–5945) is reachable. `TEST_RX_REGISTER` must return 0 for any frequency
outside that range rather than pretending success — the server uses it to warn
"Failed to set frequency on node N".

## 4. Firmware architecture

### 4.1 Seat state

Replace the single `TimingState`/extremum pair with a seat array. One seat:

```cpp
struct Seat {
  // settings (protocol-visible)
  uint16_t frequency;        // 0 = disabled
  uint8_t  enterAt, exitAt;
  // sampler state
  uint8_t  rssi;             // last filtered reading, always 1..254
  uint8_t  peak, nadir;      // since reset
  uint8_t  passPeak, passNadir;
  bool     crossing;
  uint32_t crossingStartUs;
  uint32_t lastPassUs;       // plateau midpoint of the last pass
  uint8_t  lapId;            // wraps at 256
  uint32_t lastSampleUs;
  uint16_t sampleIntervalUs; // for the 0x0D loop-time field
  // filters and history
  BurstFilter<...> burst;    // per seat: windows are in *samples of this seat*
  KalmanFilter     kalman;
  ExtremumQueue    peaks, nadirs;   // 16-32 entries each, oldest-first
  bool     pendingCrossingClear;
};
```

Memory target: 8 seats × (2 × 32 × 8 B queues + ~120 B state) ≈ 5 KB. Drop the
per-seat `RSSI_HISTORY` buffer in 8-pilot mode (`RSSI_HISTORY_ENABLED 0` on C5),
or keep it for the single-seat build only; it is not part of the node protocol.

### 4.2 Sampler and hop schedule

The radio is one resource. A sampler task (the existing timing task) runs a
round-robin over **enabled** seats:

```
for each enabled seat:
    tune(seat.frequency)          // skipped when already parked there
    wait until the retune settles // see §5
    read phy_get_sigrssi() once per C5_SAMPLE_PERIOD_US
    filter -> seat.rssi, seat.peak/nadir, extremums, crossing detection
    stamp seat.lastSampleUs = esp_timer_get_time()
```

Requirements:

- A full sweep of eight seats must complete in **≤ 5 ms** (≥ 200 samples per
  seat per second). If the measured settle time makes that impossible, see §5 —
  do not silently ship a slower rate.
- Never tune inside `handleSerialInput()`: serial reads answer from cached seat
  state so an 8-seat poll cannot stall the protocol.
- The crossing decision for a seat uses only that seat's samples; a gap longer
  than 100 ms between two samples of the same seat cancels its crossing
  (stale data must not produce a lap).
- On `WRITE_FREQUENCY` for a disabled seat (1111/0) remove it from the schedule
  after the current sweep; on re-enable add it at the end of the next sweep.

### 4.3 Pass timing

A pass is a crossing that starts above `enterAt` and ends below `exitAt`.
The reported time is the **plateau midpoint** of that crossing on the node's
microsecond clock:

```
lastPassUs = peakStartUs + (peakEndUs - peakStartUs) / 2
```

`ms_since_lap` = (now − lastPassUs) / 1000, clamped to 65535, computed at read
time so serial latency does not age the value. The server applies its own
round-trip correction; the node must not add poll delay. This is the same
definition the C5MK firmware documents and it keeps lap times comparable with
RX5808 nodes.

Other rules:

- Lap id increments per pass and wraps at 256.
- `passPeak` = highest filtered RSSI during the pass; `passNadir` = lowest RSSI
  since the previous pass ended.
- The crossing flag stays set for 250 ms after the pass ends so a 50 Hz poll
  cannot miss a short pass; `0x78` clears it immediately (race start/stop).
- Extremums are queued oldest-first; a peak is flagged with bit 1 of the
  extremum flags byte, a nadir is sent with the flag clear. `duration` is
  peak end − start (API 33 semantics, which the server expects at API 36).
- Reject VTX power-on jumps (a rise from the floor to above `enterAt` in under
  ~30 ms that falls back within ~90 ms) so switching a VTX on does not record a
  pass. Keep the thresholds as build flags; this is optional for phase 1 but
  belongs in the bench checklist.
- `min lap time` per seat, default 0 (server enforces its own minimum).

## 5. The hard constraint: 15 ms settle versus eight seats

`C5_SETTLE_MS` is **15 ms**. Eight seats at that budget is 120 ms per sweep —
eight samples per second per seat, which cannot time a 2 ms gate pass. This is
the main engineering risk, and it must be measured, not guessed:

1. On hardware, log `phy_get_sigrssi()` at ~10 µs resolution for 20 ms after
   `RFChannelSel` and after a `esp_wifi_set_channel` hop, at several channels
   (R1, R4, R8, E6). Find the time until the reading is within **1 count** of
   its steady value.
2. Split the path: a hop that stays inside the current Wi-Fi channel (offset
  ≤ tolerance) is cheap; an `RFChannelSel` hop is the expensive one. Most race
   channels are not on a Wi-Fi centre (R2/R3/R5/R6, F1/F3, E…), so assume the
   expensive path for the schedule.
3. Set `C5_HOP_SETTLE_MS` / `C5_HOP_SETTLE_US` from the measurement and record
   the resulting sweep time in the bench notes. If the settle cannot be brought
   under ~400 µs, the design needs a two-tier schedule: sample all seats
   coarsely (e.g. 50 Hz) and dwell on any seat whose reading is within 15 counts
   of `enterAt` (a "hot" seat gets 2–3 ms of consecutive samples). Document
   whichever is chosen.

Do not reduce `C5_SETTLE_MS` for the single-seat build as part of this work;
the 8-pilot path gets its own constant so single-seat timing cannot regress.

## 6. Serial behaviour

- Reads are answered from cached state; the longest read path is a memcpy.
- Budget: 8 seats × (slot switch 3 B + two block reads 9 B each) ≈ 170 B per
  poll cycle. At 50 Hz that is ~8.5 kB/s ≈ 85 kbit/s of payload, which fits
  115200 with little headroom and 921600 comfortably. The Rust server probes
  115200 first: keep every reply under ~2 ms or the discovery probe will look
  like a dead port and retry at the slower baud.
- Nothing may print to this port. `silenceSystemOutput()` already runs; any new
  debug output must go through the existing debug macros (off by default).
- After a USB open/reset the firmware must stay silent until commanded. Linux
  raises DTR/RTS on open and resets the C5; anything emitted during boot arrives
  before the server's revision request and breaks discovery.

## 7. API level and version

- `NODE_API_LEVEL 36` **only together with `0x04`**. The Rust server runs the
  register test for API ≥ 36 after every frequency write, retries the whole
  write five times and then emits "Failed to set frequency on node N". Bumping
  the level without the handler breaks every seat change.
- `SF_FW_VERSION` to `2.1.0`; `READ_FW_PROCTYPE` to `ESP32-C5` on these builds
  so the UI names the board correctly.
- Single-seat targets keep API 35 and their current behaviour unchanged.

## 8. Build configuration

```ini
; platformio.ini, C5 native builds only
-DSF_SEAT_COUNT=8
```

`SF_SEAT_COUNT` defaults to 1; only `esp32-c5` and `esp32-c5-zero` set 8. Do not
expose a runtime seat count in this phase.

## 9. Acceptance tests

### 9.1 Host unit tests (`bash test/unit/run.sh`)

Add to the existing g++ suite, with a synthetic clock and sample source:

1. **Hop schedule** — eight enabled seats are visited in order; a disabled seat
   is skipped; re-enabling puts it back; a sweep of eight stays under the §5
   budget given the configured settle.
2. **Crossing** — a synthetic RSSI trace (floor → plateau → floor) records
   exactly one pass for that seat and none for its neighbours; the timestamp is
   the plateau midpoint ± the sample period.
3. **Short pass** — a 2 ms pass is still counted (it is why the 250 ms flag hold
   exists).
4. **Disabled seat** — writes 1111, reads report frequency 0, RSSI 1, no
   crossing, and `0x04` returns 0.
5. **Extremum order** — peak and nadir queued out of order are returned
   oldest-first with the right flag.
6. **Protocol encoder** — every response in §3 has the right length and
   checksum, slot switching routes reads and writes to the selected seat, and an
   out-of-range slot is rejected by echoing the current one.

### 9.2 Protocol conformance against the real server (new)

Add `test/host/`: compile `node_mode.cpp` plus a stub seat bank (no radio, state
driven by a scripted trace) into a host binary that speaks the protocol on a
PTY, in the same way the `fake_c5mk.py` harness in the Rust repo does.

Then a script, `test/tools/test_rust_server_8seats.sh`:

1. download `rh-server-linux-x86_64` for a pinned tag from
   `github.com/RaceFPV/RotorHazard` (public),
2. create a PTY pair, point the simulator at the slave,
3. start the server with `SERIAL_PORTS=[slave]`,
4. assert: eight nodes discovered, the startup profile programmed to all six
   defined seats (fresh DB), a frequency change confirmed, an untunable one
   refused with the priority message, and a scripted pass arriving as a lap in
   `load_data`/`race_data` with the timestamp the simulator sent ± 2 ms.

Exit criteria for phase 2 and 3: this script passes on CI, against the pinned
release, with no hardware attached.

### 9.3 Hardware bench checklist (extend docs/ESP32_C5.md)

- Eight VTXs (or one VTX moved seat by seat) on R1–R8; confirm each seat only
  responds on its own frequency (no cross-talk) and that RSSI floor/noise looks
  like the single-seat build.
- Measure the sweep rate from `0x0D`'s loop-time field; record samples/s/seat.
- Inject 20 passes per seat with a servo/shutter or a signal generator and
  compare lap times against an RX5808 node on the same pilot; target ±2 ms.
- Two seats simultaneously (two VTXs crossing together) — no lap may be
  attributed to the wrong seat.
- 30 minutes at 8 seats: no serial timeouts in the server log, no reset.
- Unplug/replug: the server reopens the port and re-discovers eight seats.

## 10. Phases

| Phase | Work | Exit criterion |
|---|---|---|
| P0 | Bench-measure settle time and hop cost (§5) | **open** — `C5_HOP_SETTLE_US` is 500 µs, a placeholder |
| P1 | Seat struct + sampler refactor, single seat unchanged | **done** — unit tests pass, non-C5 builds unchanged |
| P2 | Multi-node protocol, slot routing, `0x7A` echo | **done** — conformance shows 8 nodes discovered and programmed |
| P3 | API 36 `0x04`, disabled seats, warning path | **done** — server silent on a good frequency, warns on 5000 MHz |
| P4 | Pass timing, extremums, 250 ms flag, force-end | **done on the host** — laps time within 1 ms of the injected peak |
| P5 | Bench validation, docs, 2.1.0 release | **open** — §9.3 checklist |

P1 and P2 landed behind `SF_SEAT_COUNT`, so the refactor cannot break the
C3/C6/S2/S3 builds.

## 11. Provenance

The behaviour in §5 (a PLL retune is settled after ~300 µs) and the rejection
rules in §4.3 are facts taken from the published notes of the FPVGate C5MK
project, which is CC BY-NC-SA 4.0. No code from it is used here: this
implementation was written for StarForge from the protocol and those published
facts, the same way `docs/ESP32_C5.md` records its sources. Do not paste code
from that project into this repository - the licences are incompatible.

## 12. Risks

- **Settle budget** (§5) — the whole feature depends on it. Measure first.
- **Undocumented PHY functions** — unchanged from `docs/ESP32_C5.md`; the C5
  environments stay pinned to pioarduino 55.03.312.
- **Wi-Fi interference** — eight seats means eight centre frequencies sampled in
  turn; busy 5 GHz Wi-Fi shows up as spikes on the seats near those channels.
  The burst filter is per seat and its windows are in samples of that seat, so
  the effective time window shrinks as the sample rate drops; re-tune the
  window constants after P0.
- **Single-core CPU** — the C5 runs the sampler and the serial handler on one
  core; the sampler must yield between hops and the serial handler must stay
  cheap.
- **RAM** — extremum queues and history per seat; trim history in 8-pilot mode.

## 13. Open questions

1. Should the eight seats follow a band automatically (R1–R8) on first boot, or
   wait for the server's profile? Default: wait — the server writes the profile,
   and inventing frequencies makes the first race misleading.
2. Do we want `READ_LAP_STATS` (0x05) for very old servers? Not needed at API 36.
3. Is a 4-seat build useful for C5 boards with less RAM? Leave the constant
   configurable but do not ship it.
