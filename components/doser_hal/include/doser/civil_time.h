#pragma once
// Conversion calendar date <-> Unix time (UTC), independent of TZ/libc.
// Algorithm by H. Hinnant ("days_from_civil"). Header-only, host-tested.

#include <cstdint>

namespace doser::civil {

struct DateTime {
    int year;    // e.g. 2026
    int month;   // 1..12
    int day;     // 1..31
    int hour;    // 0..23
    int minute;  // 0..59
    int second;  // 0..59
    int weekday; // 1 = Monday .. 7 = Sunday (ISO)
};

constexpr int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

constexpr int64_t to_unix(const DateTime& t) {
    return days_from_civil(t.year, static_cast<unsigned>(t.month), static_cast<unsigned>(t.day)) * 86400 +
           t.hour * 3600 + t.minute * 60 + t.second;
}

constexpr DateTime from_unix(int64_t ts) {
    int64_t days = ts >= 0 ? ts / 86400 : (ts - 86399) / 86400;
    int64_t secs = ts - days * 86400;
    DateTime t{};
    t.hour = static_cast<int>(secs / 3600);
    t.minute = static_cast<int>((secs % 3600) / 60);
    t.second = static_cast<int>(secs % 60);
    // 1970-01-01 was a Thursday (ISO 4)
    t.weekday = static_cast<int>(((days % 7) + 7 + 3) % 7) + 1;
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    t.day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    t.month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    t.year = static_cast<int>(static_cast<int64_t>(yoe) + era * 400 + (t.month <= 2));
    return t;
}

}  // namespace doser::civil
