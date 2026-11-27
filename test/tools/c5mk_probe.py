#!/usr/bin/env python3
"""Set a seat frequency and report the operator warnings the server raised.

Usage: c5mk_probe.py <url> <node> <frequency> [<frequency> ...]
One JSON line per step: {"frequency": N, "priority": [...], "failed": bool}
"""

import json
import sys
import time

import socketio

url = sys.argv[1]
node = int(sys.argv[2])
freqs = [int(f) for f in sys.argv[3:]]

messages = []
sio = socketio.Client()


@sio.on("priority_message")
def on_priority(msg):
    messages.append(msg.get("message") if isinstance(msg, dict) else str(msg))


sio.connect(url, wait_timeout=15)
time.sleep(1.0)

for freq in freqs:
    messages.clear()
    sio.emit("set_frequency", {"node": node, "frequency": freq})
    time.sleep(1.2)
    failed = [m for m in messages if m and "Failed to set frequency" in m]
    print(json.dumps({"frequency": freq, "priority": messages, "failed": bool(failed)}))

sio.disconnect()
