// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// Host simulation of the dosing engine (components/doser_app/src/dosing.cpp).
// Motor, clock and storage are simulated; the engine runs
// unchanged. Checks the flows retraction/compensation, booking, abort,
// priming dead-man, calibration and error cases.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

static int g_fail = 0;
static int g_checks = 0;
bool g_log_verbose = false;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)
// evaluates `a` once (it may be a call that starts a job)
#define CHECK_STR(a, b)                                                                                     \
    do {                                                                                                    \
        ++g_checks;                                                                                         \
        const char* got_ = (a);                                                                             \
        if (!got_ || std::strcmp(got_, (b)) != 0) {                                                         \
            ++g_fail;                                                                                       \
            std::printf("FAIL %s:%d: %s = %s, expected %s\n", __FILE__, __LINE__, #a, got_ ? got_ : "null", \
                        (b));                                                                               \
        }                                                                                                   \
    } while (0)

#include "sim.h"

// ---------------------------------------------------------------------------
constexpr uint32_t kSpr = 3200;
constexpr uint32_t kRetract = 800;  // ¼ rev

static uint32_t steps_for(uint32_t ul, float upr = dose::kNominalUlPerRev) { return dose::ul_to_steps(ul, upr, kSpr); }

static void test_dose_and_retract() {
    sim::reset();
    auto& m = sim::motor[0];
    // 1. Dose without previous retraction: main move, then retraction
    CHECK(dosing::dose(0, 1000, dosing::Trigger::Manual) == nullptr);
    CHECK(sim::stored[0].ledger.sum_from(0) == 1000);  // booked and saved before the move
    CHECK(sim::run_until_idle(0));
    CHECK(m.moves.size() == 2 && m.moves[0] == int32_t(steps_for(1000)) && m.moves[1] == -int32_t(kRetract));
    auto s = dosing::status(0);
    CHECK_STR(s.last_result, "success");
    CHECK(s.last_done_ul == 1000 && s.state.retracted_mrev == 250 && s.state.total_ul == 1000);
    // history: one dose entry, manual, success
    auto* ev = sim::last_event(history::Kind::Dose, 1);
    CHECK(sim::events.size() == 1 && ev && ev->source == history::Source::Manual);
    CHECK(ev && std::strcmp(history::code_str(ev->code), "success") == 0 && ev->target_ul == 1000 && ev->done_ul == 1000);
    CHECK(sim::stored[0].retracted_mrev == 250);
    // 2. Second dose: compensate retraction first, then pump, then retract
    m.moves.clear();
    CHECK(dosing::dose(0, 500, dosing::Trigger::Manual) == nullptr);
    CHECK(dosing::status(0).phase == dosing::Phase::Unretract);
    CHECK(sim::run_until_idle(0));
    CHECK(m.moves.size() == 3 && m.moves[0] == int32_t(kRetract) && m.moves[1] == int32_t(steps_for(500)) &&
          m.moves[2] == -int32_t(kRetract));
    s = dosing::status(0);
    CHECK(s.used_today_ul == 1500 && s.state.tube_ms > 0);
    // 3. Busy: second job is rejected
    CHECK(dosing::dose(0, 500, dosing::Trigger::Manual) == nullptr);
    CHECK_STR(dosing::dose(0, 500, dosing::Trigger::Manual), "pump_busy");
    CHECK_STR(dosing::prime_start(0), "pump_busy");
    CHECK(sim::run_until_idle(0));
}

