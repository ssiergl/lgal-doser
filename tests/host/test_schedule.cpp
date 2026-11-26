// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// Host tests for the schedule logic (components/doser_app/src/schedule_core.cpp).
//   tests/host/run.sh

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string_view>

#include "doser/civil_time.h"
#include "doser/schedule_core.h"

static int g_fail = 0;
static int g_checks = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

using namespace doser;
using namespace doser::sched;

static int64_t utc_of(int y, int mo, int d, int h, int mi) { return civil::to_unix({y, mo, d, h, mi, 0, 0}); }
static int32_t day_num(int y, int mo, int d) { return static_cast<int32_t>(civil::days_from_civil(y, mo, d)); }

static constexpr uint8_t kMon = 1, kTue = 2, kSun = 64;

static DayRule weekdays(uint8_t mask) { return DayRule{DayMode::Weekdays, mask, 1, 0, 0}; }
static DayRule every(uint8_t n, int32_t anchor) { return DayRule{DayMode::Interval, 0, n, 0, anchor}; }

struct TD {
    uint16_t minute;
    uint32_t ul;
};

// "times" plan with the entries in the given order (validation checks it)
static PumpSchedule times(std::initializer_list<TD> list, DayRule d = weekdays(0x7F)) {
    PumpSchedule p;
    p.kind = Kind::Times;
    for (const TD& t : list) {
        p.times[p.time_count].minute = t.minute;
        p.times[p.time_count].ul = t.ul;
        ++p.time_count;
    }
    p.days = d;
    return p;
}

static PumpSchedule at(uint16_t minute, uint32_t ul, DayRule d = weekdays(0x7F)) { return times({{minute, ul}}, d); }

static PumpSchedule spread(uint16_t from, uint16_t to, uint16_t n, uint32_t ul, DayRule d = weekdays(0x7F)) {
    PumpSchedule p;
    p.kind = Kind::Spread;
    p.start_min = from;
    p.end_min = to;
    p.count = n;
    p.amount_ul = ul;
    p.days = d;
    return p;
}

static void test_days() {
    const int32_t fri = day_num(2026, 11, 6);
    CHECK(weekday_index(fri) == 4);
    CHECK(weekday_index(day_num(2026, 11, 9)) == 0);  // Monday
    CHECK(active_on(weekdays(1 << 4), fri) && !active_on(weekdays(kMon), fri));
    CHECK(active_on(every(3, fri), fri) && active_on(every(3, fri), fri + 3) && active_on(every(3, fri), fri - 3));
    CHECK(!active_on(every(3, fri), fri + 1));

    CHECK(!can_coincide(weekdays(kMon), weekdays(kTue), 0));
    CHECK(can_coincide(weekdays(kMon), weekdays(kTue), 1));    // Monday, then Tuesday
    CHECK(can_coincide(weekdays(kSun), weekdays(kMon), 1));    // across the week boundary
    CHECK(!can_coincide(every(2, 0), every(2, 1), 0));
    CHECK(can_coincide(every(2, 0), every(2, 1), 1));
    const int32_t mon = day_num(2026, 11, 9);
    CHECK(can_coincide(every(7, mon), weekdays(kMon), 0));
    CHECK(!can_coincide(every(7, mon), weekdays(kTue), 0));
    CHECK(can_coincide(every(3, mon), weekdays(kSun), 0));
    CHECK(can_coincide(every(14, 0), every(21, 7), 0));
    CHECK(!can_coincide(every(14, 0), every(21, 3), 0));

    // Property: the congruence check equals a brute-force search
    std::srand(42);
    for (int n = 0; n < 3000; ++n) {
        auto random_rule = [] {
            return std::rand() % 2 ? weekdays(static_cast<uint8_t>(1 + std::rand() % 127))
                                   : every(static_cast<uint8_t>(1 + std::rand() % kMaxInterval), std::rand() % 1000);
        };
        const DayRule a = random_rule(), b = random_rule();
        const int off = std::rand() % 2;
        bool brute = false;
        for (int32_t d = 20000; d < 20000 + 7 * 30 * 29 && !brute; ++d) brute = active_on(a, d) && active_on(b, d + off);
        CHECK(can_coincide(a, b, off) == brute);
    }
}

