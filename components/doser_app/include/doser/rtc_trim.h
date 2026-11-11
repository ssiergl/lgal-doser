#pragma once
// RTC upkeep on the device: keeps the DS3231 in phase with NTP and trims its
// aging register automatically (logic in rtc_trim_core.h, state in NVS "hw").
// Every RTC write of the time synchronization goes through here.

#include <cstdint>

#include "doser/rtc_trim_core.h"
#include "esp_err.h"

namespace doser::rtc_trim {

void init();
// Fresh NTP time in the system clock: measure, trim, set if needed (async)
void on_ntp();
// Time set by hand: write the RTC and start over (async)
void on_manual();
// The RTC lost its time while the device was off although the firmware had
// set it before: the backup battery is empty or missing
bool battery_warning();

struct Status {
    State state;
    int32_t offset_ms;     // last measurement, RTC minus system time
    int64_t measured_at;   // 0 = not measured since start
};
Status status();
esp_err_t set_automatic(bool on);

}  // namespace doser::rtc_trim
