#!/bin/bash
# Does the real RotorHazard server drive an eight-seat node correctly?
#
# A host build of the portable protocol core (test/host/pty_node.cpp) stands in
# for the firmware on a PTY, so this runs with no hardware. It covers the parts
# that only exist for multi-node boards: eight-seat discovery, per-seat
# frequency programming and slot routing, the API 36 register test (a seat on an
# unreachable frequency must raise the operator warning), and sustained polling
# while the sampler keeps running.
#
# Lap ingestion is not exercised here: that needs a running race, and the
# engine's own unit tests (test/unit/test_seat_engine.cpp) plus the server's
# protocol suite cover it.
#
#   RH_SERVER=/path/to/rh-server  PYTHON_BIN=/path/to/python  bash test/tools/test_rust_server_8seats.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(git -C "$HERE" rev-parse --show-toplevel)"
RH_SERVER="${RH_SERVER:-$HOME/git/RotorHazard/rust/target/release/rh-server}"
PY="${PYTHON_BIN:-$HOME/.venvs/rh/bin/python}"
[ -x "$PY" ] || PY=python3
[ -x "$RH_SERVER" ] || { echo "FAIL: no server binary (set RH_SERVER)"; exit 1; }

WORK=$(mktemp -d /tmp/rh-8seats.XXXXXX)
cleanup() {
    [ -n "${SERVER_PID:-}" ] && kill "$SERVER_PID" 2>/dev/null
    [ -n "${NODE_PID:-}" ] && kill "$NODE_PID" 2>/dev/null
    [ -n "${KEEP:-}" ] || rm -rf "$WORK"
}
trap cleanup EXIT

echo "== building the host node"
mkdir -p "$WORK/build"
g++ -std=c++17 -O2 -I "$ROOT/src" -o "$WORK/build/pty_node" \
    "$ROOT/test/host/pty_node.cpp" \
    "$ROOT/src/multipilot/seat_engine.cpp" \
    "$ROOT/src/multipilot/rh_node.cpp" -lutil || exit 1

echo "== starting the node"
"$WORK/build/pty_node" --port-file "$WORK/port" --state "$WORK/state.json" \
    > "$WORK/node.log" 2>&1 &
NODE_PID=$!
for _ in $(seq 1 50); do [ -s "$WORK/port" ] && break; sleep 0.1; done
PTY=$(cat "$WORK/port" 2>/dev/null)
[ -n "$PTY" ] || { echo "FAIL: no pty"; cat "$WORK/node.log"; exit 1; }

HTTP_PORT=$("$PY" - <<'PY'
import socket
s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()
PY
)
mkdir -p "$WORK/data"
printf '{"SECRETS":{"ADMIN_USERNAME":"","ADMIN_PASSWORD":""},"GENERAL":{"HTTP_PORT":%s,"MOCK_NODE_SIGNAL":"0","SERIAL_PORTS":["%s"]}}' \
    "$HTTP_PORT" "$PTY" > "$WORK/data/config.json"

echo "== starting the server ($HTTP_PORT) on $PTY"
RH_DATA_DIR="$WORK/data" setsid nohup "$RH_SERVER" > "$WORK/server.log" 2>&1 < /dev/null &
SERVER_PID=$!
for _ in $(seq 1 120); do
    sleep 0.5
    curl -sf "http://127.0.0.1:$HTTP_PORT/api/status" > /dev/null 2>&1 && break
done

fail=0
require() { if ! grep -qE "$1" "$WORK/server.log"; then echo "FAIL: $2"; fail=1; fi; }

echo "== discovery"
require "Serial multi-node found.*count=8" "the board was not detected as eight seats"
require "Found 8 serial node" "the server did not create eight nodes"

echo "== the server looked like a real device to it"
"$PY" - "$WORK/state.json" <<'PY' || fail=1
import json, sys, time
path = sys.argv[1]
deadline = time.time() + 20
while time.time() < deadline:
    try:
        state = json.load(open(path))
    except Exception:
        time.sleep(0.2); continue
    freqs = [f for f in state["seats"] if f]
    if len(freqs) >= 6:
        print("  programmed seats:", freqs)
        sys.exit(0)
    time.sleep(0.2)
print("FAIL: the profile never reached the board:", state)
sys.exit(1)
PY

echo "== register test: unreachable frequency must warn, a good one must not"
OUT=$("$PY" "$HERE/c5mk_probe.py" "http://127.0.0.1:$HTTP_PORT" 0 5000 5800)
echo "$OUT" | sed 's/^/  /'
echo "$OUT" | "$PY" -c '
import json, sys
lines = [json.loads(l) for l in sys.stdin if l.strip()]
bad = [l for l in lines if l["frequency"] == 5000]
good = [l for l in lines if l["frequency"] == 5800]
ok = bool(bad) and bad[0]["failed"] and bool(good) and not good[0]["failed"]
print("  PASS: warning raised for 5000, silent for 5800" if ok else "  FAIL: register test not reported")
sys.exit(0 if ok else 1)
' || fail=1

echo "== sustained polling (8 s) while the sampler runs"
# The 5000 MHz probe above is supposed to have warned once; anything further
# means a seat that should be fine is not.
WARN_BEFORE=$(grep -c "failed register test" "$WORK/server.log" || true)
sleep 8
WARN_AFTER=$(grep -c "failed register test" "$WORK/server.log" || true)
if [ "$WARN_AFTER" -gt "$WARN_BEFORE" ]; then
    echo "FAIL: a seat failed its register test unexpectedly"
    grep "failed register test" "$WORK/server.log" | tail -3
    fail=1
fi
"$PY" - "$WORK/state.json" <<'PY' || fail=1
import json, sys
state = json.load(open(sys.argv[1]))
print("  board passes:", state["passes"], "programmed:", state["programmed"])
if state["passes"] < 2:
    print("FAIL: the sampler stopped producing passes")
    sys.exit(1)
PY

if [ "$fail" -eq 0 ]; then
    echo "PASS: eight-seat node on the real RotorHazard server"
else
    echo "server log:"; tail -30 "$WORK/server.log"
    exit 1
fi
