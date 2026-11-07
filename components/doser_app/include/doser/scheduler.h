#pragma once
// Scheduler (M3): runs the plans of all pumps via the dosing engine.
//
// A task checks once per second whether the next slot of a pump is due (the
// slot itself is computed from the schedule; nothing runs every second).
// Every slot is processed exactly once (cursor in EEPROM):
//   * started             -> dosing::dose(), trigger "schedule" or "boost"
//   * lock time / busy    -> delayed and retried, dropped after max_delay_min
//   * missed (power loss) -> skipped or caught up (policy per pump)
//   * rejected            -> skipped with reason (daily limit, empty, mode ...)
// Without a valid clock nothing is scheduled.
//
// All functions are thread-safe. Return const char*: nullptr = ok, otherwise
// an error code (English, snake_case) for API and UI.

#include <cstdint>

#include "doser/dosing.h"
#include "doser/schedule_core.h"
#include "esp_err.h"

namespace doser::scheduler {

constexpr int kMaxConflicts = 8;

struct SaveResult {
    sched::Validation validation;  // error "conflict": see conflicts
    sched::Conflict conflicts[kMaxConflicts];
};

esp_err_t init();  // after dosing::init

// Schedules and lock times are validated together with everything else
const char* set_schedule(int pump, const sched::PumpSchedule& s, SaveResult* r = nullptr);
sched::PumpSchedule schedule(int pump);
const char* set_locks(const sched::Locks& l, SaveResult* r = nullptr);
sched::Locks locks();
// All schedules and the lock rules at once (restore of a backup): checked
// together, nothing is changed if they do not fit
const char* set_all(const sched::PumpSchedule (&all)[5], const sched::Locks& l, SaveResult* r = nullptr);
const char* set_config(const sched::Config& c);
sched::Config config();

// Boost: extra amount spread over `days` days on top of the existing slots
const char* boost_start(int pump, uint32_t total_ul, uint16_t days);
void boost_stop(int pump);

// Manual dose with the lock-time policy: Reject -> "collision_lock" with
// *retry_at; Queue -> *queued = true, started as soon as the lock ends.
const char* manual_dose(int pump, uint32_t ul, dosing::Trigger trigger, int tier, int64_t* retry_at, bool* queued);
void cancel_queued(int pump);

struct Status {
    bool clock_valid;
    // next slot
    bool has_next;
    int64_t next_utc;
    uint32_t next_ul;  // incl. no boost share (decided when due)
    // delayed slot
    bool delayed;
    int64_t delayed_since;
    int64_t retry_at;           // INT64_MAX = other pump still running
    const char* delay_reason;   // "collision_lock", "pump_busy", "locked"
    // queued manual dose
    bool queued;
    uint32_t queued_ul;
    int64_t queued_since;
    // statistics
    uint32_t skipped;
    const char* last_skip_reason;  // "missed", "daily_limit", "delay_timeout", ...
    int64_t last_skip_at;
    int64_t last_run_at;
    // plan
    uint32_t planned_today_ul;
    float remaining_days;  // < 0: not tracked or nothing planned
    sched::Boost boost;
    bool boost_active;
};
Status status(int pump);
// Local day number of now (for API/UI), -1 without valid clock
int32_t today();
// UTC -> local wall-clock seconds in the device time zone (for API/UI)
int64_t to_local(int64_t utc);

}  // namespace doser::scheduler
