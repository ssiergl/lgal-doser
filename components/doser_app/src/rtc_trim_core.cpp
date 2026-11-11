#include "doser/rtc_trim_core.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace doser::rtc_trim {

Decision decide(const State& s, int64_t now, int32_t offset_ms, int8_t aging) {
    Decision d;
    d.aging = aging;
    const bool far = std::abs(offset_ms) >= kSetOffsetMs;
    d.set_rtc = far || !s.ever_set;
    // no anchor yet, aging changed by hand or the RTC was set meanwhile: start over
    if (!s.anchored || s.aging != aging || std::abs(offset_ms - s.anchor_ms) > kJumpMs || now < s.anchor_utc) {
        d.anchor = true;
        return d;
    }
    const int64_t span = now - s.anchor_utc;
    if (span < kMinSpanS) {
        d.set_rtc = false;  // keep measuring (a day drifts well below kSetOffsetMs)
        return d;
    }
    d.anchor = true;
    d.ppm = static_cast<float>(offset_ms - s.anchor_ms) * 1000.0f / static_cast<float>(span);
    if (std::fabs(d.ppm) > kMaxPpm) return d;  // implausible: only start over
    const int step = std::clamp(static_cast<int>(std::lround(d.ppm / kPpmPerStep)), -kMaxStep, kMaxStep);
    d.trim = true;
    d.aging = static_cast<int8_t>(std::clamp(aging + step, -128, 127));  // RTC fast (ppm > 0) -> slower
    return d;
}

void apply(State* s, const Decision& d, int64_t now, int32_t offset_ms, int8_t aging_before) {
    if (d.set_rtc) s->ever_set = 1;
    if (d.trim) {
        s->trim_utc = now;
        s->trim_ppm = d.ppm;
        s->trim_from = aging_before;
        s->trim_to = d.aging;
    }
    if (d.anchor) {
        s->anchored = 1;
        s->aging = d.aging;
        s->anchor_ms = offset_ms;
        s->anchor_utc = now;
    }
}

}  // namespace doser::rtc_trim
