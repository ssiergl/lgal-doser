#pragma once
// Schedule logic (M3) without hardware/ESP dependencies (host tests: tests/host).
//
// Every pump has exactly one plan. It runs on certain days (daily, weekdays
// or every N days) and is either "spread" ("X ml in N doses between A and
// B") or "times" (single times, each with its own amount). Whether it runs
// at all is the pump's mode (auto / manual only). Times are local wall-clock
// minutes on a grid (default 5 min); the mapping to UTC around DST changes is done with
// tz::to_utc (gap: shifted forward, ambiguous: first occurrence).
//
// Days are counted as local day numbers (days since 1970-01-01 in local time).
// Units: amounts in µl, times in seconds unless named *_min.

#include <cstdint>

#include "doser/posix_tz.h"

namespace doser::sched {

constexpr int kPumps = 5;
constexpr int kMaxTimes = 24;         // single times of a "times" plan
constexpr int kMaxSlotsPerDay = 96;   // per pump and day
constexpr int kMaxLocks = 10;
constexpr int kMaxInterval = 30;      // "every N days"
constexpr uint16_t kMinutesPerDay = 1440;
constexpr int64_t kOnTimeTolerance = 120;  // a slot started this late is not "missed"

// Scheduler parameters (super admin, functional spec 11.2)
struct Config {
    uint16_t grid_min = 5;        // smallest scheduling grid
    uint16_t max_delay_min = 30;  // a delayed slot (lock time, pump busy) is dropped after this
};
// nullptr = valid, otherwise name of the invalid field
const char* validate(const Config& c);

// ---------------------------------------------------------------------------
// Days
enum class DayMode : uint8_t { Weekdays = 0, Interval = 1 };

struct DayRule {
    DayMode mode = DayMode::Weekdays;
    uint8_t weekdays = 0x7F;  // bit 0 = Monday .. bit 6 = Sunday
    uint8_t interval = 1;     // every N days (Interval), 1..kMaxInterval
    uint8_t reserved = 0;
    int32_t anchor_day = 0;   // Interval: a day on which the plan runs
};

inline int weekday_index(int32_t day) { return static_cast<int>(((day % 7) + 7 + 3) % 7); }  // 0 = Monday
inline int32_t day_of(int64_t local) { return static_cast<int32_t>(local >= 0 ? local / 86400 : (local - 86399) / 86400); }
bool active_on(const DayRule& r, int32_t day);
// Is there a day d on which a runs and b runs on d + offset? (exact, all future days)
bool can_coincide(const DayRule& a, const DayRule& b, int offset);

// ---------------------------------------------------------------------------
// Plan and slots
enum class Kind : uint8_t { None = 0, Spread = 1, Times = 2 };  // None = no schedule
const char* kind_str(Kind k);
bool parse_kind(const char* s, Kind* out);

enum class PowerLoss : uint8_t { SkipMissed = 0, CatchUpWindow = 1 };
const char* power_loss_str(PowerLoss p);
bool parse_power_loss(const char* s, PowerLoss* out);

struct TimeDose {
    uint16_t minute = 0;
    uint16_t reserved = 0;
    uint32_t ul = 0;
};

struct PumpSchedule {
    Kind kind = Kind::None;
    uint8_t reserved = 0;         // formerly the pause flag; keeps the stored layout
    PowerLoss power_loss = PowerLoss::SkipMissed;
    uint8_t time_count = 0;       // Times: used entries (ascending, distinct)
    uint16_t catch_up_min = 60;   // CatchUpWindow: only slots at most this old
    uint16_t count = 1;           // Spread: number of doses
    uint16_t start_min = 0;       // Spread: first dose
    uint16_t end_min = 0;         // Spread: last dose (inclusive)
    uint32_t amount_ul = 0;       // Spread: total per day
    DayRule days;
    TimeDose times[kMaxTimes];
};
static_assert(sizeof(PumpSchedule) == 24 + 8 * kMaxTimes);

struct Slot {
    uint16_t minute;
    uint8_t index;     // position in the plan's day (0-based)
    uint8_t reserved;
    uint32_t ul;
};

// Slots of the plan on a day it runs (sorted by time), independent of days
// and mode; a plan without kind has none. Returns the number of slots or
// -1 with *err: "invalid_time", "off_grid", "invalid_range", "invalid_count",
// "slots_too_dense", "duplicate_time", "dose_too_small", "dose_too_large",
// "invalid_days", "invalid_kind"; *entry: affected entry of "times" or -1.
int expand(const PumpSchedule& s, uint16_t grid_min, uint32_t min_dose_ul, Slot* out, int max, const char** err,
           int* entry = nullptr);
// Plan active on that day (has a kind, day rule)
bool runs_on(const PumpSchedule& s, int32_t day);
// Slots of the pump on a day (empty if it does not run), sorted by time
int day_slots(const PumpSchedule& s, int32_t day, uint16_t grid_min, Slot* out, int max);
// Planned amount of the pump on a day
uint32_t day_total(const PumpSchedule& s, int32_t day);

// ---------------------------------------------------------------------------
// Runtime of one scheduled dose (compensate retraction + main move + retraction)
struct RunParams {
    float ul_per_rev = 370.0f;
    uint16_t rpm = 200;
    uint16_t retract_mrev = 250;
    uint16_t accel_rpm_s = 600;
};
constexpr uint32_t kRunMarginS = 2;  // driver enable, task ticks, StealthChop tuning
uint32_t run_seconds(uint32_t ul, const RunParams& p);

// ---------------------------------------------------------------------------
// Chemical lock times between pumps (symmetric): after the end of a dose on
// one pump the other one must wait at least gap_min.
enum class ManualDuringLock : uint8_t { Reject = 0, Queue = 1 };
const char* manual_during_lock_str(ManualDuringLock m);
bool parse_manual_during_lock(const char* s, ManualDuringLock* out);

struct LockRule {
    uint8_t a, b;  // pump index 0..4, a != b
    uint16_t gap_min;
};

struct Locks {
    uint8_t count = 0;
    ManualDuringLock manual = ManualDuringLock::Reject;
    LockRule rules[kMaxLocks] = {};
};
// Lock time between two pumps in seconds (largest matching rule), 0 = none
int64_t lock_gap(const Locks& l, int pa, int pb);
// Runtime guard: earliest start for pump `pump`. last_end: end of the last
// dose per pump (0 = none), running: dose in progress (end unknown ->
// INT64_MAX). Returns `now` or later.
int64_t lock_until(const Locks& l, int pump, const int64_t (&last_end)[kPumps], const bool (&running)[kPumps],
                   int64_t now);

// ---------------------------------------------------------------------------
// Validation of all schedules together (before saving)
struct Context {
    RunParams run[kPumps];
    uint32_t daily_limit_ul[kPumps] = {};
    uint32_t min_dose_ul = 20;
    uint16_t grid_min = 5;
    int32_t today = 0;  // local day number, for the daily limit check
};

struct SlotRef {
    uint8_t pump;
    uint8_t reserved;
    uint16_t minute;
};

struct Conflict {
    const char* code;  // "overlap" (same pump) or "collision_lock" (lock time)
    SlotRef first, second;
    uint8_t next_day;  // second slot is on the following day
    uint16_t gap_min;  // collision_lock: required gap
};

struct Validation {
    const char* error = nullptr;  // structural error (see expand) or "invalid_catch_up",
                                  // "exceeds_daily_limit", "invalid_lock", "conflict"
    int pump = -1;
    int entry = -1;               // entry of "times" (0-based)
    int rule = -1;                // invalid_lock: rule (0-based)
    int conflicts = 0;            // number of entries in the conflict array
};
Validation validate(const PumpSchedule (&all)[kPumps], const Locks& locks, const Context& ctx, Conflict* out,
                    int max_out);

// ---------------------------------------------------------------------------
// Due slots. The scheduler keeps a cursor (last processed slot); slots are
// ordered by (utc, tag) so that two slots mapped to the same UTC second by
// a DST gap are both executed exactly once.
struct Cursor {
    int64_t utc = 0;
    uint32_t tag = 0;  // minute * 16 + 1 (minutes are unique per plan), 0 = before all slots of that second
    uint32_t reserved = 0;  // explicit padding (CRC over defined bytes)
};

struct Due {
    int64_t utc;
    int32_t day;
    uint16_t minute;
    uint32_t ul;
    uint32_t tag;
    int remaining_today;  // slots of the same local day from this one on (incl.)
};
// Earliest slot after the cursor with utc <= until. false = none.
bool next_due(const PumpSchedule& s, const tz::Zone& z, uint16_t grid_min, const Cursor& after, int64_t until,
              Due* out);
inline Cursor cursor_of(const Due& d) { return Cursor{d.utc, d.tag}; }

// Number of slots in (after, until] (e.g. how many were skipped)
int count_due(const PumpSchedule& s, const tz::Zone& z, uint16_t grid_min, Cursor after, int64_t until);

enum class Action : uint8_t { Run, SkipMissed };
// What to do with a slot that becomes due at `now` (power loss, clock jump)
Action on_due(const PumpSchedule& s, const Due& d, int64_t now);
// After a long outage: cursor from which slots can still run at all; slots
// between `c` and the result are skipped without looking at each one.
Cursor resume_point(const PumpSchedule& s, const Cursor& c, int64_t now);

// ---------------------------------------------------------------------------
// Boost: extra amount spread over `days` days on top of the existing slots.
// Each day gets (remaining / remaining days), split evenly over the slots
// still to come that day; amounts that cannot be dosed move to later days.
struct Boost {
    uint32_t remaining_ul = 0;  // 0 = no boost active
    uint16_t days = 0;
    uint16_t reserved = 0;
    int32_t start_day = 0;
    int32_t budget_day = 0;     // day the budget below belongs to
    uint32_t budget_ul = 0;     // still to dose on budget_day
    uint32_t total_ul = 0;      // as started (display)
};
static_assert(sizeof(Boost) == 24);
void boost_start(Boost& b, uint32_t total_ul, uint16_t days, int32_t today);
bool boost_active(const Boost& b, int32_t today);
// Share for the slot due now; reduces the boost. remaining_slots: slots of
// today from this one on (Due::remaining_today).
uint32_t boost_take(Boost& b, int32_t today, int remaining_slots);
// Dose failed or was aborted: give the share back
void boost_refund(Boost& b, int32_t today, uint32_t ul);

// ---------------------------------------------------------------------------
// Run state per pump (written after every processed slot -> EEPROM A/B,
// NVS as fallback), sealed like the dosing records.
struct RunState {
    Cursor cursor;
    Boost boost;
    uint32_t skipped = 0;  // slots skipped since the counter was reset (statistics)
    uint32_t reserved = 0;
};
static_assert(sizeof(RunState) == 48);

struct RunRecord {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t seq;
    uint32_t reserved;
    RunState state;
    uint32_t crc;
    uint32_t reserved2;
};
static_assert(sizeof(RunRecord) == 72);
constexpr uint32_t kRunMagic = 0x4C475343;  // "LGSC"
constexpr uint16_t kRunVersion = 1;

void seal(RunRecord& r, uint32_t seq, const RunState& s);
bool valid(const RunRecord& r);
// Index (0/1) of the newer valid record, -1 if none is valid
int newest(const RunRecord& a, const RunRecord& b);

// ---------------------------------------------------------------------------
// Forecast: days until the container is empty, based on the plan of the
// next 7 days incl. boost. < 0 = no consumption planned.
float remaining_days(const PumpSchedule& s, const Boost& b, int32_t today, int64_t remaining_ul);

}  // namespace doser::sched