static void test_expand() {
    Slot s[kMaxSlotsPerDay];
    const char* err = nullptr;
    int entry = -1;
    CHECK(expand(spread(480, 1200, 4, 12000), 5, 20, s, kMaxSlotsPerDay, &err) == 4);
    CHECK(s[0].minute == 480 && s[1].minute == 720 && s[2].minute == 960 && s[3].minute == 1200);
    CHECK(s[0].ul == 3000 && s[3].ul == 3000 && s[2].index == 2);
    CHECK(expand(spread(480, 1200, 3, 10000), 5, 20, s, kMaxSlotsPerDay, &err) == 3);
    CHECK(s[0].ul == 3334 && s[1].ul == 3333 && s[2].ul == 3333);
    CHECK(expand(spread(480, 490, 3, 3000), 5, 20, s, kMaxSlotsPerDay, &err) == 3);
    CHECK(s[0].minute == 480 && s[1].minute == 485 && s[2].minute == 490);
    CHECK(expand(spread(480, 500, 3, 3000), 5, 20, s, kMaxSlotsPerDay, &err) == 3);
    CHECK(s[1].minute == 490);
    CHECK(expand(spread(0, 1435, 96, 96000), 5, 20, s, kMaxSlotsPerDay, &err) == 96);  // every 15 min
    CHECK(s[1].minute == 15 && s[95].minute == 1435);
    // a single dose ignores the end
    CHECK(expand(spread(600, 0, 1, 5000), 5, 20, s, kMaxSlotsPerDay, &err) == 1 && s[0].minute == 600);
    // times: each entry with its own amount
    CHECK(expand(times({{480, 1000}, {600, 2500}, {1260, 500}}), 5, 20, s, kMaxSlotsPerDay, &err) == 3);
    CHECK(s[0].minute == 480 && s[0].ul == 1000 && s[1].ul == 2500 && s[2].minute == 1260 && s[2].ul == 500);
    // no plan: no slots, no error
    CHECK(expand(PumpSchedule{}, 5, 20, s, kMaxSlotsPerDay, &err) == 0);

    auto error_of = [&](const PumpSchedule& p) {
        err = nullptr;
        entry = -1;
        return expand(p, 5, 20, s, kMaxSlotsPerDay, &err, &entry) < 0 ? err : "ok";
    };
    auto is = [](const char* a, const char* b) { return a && b && std::string_view(a) == b; };
    CHECK(is(error_of(spread(480, 490, 4, 4000)), "slots_too_dense"));
    CHECK(is(error_of(spread(481, 600, 2, 4000)), "off_grid") && entry == -1);
    CHECK(is(error_of(spread(1440, 1440, 1, 1000)), "invalid_time"));
    CHECK(is(error_of(spread(480, 480, 1, 10)), "dose_too_small"));
    CHECK(is(error_of(spread(480, 480, 1, 2000000)), "dose_too_large"));
    CHECK(is(error_of(spread(600, 480, 2, 1000)), "invalid_range"));
    CHECK(is(error_of(spread(480, 600, 0, 1000)), "invalid_count"));
    CHECK(is(error_of(spread(480, 600, 97, 100000)), "invalid_count"));
    CHECK(is(error_of(spread(480, 600, 4, 60)), "dose_too_small"));
    CHECK(is(error_of(spread(480, 1442, 4, 6000)), "invalid_time"));
    CHECK(is(error_of(at(480, 1000, weekdays(0))), "invalid_days"));
    CHECK(is(error_of(at(480, 1000, every(0, 0))), "invalid_days"));
    CHECK(is(error_of(at(480, 1000, every(31, 0))), "invalid_days"));
    CHECK(is(error_of(at(480, 1000, every(30, 0))), "ok"));
    // errors of single times name the entry
    CHECK(is(error_of(times({{480, 1000}, {481, 1000}})), "off_grid") && entry == 1);
    CHECK(is(error_of(times({{1440, 1000}})), "invalid_time") && entry == 0);
    CHECK(is(error_of(times({{480, 1000}, {480, 2000}})), "duplicate_time") && entry == 1);
    CHECK(is(error_of(times({{600, 1000}, {480, 2000}})), "invalid_time") && entry == 1);  // not sorted
    CHECK(is(error_of(times({{480, 1000}, {600, 10}})), "dose_too_small") && entry == 1);
    CHECK(is(error_of(times({{480, 2000000}})), "dose_too_large") && entry == 0);
    CHECK(is(error_of(times({})), "invalid_count"));
    PumpSchedule full = times({});
    for (int i = 0; i < kMaxTimes; ++i) full.times[full.time_count++] = TimeDose{uint16_t(i * 60), 0, 1000};
    CHECK(is(error_of(full), "ok"));
    full.time_count = kMaxTimes + 1;
    CHECK(is(error_of(full), "invalid_count"));
    PumpSchedule bad = at(480, 1000);
    bad.kind = static_cast<Kind>(7);
    CHECK(is(error_of(bad), "invalid_kind"));
    // grid 15 min (super admin)
    err = nullptr;
    CHECK(expand(at(485, 1000), 15, 20, s, kMaxSlotsPerDay, &err) < 0 && is(err, "off_grid"));

    Kind k = Kind::None;
    CHECK(parse_kind("times", &k) && k == Kind::Times && parse_kind("spread", &k) && k == Kind::Spread);
    CHECK(parse_kind("none", &k) && k == Kind::None && !parse_kind("single", &k));
    CHECK(is(kind_str(Kind::Spread), "spread") && is(kind_str(Kind::Times), "times") && is(kind_str(Kind::None), "none"));
}

static void test_day_plan() {
    Slot s[kMaxSlotsPerDay];
    const int32_t mon = day_num(2026, 11, 9);
    PumpSchedule ps = spread(480, 1200, 3, 9000);  // 08:00, 14:00, 20:00
    CHECK(day_slots(ps, mon, 5, s, kMaxSlotsPerDay) == 3 && s[1].minute == 840 && s[1].ul == 3000);
    CHECK(day_total(ps, mon) == 9000);

    ps = times({{600, 2000}, {840, 3000}}, weekdays(kMon));
    CHECK(runs_on(ps, mon) && !runs_on(ps, mon + 1));
    CHECK(day_slots(ps, mon, 5, s, kMaxSlotsPerDay) == 2 && s[0].minute == 600 && s[1].ul == 3000);
    CHECK(day_slots(ps, mon + 1, 5, s, kMaxSlotsPerDay) == 0);
    CHECK(day_total(ps, mon) == 5000 && day_total(ps, mon + 1) == 0);
    CHECK(day_total(PumpSchedule{}, mon) == 0 && !runs_on(PumpSchedule{}, mon));
}

