#pragma once
// Trapezoidal speed profile for the step clocks.
//
// Runs in interrupt context (RMT encoder, timer ISR) and therefore uses pure
// integer arithmetic: no FPU (forbidden in ISRs on the ESP32-S3), no
// 64-bit division. No ESP-IDF dependency -> tested in tests/host.
//
// Two operating modes:
//  * step mode   (RMT):     next_interval_us() returns the interval until the
//                           next step; the step count is exact.
//  * tempo mode  (MCPWM):   advance() is called cyclically (1 ms) with the counted
//                           position and returns the new frequency;
//                           the counter (PCNT) ensures the exact end.

#include <cstdint>

#if defined(ESP_PLATFORM)
#include "esp_attr.h"
#define DOSER_ISR_CODE IRAM_ATTR
#else
#define DOSER_ISR_CODE
#endif

namespace doser {

struct RampParams {
    uint32_t steps;    // total steps
    uint32_t v_max;    // target speed [steps/s]
    uint32_t v_start;  // start/stop speed [steps/s]
    uint32_t accel;    // acceleration [steps/s²], 0 = no ramp
};

class Ramp {
public:
    static constexpr uint32_t kMinVelocity = 16;       // steps/s
    static constexpr uint32_t kMaxVelocity = 100000;   // steps/s
    static constexpr uint32_t kPulseUs = 2;            // STEP high time

    void start(const RampParams& p);

    // Step mode: interval until the next step in µs, 0 = done.
    DOSER_ISR_CODE uint32_t next_interval_us();

    // Tempo mode: dt since the last call, counted position.
    // Returns the new speed [steps/s], 0 = (soft) stop finished.
    DOSER_ISR_CODE uint32_t advance(uint32_t dt_us, uint32_t position);

    // Decelerate with the configured ramp, then done.
    void request_stop() { stop_requested_ = true; }
    // Mark as finished immediately (hardware has already been stopped).
    DOSER_ISR_CODE void finish() { done_ = true; }

    uint32_t position() const { return pos_; }
    uint32_t target() const { return target_; }
    uint32_t velocity() const { return done_ ? 0 : v_milli_ / 1000; }
    bool done() const { return done_; }
    bool stopping() const { return stop_requested_; }

private:
    DOSER_ISR_CODE bool must_decelerate(uint32_t remaining) const;
    DOSER_ISR_CODE void step_velocity(uint32_t dt_us, uint32_t remaining);

    volatile uint32_t pos_ = 0;
    volatile uint32_t target_ = 0;
    volatile uint32_t v_milli_ = 0;  // speed in 1/1000 steps/s
    uint32_t vmax_milli_ = 0;
    uint32_t vstart_milli_ = 0;
    uint32_t accel_ = 0;
    uint32_t carry_ = 0;  // remaining fraction of the intervals (exact mean)
    volatile bool stop_requested_ = false;
    volatile bool done_ = true;
};

}  // namespace doser
