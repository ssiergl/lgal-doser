#pragma once
// Time management (spec chapter 4): NTP > hardware RTC > manual input.
//
// * At startup the system time is set from the RTC (if valid).
// * Every NTP synchronization adjusts the RTC.
// * Without NTP the system time is corrected hourly from the (more accurate)
//   RTC.
// * Time zone as POSIX string -> DST handled autonomously, even offline.
// The RTC stores UTC.

#include <cstdint>
#include <functional>

namespace lgal::timesync {

struct RtcHooks {
    // true + utc if the RTC has a valid time
    std::function<bool(int64_t* utc)> read;
    std::function<bool(int64_t utc)> write;
};

enum class Source : uint8_t { None, Rtc, Ntp, Manual };

struct Status {
    bool valid;
    Source source;
    int64_t utc;
    int64_t last_ntp_sync;  // 0 = never
    bool rtc_ok;
};

void init(const RtcHooks& rtc, const char* tz_posix, const char* ntp_server);
Status status();
const char* source_str(Source s);
// Tertiary source (WebUI/browser): sets system and RTC
bool set_manual(int64_t utc);
const char* timezone();

}  // namespace lgal::timesync
