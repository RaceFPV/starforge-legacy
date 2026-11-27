# StarForge Flasher (RotorHazard plugin)

Puts a **Flash** button in RotorHazard's settings page so a bare ESP32-C5 node
can be updated from the machine running the timer - no PC at the bench.

## Install

1. Copy this folder into the server's data directory:

       cp -r plugins/starforge_flasher ~/.rotorhazard/plugins/

   (or wherever RotorHazard keeps `config.json` and `plugins/`).

2. esptool must be importable by the Python that runs the plugin bridge:

       python3 -m pip install esptool

   The plugin reports that command in the panel if it is missing.

3. Restart RotorHazard. A "StarForge flasher" panel appears on the Settings page.

## Use

1. Unpack a StarForge release so a folder holds `bootloader.bin`,
   `partitions.bin` and `firmware.bin`; set **Firmware folder** to it.
2. Plug the board into this machine on its **USB** port (not UART).
3. Press **Flash the free board**.

The panel reports progress and the outcome in place. If the write fails, hold
BOOT, tap RESET, release BOOT and try again - automatic reset over native USB is
not always reliable.

## Limits

- **A board the timer is reading cannot be flashed.** The server owns that
  serial port; the plugin only offers ports that are not in
  `GENERAL.SERIAL_PORTS`. Stop the timer, or plug in a spare board.
- The board resumes as a timing node, so it must be on the same machine as
  RotorHazard - USB only, there is no network transport.
- Firmware is whatever is in the folder; the plugin does not download releases.
