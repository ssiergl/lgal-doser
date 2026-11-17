// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

#include "doser/supply_core.h"

namespace doser::supply {

State classify(uint32_t mv, State prev) {
    uint32_t missing = kMissingMv, low = kLowMv;
    switch (prev) {
        case State::Missing:  // has to rise clearly above a threshold to leave
            missing += kHysteresisMv;
            low += kHysteresisMv;
            break;
        case State::Low:
            missing -= kHysteresisMv;
            low += kHysteresisMv;
            break;
        case State::Ok:  // has to fall clearly below a threshold to leave
            missing -= kHysteresisMv;
            low -= kHysteresisMv;
            break;
        case State::None:
            break;
    }
    if (mv < missing) return State::Missing;
    if (mv < low) return State::Low;
    return State::Ok;
}

const char* state_name(State s) {
    switch (s) {
        case State::None: return "none";
        case State::Ok: return "ok";
        case State::Low: return "low";
        case State::Missing: return "missing";
    }
    return "none";
}

bool Tracker::update(uint32_t mv) {
    if (state_ == State::None) return false;
    const State c = classify(mv, state_);
    if (c == state_) {
        count_ = 0;
        return false;
    }
    if (c != pending_) {
        pending_ = c;
        count_ = 0;
    }
    if (++count_ < kConfirm) return false;
    if (state_ == State::Missing) returned_ = true;
    state_ = c;
    count_ = 0;
    return true;
}

}  // namespace doser::supply