static void test_runtime() {
    RunParams p;  // 200 rpm, ¼ rev retraction, 600 rpm/s
    p.ul_per_rev = 370;
    CHECK(run_seconds(1000, p) == 4);    // 1.78 s of moves + 2 s margin
    CHECK(run_seconds(100000, p) == 85);  // 270 rev at 200 rpm ≈ 82 s
    p.rpm = 100;
    CHECK(run_seconds(100000, p) > 160 && run_seconds(100000, p) < 170);
    p.ul_per_rev = 740;  // twice the volume per revolution: half the time
    CHECK(run_seconds(100000, p) > 80 && run_seconds(100000, p) < 88);
    p.retract_mrev = 0;
    p.ul_per_rev = 370;
    CHECK(run_seconds(20, p) == 3);  // smallest dose: well below a second

    // lock time from the minute in which the other pump's dose ended
    CHECK(lock_start(120) == 120 && lock_start(179) == 120);
    Locks l;
    l.count = 1;
    l.rules[0] = LockRule{0, 1, 30};
    const int64_t t0 = 1800000000 / 60 * 60;  // 08:00:00 somewhere
    int64_t last_end[kPumps] = {t0 + 7, 0, 0, 0, 0};
    bool running[kPumps] = {};
    CHECK(lock_until(l, 1, last_end, running, t0 + 1800) == t0 + 1800);  // exactly 30 min later: on time
    CHECK(lock_until(l, 1, last_end, running, t0 + 1799) == t0 + 1800);
    last_end[0] = t0 + 61;  // the dose ran into the next minute
    CHECK(lock_until(l, 1, last_end, running, t0 + 1800) == t0 + 1860);
    CHECK(lock_until(l, 2, last_end, running, t0) == t0);  // no rule for pump 3
    running[0] = true;
    CHECK(lock_until(l, 1, last_end, running, t0 + 1800) == INT64_MAX);
    // the pump holding the lock: the running one, the one with the latest end, or none
    int by = 7;
    CHECK(lock_until(l, 1, last_end, running, t0 + 1800, &by) == INT64_MAX && by == 0);
    running[0] = false;
    l.count = 2;
    l.rules[1] = LockRule{2, 1, 45};
    last_end[2] = t0 + 30;
    CHECK(lock_until(l, 1, last_end, running, t0 + 1800, &by) == t0 + 2700 && by == 2);
    CHECK(lock_until(l, 1, last_end, running, t0 + 2700, &by) == t0 + 2700 && by == -1);
    CHECK(parse_manual_during_lock("ask", &l.manual) && l.manual == ManualDuringLock::Ask);
    CHECK(!std::strcmp(manual_during_lock_str(ManualDuringLock::Ask), "ask"));
    CHECK(!parse_manual_during_lock("maybe", &l.manual) && l.manual == ManualDuringLock::Ask);
}

static Validation run_validate(const PumpSchedule (&all)[kPumps], const Locks& l, Conflict* c, int max,
                               uint32_t limit0 = 0) {
    Context ctx;
    for (auto& r : ctx.run) {  // the run times below assume 370 µl/rev
        r.rpm = 100;
        r.ul_per_rev = 370;
    }
    ctx.daily_limit_ul[0] = limit0;
    ctx.today = day_num(2026, 11, 6);
    return validate(all, l, ctx, c, max);
}

static bool is(const char* a, const char* b) { return a && b && std::string_view(a) == b; }


