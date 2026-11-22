// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
// JSON/CSV form of history events and the daily sums for the charts.
// Pure logic, host-tested; history_api.cpp does the HTTP part.

#include <cstddef>
#include <cstdint>

#include "doser/history_core.h"
#include "doser/limits.h"
#include "lgal/json.h"

namespace doser::history_json {

// Local seconds -> "YYYY-MM-DD HH:MM:SS"
void fmt_local_s(int64_t local, char (&out)[20]);

// One event as an object in an open array. Fields depend on the kind:
//   dose/prime/calib_run  target_ml, done_ml
//   dose                  lock_overridden: pump 1..5 whose lock time the dose passed over
//                         (lock=override), missing otherwise; CSV value column
//   skip                  target_ml (single slot), count
//   container             remaining_ml, capacity_ml
//   calibration           target_ml, measured_ml, ul_per_rev
//   tube                  hours
//   alarm                 code only; pump 0 = device
//   settings              changed: ["name","speed",…], direction: cw|ccw (the new one,
//                         with "direction" in changed); missing in older entries
//   system "settings"     area: language|time|led|name (missing in older entries)
// leave_open: the caller adds members and closes the object.
void write_event(lgal::Json& j, const history::Event& e, int64_t local, bool leave_open = false);

// "dose,skip" -> kind bitmask for history::Filter; false on an unknown kind
bool parse_kinds(const char* s, uint16_t* out);

// CSV with a header line; value = lock_overridden (dose), remaining ml (container), µl/rev
// (calibration), hours (tube), slot count (skip), the changed groups
// ("name+speed", settings) or the area (system "settings")
const char* csv_header();
// Line incl. "\r\n"; returns the length, 0 if it does not fit
size_t csv_line(const history::Event& e, int64_t local, char* out, size_t len);

// Dosed amounts per local day and pump (dose events of every source) and
// skipped slots, for the last `days` local days up to and including `today`.
constexpr int kMaxDays = 90;
struct Daily {
    int32_t first_day = 0;  // local day number of index 0
    int days = 0;
    uint32_t dosed_ul[kMaxDays][kMaxPumps] = {};
    uint16_t doses[kMaxDays][kMaxPumps] = {};
    uint16_t skipped[kMaxDays][kMaxPumps] = {};

    void reset(int32_t today, int n_days);
    // Adds the event if it falls into the range; local = to_local(e.utc)
    void add(const history::Event& e, int64_t local);
    void write(lgal::Json& j) const;  // "days":[{"date","ml":[…],"doses":[…],"skipped":[…]}] oldest first, kMaxPumps each
};

}  // namespace doser::history_json
