#!/bin/bash
# Host unit tests (no hardware). Needs g++.
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -std=c++17 -Wall -Wextra -O2 -o build/test_burst_filter test_burst_filter.cpp
./build/test_burst_filter
