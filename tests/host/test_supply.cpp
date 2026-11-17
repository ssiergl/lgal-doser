// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// Host tests for the 24 V supply states (doser/supply_core.h).
#include <cstdio>
#include <cstring>

#include "doser/supply_core.h"

using namespace doser::supply;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

static void test_classify() {
    std::printf("Supply: thresholds\n");
    // boot measurement: plain thresholds
    CHECK(classify(0) == State::Missing);
    CHECK(classify(3300) == State::Missing);  // rail back-fed through the I/O pins
    CHECK(classify(11999) == State::Missing);
    CHECK(classify(12000) == State::Low);
    CHECK(classify(19999) == State::Low);
    CHECK(classify(20000) == State::Ok);
    CHECK(classify(24000) == State::Ok);
    CHECK(classify(34000) == State::Ok);
    // hysteresis: leaving a state needs 0.5 V beyond the threshold
    CHECK(classify(12400, State::Missing) == State::Missing);
    CHECK(classify(12500, State::Missing) == State::Low);
    CHECK(classify(20400, State::Missing) == State::Low);
    CHECK(classify(20500, State::Missing) == State::Ok);
    CHECK(classify(11600, State::Low) == State::Low);
    CHECK(classify(11499, State::Low) == State::Missing);
    CHECK(classify(20400, State::Low) == State::Low);
    CHECK(classify(20500, State::Low) == State::Ok);
    CHECK(classify(19600, State::Ok) == State::Ok);
    CHECK(classify(19499, State::Ok) == State::Low);
    CHECK(classify(11499, State::Ok) == State::Missing);
    CHECK(std::strcmp(state_name(State::Missing), "missing") == 0);
    CHECK(std::strcmp(state_name(State::Low), "low") == 0);
    CHECK(std::strcmp(state_name(State::Ok), "ok") == 0);
    CHECK(std::strcmp(state_name(State::None), "none") == 0);
}

static void test_tracker() {
    std::printf("Supply: debounce and return\n");
    // no sense input: never changes
    Tracker none;
    CHECK(!none.update(0) && none.state() == State::None && !none.returned());

    // 24 V lost while running: kConfirm readings in a row
    Tracker t(State::Ok);
    CHECK(!t.update(0) && !t.update(0) && t.state() == State::Ok);
    CHECK(t.update(0) && t.state() == State::Missing && !t.returned());
    // one good reading in between starts the count again
    Tracker u(State::Ok);
    CHECK(!u.update(0) && !u.update(0) && !u.update(24000) && !u.update(0) && !u.update(0));
    CHECK(u.update(0) && u.state() == State::Missing);
    // back: Missing -> Ok, returned stays set
    CHECK(!t.update(24000) && !t.update(24000));
    CHECK(t.update(24000) && t.state() == State::Ok && t.returned());
    CHECK(!t.update(24000) && t.returned());

    // missing at boot, then undervoltage: returned too (restart with peripherals)
    Tracker b(State::Missing);
    for (int i = 0; i < kConfirm - 1; ++i) CHECK(!b.update(15000));
    CHECK(b.update(15000) && b.state() == State::Low && b.returned());

    // noise around a threshold does not toggle
    Tracker n(State::Ok);
    for (int i = 0; i < 10; ++i) CHECK(!n.update(i % 2 ? 19600 : 20400));
    CHECK(n.state() == State::Ok);
    // a sagging supply: Ok -> Low, not returned
    for (int i = 0; i < kConfirm - 1; ++i) CHECK(!n.update(18000));
    CHECK(n.update(18000) && n.state() == State::Low && !n.returned());
    // Low -> Missing needs the same count, a change of the pending state restarts it
    CHECK(!n.update(0) && !n.update(24000) && !n.update(0) && !n.update(0) && n.state() == State::Low);
    CHECK(n.update(0) && n.state() == State::Missing);
}

int main() {
    test_classify();
    test_tracker();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
