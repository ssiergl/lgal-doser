// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// Host tests for the motion lock (doser/motion.h): motion.cpp unchanged with real threads
// (tests/host/stubs_threads); drivers, EN and step clocks are fakes. A lock that comes while
// run() sets up a driver must hold: once lock() has returned, no pump runs and EN is off.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>

#include "doser/motion.h"
#include "doser/safe_init.h"
#include "doser/stepgen.h"
#include "doser/tmc2209.h"
#include "esp_timer.h"

using namespace doser;
using motion::LockBy;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

bool g_log_verbose = false;
static const auto g_t0 = std::chrono::steady_clock::now();
int64_t esp_timer_get_time() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - g_t0).count();
}

namespace {
std::atomic<bool> g_en{false};
std::atomic<int> g_en_offs{0};          // calls of set_drivers_enabled(false)
std::atomic<int> g_tunings{0};          // IHOLD_IRUN writes with hold = run current (StealthChop tuning)
std::atomic<uint32_t> g_ihold_irun{0};  // last IHOLD_IRUN write
// Called once in the next set_drivers_enabled(true), before or after EN goes on
std::function<void()> g_hook;
bool g_hook_after_on = false;

bool tuning_value(uint32_t ihold_irun) { return (ihold_irun & 0x1F) == (ihold_irun >> 8 & 0x1F); }
}  // namespace

namespace doser {
void set_drivers_enabled(bool on) {
    if (on && g_hook) {
        const auto hook = std::move(g_hook);
        g_hook = nullptr;
        if (!g_hook_after_on) hook();
        g_en = true;
        if (g_hook_after_on) hook();
        return;
    }
    if (!on) ++g_en_offs;
    g_en = on;
}
bool drivers_enabled() { return g_en; }

esp_err_t TmcBus::init(uart_port_t, gpio_num_t, gpio_num_t, uint32_t) { return ESP_OK; }
esp_err_t Tmc2209::probe(uint8_t* version) {
    if (version) *version = 0x21;
    return ESP_OK;
}
esp_err_t Tmc2209::apply(const tmc::DriverRegs&) { return ESP_OK; }
esp_err_t Tmc2209::write(uint8_t reg, uint32_t value) {
    if (reg == tmc::reg::IHOLD_IRUN) {
        if (tuning_value(value)) ++g_tunings;
        g_ihold_irun = value;
    }
    return ESP_OK;
}
esp_err_t Tmc2209::read(uint8_t, uint32_t* value) {
    *value = 0;
    return ESP_OK;
}

namespace step {
struct FakeChannel : Channel {
    std::atomic<bool> running{false};
    std::atomic<int> starts{0};
    uint32_t steps = 0;
    esp_err_t start(const RampParams& move) override {
        steps = move.steps;
        ++starts;
        running = true;
        return ESP_OK;
    }
    void stop(bool) override { running = false; }
    bool busy() const override { return running; }
    uint32_t position() const override { return 0; }
    uint32_t target() const override { return steps; }
    uint32_t velocity() const override { return 0; }
    const char* kind() const override { return "fake"; }
};
static FakeChannel g_channel[kMaxPumps];
esp_err_t init() { return ESP_OK; }
Channel* channel(int i) { return i >= 0 && i < kMaxPumps ? &g_channel[i] : nullptr; }
}  // namespace step
}  // namespace doser

