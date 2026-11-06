#!/bin/sh
# Builds and runs the host tests (no ESP-IDF needed).
set -e
cd "$(dirname "$0")/../.."
OUT="${TMPDIR:-/tmp}/lgal_host_tests"
c++ -std=c++20 -O1 -Wall -Wextra -Werror \
    -I components/doser_hal/include \
    tests/host/test_main.cpp \
    components/doser_hal/src/tmc2209_proto.cpp \
    components/doser_hal/src/ramp.cpp \
    -o "$OUT"
"$OUT"
