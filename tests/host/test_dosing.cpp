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
#define CHECK_STR(a, b) CHECK((a) && std::strcmp((a), (b)) == 0)

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
    // changing the container while moving is locked
    CHECK(dosing::dose(p, 1000, dosing::Trigger::Api) == nullptr);
    CHECK_STR(dosing::set_container(p, 100000, -1), "pump_busy");
    CHECK(sim::run_until_idle(p));
    CHECK_STR(dosing::set_container(p, 600000, -1), "invalid_param");  // > capacity
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
    CHECK_STR(dosing::calibrate_apply(p, 18500), "no_calibration_run");
    CHECK(dosing::calibrate_start(p) == nullptr);  // default 20 ml
    CHECK(sim::run_until_idle(p));
    auto s = dosing::status(p);
    CHECK(s.calib_pending && s.calib_steps == steps_for(20000) && s.calib_target_ul == 20000);
    // retracted before (test 1) -> compensate first
    CHECK(m.moves.size() == 3 && m.moves[0] == int32_t(kRetract) && m.moves[1] == int32_t(steps_for(20000)));
    CHECK_STR(dosing::calibrate_apply(p, 500), "measured_out_of_range");
    CHECK(dosing::calibrate_apply(p, 18500) == nullptr);
    s = dosing::status(p);
    CHECK(!s.calib_pending && std::fabs(s.settings.ul_per_rev - 342.25f) < 0.1f);
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

static void test_engineering() {
    sim::reset();
    auto e = dosing::engineering();
    CHECK(e.retract_mrev == 250 && e.calib_target_ul == 20000 && e.tier_rpm[0] == 100 && e.tier_rpm[2] == 300);
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
    std::printf("Engineering parameters\n");
    test_engineering();
    std::printf("Lock times between pumps\n");
    test_locks();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
