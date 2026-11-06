// Host tests for the schedule logic (components/doser_app/src/schedule_core.cpp).
//   tests/host/run.sh

#include <cstdio>
#include <cstdlib>
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

static Program single(uint16_t minute, uint32_t ul, DayRule d = weekdays(0x7F)) {
    Program p;
    p.kind = Kind::Single;
    p.start_min = minute;
    p.amount_ul = ul;
    p.days = d;
    return p;
}

static Program generator(uint16_t from, uint16_t to, uint16_t n, uint32_t ul, DayRule d = weekdays(0x7F)) {
    Program p;
    p.kind = Kind::Generator;
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
    CHECK(expand(generator(480, 1200, 4, 12000), 2, 5, 20, s, kMaxSlotsPerDay, &err) == 4);
    CHECK(s[0].minute == 480 && s[1].minute == 720 && s[2].minute == 960 && s[3].minute == 1200);
    CHECK(s[0].ul == 3000 && s[3].ul == 3000 && s[0].program == 2);
    CHECK(expand(generator(480, 1200, 3, 10000), 0, 5, 20, s, kMaxSlotsPerDay, &err) == 3);
    CHECK(s[0].ul == 3334 && s[1].ul == 3333 && s[2].ul == 3333);
    CHECK(expand(generator(480, 490, 3, 3000), 0, 5, 20, s, kMaxSlotsPerDay, &err) == 3);
    CHECK(s[0].minute == 480 && s[1].minute == 485 && s[2].minute == 490);
    CHECK(expand(generator(480, 500, 3, 3000), 0, 5, 20, s, kMaxSlotsPerDay, &err) == 3);
    CHECK(s[1].minute == 490);
    CHECK(expand(generator(0, 1435, 96, 96000), 0, 5, 20, s, kMaxSlotsPerDay, &err) == 96);  // every 15 min
    CHECK(s[1].minute == 15 && s[95].minute == 1435);
    CHECK(expand(generator(600, 600, 1, 5000), 0, 5, 20, s, kMaxSlotsPerDay, &err) == 1 && s[0].minute == 600);

    auto error_of = [&](const Program& p) {
        err = nullptr;
        return expand(p, 0, 5, 20, s, kMaxSlotsPerDay, &err) < 0 ? err : "ok";
    };
    auto is = [](const char* a, const char* b) { return a && b && std::string_view(a) == b; };
    CHECK(is(error_of(generator(480, 490, 4, 4000)), "slots_too_dense"));
    CHECK(is(error_of(single(481, 1000)), "off_grid"));
    CHECK(is(error_of(single(1440, 1000)), "invalid_time"));
    CHECK(is(error_of(single(480, 10)), "dose_too_small"));
    CHECK(is(error_of(single(480, 2000000)), "dose_too_large"));
    CHECK(is(error_of(generator(600, 480, 2, 1000)), "invalid_range"));
    CHECK(is(error_of(generator(480, 600, 0, 1000)), "invalid_count"));
    CHECK(is(error_of(generator(480, 600, 97, 100000)), "invalid_count"));
    CHECK(is(error_of(generator(480, 600, 4, 60)), "dose_too_small"));
    CHECK(is(error_of(generator(480, 1442, 4, 6000)), "invalid_time"));
    CHECK(is(error_of(single(480, 1000, weekdays(0))), "invalid_days"));
    CHECK(is(error_of(single(480, 1000, every(0, 0))), "invalid_days"));
    CHECK(is(error_of(single(480, 1000, every(31, 0))), "invalid_days"));
    CHECK(is(error_of(single(480, 1000, every(30, 0))), "ok"));
    // grid 15 min (super admin)
    err = nullptr;
    CHECK(expand(single(485, 1000), 0, 15, 20, s, kMaxSlotsPerDay, &err) < 0 && is(err, "off_grid"));
}