static void test_abort_and_container() {
    sim::reset();
    const int p = 1;
    CHECK(dosing::set_container(p, -1, 500000) == nullptr);  // new container 500 ml, full
    CHECK(dosing::status(p).state.remaining_ul == 500000);
    auto* c = sim::last_event(history::Kind::Container, 2);
    CHECK(c && c->value == 500000 && c->target_ul == 500000 && std::strcmp(history::code_str(c->code), "refilled") == 0);
    CHECK(dosing::dose(p, 2000, dosing::Trigger::Api) == nullptr);
    CHECK(dosing::status(p).state.remaining_ul == 498000);  // booked at once
    CHECK(sim::run_until_idle(p));
    CHECK(dosing::status(p).state.remaining_ul == 498000);
    // Abort in the middle of the main move: credit the amount not pumped
    const uint32_t used_before = dosing::status(p).used_today_ul;
    CHECK(dosing::dose(p, 20000, dosing::Trigger::Api) == nullptr);
    for (int i = 0; i < 100; ++i) sim::tick();  // 2 s
    CHECK(dosing::status(p).phase == dosing::Phase::Main);
    dosing::stop(p);
    CHECK(sim::run_until_idle(p));
    auto s = dosing::status(p);
    CHECK_STR(s.last_result, "aborted");
    CHECK(s.last_done_ul > 0 && s.last_done_ul < 20000);
    CHECK(s.used_today_ul == used_before + s.last_done_ul);
    CHECK(s.state.remaining_ul == int32_t(498000 - s.last_done_ul));
    CHECK(s.state.retracted_mrev == 250);  // retraction executed anyway
    CHECK(sim::motor[p].moves.back() == -int32_t(kRetract));
    auto* d = sim::last_event(history::Kind::Dose, 2);
    CHECK(d && d->source == history::Source::Api && std::strcmp(history::code_str(d->code), "aborted") == 0);
    CHECK(d && d->target_ul == 20000 && d->done_ul == s.last_done_ul);
    // changing the container while moving is locked
    CHECK(dosing::dose(p, 1000, dosing::Trigger::Api) == nullptr);
    CHECK_STR(dosing::set_container(p, 100000, -1), "pump_busy");
    CHECK(sim::run_until_idle(p));
    CHECK_STR(dosing::set_container(p, 600000, -1), "invalid_param");  // > capacity
    // a forgotten refill: the container stops at 0 ml, the dose still runs
    CHECK(dosing::set_container(p, 1500, -1) == nullptr);
    CHECK(dosing::dose(p, 2000, dosing::Trigger::Api) == nullptr);
    CHECK(dosing::status(p).state.remaining_ul == 0);
    CHECK(sim::run_until_idle(p));
    CHECK(dosing::status(p).state.remaining_ul == 0);
    // aborted with less pumped than was left: the rest comes back
    CHECK(dosing::set_container(p, 15000, -1) == nullptr);
    CHECK(dosing::dose(p, 20000, dosing::Trigger::Api) == nullptr);
    CHECK(dosing::status(p).state.remaining_ul == 0);
    for (int i = 0; i < 100; ++i) sim::tick();
    dosing::stop(p);
    CHECK(sim::run_until_idle(p));
    s = dosing::status(p);
    CHECK(s.last_done_ul < 15000 && s.state.remaining_ul == int32_t(15000 - s.last_done_ul));
}

static void test_stop_during_unretract() {
    sim::reset();
    const int p = 2;
    CHECK(dosing::dose(p, 1000, dosing::Trigger::Manual) == nullptr);
    CHECK(sim::run_until_idle(p));
    const uint32_t used = dosing::status(p).used_today_ul;
    auto& m = sim::motor[p];
    m.moves.clear();
    CHECK(dosing::dose(p, 1000, dosing::Trigger::Manual) == nullptr);
    sim::tick();  // compensate partially
    dosing::stop(p);
    CHECK(sim::run_until_idle(p));
    auto s = dosing::status(p);
    CHECK_STR(s.last_result, "aborted");
    CHECK(s.used_today_ul == used);                 // nothing pumped, fully credited
    CHECK(s.state.retracted_mrev == 250);           // back in retracted position
    CHECK(m.moves.size() == 2 && m.moves[0] == int32_t(kRetract) && m.moves[1] < 0 && -m.moves[1] < int32_t(kRetract));
}

static void test_limits_and_checks() {
    sim::reset();
    const int p = 3;
    auto set = dosing::settings(p);
    set.daily_limit_ul = 5000;
    CHECK(dosing::set_settings(p, set) == nullptr);
    CHECK(dosing::dose(p, 4000, dosing::Trigger::Manual) == nullptr);
    CHECK(sim::run_until_idle(p));
    CHECK_STR(dosing::dose(p, 1001, dosing::Trigger::Manual), "daily_limit");
    CHECK(dosing::dose(p, 1000, dosing::Trigger::Manual) == nullptr);  // exactly up to the limit
    CHECK(sim::run_until_idle(p));
    CHECK_STR(dosing::dose(p, 20, dosing::Trigger::Manual), "daily_limit");
    // Without a valid time the limit cannot be checked
    sim::time_valid = false;
    CHECK_STR(dosing::dose(p, 20, dosing::Trigger::Manual), "time_invalid");
    sim::time_valid = true;
    set.daily_limit_ul = 0;
    set.mode = dose::Mode::ManualOnly;
    CHECK(dosing::set_settings(p, set) == nullptr);
    CHECK_STR(dosing::dose(p, 100, dosing::Trigger::Schedule), "pump_manual_only");
    CHECK_STR(dosing::dose(p, 10, dosing::Trigger::Manual), "dose_too_small");
    // OTA lock
    sim::locked = true;
    CHECK_STR(dosing::dose(p, 100, dosing::Trigger::Manual), "locked");
    sim::locked = false;
    set.mode = dose::Mode::Disabled;
    CHECK(dosing::set_settings(p, set) == nullptr);
    CHECK_STR(dosing::dose(p, 100, dosing::Trigger::Manual), "pump_disabled");
    set.mode = dose::Mode::Auto;
    set.speed_tier = 5;
    CHECK_STR(dosing::set_settings(p, set), "invalid_param");
}

