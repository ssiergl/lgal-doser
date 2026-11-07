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

#include "esp_err.h"
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

// Time zone and NTP server come from NVS ("lgal": "tz", "ntp"); the given
// values are the defaults until they are set. tz_valid checks POSIX TZ
// strings (the application provides the parser).
void init(const RtcHooks& rtc, const char* default_tz, const char* default_ntp, bool (*tz_valid)(const char*));
Status status();
const char* source_str(Source s);
// Tertiary source (WebUI/browser): sets system and RTC
bool set_manual(int64_t utc);

inline constexpr int kMaxTz = 63;
inline constexpr int kMaxNtp = 63;
// Current POSIX TZ string. The pointer stays valid; a later change
// switches to a second buffer, so readers never see a half-written string.
const char* timezone();
const char* ntp_server();  // "" = NTP off
bool timezone_valid(const char* tz);
// Host name or IPv4 address, "" = off
bool ntp_server_valid(const char* host);
// Validate, store and apply (ESP_ERR_INVALID_ARG if invalid)
esp_err_t set_timezone(const char* tz);
esp_err_t set_ntp_server(const char* host);

}  // namespace lgal::timesync
