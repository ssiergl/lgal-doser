// Host tests for the schedule logic (components/doser_app/src/schedule_core.cpp).
//   tests/host/run.sh

#include <cstdio>
#include <cstdlib>
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
    RunParams p;  // 370 µl/rev, 200 rpm, ¼ rev retraction, 600 rpm/s
    CHECK(run_seconds(1000, p) == 4);    // 1.78 s of moves + 2 s margin
    CHECK(run_seconds(100000, p) == 85);  // 270 rev at 200 rpm ≈ 82 s
    p.rpm = 100;
    CHECK(run_seconds(100000, p) > 160 && run_seconds(100000, p) < 170);
    p.ul_per_rev = 740;  // twice the volume per revolution: half the time
    CHECK(run_seconds(100000, p) > 80 && run_seconds(100000, p) < 88);
    p.retract_mrev = 0;
    p.ul_per_rev = 370;
    CHECK(run_seconds(20, p) == 3);  // smallest dose: well below a second
}

static Validation run_validate(const PumpSchedule (&all)[kPumps], const Locks& l, Conflict* c, int max,
                               uint32_t limit0 = 0) {
    Context ctx;
    for (auto& r : ctx.run) r.rpm = 100;
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
        l.rules[0] = LockRule{1, 0, 5};
        Validation v = run_validate(all, l, c, 8);
        CHECK(is(v.error, "conflict") && v.conflicts == 1 && is(c[0].code, "collision_lock"));
        CHECK(c[0].first.pump == 0 && c[0].second.pump == 1 && c[0].gap_min == 5);
        all[1] = at(490, 1000);
        CHECK(run_validate(all, l, c, 8).error == nullptr);
        all[1] = at(480, 1000);  // simultaneous: once
        v = run_validate(all, l, c, 8);
        CHECK(v.conflicts == 1);
        all[1] = at(475, 1000);  // the other way round
        v = run_validate(all, l, c, 8);
        CHECK(v.conflicts == 1 && c[0].first.pump == 1 && c[0].second.pump == 0);
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

static void test_boost() {
    const int32_t day = day_num(2026, 11, 6);
    Boost b;
    boost_start(b, 30000, 3, day);
    CHECK(boost_active(b, day));
    // started at noon: only 2 slots left today
    CHECK(boost_preview(b, day, 2) == 5000 && b.remaining_ul == 30000);  // preview takes nothing
    CHECK(boost_take(b, day, 2) == 5000);
    CHECK(boost_take(b, day, 1) == 5000);
    // day 2: 20 ml for 2 days -> 10 ml, 4 slots
    uint32_t sum = 0;
    for (int k = 4; k >= 1; --k) sum += boost_take(b, day + 1, k);
    CHECK(sum == 10000);
    // day 3: first slot fails and is refunded, the rest catches up
    uint32_t s1 = boost_take(b, day + 2, 4);
    CHECK(s1 == 2500);
    boost_refund(b, day + 2, s1);
    sum = 0;
    for (int k = 3; k >= 1; --k) sum += boost_take(b, day + 2, k);
    CHECK(sum == 10000 && b.remaining_ul == 0 && !boost_active(b, day + 2));
    CHECK(boost_take(b, day + 3, 4) == 0);

    // uneven amounts add up exactly
    boost_start(b, 10000, 3, day);
    sum = 0;
    for (int dd = 0; dd < 3; ++dd)
        for (int k = 3; k >= 1; --k) sum += boost_take(b, day + dd, k);
    CHECK(sum == 10000 && b.remaining_ul == 0);

    // a day without slots moves its share to the following days
    boost_start(b, 9000, 3, day);
    sum = 0;
    for (int k = 2; k >= 1; --k) sum += boost_take(b, day + 1, k);
    CHECK(sum == 4500);
    sum = boost_take(b, day + 2, 1);
    CHECK(sum == 4500 && b.remaining_ul == 0);
    // expired with a rest: discarded
    boost_start(b, 9000, 1, day);
    CHECK(boost_take(b, day + 1, 1) == 0 && b.remaining_ul == 0);
}


static void test_forecast() {
    const int32_t day = day_num(2026, 11, 6);
    PumpSchedule ps;
    Boost none;
    CHECK(remaining_days(ps, none, day, 500000) < 0);
    ps = spread(480, 1200, 4, 10000);
    CHECK(remaining_days(ps, none, day, 500000) == 50.0f);
    CHECK(remaining_days(ps, none, day, 0) == 0.0f);
    Boost b;
    boost_start(b, 30000, 3, day);
    const float with_boost = remaining_days(ps, b, day, 500000);  // 100 ml in 7 days
    CHECK(with_boost > 34.9f && with_boost < 35.1f);
    ps.days = weekdays(0x1F);  // Monday to Friday
    const float weekdays_only = remaining_days(ps, none, day, 500000);
    CHECK(weekdays_only > 69.9f && weekdays_only < 70.1f);
    ps = times({{480, 4000}, {1200, 6000}});
    CHECK(remaining_days(ps, none, day, 500000) == 50.0f);
}

static void test_run_records() {
    RunState st;
    st.cursor = Cursor{1793968215, 7713};
    boost_start(st.boost, 30000, 3, 20763);
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
    std::printf("Schedule: boost\n");
    test_boost();
    std::printf("Schedule: forecast\n");
    test_forecast();
    std::printf("Schedule: run state records\n");
    test_run_records();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
