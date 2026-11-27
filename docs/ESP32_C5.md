# ESP32-C5: built-in receiver

On an ESP32-C5 the chip's own 5 GHz radio is the receiver. There is no RX5808
and no RSSI wiring: plug the board into the RotorHazard server over USB, fit a
5.8 GHz antenna, and add it as a timing node. RotorHazard sees an ordinary
StarForge node.

**Status: compiles and passes its unit tests; not yet tested on hardware.**
See [Bench checklist](#bench-checklist) before racing on it.

## Boards

| Environment | Board | Notes |
|---|---|---|
| `esp32-c5` | Espressif ESP32-C5-DevKitC-1 | Use the native **USB** port, not the UART port. RGB LED on GPIO27. |
| `esp32-c5-zero` | Waveshare ESP32-C5-Zero | Onboard antenna by default; build with `-DC5_ANT_EXTERNAL=1` for the IPEX connector (GPIO26). RGB LED on GPIO27. |
| `esp32-c5-xiao` | Seeed Studio XIAO ESP32-C5 | Native USB-C, 8 MB flash. **Fit the u.FL antenna** (the board is deaf without it); a 5.8 GHz antenna beats the bundled dual-band one for our use. Plain LED on GPIO27 (no WS2812). |

```bash
pio run -e esp32-c5-zero --target upload
```

Flashing needs esptool 4.8.1 or later. The desktop flasher's bundled esptool
has been bumped to v4.8.1 in `flash_tool/download-esptool.sh`; re-run it (or
let the flasher workflow run it) before shipping a flasher build. The C5's
bootloader goes at **0x2000**, not 0x0.

The flasher's custom RX5808 pin option does nothing on these builds.

## Channels

Every frequency RotorHazard can send to a StarForge node (5645-5945 MHz) is
tuned exactly, including R8 (5917) and E6-E8, which are outside the Wi-Fi
channel list.

## How it works

```
RotorHazard --USB--> node_mode.cpp (unchanged)
                          |
                     timing_core.cpp (unchanged lap/peak/extremum logic)
                          |  readRawRSSI() / setRX5808Frequency()
                          v
                     receiver/c5_radio.cpp
                          |  tune: Wi-Fi channel, then exact PHY frequency
                          |  read: PHY signal-RSSI register, 1 kHz
                          v
                     filters/burst_filter.h
                          |  opening (5 ms): drops Wi-Fi packet spikes
                          |  closing (9 ms): fills VTX reading dropouts
                          |  dBm -> 0..255, linear in dB
                          v
                     Kalman filter (lighter settings on C5) -> lap detection
```

- **Radio start-up:** Wi-Fi station mode that never connects or scans, 5 GHz
  only, 20 MHz bandwidth, power saving off, promiscuous so the receiver stays
  on. Nothing is transmitted.
- **Tuning:** the Wi-Fi driver is put on the nearest 5 GHz channel. If the
  target is more than `C5_WIFI_TUNE_TOLERANCE_MHZ` (1 MHz) from that channel's
  centre, the PHY is moved to the exact frequency with `RFChannelSel(mhz, 0)`
  followed by one `phy_param_track_tot` pass. Readings are dropped for
  `C5_SETTLE_MS` (15 ms) after a retune, and the timing task waits for that
  the same way it waits after an RX5808 retune.
- **Reading:** `phy_check_sigrssi_en(1)` puts the baseband RSSI register into
  continuous mode; `phy_get_sigrssi()` reads it as signed dBm. An esp_timer
  samples it every 1 ms.
- **Filter:** a grey-scale morphological opening then closing. Unlike a
  peak-hold, it delays rising and falling edges by the same 12 ms, so a pass
  keeps its shape and the peak's timestamp moves by a constant that cancels
  out of lap times. Unit tests: `bash test/unit/run.sh`.

### Settings (build flags)

| Flag | Default | Meaning |
|---|---|---|
| `C5_RSSI_DBM_FLOOR` | -100 | dBm that reads as RSSI 0 |
| `C5_RSSI_DBM_CEIL` | -20 | dBm that reads as RSSI 255 |
| `C5_FILTER_OPEN_SAMPLES` | 5 | upward spikes shorter than this many ms are removed |
| `C5_FILTER_CLOSE_SAMPLES` | 9 | downward dips shorter than this many ms are filled |
| `C5_WIFI_TUNE_TOLERANCE_MHZ` | 1 | above this offset, tune the PHY exactly |
| `C5_SETTLE_MS` | 15 | readings ignored after a retune |
| `C5_ENABLE_11P` | 0 | 802.11p receive settings from `C5_11P_FROM_MHZ` (5750) up; untested |
| `C5_ANT_EXTERNAL` | 0 | C5-Zero only: 1 selects the IPEX antenna |
| `SF_RECEIVER_C5_NATIVE` | 1 on C5 | 0 makes a C5 drive an external RX5808 like the other targets (RSSI GPIO1, DATA GPIO5, CLK GPIO4, SEL GPIO6) |

The 0-255 scale is linear in dB at about 3.2 counts per dB. RotorHazard's
enter/exit levels will need setting fresh for C5 nodes; do not copy them
from RX5808 nodes.

## Serial output fixes (all targets)

The USB port carries RotorHazard's binary protocol, so stray text breaks it.
On the C3, C5, C6 and S3, ESP-IDF's console has the USB Serial/JTAG port as a
secondary output, which is the same port Arduino's `Serial` uses.

- `src/system/serial_guard.*`: `silenceSystemOutput()` runs right after
  `Serial.begin()` on every target. It turns off ESP-IDF logging and the ROM
  printf channels, which the Wi-Fi and PHY libraries use (the PHY version
  line, for example). Crash dumps are not affected. Set
  `-DSF_SILENCE_SYSTEM_OUTPUT=0` to turn it off for debugging.
- `timing_core.cpp`: the two "failed to allocate RSSI history" warnings
  printed even with debug off. They now print only in debug mode.

## Other fix (all targets)

The timing task was pinned to core 1 on every chip except the C3 and C6. On
single-core chips (the S2, and now the C5) core 1 doesn't exist. It now picks
by `portNUM_PROCESSORS`, so single-core chips aren't pinned.

## Risks

- **Undocumented PHY functions.** `phy_check_sigrssi_en`, `phy_get_sigrssi`,
  `RFChannelSel`, `phy_param_track_tot` and `phy_11p_set` are exported by
  Espressif's `libphy.a` but aren't public API. The C5 environments are pinned
  to pioarduino 55.03.312 (arduino-esp32 3.3.12, ESP-IDF 5.5.5), the version
  whose library these were checked against. Before moving the pin, re-check
  them:
  ```bash
  L=~/.platformio/packages/framework-arduinoespressif32-libs/esp32c5/ld/libphy.a
  riscv32-esp-elf-nm -g --defined-only $L | grep -E "sigrssi|RFChannelSel|param_track_tot|11p_set"
  ```
  `phy_get_sigrssi` should still be a plain read of bits 8-15 of register
  `0x600A706C`, sign-extended.
- **Re-enabling signal-RSSI mode after every tune.** This is done because a
  channel change reprograms the baseband. If the bench shows glitches right
  after a retune, try enabling it once at start-up only.
- **Driver retuning behind our back.** Nothing in this mode should make the
  Wi-Fi driver change channel, but if a reading collapses to the noise floor
  mid-race, that is the first suspect.

## Provenance

This code was written from scratch for StarForge. No code from FPVGateC5RX,
esp-sdr, C5VRX or double-ESP-resso was read or used.

| Fact | Source |
|---|---|
| Which libphy functions read signal RSSI and tune by MHz; that a 5 GHz "channel" inside the PHY is the frequency in MHz; that Wi-Fi packets show as 1-3 ms spikes and a VTX reading dips for 1-6 ms | The public documentation (not code) of FPVGateC5RX. Facts only. |
| Function signatures and register behaviour | Disassembly of Espressif's Apache-2.0 `libphy.a` for the C5 (arduino-esp32 3.3.12) |
| Wi-Fi start-up, country, band mode and promiscuous calls | ESP-IDF `esp_wifi.h` (Apache-2.0) |
| Waveshare C5-Zero antenna switch (GPIO26, LOW = onboard) and RGB LED (GPIO27) | arduino-esp32 `variants/waveshare_esp32_c5_zero/pins_arduino.h` |
| ESP-IDF console routing on the C5 (secondary USB Serial/JTAG output) | The arduino-esp32 C5 `sdkconfig` |

## Bench checklist

1. Flash `esp32-c5-zero`. Connect RotorHazard. It should find the node and
   show firmware `ESP32_2.0.4`, with no garbage on connect. If anything
   shows, capture the raw port with a hex dump.
2. VTX at 1 m on R1, R4, R8 and E6 in turn. In RotorHazard's RSSI graph, note
   the reading with the VTX on and off. Expect about 30 dB of separation,
   about 100 counts.
3. VTX off on a channel with busy Wi-Fi nearby (R4-R6 / F2-F5). The
   reading should sit flat near the floor with no spikes. If spikes get
   through, raise `C5_FILTER_OPEN_SAMPLES`.
4. VTX on, steady: the line should be flat. If it ripples, raise
   `C5_FILTER_CLOSE_SAMPLES`.
5. Change frequency in RotorHazard several times. The node should follow it,
   and no lap should register on the change.
6. 30 minutes on R8 with the VTX on: no drop to the floor, no reset.
7. Laps: run passes alongside an RX5808 node on the same pilot. Compare lap
   times.
8. Tune `C5_RSSI_DBM_FLOOR`/`CEIL` so the quiet floor reads 10-20 and a close
   pass reaches about 200.

## Flashing from the RotorHazard host (no PC)

A node plugged into a Raspberry Pi can be updated from that Pi. The board is a USB
CDC device (`/dev/ttyACM0`, or better `/dev/serial/by-id/usb-Espressif*`) and
`esptool` talks to the C5's ROM bootloader over the same cable the timer uses.
Addresses: bootloader `0x2000`, partitions `0x8000`, firmware `0x10000`, and
`nvs` `0x9000` / `spiffs` `0x290000` when those parts ship.

**RotorHazard owns the port while it runs**, so anything that flashes must stop
it first - and the C5 reboots when the port is opened. Three routes, in order of
how much machinery they add:

1. **`tools/flash_pi.sh`** (recommended, works over SSH):

       tools/flash_pi.sh --firmware ~/StarForge-esp32-c5-xiao
       tools/flash_pi.sh --build -e esp32-c5-xiao
       tools/flash_pi.sh --firmware DIR --port /dev/ttyACM0 --service rh-server

   It stops the service, flashes, verifies, restarts the service, and prints the
   BOOT-button recovery sequence if the automatic reset does not take. Needs
   esptool 4.8.1 or later (`python3 -m pip install --user esptool`).

2. **A browser flasher** (esptool-js / Web Serial) served by the Pi and opened in
   Chromium *on the Pi*. It works, but Web Serial also needs exclusive access, so
   the timer still has to be stopped first. Good for a phone-free workstation,
   not a hands-off update.

3. **A RotorHazard plugin.** Not recommended as the flasher itself: the plugin
   bridge cannot stop the server that owns the port, and giving arbitrary
   flashing privileges to the timing process is a bigger change than the problem
   deserves. If a button inside RotorHazard is wanted, the clean shape is a small
   sidecar service that owns stop/flash/start plus a plugin panel that calls it.

Automatic reset over native USB is not always reliable - if the write fails,
hold BOOT, tap RESET, release BOOT and run the command again. On a C5 in the
ROM bootloader, `--after watchdog-reset` leaves download mode more reliably
than `--after hard_reset`; `flash_pi.sh` uses it by default.
