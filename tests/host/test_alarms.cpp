// Host tests for the status LED alarms.
#include <cstdio>

#include "doser/alarms.h"

using namespace doser;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

static alarms::Output eval_one(const alarms::Pump& p, bool rtc_ok = true, bool clock_valid = true) {
    alarms::Input in;
    in.pumps[2] = p;
    in.rtc_ok = rtc_ok;
    in.clock_valid = clock_valid;
    return alarms::evaluate(in);
}

static void test_alarms() {
    std::printf("Alarms\n");
    alarms::Pump p;
    auto o = eval_one(p);
    CHECK(!o.critical && !o.warning);

    // critical
    p = {};
    p.fault = true;
    CHECK(eval_one(p).critical);
    p = {};
    p.limit_hit_today = true;
    CHECK(eval_one(p).critical);
    o = eval_one(alarms::Pump{}, false);
    CHECK(o.critical && !o.warning);

    // warning: container low by percent or days, only when tracked
    p = {};
    p.tracked = true;
    p.percent = 10.0f;
    o = eval_one(p);
    CHECK(o.warning && !o.critical);
    p.percent = 10.1f;
    CHECK(!eval_one(p).warning);
    p.remaining_days = 2.9f;
    CHECK(eval_one(p).warning);
    p.remaining_days = 3.0f;
    CHECK(!eval_one(p).warning);
    p.remaining_days = -1.0f;  // unknown
    CHECK(!eval_one(p).warning);
    p = {};
    p.percent = 0.0f;  // not tracked: no warning
    p.remaining_days = 0.5f;
    CHECK(!eval_one(p).warning);

    // tube lifetime, no valid time
    p = {};
    p.tube_percent = 99.9f;
    CHECK(!eval_one(p).warning);
    p.tube_percent = 100.0f;
    CHECK(eval_one(p).warning);
    o = eval_one(alarms::Pump{}, true, false);
    CHECK(o.warning && !o.critical);

    // disabled pumps are ignored completely
    p = {};
    p.active = false;
    p.fault = p.limit_hit_today = true;
    p.tracked = true;
    p.percent = 0.0f;
    p.tube_percent = 200.0f;
    o = eval_one(p);
    CHECK(!o.critical && !o.warning);

    // any pump counts
    alarms::Input in;
    in.pumps[4].fault = true;
    in.pumps[0].tube_percent = 150.0f;
    o = alarms::evaluate(in);
    CHECK(o.critical && o.warning);
}

int main() {
    test_alarms();
    std::printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
