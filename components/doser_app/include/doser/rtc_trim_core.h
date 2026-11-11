#pragma once
// Automatic DS3231 aging trim from NTP (pure logic, host-testable).
//
// At every NTP synchronization the firmware measures the phase of the RTC
// second against the fresh system time (offset in ms, at the second edge).
// The first measurement is the anchor. Once at least a day has passed, the
// change of the offset gives the drift in ppm; the aging register (about
// 0.1 ppm per step, positive = slower) is corrected by that amount and a new
// anchor starts. In between the RTC is not rewritten, otherwise the drift
// could not be seen; it is only set when it is off by half a second or more.

#include <cstdint>

namespace doser::rtc_trim {

inline constexpr int64_t kMinSpanS = 86400;    // measure over at least a day
inline constexpr int32_t kSetOffsetMs = 500;   // rewrite the RTC from this offset on
inline constexpr int32_t kJumpMs = 2000;       // larger change since the anchor: start over
inline constexpr float kPpmPerStep = 0.1f;     // aging register, typical at 25 °C
inline constexpr float kMaxPpm = 20.0f;        // beyond the crystal tolerance: implausible
inline constexpr int kMaxStep = 30;            // largest correction per round

// Stored state (NVS)
struct State {
    uint8_t automatic = 1;   // trim switched on (factory setting)
    uint8_t ever_set = 0;    // the firmware has set the RTC at least once
    uint8_t anchored = 0;
    int8_t aging = 0;        // aging register when the anchor was taken
    int32_t anchor_ms = 0;   // RTC minus system time at the anchor
    int64_t anchor_utc = 0;
    int64_t trim_utc = 0;    // last correction, 0 = none yet
    float trim_ppm = 0.0f;   // drift measured for it
    int8_t trim_from = 0;
    int8_t trim_to = 0;
};

struct Decision {
    bool set_rtc = false;    // write the RTC (in phase) before anchoring
    bool anchor = false;     // take a new anchor
    bool trim = false;       // aging changes to `aging`
    int8_t aging = 0;
    float ppm = 0.0f;        // measured drift (valid with trim)
};

// offset_ms: RTC minus fresh NTP system time; aging: register now
Decision decide(const State& s, int64_t now, int32_t offset_ms, int8_t aging);
// Applies the decision; offset_ms = offset after a possible write
void apply(State* s, const Decision& d, int64_t now, int32_t offset_ms, int8_t aging_before);

}  // namespace doser::rtc_trim