static void test_priming() {
    sim::reset();
    const int p = 4;
    CHECK(dosing::set_container(p, -1, 500000) == nullptr);
    const auto before = dosing::status(p).state;
    CHECK(dosing::prime_start(p) == nullptr);
    for (int i = 0; i < 50; ++i) {  // 1 s with heartbeat every 100 ms
        sim::tick();
        if (i % 5 == 0) CHECK(dosing::prime_beat(p) == nullptr);
    }
    CHECK(dosing::status(p).activity == dosing::Activity::Priming);
    // heartbeat stops -> stop after 500 ms
    CHECK(sim::run_until_idle(p, 5));
    auto s = dosing::status(p);
    CHECK_STR(s.last_result, "heartbeat_lost");
    CHECK(s.state.ledger.sum_from(0) == before.ledger.sum_from(0));  // does not count toward the limit
    CHECK(s.state.total_ul == before.total_ul);
    CHECK(s.state.tube_ms > before.tube_ms + 1000);                  // but toward the tube runtime
    CHECK(s.last_done_ul > 0 && s.state.remaining_ul == 500000);  // nor toward the container
    CHECK(s.state.retracted_mrev == 250);
    CHECK_STR(dosing::prime_beat(p), "not_priming");
    // held permanently -> maximum duration
    CHECK(dosing::prime_start(p) == nullptr);
    int n = 0;
    while (dosing::status(p).activity != dosing::Activity::Idle && n < 70 * 50) {
        sim::tick();
        dosing::prime_beat(p);
        ++n;
    }
    CHECK_STR(dosing::status(p).last_result, "max_duration");
    CHECK(n >= 60 * 50 && n < 62 * 50);
    // release -> regular stop
    CHECK(dosing::prime_start(p) == nullptr);
    sim::tick();
    dosing::stop(p);
    CHECK(sim::run_until_idle(p, 5));
    CHECK_STR(dosing::status(p).last_result, "success");
}

static void test_calibration() {
    sim::reset();
    const int p = 0;
    auto& m = sim::motor[p];
    m.moves.clear();
    CHECK_STR(dosing::calibrate_apply(p, 9250), "no_calibration_run");
    CHECK(dosing::calibrate_start(p) == nullptr);  // default 10 ml
    CHECK(sim::run_until_idle(p));
    auto s = dosing::status(p);
    CHECK(s.calib_pending && s.calib_steps == steps_for(10000) && s.calib_target_ul == 10000);
    // retracted before (test 1) -> compensate first
    CHECK(m.moves.size() == 3 && m.moves[0] == int32_t(kRetract) && m.moves[1] == int32_t(steps_for(10000)));
    CHECK_STR(dosing::calibrate_apply(p, 500), "measured_out_of_range");
    CHECK(dosing::calibrate_apply(p, 9250) == nullptr);
    s = dosing::status(p);
    CHECK(!s.calib_pending && std::fabs(s.settings.ul_per_rev - 342.25f) < 0.1f);
    auto* run = sim::last_event(history::Kind::CalibRun, 1);
    auto* cal = sim::last_event(history::Kind::Calibration, 1);
    CHECK(run && run->target_ul == 10000 && std::strcmp(history::code_str(run->code), "success") == 0);
    CHECK(cal && cal->target_ul == 10000 && cal->done_ul == 9250 && cal->value >= 342150 && cal->value <= 342350);
    // next dose uses the new value
    m.moves.clear();
    CHECK(dosing::dose(p, 1000, dosing::Trigger::Manual) == nullptr);
    CHECK(sim::run_until_idle(p));
    CHECK(m.moves[1] == int32_t(steps_for(1000, s.settings.ul_per_rev)));
    // aborted calibration -> nothing to apply
    CHECK(dosing::calibrate_start(p, 5000) == nullptr);
    for (int i = 0; i < 20; ++i) sim::tick();
    dosing::stop(p);
    CHECK(sim::run_until_idle(p));
    CHECK(!dosing::status(p).calib_pending);
    CHECK_STR(dosing::status(p).last_result, "aborted");
    run = sim::last_event(history::Kind::CalibRun, 1);
    CHECK(run && std::strcmp(history::code_str(run->code), "aborted") == 0 && run->done_ul < 5000);
}

