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
$CXX \
    -I components/lgal_core/include \
    tests/host/test_auth.cpp \
    components/lgal_core/src/sha256.cpp \
    components/lgal_core/src/auth_core.cpp \
    -o "$OUT/auth"
"$OUT/auth"

echo
$CXX \
    -I components/lgal_core/include \
    tests/host/test_net_config.cpp \
    components/lgal_core/src/net_config.cpp \
    components/lgal_core/src/json.cpp \
    components/lgal_core/src/json_reader.cpp \
    -o "$OUT/net_config"
"$OUT/net_config"

echo
$CXX \
    -I components/lgal_core/include \
    tests/host/test_mqtt_config.cpp \
    components/lgal_core/src/mqtt_config.cpp \
    components/lgal_core/src/net_config.cpp \
    components/lgal_core/src/json.cpp \
    components/lgal_core/src/json_reader.cpp \
    -o "$OUT/mqtt_config"
"$OUT/mqtt_config"

echo
$CXX \
    -I components/lgal_core/include \
    tests/host/test_dns.cpp \
    components/lgal_core/src/dns_reply.cpp \
    -o "$OUT/dns"
"$OUT/dns"

echo
$CXX \
    -I components/lgal_core/include \
    tests/host/test_ws.cpp \
    components/lgal_core/src/ws_core.cpp \
    -o "$OUT/ws"
"$OUT/ws"

echo
# Dosing engine unchanged against simulated motor/storage (stubs first)
$CXX \
    -I tests/host/stubs \
    -I components/doser_hal/include \
    -I components/doser_app/include \
    -I components/lgal_core/include \
    tests/host/test_dosing.cpp \
    components/doser_app/src/dosing.cpp \
    components/doser_app/src/history_core.cpp \
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
$CXX \
    -I components/lgal_core/include \
    -I components/doser_hal/include \
    -I components/doser_app/include \
    tests/host/test_schedule_json.cpp \
    components/doser_app/src/schedule_json.cpp \
    components/doser_app/src/schedule_core.cpp \
    components/doser_app/src/dosing_core.cpp \
    components/doser_hal/src/posix_tz.cpp \
    components/lgal_core/src/json.cpp \
    components/lgal_core/src/json_reader.cpp \
    -o "$OUT/schedule_json"
"$OUT/schedule_json"

echo
# Scheduler with the real dosing engine against simulated motor/clock/storage
$CXX \
    -I tests/host/stubs \
    -I components/doser_hal/include \
    -I components/doser_app/include \
    -I components/lgal_core/include \
    tests/host/test_scheduler.cpp \
    components/doser_app/src/scheduler.cpp \
    components/doser_app/src/history_core.cpp \
    components/doser_app/src/schedule_core.cpp \
    components/doser_app/src/dosing.cpp \
    components/doser_app/src/dosing_core.cpp \
    components/doser_hal/src/posix_tz.cpp \
    components/doser_hal/src/tmc2209_proto.cpp \
    -o "$OUT/scheduler"
"$OUT/scheduler" $1

echo
$CXX \
    -I components/doser_app/include \
    -I components/lgal_core/include \
    tests/host/test_alarms.cpp \
    components/doser_app/src/alarms.cpp \
    -o "$OUT/alarms"
"$OUT/alarms"

echo
$CXX \
    -I components/doser_app/include \
    tests/host/test_rtc_trim.cpp \
    components/doser_app/src/rtc_trim_core.cpp \
    -o "$OUT/rtc_trim"
"$OUT/rtc_trim"

echo
$CXX \
    -I components/doser_app/include \
    tests/host/test_history.cpp \
    components/doser_app/src/history_core.cpp \
    -o "$OUT/history"
"$OUT/history"

echo
$CXX \
    -I components/doser_app/include \
    -I components/doser_hal/include \
    -I components/lgal_core/include \
    tests/host/test_history_json.cpp \
    components/doser_app/src/history_json.cpp \
    components/doser_app/src/history_core.cpp \
    components/doser_app/src/schedule_json.cpp \
    components/doser_app/src/schedule_core.cpp \
    components/doser_app/src/dosing_core.cpp \
    components/doser_hal/src/posix_tz.cpp \
    components/lgal_core/src/json.cpp \
    components/lgal_core/src/json_reader.cpp \
    -o "$OUT/history_json"
"$OUT/history_json"

echo
$CXX \
    -I components/doser_app/include \
    -I components/doser_hal/include \
    -I components/lgal_core/include \
    tests/host/test_ha.cpp \
    components/doser_app/src/ha_core.cpp \
    components/doser_app/src/alarms.cpp \
    components/lgal_core/src/json.cpp \
    components/lgal_core/src/json_reader.cpp \
    -o "$OUT/ha"
"$OUT/ha"

echo
$CXX \
    -I components/doser_app/include \
    tests/host/test_event_cursor.cpp \
    components/doser_app/src/event_cursor.cpp \
    -o "$OUT/event_cursor"
"$OUT/event_cursor"

echo
$CXX \
    -I tests/host/stubs \
    -I components/doser_app/include \
    -I components/doser_hal/include \
    -I components/lgal_core/include \
    tests/host/test_backup_json.cpp \
    components/doser_app/src/backup_json.cpp \
    components/lgal_core/src/mqtt_config.cpp \
    components/doser_app/src/schedule_json.cpp \
    components/doser_app/src/schedule_core.cpp \
    components/doser_app/src/dosing_core.cpp \
    components/doser_hal/src/posix_tz.cpp \
    components/doser_hal/src/tmc2209_proto.cpp \
    components/lgal_core/src/net_config.cpp \
    components/lgal_core/src/json.cpp \
    components/lgal_core/src/json_reader.cpp \
    -o "$OUT/backup_json"
"$OUT/backup_json"
