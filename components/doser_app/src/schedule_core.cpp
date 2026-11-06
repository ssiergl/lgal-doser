#include "doser/schedule_core.h"

#include <cmath>
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

int slots_of(const Program& p) { return p.kind == Kind::Generator ? p.count : 1; }

bool before(int64_t utc_a, uint32_t tag_a, int64_t utc_b, uint32_t tag_b) {
    return utc_a < utc_b || (utc_a == utc_b && tag_a < tag_b);
}

uint32_t tag_of(const Slot& s) { return s.minute * 16u + s.program + 1u; }

// Duration of one move with a trapezoidal (or triangular) ramp
double move_seconds(double revs, double rps, double accel_rps2) {
    if (revs <= 0) return 0;
    if (revs >= rps * rps / accel_rps2) return revs / rps + rps / accel_rps2;
    return 2.0 * std::sqrt(revs / accel_rps2);
}

}  // namespace

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
int expand(const Program& p, uint8_t index, uint16_t grid_min, uint32_t min_dose_ul, Slot* out, int max,
           const char** err) {
    auto fail = [&](const char* e) {
        if (err) *err = e;
        return -1;
    };
    if (grid_min == 0) grid_min = 1;
    if (!valid_days(p.days)) return fail("invalid_days");
    if (p.start_min >= kMinutesPerDay) return fail("invalid_time");
    if (p.start_min % grid_min) return fail("off_grid");

    if (p.kind == Kind::Single) {
        if (p.amount_ul < min_dose_ul) return fail("dose_too_small");
        if (p.amount_ul > dose::kMaxDoseUl) return fail("dose_too_large");
        if (max < 1) return fail("too_many_slots");
        out[0] = Slot{p.start_min, index, 0, p.amount_ul};
        return 1;
    }
    if (p.kind != Kind::Generator) return fail("invalid_kind");
    const int n = p.count;
    if (n < 1 || n > kMaxSlotsPerDay) return fail("invalid_count");
    if (n > max) return fail("too_many_slots");
    if (n > 1) {
        if (p.end_min >= kMinutesPerDay) return fail("invalid_time");
        if (p.end_min % grid_min) return fail("off_grid");
        if (p.end_min <= p.start_min) return fail("invalid_range");
    }
    const uint32_t base = p.amount_ul / n, rest = p.amount_ul % n;
    if (base < min_dose_ul) return fail("dose_too_small");
    if (base + (rest ? 1 : 0) > dose::kMaxDoseUl) return fail("dose_too_large");

    const uint32_t span = n > 1 ? p.end_min - p.start_min : 0;
    for (int i = 0; i < n; ++i) {
        uint16_t minute = p.start_min;
        if (n > 1) {
            // even spacing, rounded to the grid (half up)
            const uint32_t num = 2u * i * span + uint32_t(n - 1) * grid_min;
            const uint32_t den = 2u * uint32_t(n - 1) * grid_min;
            minute = static_cast<uint16_t>(p.start_min + num / den * grid_min);
        }
        if (i > 0 && minute <= out[i - 1].minute) return fail("slots_too_dense");
        out[i] = Slot{minute, index, 0, base + (uint32_t(i) < rest ? 1u : 0u)};
    }
    return n;
}

int day_slots(const PumpSchedule& s, int32_t day, uint16_t grid_min, Slot* out, int max) {
    int n = 0;
    Slot buf[kMaxSlotsPerDay];
    const int programs = s.count < kMaxPrograms ? s.count : kMaxPrograms;
    for (int i = 0; i < programs; ++i) {
        const Program& p = s.programs[i];
        if (!p.enabled || !active_on(p.days, day)) continue;
        const int k = expand(p, static_cast<uint8_t>(i), grid_min, 0, buf, kMaxSlotsPerDay, nullptr);
        for (int j = 0; j < k && n < max; ++j) {
            // insertion sort by (minute, program)
            int pos = n++;
            while (pos > 0 && (out[pos - 1].minute > buf[j].minute ||
                               (out[pos - 1].minute == buf[j].minute && out[pos - 1].program > buf[j].program))) {
                out[pos] = out[pos - 1];
                --pos;
            }
            out[pos] = buf[j];
        }
    }
    return n;
}