static void test_fault() {
    sim::reset();
    const int p = 1;
    const auto before = dosing::status(p);
    CHECK(dosing::dose(p, 10000, dosing::Trigger::Manual) == nullptr);
    for (int i = 0; i < 80; ++i) sim::tick();
    // driver reports short circuit: motion stops immediately
    auto& m = sim::motor[p];
    m.fault = "short_circuit";
    m.running = false;
    CHECK(sim::run_until_idle(p, 1));
    auto s = dosing::status(p);
    CHECK_STR(s.last_result, "fault");
    CHECK(s.state.retracted_mrev == 0);  // no retraction after a fault
    CHECK(s.used_today_ul == before.used_today_ul + s.last_done_ul);
    CHECK(s.last_done_ul > 0 && s.last_done_ul < 10000);
    // OTA lock during the move
    CHECK(dosing::dose(p, 10000, dosing::Trigger::Manual) == nullptr);
    for (int i = 0; i < 20; ++i) sim::tick();
    sim::locked = true;
    sim::motor[p].running = false;  // motion::lock stops immediately
    CHECK(sim::run_until_idle(p, 1));
    CHECK_STR(dosing::status(p).last_result, "locked");
    CHECK(!dosing::busy());
}

// Pump on, without a daily limit
static void enable(int p) {
    auto s = dosing::settings(p);
    s.mode = dose::Mode::Auto;
    s.daily_limit_ul = 0;
    CHECK(dosing::set_settings(p, s) == nullptr);
}

// A move that does not start (driver missing, speed refused, ...): the
// booking is undone, nothing runs and nothing is recorded
static void test_start_errors() {
    sim::reset();
    const int p = 3;
    enable(p);
    sim::events.clear();
    auto& m = sim::motor[p];
    const auto before = dosing::status(p);
    const struct {
        esp_err_t err;
        const char* code;
    } cases[] = {{ESP_ERR_NOT_FOUND, "driver_unavailable"},
                 {ESP_ERR_INVALID_ARG, "speed_out_of_range"},
                 {ESP_ERR_INVALID_STATE, "pump_busy"},
                 {ESP_FAIL, "internal_error"}};
    for (const auto& c : cases) {
        m.fail_move = static_cast<int>(m.moves.size());
        m.fail_err = c.err;
        CHECK_STR(dosing::dose(p, 1000, dosing::Trigger::Manual), c.code);
        const auto s = dosing::status(p);
        CHECK(s.activity == dosing::Activity::Idle);
        CHECK(s.used_today_ul == before.used_today_ul && s.state.total_ul == before.state.total_ul);
        CHECK(sim::stored[p].total_ul == before.state.total_ul);  // the store is set back as well
    }
    m.fail_move = static_cast<int>(m.moves.size());
    m.fail_err = ESP_ERR_NOT_FOUND;
    CHECK_STR(dosing::prime_start(p), "driver_unavailable");
    m.fail_move = static_cast<int>(m.moves.size());
    m.fail_err = ESP_FAIL;
    CHECK_STR(dosing::calibrate_start(p), "internal_error");
    CHECK(!dosing::busy() && sim::events.empty());
}

