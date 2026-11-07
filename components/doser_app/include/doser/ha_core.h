#pragma once
// MQTT topics, Home Assistant discovery and command parsing (M6). Pure
// logic, host-tested (tests/host/test_ha.cpp); mqtt_ha.cpp feeds it.
//
// Topics below the base topic <b>:
//   <b>/status                online | offline (retained, last will)
//   <b>/state                 {"critical","warning","conditions":[...]}
//   <b>/pump/<n>/state        {"activity","total_ml","planned_ml","remaining_ml",
//                              "days","next","schedule","amount_ml","tube_h","last"}
//   commands (only with "control" enabled):
//   <b>/pump/<n>/dose/set     "PRESS" = stored amount, or an amount in ml ("2.5")
//   <b>/pump/<n>/amount/set   amount in ml for "PRESS"
//   <b>/pump/<n>/schedule/set ON | OFF (pause/resume the schedule)
//   <b>/stop/set              any payload: stop all pumps
// Discovery: one retained device message <prefix>/device/<id>/config with
// all entities. Entities that do not apply (control off) are sent with the
// platform only, which removes them in Home Assistant.

#include <cstdint>
#include <string>

#include "lgal/json.h"

namespace doser::ha {

constexpr int kPumps = 5;

struct DeviceInfo {
    std::string id;       // "lgal_doser_eeff" (unique, object id)
    std::string name;     // device name in Home Assistant
    std::string firmware;
    std::string url;      // configuration URL, "" = none
    std::string base;     // base topic
    bool german = false;  // entity names in German
    bool control = false;
    std::string pump_names[kPumps];  // "" = "Pumpe n" / "Pump n"
};

std::string discovery_topic(const std::string& prefix, const std::string& id);
void write_discovery(lgal::Json& j, const DeviceInfo& d);

struct PumpState {
    const char* activity = "idle";
    uint64_t total_ul = 0;
    uint32_t planned_ul = 0;
    bool tracked = false;        // container size known
    int32_t remaining_ul = 0;
    float days = -1.0f;          // < 0 = unknown
    bool has_next = false;
    int64_t next_utc = 0;
    bool has_schedule = false;
    bool schedule_on = false;
    uint32_t amount_ul = 0;
    float tube_h = 0.0f;         // tube operating hours
    const char* last = nullptr;  // result of the last job
};
void write_pump_state(lgal::Json& j, const PumpState& s);

struct DeviceState {
    bool critical = false;
    bool warning = false;
    uint16_t conditions = 0;  // alarms::conditions() bits
};
void write_device_state(lgal::Json& j, const DeviceState& s);

// "2026-11-07T07:00:00+00:00"
void fmt_iso_utc(int64_t utc, char (&out)[26]);

enum class Cmd : uint8_t { None, Dose, Amount, Schedule, Stop };
struct Command {
    Cmd cmd = Cmd::None;
    int pump = -1;      // 0..4
    bool press = false; // dose: use the stored amount
    uint32_t ul = 0;    // dose/amount
    bool on = false;    // schedule
};
constexpr uint32_t kMaxCommandUl = 1000000;  // 1 l: plausibility only, dosing checks the limits
// Topic and payload as received (not terminated). false = not ours or invalid.
bool parse_command(const std::string& base, const char* topic, int topic_len, const char* data, int data_len,
                   Command* out);
// Amount "2.5" (ml, up to 3 decimals) -> µl; false if not a number in 0 < x <= kMaxCommandUl
bool parse_ml(const char* s, int len, uint32_t* ul);

}  // namespace doser::ha