static void test_validate() {
    Conflict c[8]{};
    Locks none;
    {
        // overlap inside one plan
        PumpSchedule all[kPumps];
        all[0] = times({{480, 1000000}, {505, 1000}});  // 1 l at 100 rpm ≈ 27 min
        Validation v = run_validate(all, none, c, 8);
        CHECK(is(v.error, "conflict") && v.conflicts == 1 && is(c[0].code, "overlap"));
        CHECK(c[0].first.pump == 0 && c[0].first.minute == 480 && c[0].second.minute == 505 && !c[0].next_day);
        all[0] = times({{480, 1000000}, {510, 1000}});
        CHECK(run_validate(all, none, c, 8).error == nullptr);
        all[0] = spread(480, 1200, 4, 40000);  // 10 ml at 100 rpm ≈ 18 s
        CHECK(run_validate(all, none, c, 8).error == nullptr);
        all[0] = spread(480, 490, 3, 1500000);  // 500 ml every 5 min
        v = run_validate(all, none, c, 8);
        CHECK(is(v.error, "conflict") && v.conflicts == 3);  // 1st with 2nd and 3rd, 2nd with 3rd
    }
    {
        // across midnight, only if the following day really has the slot
        PumpSchedule all[kPumps];
        all[0] = times({{0, 1000}, {1435, 500000}});  // 23:55, ≈ 14 min
        Validation v = run_validate(all, none, c, 8);
        CHECK(is(v.error, "conflict") && v.conflicts == 1 && c[0].next_day == 1);
        CHECK(c[0].first.minute == 1435 && c[0].second.minute == 0);
        all[0].days = weekdays(kMon);  // Tuesday has no slot at 00:00
        CHECK(run_validate(all, none, c, 8).error == nullptr);
    }
    {
        // lock time 5 min between pump 1 and 2 (index 0 and 1)
        PumpSchedule all[kPumps];
        all[0] = at(480, 1000);  // ≈ 4 s
        all[1] = at(485, 1000);
        Locks l;
        l.count = 1;
        l.rules[0] = LockRule{1, 0, 6};
        Validation v = run_validate(all, l, c, 8);
        CHECK(is(v.error, "conflict") && v.conflicts == 1 && is(c[0].code, "collision_lock"));
        CHECK(c[0].first.pump == 0 && c[0].second.pump == 1 && c[0].gap_min == 6);
        all[1] = at(490, 1000);
        CHECK(run_validate(all, l, c, 8).error == nullptr);
        // exactly the lock time apart fits: it counts from the minute in which the dose ends
        l.rules[0] = LockRule{1, 0, 5};
        all[1] = at(485, 1000);
        CHECK(run_validate(all, l, c, 8).error == nullptr);
        all[0] = at(480, 100000);  // 100 ml ≈ 85 s: ends in the next minute
        v = run_validate(all, l, c, 8);
        CHECK(is(v.error, "conflict") && v.conflicts == 1 && is(c[0].code, "collision_lock"));
        all[0] = at(480, 1000);
        all[1] = at(480, 1000);  // simultaneous: once
        v = run_validate(all, l, c, 8);
        CHECK(v.conflicts == 1);
        all[1] = at(475, 1000);  // the other way round, again exactly the lock time
        CHECK(run_validate(all, l, c, 8).error == nullptr);
        l.rules[0] = LockRule{1, 0, 6};
        v = run_validate(all, l, c, 8);
        CHECK(v.conflicts == 1 && c[0].first.pump == 1 && c[0].second.pump == 0);
        l.rules[0] = LockRule{1, 0, 5};
        all[1] = at(475, 1000, weekdays(kTue));  // never on the same day
        all[0].days = weekdays(kMon);
        CHECK(run_validate(all, l, c, 8).error == nullptr);
        all[1] = at(480, 1000);
        all[0].days = weekdays(0x7F);
        // no rule for pump 3: no conflict
        l.rules[0] = LockRule{2, 0, 5};
        CHECK(run_validate(all, l, c, 8).error == nullptr);
        l.count = 2;
        l.rules[1] = LockRule{1, 1, 5};
        v = run_validate(all, l, c, 8);
        CHECK(is(v.error, "invalid_lock") && v.rule == 1);
    }
    {
        // structural errors and daily limit
        PumpSchedule all[kPumps];
        all[3] = times({{480, 1000}, {483, 1000}});
        Validation v = run_validate(all, none, c, 8);
        CHECK(is(v.error, "off_grid") && v.pump == 3 && v.entry == 1);
        all[3] = at(480, 1000);
        all[3].power_loss = PowerLoss::CatchUpWindow;
        all[3].catch_up_min = 0;
        v = run_validate(all, none, c, 8);
        CHECK(is(v.error, "invalid_catch_up") && v.pump == 3);
        all[3] = PumpSchedule{};
        all[0] = spread(480, 1200, 4, 11000, every(3, day_num(2026, 11, 20)));
        CHECK(run_validate(all, none, c, 8, 11000).error == nullptr);
        v = run_validate(all, none, c, 8, 10000);  // 11 ml every 3rd day
        CHECK(is(v.error, "exceeds_daily_limit") && v.pump == 0);
        all[0] = times({{480, 6000}, {1200, 5000}});
        v = run_validate(all, none, c, 8, 10000);  // sum of the single times
        CHECK(is(v.error, "exceeds_daily_limit") && v.pump == 0);
    }
    {
        // more conflicts than space: count is capped
        PumpSchedule all[kPumps];
        all[0] = spread(0, 1425, 96, 96 * 600000);  // 600 ml every 15 min
        Validation v = run_validate(all, none, c, 8);
        CHECK(is(v.error, "conflict") && v.conflicts == 8);
    }
}

