// Host tests for the JSON/CSV form of history events and the daily sums.
#include <cstdio>
#include <cstring>
#include <string>

#include "doser/history_json.h"
#include "doser/schedule_json.h"
#include "lgal/json_reader.h"

using namespace doser;
using namespace doser::history;
namespace hj = doser::history_json;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

static lgal::JsonToken g_tok[256];

static const int32_t kDay = sched_json::days_from_civil(2026, 11, 7);
static int64_t local_at(int32_t day, int h, int m, int s = 0) { return int64_t(day) * 86400 + h * 3600 + m * 60 + s; }

static Event ev(Kind k, uint8_t pump, uint32_t target, uint32_t done, int32_t value = 0, const char* code = "success") {
    Event e;
    e.utc = 1794000000u;
    e.kind = k;
    e.pump = pump;
    e.source = Source::Schedule;
    e.code = code_of(code);
    e.target_ul = target;
    e.done_ul = done;
    e.value = value;
    return e;
}

static void test_event_json() {
    std::printf("Event JSON\n");
    char loc[20];
    hj::fmt_local_s(local_at(kDay, 8, 5, 9), loc);
    CHECK(std::strcmp(loc, "2026-11-07 08:05:09") == 0);

    lgal::Json j;
    j.arr();
    hj::write_event(j, ev(Kind::Dose, 2, 1500, 1250, 0, "aborted"), local_at(kDay, 8, 0));
    hj::write_event(j, ev(Kind::Container, 3, 500000, 0, 480500, "refilled"), local_at(kDay, 9, 0));
    Event c = ev(Kind::Calibration, 1, 20000, 18500, 342250, "set");
    c.source = Source::None;
    hj::write_event(j, c, local_at(kDay, 10, 0));
    hj::write_event(j, ev(Kind::Skip, 4, 0, 0, 3, "missed"), local_at(kDay, 11, 0));
    hj::write_event(j, ev(Kind::Alarm, 0, 0, 0, 0, "rtc_fault"), local_at(kDay, 12, 0));
    j.end();
    lgal::JsonReader r;
    CHECK(r.parse(j.c_str(), j.str().size(), g_tok, 256));
    auto a = r.root();
    int64_t v = 0;
    CHECK(a[0]["kind"].equals("dose") && a[0]["code"].equals("aborted") && a[0]["source"].equals("schedule"));
    CHECK(a[0]["pump"].get(&v) && v == 2);
    CHECK(a[0]["target_ml"].get_decimal(1000, &v) && v == 1500 && a[0]["done_ml"].get_decimal(1000, &v) && v == 1250);
    CHECK(a[0]["local"].equals("2026-11-07 08:00:00") && a[0]["utc"].get(&v) && v == 1794000000);
    CHECK(a[1]["remaining_ml"].get_decimal(1000, &v) && v == 480500 && a[1]["capacity_ml"].get_decimal(1000, &v) && v == 500000);
    CHECK(a[1]["code"].equals("refilled"));
    CHECK(!a[2]["source"].valid() && a[2]["ul_per_rev"].get_decimal(100, &v) && v == 34225);
    CHECK(a[2]["measured_ml"].get_decimal(1000, &v) && v == 18500);
    CHECK(a[3]["count"].get(&v) && v == 3 && !a[3]["target_ml"].valid());
    CHECK(a[4]["code"].equals("rtc_fault") && a[4]["pump"].get(&v) && v == 0);
}

