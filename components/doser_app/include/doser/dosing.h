#pragma once
// Dosing engine (M2): amount in µl -> steps, anti-drip retraction,
// calibration, priming with dead-man heartbeat, container, daily limit,
// tube runtime.
//
// Sequence of one run per pump:
//   [compensate retraction] -> main move -> [retraction]
// Amounts are booked in full at start (container, daily limit) and saved;
// an abort credits back the amount not pumped. If power fails during the
// move, the full amount stays booked (safe side).
//
// All functions are thread-safe. Return const char*: nullptr = ok,
// otherwise an error code (English, snake_case) for API and UI.

#include <cstdint>

#include "doser/dosing_core.h"
#include "doser/history_core.h"
#include "doser/schedule_core.h"
#include "esp_err.h"

namespace doser::dosing {

enum class Activity : uint8_t { Idle, Dosing, Priming, Calibrating };
enum class Phase : uint8_t { None, Unretract, Main, Retract };
enum class Trigger : uint8_t { Manual, Api, Schedule, Boost, Mqtt };

const char* activity_str(Activity a);
const char* phase_str(Phase p);
const char* trigger_str(Trigger t);

struct Status {
    Activity activity;
    Phase phase;
    Trigger trigger;
    uint32_t target_ul;      // running job
    uint32_t done_ul;        // of which pumped (main move)
    float rpm;               // main move
    uint32_t prime_ms;       // priming: duration so far
    // last finished job
    const char* last_result;  // "success", "aborted", "fault", "locked", nullptr
    Activity last_activity;
    uint32_t last_target_ul;
    uint32_t last_done_ul;
    int64_t last_at;
    // calibration run finished, waiting for the measured amount
    bool calib_pending;
    uint32_t calib_steps;
    uint32_t calib_target_ul;
    // safety
    uint32_t used_today_ul;  // pumped within the limit window
    bool time_valid;
    uint32_t steps_per_rev;
    dose::PumpState state;
    dose::PumpSettings settings;
};

esp_err_t init();  // after motion::init and eeprom::init

// Dosing (amount in µl, speed tier 0..2 or -1 = pump setting).
// "collision_lock": a lock time to another pump is running; *retry_at is the
// UTC from which the dose is allowed (INT64_MAX: the other pump is still
// running, end unknown).
const char* dose(int pump, uint32_t ul, Trigger trigger, int tier = -1, int64_t* retry_at = nullptr);

// Priming ("fill tube"): runs as long as beat() arrives in time
const char* prime_start(int pump, int tier = -1);
const char* prime_beat(int pump);
// Ends priming only (other activities keep running); "not_priming" otherwise
const char* prime_stop(int pump);

// Calibration: pumps the target amount (0 = engineering default) with the
// current calibration value; then apply or discard the measured amount.
const char* calibrate_start(int pump, uint32_t target_ul = 0, int tier = -1);
const char* calibrate_apply(int pump, uint32_t measured_ul);
void calibrate_cancel(int pump);

// Soft stop of the running job (retraction is still executed)
void stop(int pump);
void stop_all();

// Records a mode change (history "mode") and any other change ("settings";
// also_changed: settings outside PumpSettings changed with it, e.g. the
// direction). source None records nothing (backup restore logs on its own).
const char* set_settings(int pump, const dose::PumpSettings& s, history::Source source = history::Source::None,
                         bool also_changed = false);
// Chemical lock times between pumps, checked on every dose (all triggers).
// Priming and calibration are not affected (operator present, measuring cup).
void set_locks(const sched::Locks& l);
// Parameters for the run-time estimate of scheduled doses
sched::RunParams run_params(int pump);
const char* set_engineering(const dose::Engineering& e);
// Container: set remaining amount (refill), capacity (-1 = unchanged)
const char* set_container(int pump, int64_t remaining_ul, int64_t capacity_ul);
// Tube counter: reset (0 = new tube, the pump has to be calibrated again) or
// set to hours
const char* set_tube_hours(int pump, uint32_t hours);

dose::Engineering engineering();
dose::PumpSettings settings(int pump);
Status status(int pump);

bool busy();     // any job active (OTA lock)
bool service();  // calibration/priming active (LED)

}  // namespace doser::dosing
