#!/bin/bash
# Host unit tests (no hardware). Needs g++.
set -e
cd "$(dirname "$0")"
mkdir -p build
REPO="$(cd ../.. && pwd)"

g++ -std=c++17 -Wall -Wextra -O2 -o build/test_burst_filter test_burst_filter.cpp
./build/test_burst_filter

g++ -std=c++17 -Wall -Wextra -O2 -I"$REPO/src" -o build/test_seat_engine \
    test_seat_engine.cpp "$REPO/src/multipilot/seat_engine.cpp" "$REPO/src/multipilot/rh_node.cpp"
./build/test_seat_engine

python3 test_flasher_plugin.py