static void test_kinds_and_csv() {
    std::printf("Kinds and CSV\n");
    uint16_t m = 0;
    CHECK(hj::parse_kinds("dose,skip", &m) && m == ((1u << 1) | (1u << 4)));
    CHECK(hj::parse_kinds("", &m) && m == 0);
    CHECK(!hj::parse_kinds("dose,,skip", &m));
    CHECK(!hj::parse_kinds("dose,foo", &m));
    CHECK(!hj::parse_kinds("dose,", &m));
    CHECK(!hj::parse_kinds("averyveryverylongkindname", &m));

    CHECK(std::string(hj::csv_header()) == "utc,local,kind,pump,source,code,target_ml,done_ml,value\r\n");
    char line[160];
    size_t n = hj::csv_line(ev(Kind::Dose, 1, 1000, 1000), local_at(kDay, 8, 0), line, sizeof(line));
    CHECK(n > 0 && std::string(line) == "1794000000,2026-11-07 08:00:00,dose,1,schedule,success,1.000,1.000,\r\n");
    n = hj::csv_line(ev(Kind::Container, 5, 250000, 0, 125500, "set"), local_at(kDay, 8, 0), line, sizeof(line));
    CHECK(n > 0 && std::string(line) == "1794000000,2026-11-07 08:00:00,container,5,schedule,set,250.000,0.000,125.500\r\n");
    Event t = ev(Kind::Tube, 2, 0, 0, 1200, "set");
    t.source = Source::None;
    n = hj::csv_line(t, local_at(kDay, 8, 0), line, sizeof(line));
    CHECK(n > 0 && std::string(line) == "1794000000,2026-11-07 08:00:00,tube,2,,set,0.000,0.000,1200\r\n");
    CHECK(hj::csv_line(t, local_at(kDay, 8, 0), line, 20) == 0);
}

static void test_daily() {
    std::printf("Daily sums\n");
    static hj::Daily d;
    d.reset(kDay, 7);
    CHECK(d.days == 7 && d.first_day == kDay - 6);
    d.add(ev(Kind::Dose, 1, 1000, 1000), local_at(kDay, 8, 0));
    d.add(ev(Kind::Dose, 1, 1000, 800, 0, "aborted"), local_at(kDay, 23, 59, 59));
    d.add(ev(Kind::Dose, 3, 2000, 2000), local_at(kDay - 6, 0, 0));
    d.add(ev(Kind::Dose, 3, 2000, 2000), local_at(kDay - 7, 23, 59));  // before the range
    d.add(ev(Kind::Dose, 3, 2000, 2000), local_at(kDay + 1, 0, 0));    // after
    d.add(ev(Kind::Prime, 1, 0, 5000), local_at(kDay, 9, 0));          // not dosed into the tank
    d.add(ev(Kind::Skip, 2, 500, 0, 1, "daily_limit"), local_at(kDay, 9, 0));
    d.add(ev(Kind::Skip, 2, 0, 0, 4, "missed"), local_at(kDay, 10, 0));
    d.add(ev(Kind::Dose, 0, 1000, 1000), local_at(kDay, 9, 0));        // no pump
    CHECK(d.dosed_ul[6][0] == 1800 && d.doses[6][0] == 2);
    CHECK(d.dosed_ul[0][2] == 2000 && d.doses[0][2] == 1);
    CHECK(d.skipped[6][1] == 5);
    lgal::Json j;
    j.obj();
    d.write(j);
    j.end();
    lgal::JsonReader r;
    CHECK(r.parse(j.c_str(), j.str().size(), g_tok, 256));
    auto days = r.root()["days"];
    int64_t v = 0;
    CHECK(days[0]["date"].equals("2026-11-01") && days[6]["date"].equals("2026-11-07") && !days[7].valid());
    CHECK(days[6]["ml"][0].get_decimal(1000, &v) && v == 1800);
    CHECK(days[6]["doses"][0].get(&v) && v == 2 && days[6]["skipped"][1].get(&v) && v == 5);
    CHECK(days[0]["ml"][2].get_decimal(1000, &v) && v == 2000 && days[0]["ml"][4].get_decimal(1000, &v) && v == 0);
    d.reset(kDay, 500);
    CHECK(d.days == hj::kMaxDays && d.dosed_ul[6][0] == 0);
}

int main() {
    test_event_json();
    test_kinds_and_csv();
    test_daily();
    std::printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
