#pragma once
// JSON form of schedules and lock rules (see schedule_api.h for the format).
// No ESP dependencies (host tests: tests/host/test_schedule_json.cpp).
//
// Parsing checks types and formats only; limits and conflicts are checked
// by sched::validate when saving.

#include <cstdint>

#include "doser/schedule_core.h"
#include "lgal/json.h"
#include "lgal/json_reader.h"

namespace doser::sched_json {

struct Error {
    const char* code = nullptr;  // "invalid_param", "time_invalid" (interval without anchor and clock)
    int entry = -1;              // entry of "times", 0-based, -1 = whole plan
    int rule = -1;               // lock rule, 0-based
    const char* field = nullptr;
};

// today: local day number for the default anchor, < 0 = unknown
bool parse_schedule(const lgal::JsonValue& v, int32_t today, sched::PumpSchedule* out, Error* err);
// Members into the currently open object
void write_schedule(lgal::Json& j, const sched::PumpSchedule& s);

bool parse_locks(const lgal::JsonValue& v, sched::Locks* out, Error* err);
void write_locks(lgal::Json& j, const sched::Locks& l);

// One conflict as an object (pumps 1-based)
void write_conflict(lgal::Json& j, const sched::Conflict& c);

// "HH:MM" (00:00..23:59)
bool parse_hhmm(const char* s, uint16_t* minute);
void fmt_hhmm(uint16_t minute, char (&out)[6]);
// "YYYY-MM-DD" <-> day number (days since 1970-01-01, proleptic Gregorian)
int32_t days_from_civil(int y, unsigned m, unsigned d);
void civil_from_days(int32_t day, int* y, unsigned* m, unsigned* d);
bool parse_date(const char* s, int32_t* day);
void fmt_date(int32_t day, char (&out)[11]);
// Local seconds -> "YYYY-MM-DD HH:MM"
void fmt_local(int64_t local, char (&out)[17]);

}  // namespace doser::sched_json
