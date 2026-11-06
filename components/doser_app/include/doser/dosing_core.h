#pragma once
// Dosing logic without hardware/ESP dependencies (host tests: tests/host).
//
// Units: amounts in µl (integer, 0.02 ml = 20 µl), times in UTC seconds.
// Calibration and retraction refer to one motor revolution (µl/rev and
// 1/1000 rev) and therefore stay valid when the microsteps are changed.

#include <cstddef>
#include <cstdint>

namespace doser::dose {

// Nominal value without calibration: ≈ 110 ml/min at 300 rpm (datasheet ≥ 80).
constexpr float kNominalUlPerRev = 370.0f;
constexpr int kTiers = 3;
constexpr uint32_t kMaxDoseUl = 1000000;  // 1 l per single job

enum class Mode : uint8_t { Auto = 0, ManualOnly = 1, Disabled = 2 };
enum class LimitMode : uint8_t { CalendarDay = 0, Rolling24h = 1 };

const char* mode_str(Mode m);
bool parse_mode(const char* s, Mode* out);
const char* limit_mode_str(LimitMode m);
bool parse_limit_mode(const char* s, LimitMode* out);

// ---------------------------------------------------------------------------
// Global engineering parameters (super admin, functional spec 11.2)
struct Engineering {
    uint16_t retract_mrev = 250;            // anti-drip retraction [1/1000 rev], default ¼ rev
    uint32_t calib_target_ul = 20000;       // calibration target amount (default 20 ml)
    uint16_t tier_rpm[kTiers] = {100, 200, 300};  // slow / normal / fast
    uint16_t prime_max_s = 60;              // priming emergency timeout
    uint16_t prime_heartbeat_ms = 500;      // dead-man timeout while priming
    uint32_t min_dose_ul = 20;              // smallest single dose
    uint16_t tube_life_h = 1000;            // tube lifetime (BPT)
    LimitMode limit_mode = LimitMode::CalendarDay;
};
// nullptr = valid, otherwise name of the invalid field
const char* validate(const Engineering& e, uint16_t max_rpm);

// ---------------------------------------------------------------------------
// Pump master data (user)
struct PumpSettings {
    char name[24] = "";
    Mode mode = Mode::Auto;
    uint8_t speed_tier = 1;        // 0..2
    float ul_per_rev = 0.0f;       // calibration, 0 = nominal value
    int16_t retract_mrev = -1;     // -1 = global value, 0 = off
    uint32_t daily_limit_ul = 0;   // hard daily limit, 0 = none
    uint32_t capacity_ul = 0;      // container size, 0 = not tracked
    int64_t calibrated_at = 0;     // UTC of the last calibration
};

float ul_per_rev(const PumpSettings& p);  // calibrated or nominal value
uint16_t retract_mrev(const PumpSettings& p, const Engineering& e);

// ---------------------------------------------------------------------------
// Conversion (rounded; 64 bit against overflow)
uint32_t ul_to_steps(uint32_t ul, float ul_per_rev, uint32_t steps_per_rev);
uint32_t steps_to_ul(uint32_t steps, float ul_per_rev, uint32_t steps_per_rev);
uint32_t mrev_to_steps(uint32_t mrev, uint32_t steps_per_rev);
float flow_ml_min(uint32_t rpm, float ul_per_rev);

// Calibration: steps_run steps pumped, measured_ul collected.
// Plausible is 0.25x to 4x the nominal value.
struct CalibResult {
    bool ok;
    float ul_per_rev;
    const char* error;  // "measured_out_of_range", "no_steps"
};
CalibResult calibrate(uint32_t steps_run, uint32_t steps_per_rev, uint32_t measured_ul);

// ---------------------------------------------------------------------------
// Daily limit: pumped amount in hourly buckets (ring over 26 h).
// Enough for "rolling 24 h" (incl. the started hour) and for a calendar
// day with 25 hours (DST change). Boundaries are drawn conservatively:
// a started hour at the window start counts in full.
struct Ledger {
    static constexpr int kBuckets = 26;
    uint32_t newest_hour;            // UTC hour (utc / 3600) of the newest bucket
    uint32_t ul[kBuckets];

    void clear() { *this = Ledger{}; }
    void add(int64_t utc, uint32_t amount_ul);
    void subtract(int64_t utc, uint32_t amount_ul);  // credit (abort)
    // sum of all buckets from hour from_hour (inclusive), including "future" ones
    uint32_t sum_from(uint32_t from_hour) const;
};
// First hour of the limit window. local_midnight_utc: start of the local
// calendar day in UTC (only for CalendarDay).
uint32_t window_start_hour(LimitMode m, int64_t now_utc, int64_t local_midnight_utc);

// ---------------------------------------------------------------------------
// Run state (written often -> EEPROM, NVS as fallback)
struct PumpState {
    int32_t remaining_ul;    // remaining amount in container (negative = overdue)
    uint16_t retracted_mrev; // executed retraction [1/1000 rev], compensate before the next run
    uint8_t reserved[2];
    uint64_t tube_ms;       // tube runtime
    uint64_t total_ul;      // total dosed (statistics)
    Ledger ledger;
    uint32_t reserved2;     // explicit padding (CRC over defined bytes)
};
static_assert(sizeof(PumpState) == 136);

// Check before starting a dose. nullptr = allowed, otherwise error code:
// "pump_disabled", "dose_too_small", "dose_too_large", "daily_limit"
const char* check_dose(uint32_t ul, const PumpSettings& p, const Engineering& e, const PumpState& s,
                       uint32_t used_today_ul);

// ---------------------------------------------------------------------------
// Sealed record (A/B slots with sequence number and CRC32)
struct Record {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t seq;
    uint32_t reserved;
    PumpState state;
    uint32_t crc;
    uint32_t reserved2;
};
static_assert(sizeof(Record) == 160);
constexpr uint32_t kRecordMagic = 0x4C474453;  // "LGDS"
constexpr uint16_t kRecordVersion = 1;

uint32_t crc32(const void* data, size_t len, uint32_t crc = 0);
void seal(Record& r, uint32_t seq, const PumpState& s);
bool valid(const Record& r);
// Index (0/1) of the newest valid slot, -1 if none is valid
int newest(const Record& a, const Record& b);

}  // namespace doser::dose