// Faults in the moves around the main move: compensation (unretract) and
// retraction, and a following move that does not start
static void test_fault_phases() {
    sim::reset();
    const int p = 3;
    auto& m = sim::motor[p];
    auto fault = [&](const char* why) {
        m.fault = why;
        m.running = false;
    };
    auto tick_to = [&](dosing::Phase ph) {
        for (int i = 0; i < 500 && dosing::status(p).phase != ph; ++i) sim::tick();
        CHECK(dosing::status(p).phase == ph);
    };
    CHECK(dosing::dose(p, 1000, dosing::Trigger::Manual) == nullptr);
    CHECK(sim::run_until_idle(p));
    CHECK(dosing::status(p).state.retracted_mrev == 250);

    // fault while compensating: nothing pumped, all credited, the position is unknown
    auto before = dosing::status(p);
    CHECK(dosing::dose(p, 1000, dosing::Trigger::Manual) == nullptr);
    CHECK(dosing::status(p).phase == dosing::Phase::Unretract);
    sim::tick();
    fault("short_circuit");
    CHECK(sim::run_until_idle(p, 1));
    auto s = dosing::status(p);
    CHECK_STR(s.last_result, "fault");
    CHECK(s.last_done_ul == 0 && s.used_today_ul == before.used_today_ul && s.state.total_ul == before.state.total_ul);
    CHECK(s.state.retracted_mrev == 0);
    auto* ev = sim::last_event(history::Kind::Dose, p + 1);
    CHECK(ev && std::strcmp(history::code_str(ev->code), "fault") == 0 && ev->done_ul == 0);

    // the main move does not start after the compensation
    CHECK(dosing::dose(p, 1000, dosing::Trigger::Manual) == nullptr);
    CHECK(sim::run_until_idle(p));
    before = dosing::status(p);
    CHECK(before.state.retracted_mrev == 250);
    m.fail_move = static_cast<int>(m.moves.size()) + 1;
    CHECK(dosing::dose(p, 1000, dosing::Trigger::Manual) == nullptr);
    CHECK(sim::run_until_idle(p));
    s = dosing::status(p);
    CHECK_STR(s.last_result, "fault");
    CHECK(s.used_today_ul == before.used_today_ul && s.state.retracted_mrev == 0);

    // fault while retracting: the main move counts, the retraction only partly
    before = s;
    CHECK(dosing::dose(p, 1000, dosing::Trigger::Manual) == nullptr);
    tick_to(dosing::Phase::Retract);
    sim::tick();
    CHECK(m.running && m.position > 0 && m.position < kRetract);
    const uint32_t moved = m.position;
    fault("overtemperature");
    CHECK(sim::run_until_idle(p, 1));
    s = dosing::status(p);
    CHECK_STR(s.last_result, "fault");
    CHECK(s.last_done_ul == 1000 && s.used_today_ul == before.used_today_ul + 1000);
    CHECK(s.state.retracted_mrev == 250 * moved / kRetract);
    CHECK(sim::stored[p].retracted_mrev == s.state.retracted_mrev);

    // the retraction does not start: the dose is done, no retraction to compensate
    before = s;
    m.fail_move = static_cast<int>(m.moves.size()) + (before.state.retracted_mrev ? 2 : 1);
    CHECK(dosing::dose(p, 1000, dosing::Trigger::Manual) == nullptr);
    CHECK(sim::run_until_idle(p));
    s = dosing::status(p);
    CHECK_STR(s.last_result, "success");
    CHECK(s.last_done_ul == 1000 && s.state.retracted_mrev == 0);

    // stopped while compensating and the way back does not start: credited, position unknown
    CHECK(dosing::dose(p, 1000, dosing::Trigger::Manual) == nullptr);
    CHECK(sim::run_until_idle(p));
    before = dosing::status(p);
    CHECK(dosing::dose(p, 1000, dosing::Trigger::Manual) == nullptr);
    dosing::stop(p);
    m.fail_move = static_cast<int>(m.moves.size());
    CHECK(sim::run_until_idle(p));
    s = dosing::status(p);
    CHECK_STR(s.last_result, "aborted");
    CHECK(s.used_today_ul == before.used_today_ul && s.state.retracted_mrev == 0);
}

