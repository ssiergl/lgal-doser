#pragma once
// Night mode of the status LED (functional spec chapter 10): dims or switches
// off the idle indication (prio 7) in a local time window. Alarms, warnings
// and running processes stay fully visible. Pure logic, host-tested.

#include <cstdint>

namespace lgal::led {

struct NightMode {
    bool enabled = false;
    uint16_t start_min = 22 * 60;  // local minute of the day the window starts
    uint16_t end_min = 7 * 60;     // ... and ends; before start = over midnight
    uint8_t brightness = 0;        // idle brightness in the window in percent, 0 = off
};

inline bool valid(const NightMode& n) { return n.start_min < 1440 && n.end_min < 1440 && n.brightness <= 100; }

// Local minute of the day inside [start, end)? start == end = never.
inline bool in_window(uint16_t start, uint16_t end, int minute) {
    if (start == end || minute < 0 || minute >= 1440) return false;
    return start < end ? (minute >= start && minute < end) : (minute >= start || minute < end);
}

// Idle brightness for a local minute of the day; minute < 0 = no valid time -> full
inline uint8_t idle_brightness(const NightMode& n, int minute) {
    return n.enabled && in_window(n.start_min, n.end_min, minute) ? n.brightness : 100;
}

}  // namespace lgal::led
