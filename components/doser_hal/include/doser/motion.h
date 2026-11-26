// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
// Pump layer: TMC2209 configuration + step clock + shared EN.
//
// Everything here is "raw" (steps, steps/s). Dosing amounts, calibration,
// retraction etc. are in doser_app (doser/dosing.h) and build on top of it.
//
// Shared EN: as soon as one pump moves, all drivers are enabled.
// Drivers not moving freewheel with IHOLD = 0 (no current).
// After all moves have ended EN is switched off (exception: a hold current
// > 0 is configured).

#include <cstdint>

#include "doser/limits.h"
#include "doser/tmc2209_proto.h"
#include "esp_err.h"

namespace doser::motion {

struct PumpConfig {
    tmc::DriverSettings driver;   // run current, hold current, microsteps, direction
    uint16_t full_steps = 200;    // full steps per revolution (Kamoer: 1.8° step angle)
    uint32_t accel = 0;           // steps/s², 0 = default (600 rpm per s)
    uint32_t v_start = 0;         // steps/s,  0 = default (20 rpm)

    uint32_t steps_per_rev() const { return uint32_t(full_steps) * driver.microsteps; }
    uint32_t rpm_to_sps(uint32_t rpm) const { return rpm * steps_per_rev() / 60; }
    uint32_t accel_or_default() const { return accel ? accel : rpm_to_sps(600); }
    uint32_t v_start_or_default() const { return v_start ? v_start : rpm_to_sps(20); }
    uint32_t max_sps() const { return rpm_to_sps(kMaxRpm); }
};

struct Move {
    int32_t steps;     // < 0 = reverse
    uint32_t sps;      // target speed [steps/s]
    uint32_t accel;    // 0 = from PumpConfig
    uint32_t v_start;  // 0 = from PumpConfig
    bool force;        // step even without a reachable driver (test with oscilloscope)
};

struct PumpStatus {
    bool present;          // driver answers via UART
    bool configured;       // registers written successfully
    uint8_t version;
    bool running;
    bool reverse;
    uint32_t position;     // steps of the current/last move
    uint32_t target;
    uint32_t velocity;     // steps/s
    uint16_t sg_result;    // last StallGuard value (only meaningful while moving)
    uint16_t sg_min;       // minimum of the current/last move
    uint32_t drv_status;
    uint8_t gstat;
    uint32_t tstep;
    uint32_t pwm_scale;
    uint16_t run_ma;       // actually set
    uint16_t hold_ma;
    const char* fault;     // error code (driver_lost, driver_reset, driver_error, short_circuit, overtemperature), nullptr = ok
    const char* stepgen;   // "rmt" / "mcpwm"
};

// Starts buses, drivers, step clocks and the monitoring task (core 1).
// fitted: bit i = pump i+1 is fitted; the others are never probed or moved.
esp_err_t init(const PumpConfig (&configs)[kMaxPumps], uint8_t fitted = (1u << kMaxPumps) - 1);

esp_err_t run(int pump, const Move& move);
void stop(int pump, bool immediate);
void stop_all(bool immediate);
bool any_running();

// Lock: true = stop everything immediately, EN off, no new moves. Each
// reason locks on its own; the pumps are free when no reason is left.
// lock(true) waits for a move that is being set up (the first move after a
// configuration tunes its driver for 150 ms) and stops it: when it returns,
// no pump runs and EN is off.
enum class LockBy : uint8_t { Ota = 1, Supply = 2 };
void lock(bool locked, LockBy by = LockBy::Ota);
bool locked();

PumpStatus status(int pump);
PumpConfig config(int pump);
esp_err_t set_config(int pump, const PumpConfig& cfg);  // only at standstill
bool drivers_enabled();

// raw access for diagnostics
esp_err_t read_register(int pump, uint8_t reg, uint32_t* value);

struct BusStats {
    uint32_t transfers, timeouts, crc_errors, echo_errors;
};
// bus = index in pins::kUartBus (board d5v1: 0 = A, pumps 1-4; 1 = B, pump 5)
BusStats bus_stats(int bus);
int bus_count();

}  // namespace doser::motion
