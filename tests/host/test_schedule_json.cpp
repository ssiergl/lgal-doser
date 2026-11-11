// Host tests for the JSON form of schedules and lock rules.
#include <cstdio>
#include <cstring>
#include <string>

#include "doser/schedule_json.h"

using namespace doser;
using namespace doser::sched_json;
using lgal::Json;
using lgal::JsonReader;
using lgal::JsonToken;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

constexpr int32_t kToday = 20763;  // 2026-11-06, a Friday
static JsonToken g_tok[512];

struct Parsed {
    bool ok;
    Error err;
    sched::PumpSchedule s;
};

static Parsed parse_sched(const std::string& text, int32_t today = kToday) {
    Parsed p{};
    JsonReader r;
    if (!r.parse(text.data(), text.size(), g_tok, 512)) {
        std::printf("  JSON error %s at %zu\n", r.error(), r.error_pos());
        return p;
    }
    p.ok = parse_schedule(r.root(), today, &p.s, &p.err);
    return p;
}

static bool same(const sched::PumpSchedule& a, const sched::PumpSchedule& b) {
    bool eq = a.kind == b.kind && a.power_loss == b.power_loss &&
              a.catch_up_min == b.catch_up_min && a.days.mode == b.days.mode && a.days.weekdays == b.days.weekdays &&
              a.days.interval == b.days.interval &&
              (a.days.mode == sched::DayMode::Weekdays || a.days.anchor_day == b.days.anchor_day);
    if (a.kind == sched::Kind::Spread)
        eq = eq && a.count == b.count && a.start_min == b.start_min && a.end_min == b.end_min &&
             a.amount_ul == b.amount_ul;
    if (a.kind == sched::Kind::Times) {
        eq = eq && a.time_count == b.time_count;
        for (int i = 0; eq && i < a.time_count; ++i)
            eq = a.times[i].minute == b.times[i].minute && a.times[i].ul == b.times[i].ul;
    }
    return eq;
}

static std::string written(const sched::PumpSchedule& s) {
    Json j;
    j.obj();
    write_schedule(j, s);
    j.end();
    return j.str();
}

static void test_time_and_date() {
    uint16_t m = 0;
    bool all = true;
    for (uint16_t i = 0; i < 1440; ++i) {
        char t[6];
        fmt_hhmm(i, t);
        all = all && parse_hhmm(t, &m) && m == i;
    }
    CHECK(all);
    const char* bad[] = {"8:00", "24:00", "12:60", "12-00", "12:000", "", "ab:cd", " 8:00"};
    for (const char* s : bad) CHECK(!parse_hhmm(s, &m));
    CHECK(parse_hhmm("23:59", &m) && m == 1439);

    CHECK(days_from_civil(1970, 1, 1) == 0);
    CHECK(days_from_civil(2026, 11, 6) == kToday);
    int32_t d = 0;
    CHECK(parse_date("2026-11-06", &d) && d == kToday);
    CHECK(parse_date("2024-02-29", &d));
    const char* bad_dates[] = {"2026-02-29", "2026-13-01", "2026-00-10", "2026-04-31", "1969-12-31",
                               "2200-01-01", "2026-1-06",  "2026/11/06", "20261106",   ""};
    for (const char* s : bad_dates) CHECK(!parse_date(s, &d));
    bool round = true;
    for (int32_t n = 0; n <= 84005; ++n) {  // 1970-01-01 .. 2199-12-31
        char buf[11];
        fmt_date(n, buf);
        int32_t back = -1;
        round = round && parse_date(buf, &back) && back == n;
    }
    CHECK(round);
    char loc[17];
    fmt_local(int64_t(kToday) * 86400 + 14 * 3600 + 5 * 60 + 59, loc);
    CHECK(std::strcmp(loc, "2026-11-06 14:05") == 0);
    fmt_local(-60, loc);
    CHECK(std::strcmp(loc, "1969-12-31 23:59") == 0);
}

