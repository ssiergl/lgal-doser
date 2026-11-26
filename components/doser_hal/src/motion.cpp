// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

#include "doser/motion.h"

#include <atomic>
#include <cinttypes>
#include <cstdlib>

#include "doser/pins.h"
#include "doser/safe_init.h"
#include "doser/stepgen.h"
#include "doser/tmc2209.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace doser::motion {

namespace {
constexpr const char* TAG = "motion";
constexpr int kN = pins::kPumpCount;
constexpr TickType_t kLoop = pdMS_TO_TICKS(20);
constexpr int64_t kSgEveryUs = 100 * 1000;
constexpr int64_t kHealthEveryUs = 1000 * 1000;
constexpr int64_t kEnOffDelayUs = 500 * 1000;
constexpr int64_t kSgSettleUs = 300 * 1000;  // do not count the acceleration ramp in sg_min

struct Pump {
    Tmc2209 drv;
    PumpConfig cfg;
    tmc::DriverRegs regs{};
    bool fitted = true;       // channel population: never probed or moved if false
    bool present = false;
    bool configured = false;
    bool tuned = false;       // StealthChop tuning AT#1 done after configuration
    bool shaft_reverse = false;
    bool reverse = false;
    bool was_running = false;
    uint8_t version = 0;
    uint16_t sg = 0;
    uint16_t sg_min = 0;
    uint32_t drv_status = 0;
    uint32_t tstep = 0;
    uint32_t pwm_scale = 0;
    uint8_t gstat = 0;
    const char* fault = nullptr;
    int64_t start_us = 0;
};

constexpr int kBuses = static_cast<int>(pins::kUartBus.size());
TmcBus s_bus[kBuses];
Pump s_pump[kN];
SemaphoreHandle_t s_mutex = nullptr;
SemaphoreHandle_t s_ready = nullptr;
esp_err_t s_init_err = ESP_OK;
std::atomic<uint8_t> s_locks{0};  // LockBy bits
int64_t s_last_busy_us = 0;

struct Lock {
    Lock() { xSemaphoreTake(s_mutex, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(s_mutex); }
};

bool any_busy() {
    for (int i = 0; i < kN; ++i) {
        auto* ch = step::channel(i);
        if (ch && ch->busy()) return true;
    }
    return false;
}

bool any_hold_current() {
    for (auto& p : s_pump)
        if (p.configured && p.regs.current.hold_ma > 0) return true;
    return false;
}

// call with mutex held
esp_err_t configure(int i) {
    Pump& p = s_pump[i];
    p.regs = tmc::make_regs(p.cfg.driver, pins::kTmcRsenseMohm, false);
    esp_err_t err = p.drv.apply(p.regs);
    p.configured = (err == ESP_OK);
    p.shaft_reverse = false;
    p.tuned = false;
    if (p.configured) {
        ESP_LOGI(TAG, "Pump %d: %u microsteps, run current %u mA, hold current %u mA%s", i + 1, p.cfg.driver.microsteps,
                 p.regs.current.run_ma, p.regs.current.hold_ma, p.cfg.driver.invert ? ", direction inverted" : "");
    } else {
        ESP_LOGE(TAG, "Pump %d: configuration failed (%s)", i + 1, esp_err_to_name(err));
    }
    return err;
}

// call with mutex held
void probe_and_configure(int i) {
    Pump& p = s_pump[i];
    uint8_t ver = 0;
    esp_err_t err = p.drv.probe(&ver);
    const bool was = p.present;
    p.present = (err == ESP_OK);
    p.version = ver;
    if (p.present && !was) {
        ESP_LOGI(TAG, "Pump %d: TMC2209 found (version 0x%02x)", i + 1, ver);
        configure(i);
    } else if (!p.present && was) {
        ESP_LOGW(TAG, "Pump %d: driver stopped responding", i + 1);
        p.configured = false;
    } else if (err == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Pump %d: unexpected version 0x%02x", i + 1, ver);
    }
}

// Full driver status of a fault: which phase, to GND or VS, temperature
void log_drv_status(int i, uint32_t v) {
    const auto d = tmc::decode_drv_status(v);
    ESP_LOGE(TAG,
             "Pump %d: DRV_STATUS 0x%08" PRIx32 " s2ga=%d s2gb=%d s2vsa=%d s2vsb=%d ola=%d olb=%d otpw=%d ot=%d "
             "t120=%d t143=%d t150=%d t157=%d cs=%u stealth=%d",
             i + 1, v, d.s2ga, d.s2gb, d.s2vsa, d.s2vsb, d.ola, d.olb, d.otpw, d.ot, d.t120, d.t143, d.t150, d.t157,
             d.cs_actual, d.stealth);
}

void fault_stop(int i, const char* why) {
    auto* ch = step::channel(i);
    if (ch) ch->stop(true);
    s_pump[i].fault = why;
    ESP_LOGE(TAG, "Pump %d: %s - move aborted", i + 1, why);
}

void health_check(int i) {
    Pump& p = s_pump[i];
    if (!p.fitted) return;
    if (!p.present) {
        probe_and_configure(i);
        return;
    }
    uint32_t gstat = 0;
    if (p.drv.read(tmc::reg::GSTAT, &gstat) != ESP_OK) {
        // A second attempt via probe() distinguishes a glitch from "gone"
        probe_and_configure(i);
        if (!p.present) {
            auto* ch = step::channel(i);
            if (ch && ch->busy()) fault_stop(i, "driver_lost");
        }
        return;
    }
    p.gstat = static_cast<uint8_t>(gstat);
    if (!p.configured) {
        // An earlier configuration failed (e.g. a lost datagram): try again,
        // but not in the middle of a forced move without configuration
        auto* ch = step::channel(i);
        if (!(ch && ch->busy())) configure(i);
        return;
    }
    if (gstat & tmc::GSTAT_RESET) {
        // Driver restarted (e.g. 24 V lost and back) -> registers lost
        ESP_LOGW(TAG, "Pump %d: driver reset detected, reconfiguring", i + 1);
        auto* ch = step::channel(i);
        if (ch && ch->busy()) fault_stop(i, "driver_reset");
        configure(i);
    }
    if (gstat & tmc::GSTAT_DRV_ERR) {
        uint32_t st = 0;
        if (p.drv.read(tmc::reg::DRV_STATUS, &st) == ESP_OK) log_drv_status(i, st);
        auto* ch = step::channel(i);
        if (ch && ch->busy()) fault_stop(i, "driver_error");
        p.drv.write(tmc::reg::GSTAT, tmc::GSTAT_DRV_ERR);
    }
    uint32_t v = 0;
    if (p.drv.read(tmc::reg::DRV_STATUS, &v) == ESP_OK) p.drv_status = v;
    if (p.drv.read(tmc::reg::PWM_SCALE, &v) == ESP_OK) p.pwm_scale = v;
}

void sample_running(int i, int64_t now) {
    Pump& p = s_pump[i];
    if (!p.present) return;
    uint32_t v = 0;
    if (p.drv.read(tmc::reg::SG_RESULT, &v) == ESP_OK) {
        p.sg = static_cast<uint16_t>(v & 0x3FF);
        if (now - p.start_us > kSgSettleUs && p.sg < p.sg_min) p.sg_min = p.sg;
    }
    if (p.drv.read(tmc::reg::TSTEP, &v) == ESP_OK) p.tstep = v & 0xFFFFF;
    if (p.drv.read(tmc::reg::DRV_STATUS, &v) == ESP_OK) {
        p.drv_status = v;
        const auto d = tmc::decode_drv_status(v);
        if (d.short_circuit() || d.ot) log_drv_status(i, v);
        if (d.short_circuit()) fault_stop(i, "short_circuit");
        else if (d.ot) fault_stop(i, "overtemperature");
    }
}

// Logs the end of the last move once. Called by the task loop and by run(), which can start
// the next move before the loop has seen the end of the previous one.
void log_done(int i, int64_t now) {
    Pump& p = s_pump[i];
    if (!p.was_running) return;
    p.was_running = false;
    const auto* ch = step::channel(i);
    ESP_LOGI(TAG, "Pump %d: done, %" PRIu32 " of %" PRIu32 " steps in %.2f s", i + 1, ch->position(),
             ch->target(), (now - p.start_us) / 1e6);
}

void task(void*) {
    // Allocate all interrupts (UART, RMT, PCNT, GPTimer) on core 1,
    // separate from the network on core 0.
    // UART0 is the console: bus i runs on UART1 + i
    for (int b = 0; b < kBuses && s_init_err == ESP_OK; ++b)
        s_init_err = s_bus[b].init(static_cast<uart_port_t>(UART_NUM_1 + b), pins::kUartBus[b].tx,
                                   pins::kUartBus[b].rx, pins::kTmcBaud);
    if (s_init_err == ESP_OK) s_init_err = step::init();
    if (s_init_err == ESP_OK) {
        Lock l;
        for (int i = 0; i < kN; ++i) {
            s_pump[i].drv.bind(&s_bus[pins::kTmc[i].bus], pins::kTmc[i].addr);
            if (!s_pump[i].fitted) continue;
            probe_and_configure(i);
            if (!s_pump[i].present) ESP_LOGW(TAG, "Pump %d: no driver (24 V on? module inserted?)", i + 1);
        }
    }
    xSemaphoreGive(s_ready);
    if (s_init_err != ESP_OK) {
        vTaskDelete(nullptr);
        return;
    }

    int64_t next_sg = 0;
    int64_t next_health = 0;
    int health_idx = 0;
    while (true) {
        vTaskDelay(kLoop);
        const int64_t now = esp_timer_get_time();
        Lock l;

        // detect end of move
        for (int i = 0; i < kN; ++i)
            if (!step::channel(i)->busy()) log_done(i, now);

        if (now >= next_sg) {
            next_sg = now + kSgEveryUs;
            for (int i = 0; i < kN; ++i)
                if (step::channel(i)->busy()) sample_running(i, now);
        }

        // Round-robin health check: one driver per pass (1 s each)
        if (now >= next_health) {
            next_health = now + kHealthEveryUs / kN;
            health_check(health_idx);
            health_idx = (health_idx + 1) % kN;
        }

        // shared EN
        if (any_busy()) {
            s_last_busy_us = now;
        } else if (doser::drivers_enabled() && !any_hold_current() && now - s_last_busy_us > kEnOffDelayUs) {
            set_drivers_enabled(false);
            ESP_LOGI(TAG, "All pumps idle - drivers off (EN)");
        }
    }
}
}  // namespace

esp_err_t init(const PumpConfig (&configs)[kMaxPumps], uint8_t fitted) {
    s_mutex = xSemaphoreCreateMutex();
    s_ready = xSemaphoreCreateBinary();
    if (!s_mutex || !s_ready) return ESP_ERR_NO_MEM;
    for (int i = 0; i < kN; ++i) {
        s_pump[i].cfg = configs[i];
        s_pump[i].fitted = fitted >> i & 1;
    }
    if (xTaskCreatePinnedToCore(task, "motion", 4096, nullptr, 10, nullptr, 1) != pdPASS) return ESP_ERR_NO_MEM;
    if (xSemaphoreTake(s_ready, pdMS_TO_TICKS(5000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    return s_init_err;
}

esp_err_t run(int pump, const Move& move) {
    if (pump < 0 || pump >= kN || !s_mutex) return ESP_ERR_INVALID_ARG;
    auto* ch = step::channel(pump);
    if (!ch) return ESP_ERR_INVALID_STATE;
    Lock l;
    if (s_locks) return ESP_ERR_INVALID_STATE;
    Pump& p = s_pump[pump];
    if (!p.fitted) return ESP_ERR_NOT_FOUND;
    if (ch->busy()) return ESP_ERR_INVALID_STATE;
    log_done(pump, esp_timer_get_time());
    if (!p.configured && !move.force) return ESP_ERR_NOT_FOUND;
    if (move.sps == 0 || move.sps > p.cfg.max_sps()) return ESP_ERR_INVALID_ARG;
    const uint32_t steps = static_cast<uint32_t>(std::abs(move.steps));
    if (steps == 0) return ESP_OK;

    const bool rev = move.steps < 0;
    if (p.configured) {
        if (rev != p.shaft_reverse) {
            ESP_RETURN_ON_ERROR(p.drv.write(tmc::reg::GCONF, tmc::gconf_for(p.cfg.driver, rev)), TAG, "gconf");
            p.shaft_reverse = rev;
        }
        if (!p.tuned) {
            // StealthChop tuning AT#1: >= 130 ms standstill with run current
            const auto& c = p.regs.current;
            p.drv.write(tmc::reg::IHOLD_IRUN, tmc::ihold_irun(c.irun, c.irun));
            set_drivers_enabled(true);
            vTaskDelay(pdMS_TO_TICKS(150));
            p.drv.write(tmc::reg::IHOLD_IRUN, p.regs.ihold_irun);
            p.tuned = !s_locks;  // a lock in the pause switched EN off: tune again next time
        }
    }
    // A lock that came while the driver was being set up (UART, tuning pause) holds
    if (s_locks) return ESP_ERR_INVALID_STATE;
    set_drivers_enabled(true);
    s_last_busy_us = esp_timer_get_time();

    RampParams rp{};
    rp.steps = steps;
    rp.v_max = move.sps;
    rp.accel = move.accel ? move.accel : p.cfg.accel_or_default();
    rp.v_start = move.v_start ? move.v_start : p.cfg.v_start_or_default();
    esp_err_t err = ch->start(rp);
    if (err == ESP_OK) {
        p.reverse = rev;
        p.fault = nullptr;
        p.sg = 0;
        p.sg_min = 0x3FF;
        p.start_us = esp_timer_get_time();
        p.was_running = true;
        ESP_LOGI(TAG, "Pump %d: %s%" PRIu32 " steps, %" PRIu32 " steps/s, ramp %" PRIu32 " steps/s^2%s",
                 pump + 1, rev ? "-" : "", steps, move.sps, rp.accel, p.configured ? "" : " (without driver!)");
    }
    return err;
}

void stop(int pump, bool immediate) {
    auto* ch = step::channel(pump);
    if (ch) ch->stop(immediate);
}

void stop_all(bool immediate) {
    for (int i = 0; i < kN; ++i) stop(i, immediate);
}

bool any_running() { return any_busy(); }

void lock(bool locked, LockBy by) {
    const auto bit = static_cast<uint8_t>(by);
    if (!locked) {
        s_locks &= static_cast<uint8_t>(~bit);
        return;
    }
    s_locks |= bit;
    stop_all(true);
    set_drivers_enabled(false);
    // A run() that checked the lock before it was set may still be setting up its driver:
    // wait for it, then stop again whatever it started
    if (!s_mutex) return;
    Lock l;
    stop_all(true);
    set_drivers_enabled(false);
}

bool locked() { return s_locks != 0; }

bool drivers_enabled() { return doser::drivers_enabled(); }

PumpStatus status(int pump) {
    PumpStatus s{};
    if (pump < 0 || pump >= kN || !s_mutex) return s;
    auto* ch = step::channel(pump);
    Lock l;
    const Pump& p = s_pump[pump];
    s.present = p.present;
    s.configured = p.configured;
    s.version = p.version;
    s.reverse = p.reverse;
    s.sg_result = p.sg;
    s.sg_min = p.sg_min;
    s.drv_status = p.drv_status;
    s.gstat = p.gstat;
    s.tstep = p.tstep;
    s.pwm_scale = p.pwm_scale;
    s.run_ma = p.configured ? p.regs.current.run_ma : 0;
    s.hold_ma = p.configured ? p.regs.current.hold_ma : 0;
    s.fault = p.fault;
    if (ch) {
        s.running = ch->busy();
        s.position = ch->position();
        s.target = ch->target();
        s.velocity = ch->velocity();
        s.stepgen = ch->kind();
    }
    return s;
}

PumpConfig config(int pump) {
    if (pump < 0 || pump >= kN || !s_mutex) return {};
    Lock l;
    return s_pump[pump].cfg;
}

esp_err_t set_config(int pump, const PumpConfig& cfg) {
    if (pump < 0 || pump >= kN || !s_mutex) return ESP_ERR_INVALID_ARG;
    if (tmc::mres_for(cfg.driver.microsteps) == 0xFF || cfg.driver.run_ma < 50 || cfg.driver.run_ma > 1500 ||
        cfg.driver.hold_ma > cfg.driver.run_ma || cfg.full_steps == 0 || cfg.full_steps > 1000) {
        return ESP_ERR_INVALID_ARG;
    }
    auto* ch = step::channel(pump);
    Lock l;
    if (ch && ch->busy()) return ESP_ERR_INVALID_STATE;
    s_pump[pump].cfg = cfg;
    if (s_pump[pump].present) return configure(pump);
    return ESP_OK;
}

esp_err_t read_register(int pump, uint8_t reg, uint32_t* value) {
    if (pump < 0 || pump >= kN || !s_mutex) return ESP_ERR_INVALID_ARG;
    Lock l;
    return s_pump[pump].drv.read(reg, value);
}

BusStats bus_stats(int bus) {
    if (bus < 0 || bus >= kBuses) return {};
    const auto st = s_bus[bus].stats();
    return {st.transfers, st.timeouts, st.crc_errors, st.echo_errors};
}

int bus_count() { return kBuses; }

}  // namespace doser::motion
