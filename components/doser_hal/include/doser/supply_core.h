#pragma once
// 24 V supply states of the sense input (board d5v1 from v1.1 on). Pure
// logic, host-tested (tests/host/test_supply.cpp); supply.cpp measures.
//   Missing  below 12 V: the board 3.3 V rail is not powered (e.g. USB power
//            only, then the I/O pins back-feed it), its peripherals (TMC2209,
//            W5500, DS3231/EEPROM) are left alone
//   Low      12-20 V: undervoltage, no new dose starts
//   Ok       from 20 V
// Both thresholds have a hysteresis of 0.5 V; after the boot measurement a
// change needs kConfirm readings in a row.

#include <cstdint>

namespace doser::supply {

enum class State : uint8_t {
    None,  // board without sense input
    Ok,
    Low,
    Missing,
};

constexpr uint32_t kMissingMv = 12000;
constexpr uint32_t kLowMv = 20000;
constexpr uint32_t kHysteresisMv = 500;
constexpr int kConfirm = 3;

// State of one reading in mV; prev (Ok, Low, Missing) shifts the thresholds
// by the hysteresis, None = plain thresholds (boot measurement)
State classify(uint32_t mv, State prev = State::None);

const char* state_name(State s);  // "none", "ok", "low", "missing"

// Debounced state after the boot measurement
class Tracker {
public:
    explicit Tracker(State boot = State::None) : state_(boot) {}
    // New reading in mV; true = the state changed
    bool update(uint32_t mv);
    State state() const { return state_; }
    // The supply came back after it was missing (at boot or later): the
    // peripherals of the rail have to start again. Stays set.
    bool returned() const { return returned_; }

private:
    State state_;
    State pending_ = State::None;
    int count_ = 0;
    bool returned_ = false;
};

}  // namespace doser::supply