static void test_schedule_round_trip() {
    // spread on three weekdays
    sched::PumpSchedule s;
    s.kind = sched::Kind::Spread;
    s.power_loss = sched::PowerLoss::CatchUpWindow;
    s.catch_up_min = 45;
    s.count = 12;
    s.start_min = 8 * 60;
    s.end_min = 19 * 60 + 5;
    s.amount_ul = 24125;
    s.days.weekdays = 0x15;  // mon, wed, fri
    std::string text = written(s);
    CHECK(text.find("\"kind\":\"spread\",\"start\"") != std::string::npos && text.find("enabled") == std::string::npos);
    CHECK(text.find("\"weekdays\":[\"mon\",\"wed\",\"fri\"]") != std::string::npos);
    CHECK(text.find("\"end\":\"19:05\"") != std::string::npos);
    CHECK(text.find("\"ml\":24.125") != std::string::npos);
    CHECK(text.find("\"times\"") == std::string::npos);
    Parsed p = parse_sched(text);
    CHECK(p.ok && same(p.s, s));

    // single times with their own amounts, every 3rd day
    sched::PumpSchedule t;
    t.kind = sched::Kind::Times;
    t.time_count = 3;
    t.times[0] = sched::TimeDose{8 * 60, 0, 2500};
    t.times[1] = sched::TimeDose{12 * 60 + 15, 0, 20};
    t.times[2] = sched::TimeDose{21 * 60 + 30, 0, 1000000};
    t.days = sched::DayRule{sched::DayMode::Interval, 0x7F, 3, 0, kToday - 1};
    text = written(t);
    CHECK(text.find("\"times\":[{\"time\":\"08:00\",\"ml\":2.500},{\"time\":\"12:15\",\"ml\":0.020},"
                    "{\"time\":\"21:30\",\"ml\":1000.000}]") != std::string::npos);
    CHECK(text.find("\"anchor\":\"2026-11-05\"") != std::string::npos);
    CHECK(text.find("\"start\"") == std::string::npos && text.find("\"count\"") == std::string::npos);
    p = parse_sched(text);
    CHECK(p.ok && same(p.s, t));

    // no schedule
    text = written(sched::PumpSchedule{});
    CHECK(text.find("\"kind\":\"none\"") != std::string::npos && text.find("\"mode\":\"daily\"") != std::string::npos);
    p = parse_sched(text);
    CHECK(p.ok && p.s.kind == sched::Kind::None && p.s.power_loss == sched::PowerLoss::SkipMissed);
}

