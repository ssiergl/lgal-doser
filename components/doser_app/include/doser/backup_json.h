#pragma once
// Full backup of the device configuration as one JSON document (engineering
// area, M6). Pure logic, host-tested; backup_api.cpp collects and applies.
//
//   {"format":"lgal-doser-backup","version":1,"firmware","device","exported_utc",
//    "system":{"language","timezone","ntp_server",
//              "led_night":{"enabled","start","end","brightness"}},
//    "network":{"mode","lan":{...},"wlan":{...}, "wlan_password"?},
//    "pumps":[{"pump":1,"name","mode","tier","daily_limit_ml","retract_mrev",
//              "capacity_ml","ul_per_rev","calibrated_at",
//              "schedule":{...}} x5],
//    "locks":{...},
//    "engineering":{"dosing":{...},"scheduler":{"grid_min","max_delay_min"},
//                   "hardware":{"pumps":[{"run_ma","hold_ma","microsteps",
//                   "invert","full_steps","accel","v_start"}]},
//                   "history":{"keep_days"}},   (history optional: older files lack it)
//    "mqtt":{"enabled","host",...(see mqtt_config.h),"password"?,"ca_pem"},
//    "auth":{"password","keys"}?}
// Secrets (Wi-Fi and MQTT password, admin password record, API key hashes)
// are only included on request. Runtime state (container content, counters, history)
// is not part of the backup.

#include <cstdint>
#include <string>

#include "doser/dosing_core.h"
#include "doser/history_core.h"
#include "doser/motion.h"
#include "doser/schedule_core.h"
#include "lgal/json.h"
#include "lgal/json_reader.h"
#include "lgal/led_night.h"
#include "lgal/mqtt_config.h"
#include "lgal/net_config.h"

namespace doser::backup {

constexpr const char* kFormat = "lgal-doser-backup";
constexpr int kVersion = 1;

enum Part : uint8_t {
    kSystem = 1,
    kNetwork = 2,
    kPumps = 4,        // pump settings, schedules, lock rules
    kEngineering = 8,  // dosing/scheduler parameters, hardware
    kAuth = 16,        // admin password and API keys
    kMqtt = 32,        // broker settings and CA certificate
};
constexpr uint8_t kAllParts = kSystem | kNetwork | kPumps | kEngineering | kAuth | kMqtt;
const char* part_str(Part p);
// "system,pumps" -> bits; false on an unknown name
bool parse_parts(const char* s, uint8_t* out);

struct Data {
    uint8_t parts = 0;  // parts present (parse) or to write
    // system
    char language[8] = "";
    char timezone[65] = "";
    char ntp_server[65] = "";
    lgal::led::NightMode led;
    // network
    lgal::netcfg::Config net;
    bool wlan_password = false;  // net.password is part of the document
    // pumps
    dose::PumpSettings pumps[5];
    sched::PumpSchedule schedules[5];
    sched::Locks locks;
    // engineering
    dose::Engineering dosing;
    sched::Config scheduler;
    motion::PumpConfig hw[5];
    uint16_t history_keep_days = 0;  // 0 = not in the document (older backups): unchanged
    // mqtt
    lgal::mqttcfg::Config mqtt;
    bool mqtt_password = false;  // mqtt.password is part of the document
    std::string mqtt_ca;         // PEM, "" = none
};

struct Meta {
    const char* firmware = "";
    const char* device = "";
    int64_t utc = 0;
};

// Whole document; the auth part is written by the callback (opaque to this
// module) into the open "auth" object when kAuth is set.
void write(lgal::Json& j, const Data& d, const Meta& m, void (*write_auth)(lgal::Json&) = nullptr);

struct Error {
    const char* code = nullptr;  // "invalid_format", "unsupported_version", "invalid_param"
    char field[48] = "";         // e.g. "pumps[2].schedule" or "engineering.dosing.tier_rpm"
};

// Parses and validates every part present in the document. `base` supplies
// values the document does not carry (current configuration), `today` the
// local day for schedules (-1 without a valid clock). The auth part is only
// checked for presence (kAuth in parts); auth::restore_secrets validates it.
bool parse(const lgal::JsonValue& v, const Data& base, int32_t today, Data* out, Error* err);

}  // namespace doser::backup