static void test_due() {
    tz::Zone z;
    CHECK(tz::parse("CET-1CEST,M3.5.0,M10.5.0/3", &z));
    PumpSchedule ps = times({{480, 1000}, {600, 2000}, {1200, 2000}});  // 08:00, 10:00, 20:00
    Due d;
    Cursor cur{utc_of(2026, 11, 6, 0, 0), 0};
    CHECK(next_due(ps, z, 5, cur, utc_of(2026, 11, 8, 0, 0), &d));
    CHECK(d.utc == utc_of(2026, 11, 6, 7, 0) && d.minute == 480 && d.remaining_today == 3 && d.ul == 1000);
    cur = cursor_of(d);
    CHECK(next_due(ps, z, 5, cur, utc_of(2026, 11, 8, 0, 0), &d));
    CHECK(d.utc == utc_of(2026, 11, 6, 9, 0) && d.minute == 600 && d.remaining_today == 2 && d.ul == 2000);
    // nothing due before `until`
    CHECK(!next_due(ps, z, 5, cursor_of(d), utc_of(2026, 11, 6, 18, 59), &d));
    // summer time: 08:00 CEST = 06:00 UTC
    CHECK(next_due(ps, z, 5, Cursor{utc_of(2026, 7, 1, 0, 0), 0}, utc_of(2026, 7, 2, 0, 0), &d));
    CHECK(d.utc == utc_of(2026, 7, 1, 6, 0));
    CHECK(count_due(ps, z, 5, Cursor{utc_of(2026, 7, 1, 0, 0), 0}, utc_of(2026, 7, 4, 0, 0)) == 9);
    // spread plans the same way
    const PumpSchedule sp = spread(480, 1200, 3, 9000);
    CHECK(count_due(sp, z, 5, Cursor{utc_of(2026, 11, 6, 0, 0), 0}, utc_of(2026, 11, 8, 0, 0)) == 6);
    // without plan: nothing due
    CHECK(!next_due(PumpSchedule{}, z, 5, cur, utc_of(2026, 11, 8, 0, 0), &d));

    // spring forward: 02:30 does not exist and runs at 03:30, together with
    // the regular 03:30 slot; both exactly once
    const PumpSchedule night = times({{150, 1000}, {210, 2000}});  // 02:30, 03:30
    cur = Cursor{utc_of(2026, 3, 28, 23, 0), 0};
    const int64_t until = utc_of(2026, 3, 29, 12, 0);
    CHECK(next_due(night, z, 5, cur, until, &d) && d.utc == utc_of(2026, 3, 29, 1, 30) && d.minute == 150);
    CHECK(next_due(night, z, 5, cursor_of(d), until, &d) && d.utc == utc_of(2026, 3, 29, 1, 30) && d.minute == 210);
    CHECK(!next_due(night, z, 5, cursor_of(d), until, &d));
    // fall back: 02:30 happens twice, the slot runs once (first occurrence)
    cur = Cursor{utc_of(2026, 10, 24, 22, 0), 0};
    CHECK(count_due(night, z, 5, cur, utc_of(2026, 10, 25, 12, 0)) == 2);
    CHECK(next_due(night, z, 5, cur, utc_of(2026, 10, 25, 12, 0), &d) && d.utc == utc_of(2026, 10, 25, 0, 30));
    CHECK(next_due(night, z, 5, cursor_of(d), utc_of(2026, 10, 25, 12, 0), &d) &&
          d.utc == utc_of(2026, 10, 25, 2, 30));  // 03:30 CET
    // clock set back by an hour: the cursor stays, nothing runs twice
    CHECK(!next_due(night, z, 5, cursor_of(d), utc_of(2026, 10, 25, 2, 0), &d));

    // power loss
    Due late{utc_of(2026, 11, 6, 7, 0), 0, 480, 1000, 0, 1};
    CHECK(on_due(ps, late, late.utc + 60) == Action::Run);
    CHECK(on_due(ps, late, late.utc + 600) == Action::SkipMissed);
    ps.power_loss = PowerLoss::CatchUpWindow;
    ps.catch_up_min = 30;
    CHECK(on_due(ps, late, late.utc + 600) == Action::Run);
    CHECK(on_due(ps, late, late.utc + 1801) == Action::SkipMissed);
    // long outage: start shortly before the window, skipped slots can be counted
    const int64_t now = utc_of(2026, 11, 10, 9, 10);  // 10:10 local
    const Cursor old{utc_of(2026, 11, 6, 21, 0), 0};
    const Cursor r = resume_point(ps, old, now);
    CHECK(r.utc == now - 1801);
    CHECK(count_due(ps, z, 5, old, r.utc) == 3 * 3 + 1);  // 7th 08:00 .. 10th 08:00
    CHECK(next_due(ps, z, 5, r, now, &d) && d.utc == utc_of(2026, 11, 10, 9, 0));  // 10:00 local, 10 min late
    CHECK(on_due(ps, d, now) == Action::Run);
    CHECK(resume_point(ps, Cursor{now - 60, 0}, now).utc == now - 60);
}

// --- counting due slots against every single slot ---------------------------
static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(uint32_t n) {  // 0..n-1, xorshift64* (the same sequence on every run)
    g_rng ^= g_rng >> 12;
    g_rng ^= g_rng << 25;
    g_rng ^= g_rng >> 27;
    return static_cast<uint32_t>((g_rng * 0x2545F4914F6CDD1Dull) >> 32) % n;
}

// Slots in (after, until], every slot of every day looked at (two days more
// on each side than the scheduler needs): number, the latest and the earliest one
static int every_slot(const PumpSchedule& s, const tz::Zone& z, const Cursor& after, int64_t until, Due* last,
                      Due* first = nullptr) {
    Slot slots[kMaxSlotsPerDay];
    int n = 0;
    const int32_t from = day_of(tz::to_local(z, after.utc)) - 2, to = day_of(tz::to_local(z, until)) + 2;
    for (int32_t d = from; d <= to; ++d) {
        const int k = day_slots(s, d, 5, slots, kMaxSlotsPerDay);
        for (int i = 0; i < k; ++i) {
            const int64_t utc = tz::to_utc(z, int64_t(d) * 86400 + slots[i].minute * 60);
            const uint32_t tag = slots[i].minute * 16u + 1u;
            if (!(utc > after.utc || (utc == after.utc && tag > after.tag)) || utc > until) continue;
            const Due due{utc, d, slots[i].minute, slots[i].ul, tag, k - i};
            if (!n || utc > last->utc || (utc == last->utc && tag > last->tag)) *last = due;
            if (first && (!n || utc < first->utc || (utc == first->utc && tag < first->tag))) *first = due;
            ++n;
        }
    }
    return n;
}

