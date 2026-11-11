#include "doser/schedule_core.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "doser/dosing_core.h"

namespace doser::sched {
namespace {

int gcd(int a, int b) {
    while (b) {
        const int t = a % b;
        a = b;
        b = t;
    }
    return a;
}

int mod(int64_t a, int m) { return static_cast<int>(((a % m) + m) % m); }

// A day rule as a set of congruences d ≡ r (mod m)
struct Congruence {
    int m, r;
};

int congruences(const DayRule& d, Congruence (&out)[7]) {
    if (d.mode == DayMode::Interval) {
        if (d.interval < 1) return 0;
        out[0] = {d.interval, mod(d.anchor_day, d.interval)};
        return 1;
    }
    int n = 0;
    for (int w = 0; w < 7; ++w)
        if (d.weekdays & (1u << w)) out[n++] = {7, mod(w - 3, 7)};  // day 0 was a Thursday (index 3)
    return n;
}

bool valid_days(const DayRule& d) {
    if (d.mode == DayMode::Interval) return d.interval >= 1 && d.interval <= kMaxInterval;
    return d.mode == DayMode::Weekdays && (d.weekdays & 0x7F) != 0 && (d.weekdays & 0x80) == 0;
}

bool before(int64_t utc_a, uint32_t tag_a, int64_t utc_b, uint32_t tag_b) {
    return utc_a < utc_b || (utc_a == utc_b && tag_a < tag_b);
}

// Minutes are unique within a plan; the factor 16 keeps run-state cursors of
// older firmware (minute * 16 + program + 1) valid.
uint32_t tag_of(const Slot& s) { return s.minute * 16u + 1u; }

// Duration of one move with a trapezoidal (or triangular) ramp
double move_seconds(double revs, double rps, double accel_rps2) {
    if (revs <= 0) return 0;
    if (revs >= rps * rps / accel_rps2) return revs / rps + rps / accel_rps2;
    return 2.0 * std::sqrt(revs / accel_rps2);
}

}  // namespace

const char* kind_str(Kind k) { return k == Kind::Spread ? "spread" : k == Kind::Times ? "times" : "none"; }

bool parse_kind(const char* s, Kind* out) {
    if (!std::strcmp(s, "none")) *out = Kind::None;
    else if (!std::strcmp(s, "spread")) *out = Kind::Spread;
    else if (!std::strcmp(s, "times")) *out = Kind::Times;
    else return false;
    return true;
}

const char* power_loss_str(PowerLoss p) { return p == PowerLoss::CatchUpWindow ? "catch_up_window" : "skip_missed"; }

bool parse_power_loss(const char* s, PowerLoss* out) {
    if (!std::strcmp(s, "skip_missed")) *out = PowerLoss::SkipMissed;
    else if (!std::strcmp(s, "catch_up_window")) *out = PowerLoss::CatchUpWindow;
    else return false;
    return true;
}

const char* manual_during_lock_str(ManualDuringLock m) { return m == ManualDuringLock::Queue ? "queue" : "reject"; }

bool parse_manual_during_lock(const char* s, ManualDuringLock* out) {
    if (!std::strcmp(s, "reject")) *out = ManualDuringLock::Reject;
    else if (!std::strcmp(s, "queue")) *out = ManualDuringLock::Queue;
    else return false;
    return true;
}

const char* validate(const Config& c) {
    // the grid must divide a day so that slots repeat on the same minutes
    if (c.grid_min < 1 || c.grid_min > 60 || kMinutesPerDay % c.grid_min) return "grid_min";
    if (c.max_delay_min < 1 || c.max_delay_min > 720) return "max_delay_min";
    return nullptr;
}

// ---------------------------------------------------------------------------
bool active_on(const DayRule& r, int32_t day) {
    if (r.mode == DayMode::Interval) return r.interval >= 1 && mod(int64_t(day) - r.anchor_day, r.interval) == 0;
    return (r.weekdays >> weekday_index(day)) & 1u;
}

bool can_coincide(const DayRule& a, const DayRule& b, int offset) {
    Congruence ca[7], cb[7];
    const int na = congruences(a, ca), nb = congruences(b, cb);
    for (int i = 0; i < na; ++i)
        for (int j = 0; j < nb; ++j) {
            // d ≡ ra (mod ma) and d + offset ≡ rb (mod mb): solvable iff the
            // residues agree modulo gcd(ma, mb)
            const int g = gcd(ca[i].m, cb[j].m);
            if (mod(int64_t(ca[i].r) - (cb[j].r - offset), g) == 0) return true;
        }
    return false;
}

// ---------------------------------------------------------------------------
int expand(const PumpSchedule& s, uint16_t grid_min, uint32_t min_dose_ul, Slot* out, int max, const char** err,
           int* entry) {
    auto fail = [&](const char* e, int at = -1) {
        if (err) *err = e;
        if (entry) *entry = at;
        return -1;
    };
    if (grid_min == 0) grid_min = 1;
    if (s.kind == Kind::None) return 0;
    if (!valid_days(s.days)) return fail("invalid_days");

    if (s.kind == Kind::Times) {
        const int n = s.time_count;
        if (n < 1 || n > kMaxTimes) return fail("invalid_count");
        if (n > max) return fail("too_many_slots");
        for (int i = 0; i < n; ++i) {
            const TimeDose& t = s.times[i];
            if (t.minute >= kMinutesPerDay) return fail("invalid_time", i);
            if (t.minute % grid_min) return fail("off_grid", i);
            if (i > 0 && t.minute == s.times[i - 1].minute) return fail("duplicate_time", i);
            if (i > 0 && t.minute < s.times[i - 1].minute) return fail("invalid_time", i);  // not sorted
            if (t.ul < min_dose_ul) return fail("dose_too_small", i);
            if (t.ul > dose::kMaxDoseUl) return fail("dose_too_large", i);
            out[i] = Slot{t.minute, static_cast<uint8_t>(i), 0, t.ul};
        }
        return n;
    }
    if (s.kind != Kind::Spread) return fail("invalid_kind");
    if (s.start_min >= kMinutesPerDay) return fail("invalid_time");
    if (s.start_min % grid_min) return fail("off_grid");
    const int n = s.count;
    if (n < 1 || n > kMaxSlotsPerDay) return fail("invalid_count");
    if (n > max) return fail("too_many_slots");
    if (n > 1) {
        if (s.end_min >= kMinutesPerDay) return fail("invalid_time");
        if (s.end_min % grid_min) return fail("off_grid");
        if (s.end_min <= s.start_min) return fail("invalid_range");
    }
    const uint32_t base = s.amount_ul / n, rest = s.amount_ul % n;
    if (base < min_dose_ul) return fail("dose_too_small");
    if (base + (rest ? 1 : 0) > dose::kMaxDoseUl) return fail("dose_too_large");

    const uint32_t span = n > 1 ? s.end_min - s.start_min : 0;
    for (int i = 0; i < n; ++i) {
        uint16_t minute = s.start_min;
        if (n > 1) {
            // even spacing, rounded to the grid (half up)
            const uint32_t num = 2u * i * span + uint32_t(n - 1) * grid_min;
            const uint32_t den = 2u * uint32_t(n - 1) * grid_min;
            minute = static_cast<uint16_t>(s.start_min + num / den * grid_min);
        }
        if (i > 0 && minute <= out[i - 1].minute) return fail("slots_too_dense");
        out[i] = Slot{minute, static_cast<uint8_t>(i), 0, base + (uint32_t(i) < rest ? 1u : 0u)};
    }
    return n;
}

bool runs_on(const PumpSchedule& s, int32_t day) {
    return s.kind != Kind::None && active_on(s.days, day);
}

int day_slots(const PumpSchedule& s, int32_t day, uint16_t grid_min, Slot* out, int max) {
    if (!runs_on(s, day)) return 0;
    const int n = expand(s, grid_min, 0, out, max, nullptr);
    return n < 0 ? 0 : n;
}

uint32_t day_total(const PumpSchedule& s, int32_t day) {
    if (!runs_on(s, day)) return 0;
    if (s.kind == Kind::Spread) return s.amount_ul;
    uint32_t sum = 0;
    for (int i = 0; i < s.time_count && i < kMaxTimes; ++i) sum += s.times[i].ul;
    return sum;
}

// ---------------------------------------------------------------------------
uint32_t run_seconds(uint32_t ul, const RunParams& p) {
    const double rps = (p.rpm ? p.rpm : 1) / 60.0;
    const double accel = (p.accel_rpm_s ? p.accel_rpm_s : 600) / 60.0;
    const double upr = p.ul_per_rev > 0 ? p.ul_per_rev : dose::kNominalUlPerRev;
    const double retract = p.retract_mrev / 1000.0;
    const double t = move_seconds(ul / upr, rps, accel) + 2 * move_seconds(retract, rps, accel);
    return static_cast<uint32_t>(std::ceil(t)) + kRunMarginS;
}

// ---------------------------------------------------------------------------
int64_t lock_gap(const Locks& l, int pa, int pb) {
    int64_t gap = 0;
    const int n = l.count < kMaxLocks ? l.count : kMaxLocks;
    for (int i = 0; i < n; ++i) {
        const LockRule& r = l.rules[i];
        if ((r.a == pa && r.b == pb) || (r.a == pb && r.b == pa))
            if (int64_t(r.gap_min) * 60 > gap) gap = int64_t(r.gap_min) * 60;
    }
    return gap;
}

int64_t lock_until(const Locks& l, int pump, const int64_t (&last_end)[kPumps], const bool (&running)[kPumps],
                   int64_t now) {
    int64_t t = now;
    for (int q = 0; q < kPumps; ++q) {
        if (q == pump) continue;
        const int64_t gap = lock_gap(l, pump, q);
        if (!gap) continue;
        if (running[q]) return INT64_MAX;
        if (last_end[q] && last_end[q] + gap > t) t = last_end[q] + gap;
    }
    return t;
}

// ---------------------------------------------------------------------------
namespace {

struct Checker {
    const PumpSchedule (&all)[kPumps];
    const Context& ctx;
    Conflict* out;
    int max_out;
    int found = 0;