static void test_schedule_defaults() {
    // empty object: no schedule
    Parsed p = parse_sched("{}");
    CHECK(p.ok && p.s.kind == sched::Kind::None);
    // the former pause flag of old files is ignored
    p = parse_sched("{\"enabled\":false}");
    CHECK(p.ok && p.s.kind == sched::Kind::None);

    // single dose: "end" is not needed and follows the start
    p = parse_sched("{\"kind\":\"spread\",\"start\":\"07:15\",\"count\":1,\"ml\":1}");
    CHECK(p.ok && p.s.start_min == 435 && p.s.end_min == 435 && p.s.amount_ul == 1000);
    CHECK(p.s.days.mode == sched::DayMode::Weekdays && p.s.days.weekdays == 0x7F);
    CHECK(p.s.power_loss == sched::PowerLoss::SkipMissed && p.s.catch_up_min == 60);

    // times are sorted; unknown members (from a GET) are ignored; anchor defaults to today
    p = parse_sched(
        "{\"kind\":\"times\",\"times\":[{\"time\":\"20:00\",\"ml\":3},{\"time\":\"07:15\",\"ml\":1},"
        "{\"time\":\"12:00\",\"ml\":0.5}],\"days\":{\"mode\":\"interval\",\"every\":2},"
        "\"start\":\"01:00\",\"pump\":1,\"grid_min\":5,\"today\":[]}");
    CHECK(p.ok && p.s.kind == sched::Kind::Times && p.s.time_count == 3);
    CHECK(p.s.times[0].minute == 435 && p.s.times[0].ul == 1000 && p.s.times[1].minute == 720 &&
          p.s.times[1].ul == 500 && p.s.times[2].minute == 1200 && p.s.times[2].ul == 3000);
    CHECK(p.s.days.mode == sched::DayMode::Interval && p.s.days.anchor_day == kToday && p.s.start_min == 0);

    // the result passes the real validation and expands as expected
    sched::PumpSchedule all[sched::kPumps];
    all[0] = p.s;
    all[1] = parse_sched("{\"kind\":\"spread\",\"start\":\"10:00\",\"end\":\"12:00\",\"count\":3,\"ml\":3}").s;
    sched::Context ctx;
    ctx.today = kToday;
    sched::Conflict c[4]{};
    const sched::Validation v = sched::validate(all, sched::Locks{}, ctx, c, 4);
    CHECK(v.error == nullptr);
    sched::Slot slots[16];
    CHECK(sched::day_slots(all[0], kToday, 5, slots, 16) == 3);
    CHECK(sched::day_slots(all[0], kToday + 1, 5, slots, 16) == 0);
    CHECK(sched::day_slots(all[1], kToday + 1, 5, slots, 16) == 3 && slots[1].minute == 660);

    // equal times parse and are rejected by the validation (entry after sorting)
    p = parse_sched("{\"kind\":\"times\",\"times\":[{\"time\":\"08:00\",\"ml\":1},{\"time\":\"08:00\",\"ml\":2}]}");
    CHECK(p.ok);
    all[0] = p.s;
    const sched::Validation d = sched::validate(all, sched::Locks{}, ctx, c, 4);
    CHECK(d.error && !std::strcmp(d.error, "duplicate_time") && d.pump == 0 && d.entry == 1);
}