// The rest of the interface: stop priming, cancel and storage error of a
// calibration, tube hours, service state, names
static void test_interface() {
    sim::reset();
    const int p = 4;
    enable(p);
    CHECK_STR(dosing::prime_stop(7), "invalid_param");
    CHECK_STR(dosing::prime_stop(p), "not_priming");
    CHECK(dosing::prime_start(p) == nullptr);
    CHECK(dosing::prime_start(p) == nullptr);  // again = heartbeat
    CHECK(dosing::service() && dosing::busy());
    sim::tick();
    CHECK(dosing::prime_stop(p) == nullptr);
    CHECK(sim::run_until_idle(p, 2));
    CHECK_STR(dosing::status(p).last_result, "success");
    CHECK(!dosing::service());
    // stop_all ends every job
    CHECK(dosing::prime_start(p) == nullptr);
    CHECK(dosing::dose(0, 20000, dosing::Trigger::Manual) == nullptr);
    sim::tick();
    dosing::stop_all();
    CHECK(sim::run_until_idle(p, 2) && sim::run_until_idle(0, 2));
    CHECK(!dosing::busy());
    // a cancelled calibration run cannot be applied
    CHECK(dosing::calibrate_start(p, 5000) == nullptr);
    CHECK(sim::run_until_idle(p));
    CHECK(dosing::status(p).calib_pending);
    dosing::calibrate_cancel(p);
    dosing::calibrate_cancel(9);
    CHECK_STR(dosing::calibrate_apply(p, 5000), "no_calibration_run");
    // the new value cannot be saved
    CHECK(dosing::calibrate_start(p, 5000) == nullptr);
    CHECK(sim::run_until_idle(p));
    sim::settings_err = ESP_FAIL;
    CHECK_STR(dosing::calibrate_apply(p, 5000), "storage_error");
    sim::settings_err = ESP_OK;
    // tube hours: 0 = new tube
    CHECK_STR(dosing::set_tube_hours(p, 100001), "invalid_param");
    CHECK_STR(dosing::set_tube_hours(9, 1), "invalid_param");
    CHECK(dosing::set_tube_hours(p, 12) == nullptr);
    CHECK(sim::stored[p].tube_ms == 12 * 3600000ULL && !(sim::stored[p].flags & dose::kTubeChanged));
    CHECK(dosing::set_tube_hours(p, 0) == nullptr);
    CHECK(sim::stored[p].tube_ms == 0 && (sim::stored[p].flags & dose::kTubeChanged));
    const auto* tube = sim::last_event(history::Kind::Tube, p + 1);
    CHECK(tube && tube->value == 0);
    // names in the API and in the log
    CHECK_STR(dosing::activity_str(dosing::Activity::Calibrating), "calibrating");
    CHECK_STR(dosing::activity_str(dosing::Activity::Idle), "idle");
    CHECK_STR(dosing::phase_str(dosing::Phase::Unretract), "unretract");
    CHECK_STR(dosing::phase_str(dosing::Phase::None), "none");
    CHECK_STR(dosing::trigger_str(dosing::Trigger::Mqtt), "mqtt");
    CHECK_STR(dosing::trigger_str(dosing::Trigger::Manual), "manual");
}

// Start guard of the device (24 V supply)
static const char* g_guard = nullptr;

static void test_start_guard() {
    sim::reset();
    const int p = 0;
    dosing::set_start_guard([]() -> const char* { return g_guard; });
    g_guard = "supply_low";
    CHECK_STR(dosing::dose(p, 10000, dosing::Trigger::Manual), "supply_low");
    CHECK_STR(dosing::prime_start(p), "supply_low");
    CHECK(!dosing::busy());
    // a running dose goes on when the supply sags, starts are blocked
    g_guard = nullptr;
    CHECK(dosing::dose(p, 10000, dosing::Trigger::Manual) == nullptr);
    for (int i = 0; i < 20; ++i) sim::tick();
    g_guard = "supply_low";
    CHECK(sim::run_until_idle(p));
    CHECK_STR(dosing::status(p).last_result, "success");
    // 24 V lost during the move: the supply lock stops it, the result names the reason
    g_guard = nullptr;
    CHECK(dosing::dose(p, 10000, dosing::Trigger::Manual) == nullptr);
    for (int i = 0; i < 20; ++i) sim::tick();
    g_guard = "supply_missing";
    sim::locked = true;
    sim::motor[p].running = false;
    CHECK(sim::run_until_idle(p, 1));
    CHECK_STR(dosing::status(p).last_result, "supply_missing");
    CHECK_STR(dosing::dose(p, 10000, dosing::Trigger::Manual), "supply_missing");
    g_guard = nullptr;
    dosing::set_start_guard(nullptr);
    sim::locked = false;
}

