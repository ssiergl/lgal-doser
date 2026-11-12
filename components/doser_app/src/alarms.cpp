#include "doser/alarms.h"

namespace doser::alarms {

namespace {
bool low(const Pump& p) {
    return p.tracked && p.remaining_days >= 0.0f && p.remaining_days < p.low_days;
}
bool worn(const Pump& p) { return p.tube_percent >= 100.0f; }
}  // namespace

Output evaluate(const Input& in) {
    Output o;
    o.critical = !in.rtc_ok;
    o.warning = !in.clock_valid;
    for (const Pump& p : in.pumps) {
        if (!p.active) continue;
        if (p.fault || p.limit_hit_today) o.critical = true;
        if (low(p) || worn(p)) o.warning = true;
    }
    return o;
}

uint16_t conditions(const Input& in) {
    uint16_t m = in.rtc_ok ? 0 : uint16_t(1u << kRtcBit);
    for (int i = 0; i < kMaxPumps; ++i) {
        const Pump& p = in.pumps[i];
        if (!p.active) continue;
        if (low(p)) m |= cond_bit(i, Cond::ContainerLow);
        if (worn(p)) m |= cond_bit(i, Cond::TubeWorn);
    }
    return m;
}

const char* cond_code(Cond c) {
    switch (c) {
        case Cond::ContainerLow: return "container_low";
        case Cond::TubeWorn: return "tube_worn";
    }
    return "other";
}

}  // namespace doser::alarms
