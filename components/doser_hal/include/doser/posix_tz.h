#pragma once
// POSIX TZ rule evaluation ("CET-1CEST,M3.5.0,M10.5.0/3") without libc state.
//
// The scheduler works in local wall time but must know exactly how a local
// time maps to UTC around DST changes (spec chapter 4). libc's TZ handling is
// global and not host-testable, so the rule is evaluated here directly.
// Supported: std/dst names (alphabetic or <quoted>), offsets [+-]hh[:mm[:ss]],
// rules Mm.w.d, Jn and n with optional /[+-]time (also outside 0..24 h).
// A DST zone without rules uses the US default (M3.2.0,M11.1.0).

#include <cstdint>

namespace doser::tz {

struct Rule {
    enum Kind : uint8_t { MonthWeekDay, Julian1, Julian0 } kind = MonthWeekDay;
    uint8_t month = 0;  // 1..12
    uint8_t week = 0;   // 1..5 (5 = last)
    uint8_t wday = 0;   // 0 = Sunday .. 6
    uint16_t day = 0;   // Jn: 1..365, n: 0..365
    int32_t time = 7200;  // seconds after local midnight
};

struct Zone {
    int32_t std_offset = 0;  // seconds to add to UTC for local standard time
    int32_t dst_offset = 0;
    bool has_dst = false;
    Rule start, end;
};

// false = not parseable (zone stays UTC)
bool parse(const char* s, Zone* z);

// Offset to add to UTC at the given instant
int32_t offset_at(const Zone& z, int64_t utc);
inline int64_t to_local(const Zone& z, int64_t utc) { return utc + offset_at(z, utc); }

enum class Mapping : uint8_t { Unique, Gap, Ambiguous };
// Local wall time (seconds since 1970 as if UTC) -> UTC.
//   Gap (spring forward): the time does not exist, the result is shifted
//   forward by the gap (02:30 -> 03:30 CEST).
//   Ambiguous (fall back): the first occurrence is returned.
int64_t to_utc(const Zone& z, int64_t local, Mapping* how = nullptr);

}  // namespace doser::tz