static bool same_due(const Due& a, const Due& b) {
    return a.utc == b.utc && a.tag == b.tag && a.day == b.day && a.minute == b.minute && a.ul == b.ul &&
           a.remaining_today == b.remaining_today;
}

static PumpSchedule random_plan() {
    DayRule days = rnd(2) ? weekdays(static_cast<uint8_t>(1 + rnd(127)))
                          : every(static_cast<uint8_t>(1 + rnd(kMaxInterval)), static_cast<int32_t>(rnd(60)) - 30);
    if (rnd(2)) {
        const uint16_t from = static_cast<uint16_t>(5 * rnd(288));
        if (from == 1435 || rnd(8) == 0) return spread(from, from, 1, 20 + rnd(5000), days);
        const uint16_t to = static_cast<uint16_t>(from + 5 * (1 + rnd((1435 - from) / 5)));
        const int most = (to - from) / 5 + 1 < kMaxSlotsPerDay ? (to - from) / 5 + 1 : kMaxSlotsPerDay;
        const uint16_t n = static_cast<uint16_t>(2 + rnd(static_cast<uint32_t>(most - 1)));
        return spread(from, to, n, n * (20 + rnd(5000)), days);
    }
    PumpSchedule p = at(0, 1, days);
    p.time_count = 0;
    for (uint16_t m = 0; m < kMinutesPerDay && p.time_count < kMaxTimes; m += 5)
        if (rnd(288) < 6) p.times[p.time_count++] = TimeDose{m, 0, 20 + rnd(5000)};
    if (!p.time_count) p.times[p.time_count++] = TimeDose{static_cast<uint16_t>(5 * rnd(288)), 0, 1000};
    return p;
}

static void test_due_counting() {
    // gaps at midnight (Chile), half-hour offsets and DST (Lord Howe), negative change times (Nuuk),
    // a gap and a double hour across midnight (made up: slots move into the next or previous day)
    const char* zones[] = {"UTC0",
                           "CET-1CEST,M3.5.0,M10.5.0/3",
                           "EST5EDT,M3.2.0,M11.1.0",
                           "AEST-10AEDT,M10.1.0,M4.1.0/3",
                           "<+1030>-10:30<+11>-11,M10.1.0,M4.1.0",
                           "<-04>4<-03>,M9.1.6/24,M4.1.6/24",
                           "IST-5:30",
                           "<-03>3<-02>,M3.5.0/-2,M10.5.0/-1",
                           "<+01>-1<+02>,M3.5.0/23:30,M10.5.0/0:30"};
    constexpr int kZones = sizeof(zones) / sizeof(zones[0]);
    tz::Zone zone[kZones];
    for (int i = 0; i < kZones; ++i) CHECK(tz::parse(zones[i], &zone[i]));
    // their DST changes in 2026..2028, to the day
    const int64_t t0 = utc_of(2026, 1, 1, 0, 0);
    int64_t change[kZones][8];
    int changes[kZones] = {};
    for (int i = 0; i < kZones; ++i)
        for (int64_t t = t0; t < t0 + 3 * 365 * 86400LL && changes[i] < 8; t += 86400)
            if (tz::offset_at(zone[i], t) != tz::offset_at(zone[i], t + 86400)) change[i][changes[i]++] = t;
    CHECK(changes[0] == 0 && changes[1] == 6 && changes[5] == 6);

    // the same count and latest slot as looking at every slot, also around DST changes
    int differ = 0, differ_next = 0, counted = 0;
    for (int run = 0; run < 4000; ++run) {
        const int zi = static_cast<int>(rnd(kZones));
        const tz::Zone& z = zone[zi];
        const PumpSchedule s = random_plan();
        int64_t from = t0 + int64_t(rnd(2 * 365)) * 86400 + rnd(86400);
        if (changes[zi] && rnd(2)) from = change[zi][rnd(changes[zi])] - 2 * 86400 + rnd(4 * 86400);
        const uint32_t pick = rnd(3);
        const Cursor after{from, pick == 0 ? 0u : pick == 1 ? UINT32_MAX : 5u * rnd(288) * 16u + 1u};
        const uint32_t len = rnd(10) < 6 ? rnd(3 * 86400) : rnd(10) < 7 ? rnd(40 * 86400) : rnd(380 * 86400);
        const int64_t until = from + len;
        Due fast{}, slow{}, next{}, first{};
        const int n = count_due(s, z, 5, after, until, &fast), expected = every_slot(s, z, after, until, &slow, &first);
        counted += expected;
        if (n != expected || (n && !same_due(fast, slow))) {
            if (!differ) std::printf("  first difference: run %d, %d instead of %d slots\n", run, n, expected);
            ++differ;
        }
        // and the same next slot
        const bool found = next_due(s, z, 5, after, until, &next);
        if (found != (expected > 0) || (found && !same_due(next, first))) {
            if (!differ_next) std::printf("  first other next slot: run %d\n", run);
            ++differ_next;
        }
    }
    CHECK(differ == 0);
    CHECK(differ_next == 0);
    CHECK(counted > 100000);  // the runs really counted something

    // The made-up zone jumps from Sunday 29 March 2026 23:30 to Monday 00:30: Sunday's 23:45
    // (22:45 UTC) lies between Monday's 00:40 (22:40 UTC) and 00:15 (23:15 UTC)
    const tz::Zone& mid = zone[8];
    const PumpSchedule night = times({{15, 1000}, {40, 2000}, {1425, 3000}});
    const Cursor after_0040{utc_of(2026, 3, 29, 22, 40), 40 * 16 + 1};
    Due late{};
    CHECK(count_due(night, mid, 5, Cursor{utc_of(2026, 3, 29, 22, 0), 0}, utc_of(2026, 3, 29, 23, 0), &late) == 2);
    CHECK(late.minute == 1425 && late.day == day_num(2026, 3, 29) && late.utc == utc_of(2026, 3, 29, 22, 45));
    CHECK(count_due(night, mid, 5, after_0040, utc_of(2026, 3, 29, 23, 0), &late) == 1 && late.minute == 1425);
    CHECK(next_due(night, mid, 5, Cursor{utc_of(2026, 3, 29, 22, 0), 0}, utc_of(2026, 3, 30, 12, 0), &late) &&
          late.minute == 40 && late.utc == utc_of(2026, 3, 29, 22, 40));
    CHECK(next_due(night, mid, 5, after_0040, utc_of(2026, 3, 30, 12, 0), &late) && late.minute == 1425);
    CHECK(next_due(night, mid, 5, cursor_of(late), utc_of(2026, 3, 30, 12, 0), &late) && late.minute == 15 &&
          late.utc == utc_of(2026, 3, 29, 23, 15));

    // a gap of more than 400 days counts only its last 401 days
    const tz::Zone utc0{};
    const int64_t until = utc_of(2026, 11, 26, 12, 0);
    CHECK(count_due(at(480, 1000), utc0, 5, Cursor{until - 1000 * 86400, 0}, until) == 401);

    // a year without power and the densest plan: about as cheap as a day (one search
    // per slot took about 3 s on a PC; the device is many times slower, its watchdog bites after 10 s)
    const PumpSchedule dense = spread(0, 1435, kMaxSlotsPerDay, kMaxSlotsPerDay * 100);
    const tz::Zone& cet = zone[1];
    const auto start = std::chrono::steady_clock::now();
    int n = 0;
    Due d{};
    for (int p = 0; p < kPumps; ++p) n += count_due(dense, cet, 5, Cursor{until - 400 * 86400, 0}, until, &d);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    CHECK(n == kPumps * every_slot(dense, cet, Cursor{until - 400 * 86400, 0}, until, &d));
    CHECK(ms.count() < 100);
}

