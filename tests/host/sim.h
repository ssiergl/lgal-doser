#pragma once
// Simulated hardware, clock and storage for the host simulations of the
// dosing engine and the scheduler. Header with definitions: include it in
// exactly one translation unit per test binary.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "doser/dosing.h"
#include "doser/dosing_store.h"
#include "doser/history.h"
#include "doser/motion.h"
#include "freertos/task.h"
#include "lgal/timesync.h"

using namespace doser;

// ---------------------------------------------------------------------------
// Simulation
namespace sim {
int64_t now_us = 1000000;
constexpr int64_t kUtc0 = 1793968215;  // 2026-11-06 12:30:15 UTC at now_us = 0
bool time_valid = true;
int64_t utc_offset = 0;  // clock set back/forward (NTP correction)
TaskFunction_t task = nullptr;        // dosing engine (20 ms)
TaskFunction_t sched_task = nullptr;  // scheduler (1 s)
const char* tz = "CET-1CEST,M3.5.0,M10.5.0/3";

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
std::vector<history::Event> events;  // recorded history (utc 0 = "now" on the device)

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

int64_t utc() { return kUtc0 + now_us / 1000000 + utc_offset; }

// Jump the clock forward (device off or idle); nothing runs in between
void set_utc(int64_t t) {
    if (t > utc()) now_us = (t - kUtc0 - utc_offset) * 1000000;
}

void run_task(TaskFunction_t fn) {
    if (!fn) return;
    try {
        fn(nullptr);
    } catch (const TaskYield&) {
    }
}

bool moving() {
    for (int i = 0; i < 5; ++i)
        if (motor[i].running || dosing::status(i).activity != dosing::Activity::Idle) return true;
    return false;
}

// Both tasks for `seconds`: 20 ms ticks while something moves, otherwise
// one-second steps (the scheduler runs once per simulated second)
void run_for(double seconds) {
    const int64_t end = now_us + static_cast<int64_t>(seconds * 1e6);
    int64_t last_sec = utc();
    while (now_us < end) {
        if (moving()) tick();
        else advance(std::min<int64_t>(1000000 - now_us % 1000000, end - now_us));
        if (utc() != last_sec) {
            last_sec = utc();
            run_task(sched_task);
        }
    }
}

// Run until the given UTC (inclusive)
void run_until(int64_t t) { run_for(static_cast<double>(t - utc()) + 0.5); }

void reset() {
    for (auto& m : motor) m = Motor{};
    locked = false;
    time_valid = true;
    events.clear();
}

// Last recorded event of a kind for pump (1-based), nullptr = none
const history::Event* last_event(history::Kind k, int pump) {
    for (auto it = events.rbegin(); it != events.rend(); ++it)
        if (it->kind == k && it->pump == pump) return &*it;
    return nullptr;
}
}  // namespace sim

namespace doser::history {
void record(const Event& e) { sim::events.push_back(e); }
}  // namespace doser::history

// --- Stubs of the dependencies ----------------------------------------------
int64_t esp_timer_get_time() { return sim::now_us; }
BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t, void*, int, void*) {
    (std::strcmp(name, "sched") == 0 ? sim::sched_task : sim::task) = fn;
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

namespace doser::hwconfig {
uint8_t g_fitted = 0x1F;  // channel population (tests change it)
uint8_t fitted_mask() { return g_fitted; }
bool fitted(int pump) { return pump >= 0 && pump < 5 && (g_fitted >> pump & 1); }
}  // namespace doser::hwconfig

namespace lgal::timesync {
Status status() {
    Status s{};
    s.valid = sim::time_valid;
    s.utc = sim::time_valid ? sim::utc() : 0;
    return s;
}
const char* timezone() { return sim::tz; }
}  // namespace lgal::timesync

namespace doser::dosing_store {
void load_engineering(dose::Engineering*) {}
esp_err_t save_engineering(const dose::Engineering&) { return ESP_OK; }
// Stored settings use the global retraction, so the flows below run with it
// (the factory default of a pump is off, see test_retract_off)
void load_settings(int, dose::PumpSettings* p) { p->retract_mrev = -1; }
esp_err_t save_settings(int, const dose::PumpSettings&) { return ESP_OK; }
bool load_state(int, dose::PumpState*) { return false; }
esp_err_t save_state(int pump, const dose::PumpState& s) {
    ++sim::saves[pump];
    sim::stored[pump] = s;
    return ESP_OK;
}
const char* state_backend() { return "sim"; }
}  // namespace doser::dosing_store

