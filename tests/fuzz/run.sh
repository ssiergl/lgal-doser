#!/bin/sh
# Builds the parser fuzzer with AddressSanitizer and UndefinedBehaviorSanitizer
# and runs it (no ESP-IDF needed).
#   tests/fuzz/run.sh [rounds per target] [seed]
set -e
cd "$(dirname "$0")/../.."
OUT="${TMPDIR:-/tmp}/lgal_fuzz"
mkdir -p "$OUT"
c++ -std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all \
    -Wall -Wextra -Werror \
    -I tests/host/stubs \
    -I components/lgal_core/include \
    -I components/doser_app/include \
    -I components/doser_hal/include \
    tests/fuzz/fuzz_parsers.cpp \
    components/doser_app/src/backup_json.cpp \
    components/doser_app/src/schedule_json.cpp \
    components/doser_app/src/schedule_core.cpp \
    components/doser_app/src/dosing_core.cpp \
    components/doser_app/src/history_json.cpp \
    components/doser_app/src/history_core.cpp \
    components/doser_hal/src/posix_tz.cpp \
    components/doser_hal/src/tmc2209_proto.cpp \
    components/lgal_core/src/auth_core.cpp \
    components/lgal_core/src/sha256.cpp \
    components/lgal_core/src/device_name.cpp \
    components/lgal_core/src/dns_reply.cpp \
    components/lgal_core/src/json.cpp \
    components/lgal_core/src/json_reader.cpp \
    components/lgal_core/src/mqtt_config.cpp \
    components/lgal_core/src/net_config.cpp \
    components/lgal_core/src/ota_manifest.cpp \
    components/lgal_core/src/ws_core.cpp \
    -o "$OUT/fuzz_parsers"
ASAN_OPTIONS=detect_leaks=0 "$OUT/fuzz_parsers" "$@"