static void test_engineering() {
    sim::reset();
    auto e = dosing::engineering();
    CHECK(e.retract_mrev == 250 && e.calib_target_ul == 10000 && e.tier_rpm[0] == 100 && e.tier_rpm[2] == 300);
    e.retract_mrev = 500;
    e.tier_rpm[1] = 250;
    CHECK(dosing::set_engineering(e) == nullptr);
    // pump 3 with its own retraction, pump 4 global
    auto s = dosing::settings(2);
    s.retract_mrev = 100;
    CHECK(dosing::set_settings(2, s) == nullptr);
    auto s4 = dosing::settings(3);
    s4.mode = dose::Mode::Auto;  // still disabled from the limit test
    CHECK(dosing::set_settings(3, s4) == nullptr);
    sim::motor[2].moves.clear();
    sim::motor[3].moves.clear();
    CHECK(dosing::dose(2, 1000, dosing::Trigger::Manual) == nullptr);
    CHECK(dosing::dose(3, 1000, dosing::Trigger::Manual) == nullptr);  // parallel
    CHECK(sim::run_until_idle(2) && sim::run_until_idle(3));
    CHECK(sim::motor[2].moves.back() == -320);   // 0.1 rev
    CHECK(sim::motor[3].moves.back() == -1600);  // 0.5 rev global
    CHECK(sim::motor[3].sps == 250u * kSpr / 60);  // tier "normal" = 250 rpm
    e.tier_rpm[2] = 700;
    CHECK_STR(dosing::set_engineering(e), "invalid_param");
    // container thresholds: defaults, own values, alarm not above warning
    e = dosing::engineering();
    CHECK(e.warn_days == 7 && e.low_days == 4);
    CHECK(dose::warn_days(s, e) == 7 && dose::low_days(s, e) == 4);
    s.warn_days = 14;
    s.low_days = 5;
    CHECK(dosing::set_settings(2, s) == nullptr);
    CHECK(dose::warn_days(dosing::settings(2), e) == 14 && dose::low_days(dosing::settings(2), e) == 5);
    s.warn_days = 0;  // default 7 below own alarm 10
    s.low_days = 10;
    CHECK_STR(dosing::set_settings(2, s), "invalid_param");
    s.warn_days = 61;
    s.low_days = 0;
    CHECK_STR(dosing::set_settings(2, s), "invalid_param");
    auto bad = e;
    bad.low_days = 8;
    CHECK_STR(dose::validate(bad, 600), "low_days");
    bad = e;
    bad.warn_days = 61;
    CHECK_STR(dose::validate(bad, 600), "warn_days");
    bad = e;
    bad.warn_days = bad.low_days = 0;  // both off
    CHECK(dose::validate(bad, 600) == nullptr);
    s.warn_days = 0;
    s.low_days = 0;

    // calibration reminder: default 3 months, 0 = off, at most 24
    dose::PumpSettings c;
    CHECK(c.recal_months == 3);
    c.ul_per_rev = 340.0f;
    c.calibrated_at = 1790000000;
    const int64_t month = 2629746;
    CHECK(!dose::calibration_due(c, c.calibrated_at + 3 * month - 1));
    CHECK(dose::calibration_due(c, c.calibrated_at + 3 * month));
    CHECK(!dose::calibration_due(c, 0));                     // no valid time
    CHECK(!dose::calibration_due(c, c.calibrated_at - 10));  // clock behind
    c.recal_months = 12;
    CHECK(!dose::calibration_due(c, c.calibrated_at + 11 * month));
    CHECK(dose::calibration_due(c, c.calibrated_at + 12 * month));
    c.recal_months = 0;
    CHECK(!dose::calibration_due(c, c.calibrated_at + 100 * month));
    c.recal_months = 3;
    c.calibrated_at = 0;  // never calibrated: no reminder
    CHECK(!dose::calibration_due(c, 1800000000));
    s.recal_months = 25;
    CHECK_STR(dosing::set_settings(2, s), "invalid_param");
    s.recal_months = 24;
    CHECK(dosing::set_settings(2, s) == nullptr && dosing::settings(2).recal_months == 24);
    s.recal_months = 3;
    CHECK(dosing::set_settings(2, s) == nullptr);
}

// Retraction switched off (factory default): a pending retraction is
// compensated once, afterwards no retraction at all
static void test_retract_off() {
    sim::reset();
    CHECK(dose::PumpSettings{}.retract_mrev == 0);
    auto s = dosing::settings(3);
    CHECK(s.retract_mrev == -1 && dosing::status(3).state.retracted_mrev == 500);  // from test_engineering
    s.retract_mrev = 0;
    CHECK(dosing::set_settings(3, s) == nullptr);
    auto& m = sim::motor[3];
    CHECK(dosing::dose(3, 1000, dosing::Trigger::Manual) == nullptr);
    CHECK(sim::run_until_idle(3));
    CHECK(m.moves.size() == 2 && m.moves[0] == 1600 && m.moves[1] > 0);  // compensate, main move
    CHECK(dosing::status(3).state.retracted_mrev == 0);
    m.moves.clear();
    CHECK(dosing::dose(3, 1000, dosing::Trigger::Manual) == nullptr);
    CHECK(sim::run_until_idle(3));
    CHECK(m.moves.size() == 1 && m.moves[0] > 0);  // main move only
    CHECK(dosing::status(3).state.retracted_mrev == 0);
    s.retract_mrev = -1;  // back to global for the following tests
    CHECK(dosing::set_settings(3, s) == nullptr);
}

