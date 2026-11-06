#include "doser/ramp.h"

namespace doser {

namespace {
constexpr uint32_t kUsMilli = 1000000000u;  // 1 s in µs · 1000 (fits in uint32_t)

inline uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

void Ramp::start(const RampParams& p) {
    const uint32_t vmax = clamp_u32(p.v_max, kMinVelocity, kMaxVelocity);
    uint32_t vstart = clamp_u32(p.v_start, kMinVelocity, kMaxVelocity);
    if (vstart > vmax || p.accel == 0) vstart = vmax;

    done_ = true;  // ISR ignores us until everything is set
    vmax_milli_ = vmax * 1000;
    vstart_milli_ = vstart * 1000;
    accel_ = p.accel > 2000000 ? 2000000 : p.accel;
    v_milli_ = vstart_milli_;
    carry_ = 0;
    pos_ = 0;
    target_ = p.steps;
    stop_requested_ = false;
    done_ = (p.steps == 0);
}

// Braking distance from the current speed: (v² - v0²) / (2a) >= remaining ?
bool Ramp::must_decelerate(uint32_t remaining) const {
    if (stop_requested_) return true;
    if (accel_ == 0) return false;
    const uint32_t v = v_milli_ / 1000;
    const uint32_t v0 = vstart_milli_ / 1000;
    if (v <= v0) return false;
    const uint64_t need = uint64_t(v) * v - uint64_t(v0) * v0;
    const uint64_t have = uint64_t(2u * accel_) * remaining;
    return have <= need;
}

void Ramp::step_velocity(uint32_t dt_us, uint32_t remaining) {
    if (accel_ == 0) return;
    if (dt_us > 1000000) dt_us = 1000000;
    // dv [msteps/s] = a [steps/s²] · dt [µs] / 1000, without overflow
    const uint32_t dv = (accel_ / 1000) * dt_us + (accel_ % 1000) * dt_us / 1000;
    if (must_decelerate(remaining)) {
        v_milli_ = (v_milli_ > vstart_milli_ + dv) ? v_milli_ - dv : vstart_milli_;
    } else if (v_milli_ < vmax_milli_) {
        const uint32_t nv = v_milli_ + dv;
        v_milli_ = nv > vmax_milli_ ? vmax_milli_ : nv;
    }
}

uint32_t Ramp::next_interval_us() {
    if (done_) return 0;
    if (pos_ >= target_ || (stop_requested_ && v_milli_ <= vstart_milli_)) {
        target_ = pos_;
        done_ = true;
        return 0;
    }
    const uint32_t v = v_milli_;
    uint32_t interval = kUsMilli / v;
    carry_ += kUsMilli % v;
    if (carry_ >= v) {
        carry_ -= v;
        ++interval;
        if (carry_ >= v) carry_ = 0;
    }
    pos_ = pos_ + 1;
    step_velocity(interval, target_ - pos_);
    return interval;
}

uint32_t Ramp::advance(uint32_t dt_us, uint32_t position) {
    if (done_) return 0;
    pos_ = position;
    if (stop_requested_ && v_milli_ <= vstart_milli_) {
        target_ = position;
        done_ = true;
        return 0;
    }
    const uint32_t remaining = position < target_ ? target_ - position : 0;
    // Look ahead by one control cycle so the braking ramp does not start too late
    const uint32_t dt_ms = (dt_us > 1000000 ? 1000000 : dt_us) / 1000;
    const uint32_t ahead = (v_milli_ / 1000) * dt_ms / 1000 + 1;
    step_velocity(dt_us, remaining > ahead ? remaining - ahead : 0);
    const uint32_t v = v_milli_ / 1000;
    return v ? v : 1;
}

}  // namespace doser
