// Host test of the automatic RTC aging trim (doser/rtc_trim_core.h)
#include <cmath>
#include <cstdio>

#include "doser/rtc_trim_core.h"

using namespace doser::rtc_trim;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(cond)) {                                                    \
            ++g_fail;                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                 \
    } while (0)

constexpr int64_t T0 = 1794400000;

static void test_first_sync() {
    std::printf("First sync\n");
    State s;
    // brand-new RTC with a wrong time: set it, then anchor in phase
    auto d = decide(s, T0, 123456789, 0);
    CHECK(d.set_rtc && d.anchor && !d.trim);
    apply(&s, d, T0, 3, 0);
    CHECK(s.ever_set && s.anchored && s.anchor_ms == 3 && s.anchor_utc == T0 && s.aging == 0);
    // the first set happens even when the RTC is close
    State s2;
    d = decide(s2, T0, 40, 0);
    CHECK(d.set_rtc && d.anchor);
}

static void test_trim() {
    std::printf("Trim after a day\n");
    State s;
    apply(&s, decide(s, T0, 0, 5), T0, 0, 5);
    // an hour later: keep measuring, nothing written
    auto d = decide(s, T0 + 3600, 1, 5);
    CHECK(!d.set_rtc && !d.anchor && !d.trim);
    // a day later the RTC is 172.8 ms ahead = +2 ppm -> aging +20 (slower)
    d = decide(s, T0 + 86400, 173, 5);
    CHECK(d.trim && d.anchor && !d.set_rtc && d.aging == 25 && std::fabs(d.ppm - 2.0f) < 0.01f);
    apply(&s, d, T0 + 86400, 173, 5);
    CHECK(s.aging == 25 && s.anchor_ms == 173 && s.trim_from == 5 && s.trim_to == 25 && s.trim_utc == T0 + 86400);
    // slow RTC: two days, -86.4 ms = -0.5 ppm -> aging -5
    d = decide(s, T0 + 3 * 86400, 173 - 86, 25);
    CHECK(d.trim && d.aging == 20);
    // large drift: the step is limited
    State b;
    apply(&b, decide(b, T0, 0, 0), T0, 0, 0);
    d = decide(b, T0 + 86400, 1300, 0);  // 15 ppm
    CHECK(d.trim && d.aging == kMaxStep);
    // the register stays in range
    State e;
    apply(&e, decide(e, T0, 0, 120), T0, 0, 120);
    d = decide(e, T0 + 86400, 864, 120);
    CHECK(d.trim && d.aging == 127);
}

static void test_restart_cases() {
    std::printf("Start over\n");
    State s;
    apply(&s, decide(s, T0, 0, 0), T0, 0, 0);
    // aging changed by hand: new anchor, no trim
    auto d = decide(s, T0 + 2 * 86400, 100, 7);
    CHECK(d.anchor && !d.trim);
    // RTC set meanwhile (jump of seconds): new anchor and set
    d = decide(s, T0 + 2 * 86400, 3000, 0);
    CHECK(d.anchor && !d.trim && d.set_rtc);
    // implausible drift (> 20 ppm): new anchor only
    d = decide(s, T0 + 86400, 1900, 0);
    CHECK(d.anchor && !d.trim && d.set_rtc);
    // clock went backwards
    d = decide(s, T0 - 10, 0, 0);
    CHECK(d.anchor && !d.trim);
}

static void test_manual_mode() {
    std::printf("Automatic off\n");
    State s;
    s.automatic = 0;
    s.ever_set = 1;
    auto d = decide(s, T0, 200, 0);
    CHECK(!d.set_rtc && !d.anchor && !d.trim);
    d = decide(s, T0, -600, 0);
    CHECK(d.set_rtc && !d.anchor);
}

int main() {
    test_first_sync();
    test_trim();
    test_restart_cases();
    test_manual_mode();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