static void test_schedule_errors() {
    struct Case {
        const char* text;
        const char* code;
        int entry;
        const char* field;
        int32_t today;
    };
    const Case cases[] = {
        {"[]", "invalid_param", -1, "body", kToday},
        {"{\"kind\":\"single\"}", "invalid_param", -1, "kind", kToday},
        {"{\"kind\":1}", "invalid_param", -1, "kind", kToday},
        {"{\"power_loss\":\"later\"}", "invalid_param", -1, "power_loss", kToday},
        {"{\"catch_up_min\":-1}", "invalid_param", -1, "catch_up_min", kToday},
        {"{\"catch_up_min\":\"60\"}", "invalid_param", -1, "catch_up_min", kToday},
        // spread
        {"{\"kind\":\"spread\",\"count\":1,\"ml\":1}", "invalid_param", -1, "start", kToday},
        {"{\"kind\":\"spread\",\"start\":\"8:00\",\"count\":1,\"ml\":1}", "invalid_param", -1, "start", kToday},
        {"{\"kind\":\"spread\",\"start\":480,\"count\":1,\"ml\":1}", "invalid_param", -1, "start", kToday},
        {"{\"kind\":\"spread\",\"start\":\"08:00\",\"ml\":1}", "invalid_param", -1, "count", kToday},
        {"{\"kind\":\"spread\",\"start\":\"08:00\",\"count\":0,\"ml\":1}", "invalid_param", -1, "count", kToday},
        {"{\"kind\":\"spread\",\"start\":\"08:00\",\"count\":97,\"end\":\"20:00\",\"ml\":1}", "invalid_param", -1,
         "count", kToday},
        {"{\"kind\":\"spread\",\"start\":\"08:00\",\"count\":2,\"ml\":1}", "invalid_param", -1, "end", kToday},
        {"{\"kind\":\"spread\",\"start\":\"08:00\",\"count\":1}", "invalid_param", -1, "ml", kToday},
        {"{\"kind\":\"spread\",\"start\":\"08:00\",\"count\":1,\"ml\":0}", "invalid_param", -1, "ml", kToday},
        {"{\"kind\":\"spread\",\"start\":\"08:00\",\"count\":1,\"ml\":-1}", "invalid_param", -1, "ml", kToday},
        {"{\"kind\":\"spread\",\"start\":\"08:00\",\"count\":1,\"ml\":\"1\"}", "invalid_param", -1, "ml", kToday},
        // times
        {"{\"kind\":\"times\"}", "invalid_param", -1, "times", kToday},
        {"{\"kind\":\"times\",\"times\":{}}", "invalid_param", -1, "times", kToday},
        {"{\"kind\":\"times\",\"times\":[]}", "invalid_param", -1, "times", kToday},
        {"{\"kind\":\"times\",\"times\":[1]}", "invalid_param", 0, "time", kToday},
        {"{\"kind\":\"times\",\"times\":[{\"time\":\"08:00\",\"ml\":1},{\"ml\":1}]}", "invalid_param", 1, "time",
         kToday},
        {"{\"kind\":\"times\",\"times\":[{\"time\":\"08:00\",\"ml\":1},{\"time\":\"24:00\",\"ml\":1}]}",
         "invalid_param", 1, "time", kToday},
        {"{\"kind\":\"times\",\"times\":[{\"time\":\"08:00\"}]}", "invalid_param", 0, "ml", kToday},
        {"{\"kind\":\"times\",\"times\":[{\"time\":\"08:00\",\"ml\":1},{\"time\":\"09:00\",\"ml\":0}]}",
         "invalid_param", 1, "ml", kToday},
        {"{\"kind\":\"times\",\"times\":[{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}]}",
         "invalid_param", -1, "times", kToday},
        // days
        {"{\"days\":\"daily\"}", "invalid_param", -1, "days", kToday},
        {"{\"days\":{\"mode\":\"monthly\"}}", "invalid_param", -1, "mode", kToday},
        {"{\"days\":{\"mode\":\"weekdays\",\"weekdays\":[\"mon\",\"Mon\"]}}", "invalid_param", -1, "weekdays", kToday},
        {"{\"days\":{\"mode\":\"weekdays\"}}", "invalid_param", -1, "weekdays", kToday},
        {"{\"days\":{\"mode\":\"interval\"}}", "invalid_param", -1, "every", kToday},
        {"{\"days\":{\"mode\":\"interval\",\"every\":2,\"anchor\":\"06.11.2026\"}}", "invalid_param", -1, "anchor",
         kToday},
        {"{\"days\":{\"mode\":\"interval\",\"every\":2}}", "time_invalid", -1, "anchor", -1},
    };
    for (const auto& c : cases) {
        const Parsed p = parse_sched(c.text, c.today);
        const bool ok = !p.ok && p.err.code && !std::strcmp(p.err.code, c.code) && p.err.entry == c.entry &&
                        p.err.field && !std::strcmp(p.err.field, c.field);
        if (!ok)
            std::printf("  %s -> %s %s %d %s\n", c.text, p.ok ? "accepted" : "rejected", p.err.code ? p.err.code : "-",
                        p.err.entry, p.err.field ? p.err.field : "-");
        CHECK(ok);
    }
    // an empty weekday list is accepted here and rejected by the validation
    const Parsed e = parse_sched(
        "{\"kind\":\"times\",\"times\":[{\"time\":\"08:00\",\"ml\":1}],"
        "\"days\":{\"mode\":\"weekdays\",\"weekdays\":[]}}");
    CHECK(e.ok && e.s.days.weekdays == 0);
    sched::PumpSchedule all[sched::kPumps];
    all[0] = e.s;
    sched::Conflict c[2]{};
    const sched::Validation v = sched::validate(all, sched::Locks{}, sched::Context{}, c, 2);
    CHECK(v.error && !std::strcmp(v.error, "invalid_days") && v.pump == 0 && v.entry == -1);
}