    void add(const char* code, int pa, const Slot& a, int pb, const Slot& b, int next_day, int64_t gap) {
        if (found >= max_out) {
            ++found;
            return;
        }
        Conflict& c = out[found++];
        c.code = code;
        c.first = SlotRef{uint8_t(pa), 0, a.minute};
        c.second = SlotRef{uint8_t(pb), 0, b.minute};
        c.next_day = static_cast<uint8_t>(next_day);
        c.gap_min = static_cast<uint16_t>(gap / 60);
    }

    // A slot of pump pb (on the same day or the next) starts while a slot of
    // pump pa is still running or within `gap` after its end.
    void check_plans(int pa, int pb, int64_t gap, const char* code) {
        const PumpSchedule& a = all[pa];
        const PumpSchedule& b = all[pb];
        if (a.kind == Kind::None || b.kind == Kind::None) return;
        Slot sa[kMaxSlotsPerDay], sb[kMaxSlotsPerDay];
        const int na = expand(a, ctx.grid_min, 0, sa, kMaxSlotsPerDay, nullptr);
        const int nb = expand(b, ctx.grid_min, 0, sb, kMaxSlotsPerDay, nullptr);
        for (int next = 0; next <= 1; ++next) {
            if (!can_coincide(a.days, b.days, next)) continue;
            for (int x = 0; x < na; ++x) {
                const int64_t start = sa[x].minute * 60;
                const int64_t end = start + run_seconds(sa[x].ul, ctx.run[pa]) + gap;
                for (int y = 0; y < nb; ++y) {
                    const int64_t other = sb[y].minute * 60 + next * 86400;
                    if (pa == pb && x == y && !next) continue;
                    if (other < start || other >= end) continue;
                    // equal start times are found from both directions: report once
                    if (other == start && !next && pa > pb) continue;
                    add(code, pa, sa[x], pb, sb[y], next, gap);
                }
            }
        }
    }
};

}  // namespace

Validation validate(const PumpSchedule (&all)[kPumps], const Locks& locks, const Context& ctx, Conflict* out,
                    int max_out) {
    Validation v;
    Slot buf[kMaxSlotsPerDay];
    // 1. Every plan on its own
    for (int p = 0; p < kPumps; ++p) {
        if (all[p].power_loss == PowerLoss::CatchUpWindow && (all[p].catch_up_min < 1 || all[p].catch_up_min > 1440)) {
            v.error = "invalid_catch_up";
            v.pump = p;
            return v;
        }
        const char* err = nullptr;
        int entry = -1;
        if (expand(all[p], ctx.grid_min, ctx.min_dose_ul, buf, kMaxSlotsPerDay, &err, &entry) < 0) {
            v.error = err;
            v.pump = p;
            v.entry = entry;
            return v;
        }
    }
    // 2. Daily limit per day. Day rules repeat at most every lcm(7, interval)
    //    days; one year covers every combination that matters in practice.
    for (int p = 0; p < kPumps; ++p) {
        if (!ctx.daily_limit_ul[p] || all[p].kind == Kind::None) continue;
        for (int32_t d = ctx.today; d < ctx.today + 366; ++d) {
            if (day_total(all[p], d) > ctx.daily_limit_ul[p]) {
                v.error = "exceeds_daily_limit";
                v.pump = p;
                return v;
            }
        }
    }
    // 3. Lock rules
    for (int r = 0; r < locks.count; ++r) {
        const LockRule& l = locks.rules[r];
        if (r >= kMaxLocks || l.a >= kPumps || l.b >= kPumps || l.a == l.b || l.gap_min < 1 || l.gap_min > 1440) {
            v.error = "invalid_lock";
            v.rule = r;
            return v;
        }
    }
    // 4. Overlaps on the same pump and lock times between pumps
    Checker c{all, ctx, out, max_out};
    for (int p = 0; p < kPumps; ++p) c.check_plans(p, p, 0, "overlap");
    for (int pa = 0; pa < kPumps; ++pa)
        for (int pb = 0; pb < kPumps; ++pb) {
            if (pa == pb) continue;
            const int64_t gap = lock_gap(locks, pa, pb);
            if (gap) c.check_plans(pa, pb, gap, "collision_lock");
        }
    if (c.found) {
        v.error = "conflict";
        v.conflicts = c.found < max_out ? c.found : max_out;
    }
    return v;
}

// ---------------------------------------------------------------------------
bool next_due(const PumpSchedule& s, const tz::Zone& z, uint16_t grid_min, const Cursor& after, int64_t until,
              Due* out) {
    if (until < after.utc) return false;
    // one day back: a DST gap can move a slot of the previous local day
    int32_t first = day_of(tz::to_local(z, after.utc)) - 1;
    const int32_t last = day_of(tz::to_local(z, until));
    if (last - first > 400) first = last - 400;  // the scheduler skips long gaps before
    bool found = false;
    Slot slots[kMaxSlotsPerDay];
    for (int32_t d = first; d <= last; ++d) {
        const int n = day_slots(s, d, grid_min, slots, kMaxSlotsPerDay);
        for (int k = 0; k < n; ++k) {
            const int64_t utc = tz::to_utc(z, int64_t(d) * 86400 + slots[k].minute * 60);
            const uint32_t tag = tag_of(slots[k]);
            if (!before(after.utc, after.tag, utc, tag) || utc > until) continue;
            if (found && !before(utc, tag, out->utc, out->tag)) continue;
            *out = Due{utc, d, slots[k].minute, slots[k].ul, tag, n - k};
            found = true;
        }
        if (found && d >= day_of(tz::to_local(z, out->utc)) + 1) break;  // later days only get later
    }
    return found;
}

int count_due(const PumpSchedule& s, const tz::Zone& z, uint16_t grid_min, Cursor after, int64_t until) {
    int n = 0;
    Due d;
    while (next_due(s, z, grid_min, after, until, &d)) {
        ++n;
        after = cursor_of(d);
    }
    return n;
}

Cursor resume_point(const PumpSchedule& s, const Cursor& c, int64_t now) {
    const int64_t window = s.power_loss == PowerLoss::CatchUpWindow ? int64_t(s.catch_up_min) * 60 : kOnTimeTolerance;
    // Everything before now - window would be skipped anyway
    if (c.utc < now - window - 1) return Cursor{now - window - 1, UINT32_MAX};
    return c;
}

Action on_due(const PumpSchedule& s, const Due& d, int64_t now) {
    const int64_t late = now - d.utc;
    if (late <= kOnTimeTolerance) return Action::Run;
    if (s.power_loss == PowerLoss::CatchUpWindow && late <= int64_t(s.catch_up_min) * 60) return Action::Run;
    return Action::SkipMissed;
}

// ---------------------------------------------------------------------------
namespace {
uint16_t slot_count(int n) { return static_cast<uint16_t>(n < 0 ? 0 : n > 0xFFFF ? 0xFFFF : n); }
}  // namespace

void boost_start(Boost& b, uint32_t total_ul, uint16_t days, int64_t now, int slots) {
    b = Boost{};
    if (!total_ul || slots <= 0 || (!days && slots > kMaxBoostDoses)) return;
    b.remaining_ul = total_ul;
    b.total_ul = total_ul;
    b.days = days;
    b.slots_left = slot_count(slots);
    if (!days) b.doses = b.slots_left;
    b.start_utc = now;
}

bool boost_active(const Boost& b, int64_t now) {
    return b.remaining_ul > 0 && b.slots_left > 0 && now <= boost_end(b);
}

uint32_t boost_share(const Boost& b) {
    if (!b.remaining_ul || !b.slots_left) return 0;
    return b.slots_left == 1 ? b.remaining_ul : b.remaining_ul / b.slots_left;
}

uint32_t boost_take(Boost& b, int64_t slot_utc) {
    if (!b.remaining_ul) return 0;
    if (slot_utc > boost_end(b) || !b.slots_left) {  // window over
        b = Boost{};
        return 0;
    }
    const uint32_t share = boost_share(b);
    b.remaining_ul -= share;
    return share;
}

void boost_refund(Boost& b, uint32_t ul) {
    if (ul && b.total_ul) b.remaining_ul += ul;
}

void boost_pass(Boost& b, int64_t slot_utc, int n) {
    if (!b.total_ul || n <= 0) return;
    if (slot_utc > boost_end(b) || n >= b.slots_left) {
        b = Boost{};  // last slot of the window processed: what is left cannot be dosed
        return;
    }
    b.slots_left = static_cast<uint16_t>(b.slots_left - n);
}

void boost_recount(Boost& b, int slots) {
    if (!b.remaining_ul) return;
    if (slots <= 0) b = Boost{};
    else if (b.days) b.slots_left = slot_count(slots);
}

// ---------------------------------------------------------------------------
void seal(RunRecord& r, uint32_t seq, const RunState& s) {
    std::memset(&r, 0, sizeof(r));
    r.magic = kRunMagic;
    r.version = kRunVersion;
    r.size = sizeof(RunRecord);
    r.seq = seq;
    r.state = s;
    r.crc = dose::crc32(&r, offsetof(RunRecord, crc));
}

bool valid(const RunRecord& r) {
    return r.magic == kRunMagic && r.version == kRunVersion && r.size == sizeof(RunRecord) &&
           r.crc == dose::crc32(&r, offsetof(RunRecord, crc));
}

int newest(const RunRecord& a, const RunRecord& b) {
    const bool va = valid(a), vb = valid(b);
    if (va && vb) return static_cast<int32_t>(b.seq - a.seq) > 0 ? 1 : 0;
    return va ? 0 : vb ? 1 : -1;
}

// ---------------------------------------------------------------------------
float remaining_days(const PumpSchedule& s, const Boost& b, int64_t boost_until, int32_t today, int64_t now,
                     int64_t remaining_ul) {
    double planned = 0;
    for (int32_t d = today; d < today + 7; ++d) planned += day_total(s, d);
    if (boost_active(b, now)) {  // the part of the rest that falls into the next 7 days
        const double left = double(boost_until - now), week = 7.0 * 86400;
        planned += left <= week ? b.remaining_ul : b.remaining_ul * week / left;
    }
    const double per_day = planned / 7;
    if (per_day <= 0) return -1.0f;
    if (remaining_ul <= 0) return 0.0f;
    return static_cast<float>(remaining_ul / per_day);
}

}  // namespace doser::sched