namespace {
const motion::Move kMove{3200, 1000, 0, 0, false};
std::thread g_locker;

step::FakeChannel& pump(int i) { return step::g_channel[i]; }

bool any_pump_running() {
    for (const auto& ch : step::g_channel)
        if (ch.busy()) return true;
    return false;
}

// lock(true, by) from another thread, as the supply watch or the OTA task does. Waits (at most
// 2 s) until it has stopped the pumps and switched EN off: lock() does that at once, before it
// waits for a run() that is setting up a driver.
bool lock_from_other_thread(LockBy by) {
    const int offs = g_en_offs;
    g_locker = std::thread([by] { motion::lock(true, by); });
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (g_en_offs == offs) {
        if (std::chrono::steady_clock::now() > until) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

struct LockedRun {
    esp_err_t err;
    bool lock_came;  // the lock came while run() set up the driver
    bool at_once;    // lock() stopped the pumps and switched EN off without waiting for run()
    bool stopped;    // ...and no pump was running any more then
};

// Moves pump i; the lock comes from another thread in run()'s next set_drivers_enabled(true),
// just before EN goes on or just after. Returns once run() and lock() have both returned.
LockedRun run_with_lock(int i, LockBy by, bool after_on) {
    LockedRun r{};
    g_hook_after_on = after_on;
    g_hook = [&r, by] {
        r.lock_came = true;
        r.at_once = lock_from_other_thread(by);
        r.stopped = !any_pump_running();
    };
    r.err = motion::run(i, kMove);
    g_hook = nullptr;
    if (g_locker.joinable()) g_locker.join();
    return r;
}
}  // namespace

// Board without 24 V: the motion layer is never started (no mutex), the OTA lock and the factory
// reset lock all the same
static void test_lock_without_init() {
    std::printf("Motion: lock without init\n");
    motion::lock(true, LockBy::Ota);
    CHECK(motion::locked() && !g_en);
    motion::lock(false, LockBy::Ota);
    CHECK(!motion::locked());
}

static void test_lock_reasons() {
    std::printf("Motion: lock reasons\n");
    motion::lock(true, LockBy::Ota);
    CHECK(motion::run(0, kMove) == ESP_ERR_INVALID_STATE);
    motion::lock(true, LockBy::Supply);
    motion::lock(false, LockBy::Ota);
    CHECK(motion::locked());  // the supply still locks
    CHECK(motion::run(0, kMove) == ESP_ERR_INVALID_STATE);
    motion::lock(false, LockBy::Supply);
    CHECK(!motion::locked());
    CHECK(pump(0).starts == 0 && g_tunings == 0);  // refused before the driver was touched
}

// Pump 1 moves for the first time (StealthChop tuning: 150 ms standstill with EN on) while pump 2
// runs; the 24 V supply goes just before the tuning switches EN on, or in the pause
static void test_lock_while_tuning(bool in_pause) {
    std::printf("Motion: lock %s\n", in_pause ? "in the tuning pause" : "before the tuning switches EN on");
    CHECK(motion::run(1, kMove) == ESP_OK && pump(1).busy() && g_en);
    const int tunings = g_tunings;
    const auto r = run_with_lock(0, LockBy::Supply, in_pause);
    CHECK(r.lock_came && r.at_once && r.stopped);  // pump 2 stops at once, not after the tuning
    CHECK(r.err == ESP_ERR_INVALID_STATE);         // the move is refused,
    CHECK(pump(0).starts == 0);                    // pump 1 never started
    CHECK(!any_pump_running() && !g_en);
    // the cut-short tuning restored the hold current
    CHECK(g_tunings == tunings + 1 && !tuning_value(g_ihold_irun));
    motion::lock(false, LockBy::Supply);
}

// Pump 2 is tuned: the lock comes after run() checked it, just before the move starts. run() may
// start it; lock() stops it again before it returns.
static void test_lock_before_start() {
    std::printf("Motion: lock just before a move starts\n");
    const auto r = run_with_lock(1, LockBy::Ota, false);
    CHECK(r.lock_came && r.at_once);
    CHECK(r.err == ESP_OK || r.err == ESP_ERR_INVALID_STATE);
    CHECK(!any_pump_running() && !g_en);
    motion::lock(false, LockBy::Ota);
}

// The cut-short tuning runs on the next move
static void test_tuning_after_lock() {
    std::printf("Motion: tuning after a lock\n");
    const int tunings = g_tunings;
    CHECK(motion::run(0, kMove) == ESP_OK);
    CHECK(g_tunings == tunings + 1 && !tuning_value(g_ihold_irun));
    CHECK(pump(0).starts == 1 && pump(0).busy() && g_en);
    motion::lock(true, LockBy::Ota);
    CHECK(!any_pump_running() && !g_en);
    motion::lock(false, LockBy::Ota);
}

int main(int argc, char**) {
    g_log_verbose = argc > 1;
    test_lock_without_init();
    motion::PumpConfig cfgs[kMaxPumps]{};
    for (auto& c : cfgs) c.driver.hold_ma = 100;  // EN stays on when idle: only the lock switches it off
    CHECK(motion::init(cfgs) == ESP_OK);
    test_lock_reasons();
    test_lock_while_tuning(false);
    test_lock_while_tuning(true);
    test_lock_before_start();
    test_tuning_after_lock();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    std::fflush(stdout);
    std::_Exit(g_fail ? 1 : 0);  // the motion task keeps running: no static destructors under it
}