static void test_locks() {
    sched::Locks l;
    l.manual = sched::ManualDuringLock::Queue;
    l.count = 2;
    l.rules[0] = sched::LockRule{0, 1, 5};
    l.rules[1] = sched::LockRule{4, 2, 30};
    Json j;
    j.obj();
    write_locks(j, l);
    j.end();
    CHECK(j.str() == "{\"manual\":\"queue\",\"rules\":[{\"pumps\":[1,2],\"gap_min\":5},{\"pumps\":[5,3],\"gap_min\":30}]}");

    JsonReader r;
    sched::Locks back;
    Error err;
    CHECK(r.parse(j.c_str(), j.str().size(), g_tok, 512) && parse_locks(r.root(), &back, &err));
    CHECK(back.count == 2 && back.manual == sched::ManualDuringLock::Queue);
    CHECK(back.rules[1].a == 4 && back.rules[1].b == 2 && back.rules[1].gap_min == 30);

    const char* empty = "{}";
    CHECK(r.parse(empty, 2, g_tok, 512) && parse_locks(r.root(), &back, &err));
    CHECK(back.count == 0 && back.manual == sched::ManualDuringLock::Reject);

    struct Case {
        const char* text;
        const char* field;
        int rule;
    };
    const Case bad[] = {
        {"{\"manual\":\"wait\"}", "manual", -1},
        {"{\"rules\":{}}", "rules", -1},
        {"{\"rules\":[{\"pumps\":[1,6],\"gap_min\":5}]}", "pumps", 0},
        {"{\"rules\":[{\"gap_min\":5},{\"pumps\":[1],\"gap_min\":5}]}", "pumps", 0},
        {"{\"rules\":[{\"pumps\":[1,2],\"gap_min\":5},{\"pumps\":[1,2,3],\"gap_min\":5}]}", "pumps", 1},
        {"{\"rules\":[{\"pumps\":[1,2],\"gap_min\":\"5\"}]}", "gap_min", 0},
        {"{\"rules\":[{\"pumps\":[0,2],\"gap_min\":5}]}", "pumps", 0},
        {"{\"rules\":[1,2,3,4,5,6,7,8,9,10,11]}", "rules", -1},
    };
    for (const auto& c : bad) {
        Error e;
        const bool parsed = r.parse(c.text, std::strlen(c.text), g_tok, 512);
        const bool ok = parsed && !parse_locks(r.root(), &back, &e) && e.field && !std::strcmp(e.field, c.field) &&
                        e.rule == c.rule;
        if (!ok) std::printf("  %s -> %s rule %d\n", c.text, e.field ? e.field : "-", e.rule);
        CHECK(ok);
    }
    // same pump twice parses and is rejected by the validation
    const char* same_pump = "{\"rules\":[{\"pumps\":[2,2],\"gap_min\":5}]}";
    CHECK(r.parse(same_pump, std::strlen(same_pump), g_tok, 512) && parse_locks(r.root(), &back, &err));
    sched::PumpSchedule all[sched::kPumps];
    sched::Conflict c[2]{};
    CHECK(sched::validate(all, back, sched::Context{}, c, 2).error != nullptr);
}

static void test_conflict() {
    sched::Conflict c{"collision_lock", {0, 1, 8 * 60}, {1, 0, 8 * 60 + 2}, 0, 5};
    Json j;
    write_conflict(j, c);
    CHECK(j.str() ==
          "{\"code\":\"collision_lock\",\"first\":{\"pump\":1,\"time\":\"08:00\"},"
          "\"second\":{\"pump\":2,\"time\":\"08:02\"},\"next_day\":false,\"gap_min\":5}");
    sched::Conflict o{"overlap", {2, 0, 23 * 60 + 55}, {2, 1, 0}, 1, 0};
    Json k;
    write_conflict(k, o);
    CHECK(k.str().find("\"next_day\":true}") != std::string::npos);
    CHECK(k.str().find("gap_min") == std::string::npos);
}

int main() {
    std::printf("Schedule JSON: times and dates\n");
    test_time_and_date();
    std::printf("Schedule JSON: round trip\n");
    test_schedule_round_trip();
    std::printf("Schedule JSON: defaults and validation\n");
    test_schedule_defaults();
    std::printf("Schedule JSON: errors\n");
    test_schedule_errors();
    std::printf("Schedule JSON: lock rules\n");
    test_locks();
    std::printf("Schedule JSON: conflicts\n");
    test_conflict();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