static void test_day_plan() {
    PumpSchedule ps;
    ps.count = 3;
    ps.programs[0] = generator(480, 1200, 3, 9000);   // 08:00, 14:00, 20:00
    ps.programs[1] = single(600, 2000, weekdays(kMon));
    ps.programs[2] = single(300, 1000);
    ps.programs[2].enabled = 0;
    Slot s[kMaxSlotsPerDay];
    const int32_t mon = day_num(2026, 11, 9);
    CHECK(day_slots(ps, mon, 5, s, kMaxSlotsPerDay) == 4);
    CHECK(s[0].minute == 480 && s[1].minute == 600 && s[1].program == 1 && s[2].minute == 840 && s[3].minute == 1200);
    CHECK(day_slots(ps, mon + 1, 5, s, kMaxSlotsPerDay) == 3);
    CHECK(day_total(ps, mon) == 11000 && day_total(ps, mon + 1) == 9000);
    // same minute: ordered by program
    ps.programs[1] = single(840, 2000, weekdays(kMon));
    CHECK(day_slots(ps, mon, 5, s, kMaxSlotsPerDay) == 4);
    CHECK(s[1].minute == 840 && s[1].program == 0 && s[2].program == 1);
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
        PumpSchedule all[kPumps];
        all[0].count = 2;
        all[0].programs[0] = generator(480, 1200, 4, 40000);
        all[0].programs[1] = single(485, 1000);
        Validation v = run_validate(all, none, c, 8);
        CHECK(v.error == nullptr && v.conflicts == 0);  // 10 ml at 100 rpm ≈ 18 s
        all[0].programs[1] = single(480, 1000);
        v = run_validate(all, none, c, 8);
        CHECK(is(v.error, "conflict") && v.conflicts == 1);  // same start: reported once
        CHECK(is(c[0].code, "overlap") && c[0].first.minute == 480 && c[0].second.minute == 480);
        all[0].programs[0] = single(480, 1000000);  // 1 l at 100 rpm ≈ 27 min
        all[0].programs[1] = single(505, 1000);
        v = run_validate(all, none, c, 8);
        CHECK(is(v.error, "conflict") && v.conflicts == 1 && c[0].first.program == 0 && c[0].second.minute == 505);
        all[0].programs[1] = single(510, 1000);
        CHECK(run_validate(all, none, c, 8).error == nullptr);
    }
    {
        // across midnight, only if the following day really has the slot
        PumpSchedule all[kPumps];
        all[0].count = 2;
        all[0].programs[0] = single(1435, 500000, weekdays(kMon));  // 23:55, ≈ 14 min
        all[0].programs[1] = single(0, 1000, weekdays(kMon));
        CHECK(run_validate(all, none, c, 8).error == nullptr);
        all[0].programs[1].days = weekdays(kTue);
        Validation v = run_validate(all, none, c, 8);
        CHECK(is(v.error, "conflict") && c[0].next_day == 1 && c[0].second.minute == 0);
    }
    {
        // lock time 5 min between pump 1 and 2 (index 0 and 1)
        PumpSchedule all[kPumps];
        all[0].count = 1;
        all[0].programs[0] = single(480, 1000);  // ≈ 4 s
        all[1].count = 1;
        all[1].programs[0] = single(485, 1000);
        Locks l;
        l.count = 1;
        l.rules[0] = LockRule{1, 0, 5};
        Validation v = run_validate(all, l, c, 8);
        CHECK(is(v.error, "conflict") && v.conflicts == 1 && is(c[0].code, "collision_lock"));
        CHECK(c[0].first.pump == 0 && c[0].second.pump == 1 && c[0].gap_min == 5);
        all[1].programs[0] = single(490, 1000);
        CHECK(run_validate(all, l, c, 8).error == nullptr);
        all[1].programs[0] = single(480, 1000);  // simultaneous: once
        v = run_validate(all, l, c, 8);
        CHECK(v.conflicts == 1);
        all[1].programs[0] = single(475, 1000);  // the other way round
        v = run_validate(all, l, c, 8);
        CHECK(v.conflicts == 1 && c[0].first.pump == 1 && c[0].second.pump == 0);
        all[1].programs[0] = single(475, 1000, weekdays(kTue));  // never on the same day
        all[0].programs[0].days = weekdays(kMon);
        CHECK(run_validate(all, l, c, 8).error == nullptr);
        // no rule for pump 3: no conflict
        all[1].programs[0] = single(480, 1000);
        all[0].programs[0].days = weekdays(0x7F);
        l.rules[0] = LockRule{2, 0, 5};
        CHECK(run_validate(all, l, c, 8).error == nullptr);
        l.rules[0] = LockRule{1, 1, 5};
        v = run_validate(all, l, c, 8);
        CHECK(is(v.error, "invalid_lock") && v.program == 0);
    }
    {
        // structural error and daily limit
        PumpSchedule all[kPumps];
        all[3].count = 2;
        all[3].programs[0] = single(480, 1000);
        all[3].programs[1] = single(483, 1000);
        Validation v = run_validate(all, none, c, 8);
        CHECK(is(v.error, "off_grid") && v.pump == 3 && v.program == 1);
        all[0].count = 2;
        all[0].programs[0] = generator(480, 1200, 4, 8000);
        all[0].programs[1] = single(300, 3000, every(3, day_num(2026, 11, 20)));
        all[3].count = 0;
        CHECK(run_validate(all, none, c, 8, 11000).error == nullptr);
        v = run_validate(all, none, c, 8, 10000);  // 11 ml every 3rd day
        CHECK(is(v.error, "exceeds_daily_limit") && v.pump == 0);
    }
    {
        // more conflicts than space: count is capped
        PumpSchedule all[kPumps];
        all[0].count = 2;
        all[0].programs[0] = generator(0, 1425, 96, 96000);
        all[0].programs[1] = generator(0, 1425, 96, 96000);
        Validation v = run_validate(all, none, c, 8);
        CHECK(is(v.error, "too_many_slots"));
        all[0].programs[0] = generator(0, 1380, 24, 24000);
        all[0].programs[1] = generator(0, 1380, 24, 24000);
        v = run_validate(all, none, c, 8);
        CHECK(is(v.error, "conflict") && v.conflicts == 8);
    }
}

