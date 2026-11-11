#include "doser/dosing_core.h"

#include <cmath>
#include <cstring>

namespace doser::dose {

const char* mode_str(Mode m) {
    switch (m) {
        case Mode::ManualOnly: return "manual_only";
        case Mode::Disabled: return "disabled";
        default: return "auto";
    }
}

bool parse_mode(const char* s, Mode* out) {
    if (!std::strcmp(s, "auto")) *out = Mode::Auto;
    else if (!std::strcmp(s, "manual_only")) *out = Mode::ManualOnly;
    else if (!std::strcmp(s, "disabled")) *out = Mode::Disabled;
    else return false;
    return true;
}

const char* limit_mode_str(LimitMode m) { return m == LimitMode::Rolling24h ? "rolling_24h" : "calendar_day"; }

bool parse_limit_mode(const char* s, LimitMode* out) {
    if (!std::strcmp(s, "calendar_day")) *out = LimitMode::CalendarDay;
    else if (!std::strcmp(s, "rolling_24h")) *out = LimitMode::Rolling24h;
    else return false;
    return true;
}

const char* validate(const Engineering& e, uint16_t max_rpm) {
    if (e.retract_mrev > 2000) return "retract";  // max. 2 revolutions
    if (e.calib_target_ul < 1000 || e.calib_target_ul > 200000) return "calib_target";
    for (int i = 0; i < kTiers; ++i) {
        if (e.tier_rpm[i] < 10 || e.tier_rpm[i] > max_rpm) return "tier_rpm";
        if (i && e.tier_rpm[i] < e.tier_rpm[i - 1]) return "tier_rpm";  // ascending
    }
    if (e.prime_max_s < 5 || e.prime_max_s > 600) return "prime_max";
    if (e.prime_heartbeat_ms < 200 || e.prime_heartbeat_ms > 5000) return "prime_heartbeat";
    if (e.min_dose_ul < 1 || e.min_dose_ul > 10000) return "min_dose";
    if (e.tube_life_h < 10) return "tube_life";
    if (e.limit_mode != LimitMode::CalendarDay && e.limit_mode != LimitMode::Rolling24h) return "limit_mode";
    if (e.warn_days > 60) return "warn_days";
    if (e.low_days > e.warn_days) return "low_days";
    return nullptr;
}

float ul_per_rev(const PumpSettings& p) { return p.ul_per_rev > 0.0f ? p.ul_per_rev : kNominalUlPerRev; }

uint16_t retract_mrev(const PumpSettings& p, const Engineering& e) {
    return p.retract_mrev >= 0 ? static_cast<uint16_t>(p.retract_mrev) : e.retract_mrev;
}

uint8_t warn_days(const PumpSettings& p, const Engineering& e) { return p.warn_days ? p.warn_days : e.warn_days; }
uint8_t low_days(const PumpSettings& p, const Engineering& e) { return p.low_days ? p.low_days : e.low_days; }

uint32_t ul_to_steps(uint32_t ul, float upr, uint32_t spr) {
    if (upr <= 0.0f) return 0;
    const double steps = static_cast<double>(ul) * spr / upr;
    return steps >= 4294967295.0 ? 0xFFFFFFFFu : static_cast<uint32_t>(std::llround(steps));
}

uint32_t steps_to_ul(uint32_t steps, float upr, uint32_t spr) {
    if (spr == 0) return 0;
    return static_cast<uint32_t>(std::llround(static_cast<double>(steps) * upr / spr));
}

uint32_t mrev_to_steps(uint32_t mrev, uint32_t spr) {
    return static_cast<uint32_t>((static_cast<uint64_t>(mrev) * spr + 500) / 1000);
}

float flow_ml_min(uint32_t rpm, float upr) { return rpm * upr / 1000.0f; }

CalibResult calibrate(uint32_t steps_run, uint32_t spr, uint32_t measured_ul) {
    if (steps_run == 0 || spr == 0) return {false, 0.0f, "no_steps"};
    const float upr = static_cast<float>(static_cast<double>(measured_ul) * spr / steps_run);
    if (upr < kNominalUlPerRev / 4 || upr > kNominalUlPerRev * 4) return {false, upr, "measured_out_of_range"};
    return {true, upr, nullptr};
}

// ---------------------------------------------------------------------------
namespace {
uint32_t hour_of(int64_t utc) { return utc <= 0 ? 0u : static_cast<uint32_t>(utc / 3600); }
}  // namespace

void Ledger::add(int64_t utc, uint32_t amount) {
    const uint32_t h = hour_of(utc);
    if (h > newest_hour) {
        // Clear buckets of the skipped hours (at most once around)
        const uint32_t gap = h - newest_hour;
        const uint32_t n = gap >= static_cast<uint32_t>(kBuckets) ? kBuckets : gap;
        for (uint32_t i = 1; i <= n; ++i) ul[(newest_hour + i) % kBuckets] = 0;
        newest_hour = h;
    } else if (newest_hour - h >= static_cast<uint32_t>(kBuckets)) {
        // Clock jumped far back: old buckets can no longer be assigned.
        // Conservative: keep the sum and add it to the current hour.
        uint32_t keep = sum_from(0);
        std::memset(ul, 0, sizeof(ul));
        newest_hour = h;
        ul[h % kBuckets] = keep;
    }
    uint32_t& b = ul[h % kBuckets];
    b = (b > 0xFFFFFFFFu - amount) ? 0xFFFFFFFFu : b + amount;
}

void Ledger::subtract(int64_t utc, uint32_t amount) {
    const uint32_t h = hour_of(utc);
    if (h > newest_hour || newest_hour - h >= static_cast<uint32_t>(kBuckets)) return;
    uint32_t& b = ul[h % kBuckets];
    b = b > amount ? b - amount : 0;
}

uint32_t Ledger::sum_from(uint32_t from_hour) const {
    uint64_t sum = 0;
    for (int i = 0; i < kBuckets; ++i) {
        if (static_cast<uint32_t>(i) > newest_hour) break;  // (only right after clear())
        const uint32_t h = newest_hour - i;
        if (h < from_hour) break;
        sum += ul[h % kBuckets];
    }
    return sum > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<uint32_t>(sum);
}

uint32_t window_start_hour(LimitMode m, int64_t now_utc, int64_t local_midnight_utc) {
    const uint32_t now_h = hour_of(now_utc);
    if (m == LimitMode::Rolling24h) return now_h >= 24 ? now_h - 24 : 0;
    // Round down: with half-hour time zones the started hour counts too
    return hour_of(local_midnight_utc);
}

const char* check_dose(uint32_t ul, const PumpSettings& p, const Engineering& e, const PumpState& s,
                       uint32_t used_today_ul) {
    (void)s;
    if (p.mode == Mode::Disabled) return "pump_disabled";
    if (ul < e.min_dose_ul) return "dose_too_small";
    if (ul > kMaxDoseUl) return "dose_too_large";
    if (p.daily_limit_ul && static_cast<uint64_t>(used_today_ul) + ul > p.daily_limit_ul) return "daily_limit";
    return nullptr;
}

// ---------------------------------------------------------------------------
uint32_t crc32(const void* data, size_t len, uint32_t crc) {
    const auto* p = static_cast<const uint8_t*>(data);
    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void seal(Record& r, uint32_t seq, const PumpState& s) {
    std::memset(&r, 0, sizeof(r));
    r.magic = kRecordMagic;
    r.version = kRecordVersion;
    r.size = sizeof(Record);
    r.seq = seq;
    r.state = s;
    r.crc = crc32(&r, offsetof(Record, crc));
}

bool valid(const Record& r) {
    return r.magic == kRecordMagic && r.version == kRecordVersion && r.size == sizeof(Record) &&
           r.crc == crc32(&r, offsetof(Record, crc));
}

int newest(const Record& a, const Record& b) {
    const bool va = valid(a), vb = valid(b);
    if (va && vb) return static_cast<int32_t>(b.seq - a.seq) > 0 ? 1 : 0;  // overflow-safe
    if (va) return 0;
    if (vb) return 1;
    return -1;
}

}  // namespace doser::dose