static void test_boost() {
    // 10 ml over 3 days, started 18:01 with 12 doses a day: 36 doses get the same share
    const int64_t t0 = utc_of(2026, 11, 6, 17, 1);
    Boost b;
    boost_start(b, 10000, 3, t0, 36);
    CHECK(boost_active(b, t0) && b.slots_left == 36 && boost_end(b) == t0 + 3 * 86400);
    CHECK(boost_share(b) == 277);
    uint32_t sum = 0, first = 0;
    for (int i = 0; i < 36; ++i) {
        const int64_t at = t0 + 60 + i * 7200;
        const uint32_t s = boost_take(b, at);
        if (!i) first = s;
        sum += s;
        boost_pass(b, at, 1);
    }
    CHECK(first == 277 && sum == 10000 && b.remaining_ul == 0 && !boost_active(b, t0));

    // failed dose: refunded, its share goes to the later slots; so does a missed one
    boost_start(b, 9000, 1, t0, 3);
    const uint32_t s1 = boost_take(b, t0 + 100);
    CHECK(s1 == 3000);
    boost_refund(b, s1);
    boost_pass(b, t0 + 100, 1);
    CHECK(boost_share(b) == 4500 && b.remaining_ul == 9000);
    boost_pass(b, t0 + 200, 1);
    CHECK(boost_share(b) == 9000 && b.slots_left == 1);
    CHECK(boost_take(b, t0 + 300) == 9000);
    boost_pass(b, t0 + 300, 1);
    CHECK(b.remaining_ul == 0 && b.total_ul == 0);
    // after the window: the rest is dropped
    boost_start(b, 9000, 1, t0, 2);
    CHECK(boost_take(b, t0 + 86401) == 0 && b.remaining_ul == 0);
    // the last slot of the window passed with a rest (skipped): over
    boost_start(b, 6000, 2, t0, 2);
    boost_pass(b, t0 + 10, 2);
    CHECK(b.remaining_ul == 0 && !boost_active(b, t0));
    // schedule changed: new number of slots
    boost_start(b, 6000, 2, t0, 4);
    boost_recount(b, 6);
    CHECK(boost_share(b) == 1000);
    boost_recount(b, 0);
    CHECK(b.remaining_ul == 0);
    // no slots in the window: nothing starts
    boost_start(b, 6000, 2, t0, 0);
    CHECK(!boost_active(b, t0) && b.remaining_ul == 0);

    // window of the next 13 doses: no time limit, ends with the 13th slot
    boost_start(b, 13000, 0, t0, 13);
    CHECK(boost_active(b, t0 + 40 * 86400) && b.doses == 13 && b.slots_left == 13 && boost_end(b) == INT64_MAX);
    CHECK(boost_take(b, t0 + 20 * 86400) == 1000);  // a slot far ahead still belongs to it
    boost_pass(b, t0 + 20 * 86400, 1);
    boost_recount(b, 40);  // schedule changed: the count of doses stays
    CHECK(b.slots_left == 12 && boost_share(b) == 1000);
    boost_pass(b, t0 + 21 * 86400, 12);
    CHECK(b.remaining_ul == 0 && !boost_active(b, t0));
    boost_start(b, 13000, 0, t0, 13);
    boost_recount(b, 0);  // plan without slots: over
    CHECK(b.remaining_ul == 0);
    boost_start(b, 13000, 0, t0, kMaxBoostDoses + 1);  // too many doses
    CHECK(b.remaining_ul == 0);
}


