#!/bin/bash
# Flash a StarForge ESP32-C5 node from the machine running RotorHazard.
#
# Why this is a script and not a RotorHazard plugin: the timer owns the serial
# port, the board reboots when the port is opened, and the plugin bridge has no
# way to stop the server it is attached to. This stops the timer, flashes, and
# starts it again - the same work a "flash" button would have to do, minus the
# pretence that flashing belongs inside the timing process.
#
#   tools/flash_pi.sh --firmware ~/Downloads/StarForge-esp32-c5-xiao
#   tools/flash_pi.sh --build -e esp32-c5-xiao
#   tools/flash_pi.sh --firmware DIR --port /dev/ttyACM0 --service rh-server
#
# With no --firmware or --build it looks for a release zip already unpacked in
# ./firmware (bootloader.bin, partitions.bin, firmware.bin).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"

FIRMWARE_DIR="${FIRMWARE_DIR:-}"
BUILD=0
ENV_NAME="esp32-c5-xiao"
PORT=""
SERVICE="${RH_SERVICE:-rh-server}"
BAUD="${FLASH_BAUD:-460800}"
STARTED_SERVER=0
WAS_ACTIVE=0

usage() {
    sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --firmware) FIRMWARE_DIR="${2:?}"; shift 2 ;;
        --build) BUILD=1; shift ;;
        -e|--env) ENV_NAME="${2:?}"; shift 2 ;;
        --port) PORT="${2:?}"; shift 2 ;;
        --service) SERVICE="${2:?}"; shift 2 ;;
        --baud) BAUD="${2:?}"; shift 2 ;;
        -h|--help) usage 0 ;;
        *) echo "unknown option: $1" >&2; usage 1 ;;
    esac
done

say() { printf '\033[1;34m==\033[0m %s\n' "$*"; }
fail() { printf '\033[1;31mFAIL:\033[0m %s\n' "$*" >&2; exit 1; }

# --- esptool ---------------------------------------------------------------
find_esptool() {
    local candidate
    for candidate in \
        "${ESPTOOL:-}" \
        "$ROOT/.venv/bin/esptool" \
        "$HOME/.venvs/rh/bin/esptool" \
        "$(command -v esptool 2>/dev/null || true)" \
        "$(command -v esptool.py 2>/dev/null || true)" \
        "$ROOT/flash_tool/resources/bin/linux/esptool"; do
        [ -n "$candidate" ] && [ -x "$candidate" ] && { echo "$candidate"; return 0; }
    done
    if python3 -c 'import esptool' >/dev/null 2>&1; then
        echo "python3 -m esptool"
        return 0
    fi
    return 1
}
ESPTOOL="$(find_esptool)" || fail "no esptool found. Install it with: python3 -m pip install --user esptool (v4.8.1 or later for the ESP32-C5)"

# --- firmware --------------------------------------------------------------
if [ "$BUILD" -eq 1 ]; then
    PIO="${PIO:-$(command -v pio || echo "$HOME/.venvs/pio/bin/pio")}"
    [ -x "$PIO" ] || fail "platformio not found; set PIO=/path/to/pio"
    say "building $ENV_NAME"
    "$PIO" run -e "$ENV_NAME" -d "$ROOT" || fail "build failed"
    FIRMWARE_DIR="$ROOT/.pio/build/$ENV_NAME"
fi
[ -n "$FIRMWARE_DIR" ] || FIRMWARE_DIR="$PWD/firmware"
[ -d "$FIRMWARE_DIR" ] || fail "no firmware directory ($FIRMWARE_DIR); pass --firmware DIR or --build"
for part in bootloader.bin partitions.bin firmware.bin; do
    [ -f "$FIRMWARE_DIR/$part" ] || fail "$FIRMWARE_DIR/$part is missing"
done

# --- port ------------------------------------------------------------------
if [ -z "$PORT" ]; then
    # Prefer a by-id path: ttyACM numbering moves when a second board appears.
    PORT="$(ls /dev/serial/by-id/*Espressif* 2>/dev/null | head -1 || true)"
fi
if [ -z "$PORT" ]; then
    mapfile -t acm < <(ls /dev/ttyACM* 2>/dev/null)
    [ "${#acm[@]}" -ge 1 ] || fail "no serial port found; plug the node in (USB port, not UART) or pass --port"
    [ "${#acm[@]}" -eq 1 ] || fail "several ports (${acm[*]}); pass --port with the node's own"
    PORT="${acm[0]}"
fi
say "port: $PORT"

# --- stop the timer --------------------------------------------------------
if systemctl is-active --quiet "$SERVICE" 2>/dev/null; then
    WAS_ACTIVE=1
    say "stopping $SERVICE"
    sudo systemctl stop "$SERVICE" || fail "could not stop $SERVICE"
elif pgrep -f "rh-server" >/dev/null 2>&1; then
    say "stopping a rh-server that is not a systemd service"
    pkill -f "rh-server" || true
    STARTED_SERVER=1
    sleep 1
fi

restart_timer() {
    if [ "$WAS_ACTIVE" -eq 1 ]; then
        say "starting $SERVICE"
        sudo systemctl start "$SERVICE" || true
    elif [ "$STARTED_SERVER" -eq 1 ]; then
        say "RotorHazard was not running under systemd; start it again yourself"
    fi
}
trap restart_timer EXIT

# --- flash -----------------------------------------------------------------
# C5 addresses: the ROM bootloader is at 0x2000, not 0x0.
ARGS=(--chip esp32c5 --port "$PORT" --baud "$BAUD"
      --before default_reset --after watchdog-reset write_flash 0x2000)
FILES=("$FIRMWARE_DIR/bootloader.bin" 0x8000 "$FIRMWARE_DIR/partitions.bin" 0x10000 "$FIRMWARE_DIR/firmware.bin")
[ -f "$FIRMWARE_DIR/nvs.bin" ] && FILES+=(0x9000 "$FIRMWARE_DIR/nvs.bin")
[ -f "$FIRMWARE_DIR/spiffs.bin" ] && FILES+=(0x290000 "$FIRMWARE_DIR/spiffs.bin")

say "flashing ${FIRMWARE_DIR#$ROOT/}"
set +e
# shellcheck disable=SC2086
$ESPTOOL "${ARGS[@]}" "${FILES[@]}"
rc=$?
set -e
if [ "$rc" -ne 0 ]; then
    cat <<'EOF'

The write failed. On a native-USB C5 board the automatic reset is not always
enough:
  1. hold BOOT, tap RESET, release BOOT, then run this again; or
  2. unplug, hold BOOT, plug in, release BOOT, then run this again.
Close anything else that may hold the port (a serial monitor, the C5 dashboard,
another RotorHazard instance): only one process can own it.
EOF
    exit "$rc"
fi

say "verifying"
if $ESPTOOL --chip esp32c5 --port "$PORT" --before default_reset verify_flash \
        0x2000 "$FIRMWARE_DIR/bootloader.bin" >/dev/null 2>&1; then
    say "bootloader verified"
else
    say "verify_flash not conclusive on this board; the write itself succeeded"
fi

say "done - the node reboots into the new firmware"
say "if the timer is running, it should rediscover the board's seats within a few seconds"
