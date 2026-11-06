// Host simulation of the dosing engine (components/doser_app/src/dosing.cpp).
// Motor, clock and storage are simulated; the engine runs
// unchanged. Checks the flows retraction/compensation, booking, abort,
// priming dead-man, calibration and error cases.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

#include "doser/dosing.h"
#include "doser/dosing_store.h"
#include "doser/motion.h"
#include "freertos/task.h"
#include "lgal/timesync.h"

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

using namespace doser;

// ---------------------------------------------------------------------------
// Simulation
namespace sim {
int64_t now_us = 1000000;
bool time_valid = true;
TaskFunction_t task = nullptr;

struct Motor {
    bool running = false;
    uint32_t position = 0, target = 0, sps = 0;
    double frac = 0;
    const char* fault = nullptr;
    std::vector<int32_t> moves;  // started moves (± steps)
};
Motor motor[5];
bool locked = false;
int saves[5];
dose::PumpState stored[5];

void advance(int64_t dt_us) {
    now_us += dt_us;
    for (auto& m : motor) {
        if (!m.running) continue;
        m.frac += m.sps * dt_us / 1e6;
        const uint32_t add = static_cast<uint32_t>(m.frac);
        m.frac -= add;
        m.position = std::min(m.target, m.position + add);
        if (m.position >= m.target) m.running = false;
    }
}

// One tick of the engine (20 ms)
void tick() {
    advance(20000);
    try {
        task(nullptr);
    } catch (const TaskYield&) {
    }
}

// Ticks until pump i is done (max. seconds)
bool run_until_idle(int i, double max_s = 600) {
    for (int64_t n = 0; n < max_s * 50; ++n) {
        tick();
        if (dosing::status(i).activity == dosing::Activity::Idle) return true;
    }
    return false;
}

void reset() {
    for (auto& m : motor) m = Motor{};
    locked = false;
    time_valid = true;
}
}  // namespace sim

// --- Stubs of the dependencies ----------------------------------------------
int64_t esp_timer_get_time() { return sim::now_us; }
BaseType_t xTaskCreate(TaskFunction_t fn, const char*, uint32_t, void*, int, void*) {
    sim::task = fn;
    return pdPASS;
}

namespace doser::motion {
esp_err_t run(int pump, const Move& m) {
    auto& s = sim::motor[pump];
    if (sim::locked || s.running) return ESP_ERR_INVALID_STATE;
    if (m.sps == 0 || m.sps > PumpConfig{}.max_sps()) return ESP_ERR_INVALID_ARG;
    s.moves.push_back(m.steps);
    if (m.steps == 0) return ESP_OK;
    s.running = true;
    s.position = 0;
    s.frac = 0;
    s.target = static_cast<uint32_t>(std::abs(m.steps));
    s.sps = m.sps;
    s.fault = nullptr;
    return ESP_OK;
}
void stop(int pump, bool immediate) {
    auto& s = sim::motor[pump];
    if (!s.running) return;
    if (immediate) s.running = false;
    else s.target = std::min(s.target, s.position + 200);  // short braking distance
}
bool locked() { return sim::locked; }
PumpStatus status(int pump) {
    PumpStatus st{};
    const auto& s = sim::motor[pump];
    st.running = s.running;
    st.position = s.position;
    st.target = s.target;
    st.fault = s.fault;
    st.configured = true;
    return st;
}
PumpConfig config(int) { return PumpConfig{}; }
}  // namespace doser::motion

namespace lgal::timesync {
Status status() {
    Status s{};
    s.valid = sim::time_valid;
    return s;
}
}  // namespace lgal::timesync

namespace doser::dosing_store {
void load_engineering(dose::Engineering*) {}
esp_err_t save_engineering(const dose::Engineering&) { return ESP_OK; }
void load_settings(int, dose::PumpSettings*) {}
esp_err_t save_settings(int, const dose::PumpSettings&) { return ESP_OK; }
bool load_state(int, dose::PumpState*) { return false; }
esp_err_t save_state(int pump, const dose::PumpState& s) {
    ++sim::saves[pump];
    sim::stored[pump] = s;
    return ESP_OK;
}
const char* state_backend() { return "sim"; }
}  // namespace doser::dosing_store

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
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
