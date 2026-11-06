#pragma once
// Five independent step clocks, all pumps can run at the same time.
//
//   Pumps 1-4: RMT TX channels (the ESP32-S3 has exactly 4). Each step is
//              one RMT symbol that the encoder in the interrupt
//              computes -> step count exact by construction.
//   Pump 5:    an MCPWM timer generates the frequency, PCNT counts the pulses on
//              the same pin and stops the timer exactly at the target; a
//              1 kHz timer makes the frequency follow the ramp.
//
// All interrupts keep running during flash writes (NVS, OTA)
// (IRAM-safe, see sdkconfig.defaults).

#include <cstdint>

#include "doser/ramp.h"
#include "esp_err.h"

namespace doser::step {

class Channel {
public:
    virtual ~Channel() = default;
    virtual esp_err_t start(const RampParams& move) = 0;
    // immediate = false: decelerate with ramp; true: at once (emergency stop, OTA)
    virtual void stop(bool immediate) = 0;
    virtual bool busy() const = 0;
    // Steps since start(). RMT: while moving up to 48 steps ahead
    // (buffer), exact after the move has ended.
    virtual uint32_t position() const = 0;
    virtual uint32_t target() const = 0;
    virtual uint32_t velocity() const = 0;
    virtual const char* kind() const = 0;
};

esp_err_t init();
Channel* channel(int pump_index);  // 0..4

}  // namespace doser::step
