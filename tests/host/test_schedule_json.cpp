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

static bool same(const sched::Program& a, const sched::Program& b) {
    return a.enabled == b.enabled && a.kind == b.kind && a.count == b.count && a.start_min == b.start_min &&
           a.end_min == b.end_min && a.amount_ul == b.amount_ul && a.days.mode == b.days.mode &&
           a.days.weekdays == b.days.weekdays && a.days.interval == b.days.interval &&
           (a.days.mode == sched::DayMode::Weekdays || a.days.anchor_day == b.days.anchor_day);
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
    sched::PumpSchedule s;
    s.power_loss = sched::PowerLoss::CatchUpWindow;
    s.catch_up_min = 45;
    s.count = 3;
    s.programs[0] = sched::Program{1, sched::Kind::Single, 1, 8 * 60, 8 * 60, 2500, {}};
    s.programs[1] = sched::Program{0, sched::Kind::Generator, 12, 8 * 60, 19 * 60 + 5, 24125, {}};
    s.programs[1].days.weekdays = 0x15;  // mon, wed, fri
    s.programs[2] = sched::Program{1, sched::Kind::Single, 1, 21 * 60 + 30, 21 * 60 + 30, 20, {}};
    s.programs[2].days = sched::DayRule{sched::DayMode::Interval, 0x7F, 3, 0, kToday - 1};

    Json j;
    j.obj();
    write_schedule(j, s);
    j.end();
    const std::string text = j.str();
    CHECK(text.find("\"weekdays\":[\"mon\",\"wed\",\"fri\"]") != std::string::npos);
    CHECK(text.find("\"anchor\":\"2026-11-05\"") != std::string::npos);
    CHECK(text.find("\"mode\":\"daily\"") != std::string::npos);
    CHECK(text.find("\"end\":\"19:05\"") != std::string::npos);
    CHECK(text.find("\"ml\":24.125") != std::string::npos);

    const Parsed p = parse_sched(text);
    CHECK(p.ok);
    CHECK(p.s.count == 3 && p.s.power_loss == s.power_loss && p.s.catch_up_min == 45);
    for (int i = 0; i < 3; ++i) CHECK(same(p.s.programs[i], s.programs[i]));

    // an empty schedule
    Json e;
    e.obj();
    write_schedule(e, sched::PumpSchedule{});
    e.end();
    const Parsed pe = parse_sched(e.str());
    CHECK(pe.ok && pe.s.count == 0 && pe.s.power_loss == sched::PowerLoss::SkipMissed);
}

static void test_schedule_defaults() {
    const Parsed p = parse_sched(
        "{\"programs\":[{\"kind\":\"single\",\"time\":\"07:15\",\"ml\":1},"
        "{\"kind\":\"single\",\"time\":\"09:00\",\"ml\":0.5,\"days\":{\"mode\":\"interval\",\"every\":2}},"
        "{\"kind\":\"generator\",\"start\":\"10:00\",\"end\":\"12:00\",\"count\":3,\"ml\":3,\"days\":null}],"
        "\"pump\":1,\"today\":[]}");  // unknown members (from a GET) are ignored
    CHECK(p.ok && p.s.count == 3);
    const sched::Program& a = p.s.programs[0];
    CHECK(a.enabled == 1 && a.kind == sched::Kind::Single && a.start_min == 435 && a.amount_ul == 1000);
    CHECK(a.days.mode == sched::DayMode::Weekdays && a.days.weekdays == 0x7F);
    CHECK(p.s.programs[1].days.mode == sched::DayMode::Interval && p.s.programs[1].days.anchor_day == kToday);
    CHECK(p.s.programs[2].days.weekdays == 0x7F && p.s.programs[2].count == 3);
    CHECK(p.s.power_loss == sched::PowerLoss::SkipMissed && p.s.catch_up_min == 60);

    // the result passes the real validation and expands as expected
    sched::PumpSchedule all[sched::kPumps];
    all[0] = p.s;
    sched::Context ctx;
    ctx.today = kToday;
    sched::Conflict c[4]{};
    const sched::Validation v = sched::validate(all, sched::Locks{}, ctx, c, 4);
    CHECK(v.error == nullptr);
    sched::Slot slots[16];
    CHECK(sched::day_slots(p.s, kToday, 5, slots, 16) == 5);  // 07:15, 09:00, 10:00, 11:00, 12:00
    CHECK(sched::day_slots(p.s, kToday + 1, 5, slots, 16) == 4);
}