static void test_due() {
    tz::Zone z;
    CHECK(tz::parse("CET-1CEST,M3.5.0,M10.5.0/3", &z));
    PumpSchedule ps;
    ps.count = 2;
    ps.programs[0] = single(480, 1000);           // 08:00
    ps.programs[1] = generator(600, 1200, 2, 4000);  // 10:00, 20:00
    Due d;
    Cursor cur{utc_of(2026, 11, 6, 0, 0), 0};
    CHECK(next_due(ps, z, 5, cur, utc_of(2026, 11, 8, 0, 0), &d));
    CHECK(d.utc == utc_of(2026, 11, 6, 7, 0) && d.minute == 480 && d.remaining_today == 3 && d.ul == 1000);
    cur = cursor_of(d);
    CHECK(next_due(ps, z, 5, cur, utc_of(2026, 11, 8, 0, 0), &d));
    CHECK(d.utc == utc_of(2026, 11, 6, 9, 0) && d.program == 1 && d.remaining_today == 2 && d.ul == 2000);
    // nothing due before `until`
    CHECK(!next_due(ps, z, 5, cursor_of(d), utc_of(2026, 11, 6, 18, 59), &d));
    // summer time: 08:00 CEST = 06:00 UTC
    CHECK(next_due(ps, z, 5, Cursor{utc_of(2026, 7, 1, 0, 0), 0}, utc_of(2026, 7, 2, 0, 0), &d));
    CHECK(d.utc == utc_of(2026, 7, 1, 6, 0));
    CHECK(count_due(ps, z, 5, Cursor{utc_of(2026, 7, 1, 0, 0), 0}, utc_of(2026, 7, 4, 0, 0)) == 9);

    // spring forward: 02:30 does not exist and runs at 03:30, together with
    // the regular 03:30 slot; both exactly once
    PumpSchedule night;
    night.count = 2;
    night.programs[0] = single(150, 1000);  // 02:30
    night.programs[1] = single(210, 2000);  // 03:30
    cur = Cursor{utc_of(2026, 3, 28, 23, 0), 0};
    const int64_t until = utc_of(2026, 3, 29, 12, 0);
    CHECK(next_due(night, z, 5, cur, until, &d) && d.utc == utc_of(2026, 3, 29, 1, 30) && d.program == 0);
    CHECK(next_due(night, z, 5, cursor_of(d), until, &d) && d.utc == utc_of(2026, 3, 29, 1, 30) && d.program == 1);
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
    Due late{utc_of(2026, 11, 6, 7, 0), 0, 480, 0, 1000, 0, 1};
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
    ps.count = 1;
    ps.programs[0] = generator(480, 1200, 4, 10000);
    CHECK(remaining_days(ps, none, day, 500000) == 50.0f);
    CHECK(remaining_days(ps, none, day, 0) == 0.0f);
    Boost b;
    boost_start(b, 30000, 3, day);
    const float with_boost = remaining_days(ps, b, day, 500000);  // 100 ml in 7 days
    CHECK(with_boost > 34.9f && with_boost < 35.1f);
    ps.programs[0].days = weekdays(0x1F);  // Monday to Friday
    const float weekdays_only = remaining_days(ps, none, day, 500000);
    CHECK(weekdays_only > 69.9f && weekdays_only < 70.1f);
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
    std::printf("Schedule: programs and slots\n");
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
