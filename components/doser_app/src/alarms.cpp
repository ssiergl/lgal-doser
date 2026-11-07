#include "doser/alarms.h"

namespace doser::alarms {

Output evaluate(const Input& in) {
    Output o;
    o.critical = !in.rtc_ok;
    o.warning = !in.clock_valid;
    for (const Pump& p : in.pumps) {
        if (!p.active) continue;
        if (p.fault || p.limit_hit_today) o.critical = true;
        const bool low = p.tracked && (p.percent <= kLowPercent || (p.remaining_days >= 0.0f && p.remaining_days < kLowDays));
        if (low || p.tube_percent >= 100.0f) o.warning = true;
    }
    return o;
}

}  // namespace doser::alarms
