#!/bin/sh
# Builds and runs the host tests (no ESP-IDF needed).
#   tests/host/run.sh        Tests
#   tests/host/run.sh -v     simulations with engine log
set -e
cd "$(dirname "$0")/../.."
OUT="${TMPDIR:-/tmp}/lgal_host_tests"
mkdir -p "$OUT"
CXX="c++ -std=c++20 -O1 -Wall -Wextra -Werror"

$CXX \
    -I components/doser_hal/include \
    -I components/doser_app/include \
    tests/host/test_main.cpp \
    components/doser_hal/src/tmc2209_proto.cpp \
    components/doser_hal/src/ramp.cpp \
    components/doser_hal/src/posix_tz.cpp \
    components/doser_app/src/dosing_core.cpp \
    -o "$OUT/core"
"$OUT/core"

echo
$CXX \
    -I components/lgal_core/include \
    tests/host/test_json.cpp \
    components/lgal_core/src/json.cpp \
    components/lgal_core/src/json_reader.cpp \
    -o "$OUT/json"
"$OUT/json"

echo
# Dosing engine unchanged against simulated motor/storage (stubs first)
$CXX \
    -I tests/host/stubs \
    -I components/doser_hal/include \
    -I components/doser_app/include \
    -I components/lgal_core/include \
    tests/host/test_dosing.cpp \
    components/doser_app/src/dosing.cpp \
    components/doser_app/src/dosing_core.cpp \
    components/doser_app/src/schedule_core.cpp \
    components/doser_hal/src/posix_tz.cpp \
    components/doser_hal/src/tmc2209_proto.cpp \
    -o "$OUT/dosing"
"$OUT/dosing" $1

echo
$CXX \
    -I components/doser_hal/include \
    -I components/doser_app/include \
    tests/host/test_schedule.cpp \
    components/doser_app/src/schedule_core.cpp \
    components/doser_app/src/dosing_core.cpp \
    components/doser_hal/src/posix_tz.cpp \
    -o "$OUT/schedule"
"$OUT/schedule"

echo
# Scheduler with the real dosing engine against simulated motor/clock/storage
$CXX \
    -I tests/host/stubs \
    -I components/doser_hal/include \
    -I components/doser_app/include \
    -I components/lgal_core/include \
    tests/host/test_scheduler.cpp \
    components/doser_app/src/scheduler.cpp \
    components/doser_app/src/schedule_core.cpp \
    components/doser_app/src/dosing.cpp \
    components/doser_app/src/dosing_core.cpp \
    components/doser_hal/src/posix_tz.cpp \
    components/doser_hal/src/tmc2209_proto.cpp \
    -o "$OUT/scheduler"
"$OUT/scheduler" $1