static void test_locks() {
    sim::reset();
    for (int p = 0; p < 3; ++p) {
        auto s = dosing::settings(p);
        s.mode = dose::Mode::Auto;
        s.daily_limit_ul = 0;
        CHECK(dosing::set_settings(p, s) == nullptr);
    }
    sched::Locks l;
    l.count = 1;
    l.rules[0] = sched::LockRule{0, 1, 5};  // pump 1 <-> pump 2: 5 min
    dosing::set_locks(l);
    int64_t retry = 0;
    // earlier tests ended jobs a few seconds ago: the lock applies right away
    CHECK_STR(dosing::dose(0, 1000, dosing::Trigger::Manual, -1, &retry), "collision_lock");
    CHECK(retry == dosing::status(1).state.last_end + 300);
    sim::now_us += 600 * 1000000LL;
    CHECK(dosing::dose(0, 1000, dosing::Trigger::Manual) == nullptr);
    CHECK_STR(dosing::dose(1, 1000, dosing::Trigger::Manual, -1, &retry), "collision_lock");
    CHECK(retry == INT64_MAX);  // pump 1 still running, end unknown
    CHECK(sim::run_until_idle(0));
    const int64_t end = dosing::status(0).state.last_end;
    CHECK(end > 0 && sim::stored[0].last_end == end);  // saved with the end of the job
    CHECK_STR(dosing::dose(1, 1000, dosing::Trigger::Schedule, -1, &retry), "collision_lock");
    CHECK(retry == end + 300);
    // no rule for pump 3
    CHECK(dosing::dose(2, 1000, dosing::Trigger::Manual) == nullptr);
    CHECK(sim::run_until_idle(2));
    // priming is not affected
    CHECK(dosing::prime_start(1) == nullptr);
    dosing::stop(1);
    CHECK(sim::run_until_idle(1, 5));
    sim::now_us += 300 * 1000000LL;  // lock time over
    CHECK(dosing::dose(1, 1000, dosing::Trigger::Manual) == nullptr);
    CHECK(sim::run_until_idle(1));
    // symmetric: now pump 1 has to wait for pump 2
    CHECK_STR(dosing::dose(0, 1000, dosing::Trigger::Api, -1, &retry), "collision_lock");
    CHECK(retry == dosing::status(1).state.last_end + 300);
    // without a valid clock only running pumps count
    sim::time_valid = false;
    CHECK(dosing::dose(0, 1000, dosing::Trigger::Manual) == nullptr);
    CHECK_STR(dosing::dose(1, 1000, dosing::Trigger::Manual, -1, &retry), "collision_lock");
    CHECK(sim::run_until_idle(0));
    sim::time_valid = true;
    dosing::set_locks(sched::Locks{});
    CHECK(dosing::dose(1, 1000, dosing::Trigger::Manual) == nullptr);
    CHECK(sim::run_until_idle(1));
    // run-time parameters for the schedule check
    const auto rp = dosing::run_params(2);
    CHECK(rp.rpm == 250 && rp.retract_mrev == 100 && rp.accel_rpm_s == 600);
    CHECK(rp.ul_per_rev > 0);
}

int main(int argc, char**) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    g_log_verbose = argc > 1;
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    CHECK(dosing::init() == ESP_OK && sim::task);
    std::printf("Dosing with retraction\n");
    test_dose_and_retract();
    std::printf("Abort and container\n");
    test_abort_and_container();
    test_stop_during_unretract();
    std::printf("Daily limit and checks\n");
    test_limits_and_checks();
    std::printf("Priming (dead-man)\n");
    test_priming();
    std::printf("Calibration\n");
    test_calibration();
    std::printf("Driver fault and OTA lock\n");
    test_fault();
    std::printf("Moves that do not start\n");
    test_start_errors();
    std::printf("Faults while compensating and retracting\n");
    test_fault_phases();
    std::printf("Priming stop, calibration cancel, tube hours\n");
    test_interface();
    std::printf("Start guard (24 V supply)\n");
    test_start_guard();
    std::printf("Engineering parameters\n");
    test_engineering();
    std::printf("Retraction off\n");
    test_retract_off();
    std::printf("Lock times between pumps\n");
    test_locks();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
