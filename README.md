# StarForge Legacy

RotorHazard node firmware. On boot the device speaks the RotorHazard node serial protocol over USB.

## What is included

- RotorHazard node protocol (`src/node_mode.cpp`), API level 35
- RX5808 tuning, RSSI sampling, and lap detection (`src/timing_core.cpp`)
- Kalman RSSI filter
- ESP32-C5: the chip's own 5 GHz radio as the receiver, no RX5808 needed (`src/receiver/c5_radio.cpp`, see [docs/ESP32_C5.md](docs/ESP32_C5.md))
- Status LED
- Optional custom RX5808 pins stored in NVS

## Flash

The desktop flasher is in `flash_tool/`. Published releases include the macOS and Windows installers, and one firmware zip per board. The flasher downloads `StarForge-<board>.zip` from this repository.

```bash
cd flash_tool
npm install
npm start
```

## Build

```bash
pio run -e esp32-c3-supermini
pio run -e esp32-c3-supermini --target upload
```

GitHub Actions builds every board when a release is published, and checks that the firmware compiles on pull requests.

Other environments: `esp32-c3`, `esp32-c3-zero`, `esp32-c6`, `esp32dev`, `esp32dev-2mb`, `esp32-s3`, `esp32-s3-t-energy`, `esp32-s2`, `nuclearcounter`, `esp32-c5`, `esp32-c5-zero`.

The `esp32-c5` and `esp32-c5-zero` builds have no RX5808 pins: the C5 receives the VTX itself. See [docs/ESP32_C5.md](docs/ESP32_C5.md).

Host unit tests: `bash test/unit/run.sh` (needs g++).

Pin maps live in `src/config/config.h`. ESP32-C3 SuperMini defaults:

| ESP32-C3 | RX5808 |
| --- | --- |
| GPIO3 | RSSI |
| GPIO6 | DATA |
| GPIO4 | CLK |
| GPIO7 | SEL (LE) |

## RotorHazard

1. Flash this firmware.
2. Plug the board into the RotorHazard server over USB.
3. Add it as a timing node.

Do not open a serial monitor while RotorHazard is connected. Text on that port breaks the binary protocol.

A mock server for protocol checks is in `test/tools/`.