uint32_t day_total(const PumpSchedule& s, int32_t day) {
    uint32_t sum = 0;
    const int programs = s.count < kMaxPrograms ? s.count : kMaxPrograms;
    for (int i = 0; i < programs; ++i) {
        const Program& p = s.programs[i];
        if (p.enabled && active_on(p.days, day)) sum += p.amount_ul;
    }
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
        c.first = SlotRef{uint8_t(pa), a.program, a.minute};
        c.second = SlotRef{uint8_t(pb), b.program, b.minute};
        c.next_day = static_cast<uint8_t>(next_day);
        c.gap_min = static_cast<uint16_t>(gap / 60);
    }

    // Slot t of pump pb (on the same day or the next) starts while slot s of
    // pump pa is still running or within `gap` after its end.
    void check_programs(int pa, int i, int pb, int j, int64_t gap, const char* code, bool same_order_twice) {
        const Program& a = all[pa].programs[i];
        const Program& b = all[pb].programs[j];
        if (!a.enabled || !b.enabled) return;
        Slot sa[kMaxSlotsPerDay], sb[kMaxSlotsPerDay];
        const int na = expand(a, uint8_t(i), ctx.grid_min, 0, sa, kMaxSlotsPerDay, nullptr);
        const int nb = expand(b, uint8_t(j), ctx.grid_min, 0, sb, kMaxSlotsPerDay, nullptr);
        for (int next = 0; next <= 1; ++next) {
            if (!can_coincide(a.days, b.days, next)) continue;
            for (int x = 0; x < na; ++x) {
                const int64_t start = sa[x].minute * 60;
                const int64_t end = start + run_seconds(sa[x].ul, ctx.run[pa]) + gap;
                for (int y = 0; y < nb; ++y) {
                    const int64_t other = sb[y].minute * 60 + next * 86400;
                    if (pa == pb && i == j && x == y && !next) continue;
                    if (other < start || other >= end) continue;
                    // equal start times are found from both directions: report once
                    if (other == start && !next && same_order_twice && (pa > pb || (pa == pb && i > j))) continue;
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
    // 1. Every program on its own
    for (int p = 0; p < kPumps; ++p) {
        if (all[p].count > kMaxPrograms) {
            v.error = "too_many_programs";
            v.pump = p;
            return v;
        }
        if (all[p].power_loss == PowerLoss::CatchUpWindow && (all[p].catch_up_min < 1 || all[p].catch_up_min > 1440)) {
            v.error = "invalid_catch_up";
            v.pump = p;
            return v;
        }
        for (int i = 0; i < all[p].count; ++i) {
            const char* err = nullptr;
            if (expand(all[p].programs[i], uint8_t(i), ctx.grid_min, ctx.min_dose_ul, buf, kMaxSlotsPerDay, &err) < 0) {
                v.error = err;
                v.pump = p;
                v.program = i;
                return v;
            }
        }
    }
    // 2. Per day: number of slots and daily limit. Day rules repeat at most
    //    every lcm(7, interval) days; one year covers every combination that
    //    matters in practice.
    for (int p = 0; p < kPumps; ++p) {
        const PumpSchedule& s = all[p];
        for (int32_t d = ctx.today; d < ctx.today + 366; ++d) {
            int slots = 0;
            for (int i = 0; i < s.count; ++i)
                if (s.programs[i].enabled && active_on(s.programs[i].days, d)) slots += slots_of(s.programs[i]);
            if (slots > kMaxSlotsPerDay) {
                v.error = "too_many_slots";
                v.pump = p;
                return v;
            }
            if (ctx.daily_limit_ul[p] && day_total(s, d) > ctx.daily_limit_ul[p]) {
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
            v.program = r;
            return v;
        }
    }
    // 4. Overlaps on the same pump and lock times between pumps
    Checker c{all, ctx, out, max_out};
    for (int p = 0; p < kPumps; ++p)
        for (int i = 0; i < all[p].count; ++i)
            for (int j = 0; j < all[p].count; ++j) c.check_programs(p, i, p, j, 0, "overlap", true);
    for (int pa = 0; pa < kPumps; ++pa)
        for (int pb = 0; pb < kPumps; ++pb) {
            if (pa == pb) continue;
            const int64_t gap = lock_gap(locks, pa, pb);
            if (!gap) continue;
            for (int i = 0; i < all[pa].count; ++i)
                for (int j = 0; j < all[pb].count; ++j) c.check_programs(pa, i, pb, j, gap, "collision_lock", true);
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
            *out = Due{utc, d, slots[k].minute, slots[k].program, slots[k].ul, tag, n - k};
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
void boost_start(Boost& b, uint32_t total_ul, uint16_t days, int32_t today) {
    b = Boost{};
    if (!total_ul || !days) return;
    b.remaining_ul = total_ul;
    b.total_ul = total_ul;
    b.days = days;
    b.start_day = today;
    b.budget_day = today - 1;
}

bool boost_active(const Boost& b, int32_t today) {
    return b.remaining_ul > 0 && today >= b.start_day && today < b.start_day + b.days;
}

uint32_t boost_take(Boost& b, int32_t today, int remaining_slots) {
    if (!boost_active(b, today)) {
        if (today >= b.start_day + b.days) b.remaining_ul = 0;  // expired
        return 0;
    }
    if (b.budget_day != today) {
        const int32_t days_left = b.start_day + b.days - today;
        b.budget_day = today;
        b.budget_ul = days_left <= 1 ? b.remaining_ul : b.remaining_ul / days_left;
    }
    uint32_t share = remaining_slots <= 1 ? b.budget_ul : b.budget_ul / remaining_slots;
    if (share > b.remaining_ul) share = b.remaining_ul;
    b.budget_ul -= share;
    b.remaining_ul -= share;
    return share;
}

void boost_refund(Boost& b, int32_t today, uint32_t ul) {
    if (!ul) return;
    b.remaining_ul += ul;
    if (b.budget_day == today) b.budget_ul += ul;
}

// ---------------------------------------------------------------------------
float remaining_days(const PumpSchedule& s, const Boost& b, int32_t today, int64_t remaining_ul) {
    double planned = 0;
    for (int32_t d = today; d < today + 7; ++d) planned += day_total(s, d);
    if (boost_active(b, today)) {
        const int32_t days_left = b.start_day + b.days - today;
        planned += double(b.remaining_ul) * (days_left < 7 ? days_left : 7) / days_left;
    }
    const double per_day = planned / 7;
    if (per_day <= 0) return -1.0f;
    if (remaining_ul <= 0) return 0.0f;
    return static_cast<float>(remaining_ul / per_day);
}

}  // namespace doser::sched