static void test_forecast() {
    const int32_t day = day_num(2026, 11, 6);
    PumpSchedule ps;
    const int64_t now = utc_of(2026, 11, 6, 6, 0);
    Boost none;
    CHECK(remaining_days(ps, none, 0, day, now, 500000) < 0);
    ps = spread(480, 1200, 4, 10000);
    CHECK(remaining_days(ps, none, 0, day, now, 500000) == 50.0f);
    CHECK(remaining_days(ps, none, 0, day, now, 0) == 0.0f);
    Boost b;
    boost_start(b, 30000, 3, now, 12);
    const float with_boost = remaining_days(ps, b, boost_end(b), day, now, 500000);  // 100 ml in 7 days
    CHECK(with_boost > 34.9f && with_boost < 35.1f);
    boost_start(b, 140000, 14, now, 56);  // half of it falls into the next 7 days
    const float long_boost = remaining_days(ps, b, boost_end(b), day, now, 500000);  // 140 ml in 7 days
    CHECK(long_boost > 24.9f && long_boost < 25.1f);
    boost_start(b, 30000, 0, now, 12);  // window in doses, the last one in 3 days
    const float by_doses = remaining_days(ps, b, now + 3 * 86400, day, now, 500000);
    CHECK(by_doses > 34.9f && by_doses < 35.1f);
    ps.days = weekdays(0x1F);  // Monday to Friday
    const float weekdays_only = remaining_days(ps, none, 0, day, now, 500000);
    CHECK(weekdays_only > 69.9f && weekdays_only < 70.1f);
    ps = times({{480, 4000}, {1200, 6000}});
    CHECK(remaining_days(ps, none, 0, day, now, 500000) == 50.0f);
}

static void test_same() {
    const PumpSchedule a = times({{480, 1000}, {720, 2000}});
    PumpSchedule b = a;
    CHECK(same(a, b));
    b.times[5] = {999, 9, 9};  // unused entry
    CHECK(same(a, b));
    b.times[1].ul = 2100;
    CHECK(!same(a, b));
    b = a;
    b.days = weekdays(kMon);
    CHECK(!same(a, b));
    b = a;
    b.catch_up_min = 30;
    CHECK(!same(a, b));
    CHECK(same(spread(480, 1200, 4, 4000), spread(480, 1200, 4, 4000)));
    CHECK(!same(spread(480, 1200, 4, 4000), spread(480, 1200, 5, 4000)));
    CHECK(!same(spread(480, 1200, 4, 4000), spread(480, 1200, 4, 4000, every(2, 0))));
    CHECK(same(spread(480, 1200, 4, 4000, every(3, 10)), spread(480, 1200, 4, 4000, every(3, 4))));
    CHECK(!same(spread(480, 1200, 4, 4000, every(3, 10)), spread(480, 1200, 4, 4000, every(3, 5))));
    b = a;
    b.days.anchor_day = 77;  // not used by weekdays
    b.count = 9;             // not used by single times
    CHECK(same(a, b));
    PumpSchedule none;
    PumpSchedule none2;
    none2.amount_ul = 5;  // without a kind nothing else counts
    CHECK(same(none, none2) && !same(none, a));
}

static void test_run_records() {
    RunState st;
    st.cursor = Cursor{1793968215, 7713};
    boost_start(st.boost, 30000, 3, 1793968215, 12);
    st.skipped = 4;
    RunRecord a, b;
    seal(a, 7, st);
    CHECK(valid(a) && a.state.cursor.utc == 1793968215 && a.state.boost.remaining_ul == 30000);
    seal(b, 8, st);
    CHECK(newest(a, b) == 1 && newest(b, a) == 0);
    b.state.cursor.tag ^= 1;  // bit error / half written
    CHECK(!valid(b) && newest(a, b) == 0);
    RunRecord z{};
    CHECK(newest(z, z) == -1);
    seal(a, 0xFFFFFFFFu, st);  // sequence number wraps around
    seal(b, 0, st);
    CHECK(newest(a, b) == 1);
}

int main() {
    std::printf("Schedule: days\n");
    test_days();
    std::printf("Schedule: plans and slots\n");
    test_expand();
    test_day_plan();
    std::printf("Schedule: runtime\n");
    test_runtime();
    std::printf("Schedule: validation\n");
    test_validate();
    std::printf("Schedule: due slots, DST, power loss\n");
    test_due();
    std::printf("Schedule: counting due slots day by day\n");
    test_due_counting();
    std::printf("Schedule: boost\n");
    test_boost();
    std::printf("Schedule: forecast\n");
    test_forecast();
    std::printf("Schedule: same plan\n");
    test_same();
    std::printf("Schedule: run state records\n");
    test_run_records();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