static void test_schedule_errors() {
    struct Case {
        const char* text;
        const char* code;
        int program;
        const char* field;
        int32_t today;
    };
    const Case cases[] = {
        {"[]", "invalid_param", -1, "body", kToday},
        {"{\"power_loss\":\"later\"}", "invalid_param", -1, "power_loss", kToday},
        {"{\"catch_up_min\":-1}", "invalid_param", -1, "catch_up_min", kToday},
        {"{\"catch_up_min\":\"60\"}", "invalid_param", -1, "catch_up_min", kToday},
        {"{\"programs\":{}}", "invalid_param", -1, "programs", kToday},
        {"{\"programs\":[1]}", "invalid_param", 0, "programs", kToday},
        {"{\"programs\":[{\"time\":\"08:00\",\"ml\":1}]}", "invalid_param", 0, "kind", kToday},
        {"{\"programs\":[{\"kind\":\"once\",\"time\":\"08:00\",\"ml\":1}]}", "invalid_param", 0, "kind", kToday},
        {"{\"programs\":[{\"kind\":\"single\",\"time\":\"8:00\",\"ml\":1}]}", "invalid_param", 0, "time", kToday},
        {"{\"programs\":[{\"kind\":\"single\",\"time\":480,\"ml\":1}]}", "invalid_param", 0, "time", kToday},
        {"{\"programs\":[{\"kind\":\"single\",\"time\":\"08:00\"}]}", "invalid_param", 0, "ml", kToday},
        {"{\"programs\":[{\"kind\":\"single\",\"time\":\"08:00\",\"ml\":0}]}", "invalid_param", 0, "ml", kToday},
        {"{\"programs\":[{\"kind\":\"single\",\"time\":\"08:00\",\"ml\":-1}]}", "invalid_param", 0, "ml", kToday},
        {"{\"programs\":[{\"kind\":\"single\",\"time\":\"08:00\",\"ml\":\"1\"}]}", "invalid_param", 0, "ml", kToday},
        {"{\"programs\":[{\"kind\":\"single\",\"time\":\"08:00\",\"ml\":1,\"enabled\":1}]}", "invalid_param", 0,
         "enabled", kToday},
        {"{\"programs\":[{\"kind\":\"single\",\"time\":\"08:00\",\"ml\":1},"
         "{\"kind\":\"generator\",\"start\":\"08:00\",\"end\":\"09:00\",\"ml\":1}]}",
         "invalid_param", 1, "count", kToday},
        {"{\"programs\":[{\"kind\":\"generator\",\"start\":\"08:00\",\"count\":2,\"ml\":1}]}", "invalid_param", 0, "end",
         kToday},
        {"{\"programs\":[{\"kind\":\"generator\",\"start\":\"08:00\",\"end\":\"09:00\",\"count\":0,\"ml\":1}]}",
         "invalid_param", 0, "count", kToday},
        {"{\"programs\":[{\"kind\":\"single\",\"time\":\"08:00\",\"ml\":1,\"days\":\"daily\"}]}", "invalid_param", 0,
         "days", kToday},
        {"{\"programs\":[{\"kind\":\"single\",\"time\":\"08:00\",\"ml\":1,\"days\":{\"mode\":\"monthly\"}}]}",
         "invalid_param", 0, "mode", kToday},
        {"{\"programs\":[{\"kind\":\"single\",\"time\":\"08:00\",\"ml\":1,"
         "\"days\":{\"mode\":\"weekdays\",\"weekdays\":[\"mon\",\"Mon\"]}}]}",
         "invalid_param", 0, "weekdays", kToday},
        {"{\"programs\":[{\"kind\":\"single\",\"time\":\"08:00\",\"ml\":1,\"days\":{\"mode\":\"weekdays\"}}]}",
         "invalid_param", 0, "weekdays", kToday},
        {"{\"programs\":[{\"kind\":\"single\",\"time\":\"08:00\",\"ml\":1,\"days\":{\"mode\":\"interval\"}}]}",
         "invalid_param", 0, "every", kToday},
        {"{\"programs\":[{\"kind\":\"single\",\"time\":\"08:00\",\"ml\":1,"
         "\"days\":{\"mode\":\"interval\",\"every\":2,\"anchor\":\"06.11.2026\"}}]}",
         "invalid_param", 0, "anchor", kToday},
        {"{\"programs\":[{\"kind\":\"single\",\"time\":\"08:00\",\"ml\":1,"
         "\"days\":{\"mode\":\"interval\",\"every\":2}}]}",
         "time_invalid", 0, "anchor", -1},
        {"{\"programs\":[{},{},{},{},{},{},{},{},{}]}", "invalid_param", -1, "programs", kToday},
    };
    for (const auto& c : cases) {
        const Parsed p = parse_sched(c.text, c.today);
        const bool ok = !p.ok && p.err.code && !std::strcmp(p.err.code, c.code) && p.err.program == c.program &&
                        p.err.field && !std::strcmp(p.err.field, c.field);
        if (!ok)
            std::printf("  %s -> %s %s %d %s\n", c.text, p.ok ? "accepted" : "rejected", p.err.code ? p.err.code : "-",
                        p.err.program, p.err.field ? p.err.field : "-");
        CHECK(ok);
    }
    // an empty weekday list is accepted here and rejected by the validation
    const Parsed e = parse_sched(
        "{\"programs\":[{\"kind\":\"single\",\"time\":\"08:00\",\"ml\":1,"
        "\"days\":{\"mode\":\"weekdays\",\"weekdays\":[]}}]}");
    CHECK(e.ok && e.s.programs[0].days.weekdays == 0);
    sched::PumpSchedule all[sched::kPumps];
    all[0] = e.s;
    sched::Conflict c[2]{};
    const sched::Validation v = sched::validate(all, sched::Locks{}, sched::Context{}, c, 2);
    CHECK(v.error && !std::strcmp(v.error, "invalid_days") && v.pump == 0 && v.program == 0);
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
          "{\"code\":\"collision_lock\",\"first\":{\"pump\":1,\"program\":2,\"time\":\"08:00\"},"
          "\"second\":{\"pump\":2,\"program\":1,\"time\":\"08:02\"},\"next_day\":false,\"gap_min\":5}");
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
