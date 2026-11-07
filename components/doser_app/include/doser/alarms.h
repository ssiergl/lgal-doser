#pragma once
// Alarm and warning of the status LED from the pump state (functional spec
// chapter 10). Pure logic, host-tested; main.cpp collects the input.
//   critical (prio 1, red fast): a scheduled dose skipped today because of
//            the daily limit, driver fault, clock module not answering
//   warning  (prio 6, yellow slow): container at most 10 % or less than 3 days
//            left, tube lifetime reached, no valid time
// Disabled pumps are ignored.

#include <cstdint>

namespace doser::alarms {

constexpr float kLowPercent = 10.0f;
constexpr float kLowDays = 3.0f;

struct Pump {
    bool active = true;            // operating mode is not "disabled"
    bool fault = false;            // last job ended with a driver fault
    bool limit_hit_today = false;  // scheduled dose skipped today: daily limit
    bool tracked = false;          // container size known
    float percent = 100.0f;        // container fill level
    float remaining_days = -1.0f;  // < 0 = unknown
    float tube_percent = 0.0f;     // tube runtime / lifetime
};

struct Input {
    Pump pumps[5];
    bool rtc_ok = true;
    bool clock_valid = true;
};

struct Output {
    bool critical = false;
    bool warning = false;
};

Output evaluate(const Input& in);

}  // namespace doser::alarms
