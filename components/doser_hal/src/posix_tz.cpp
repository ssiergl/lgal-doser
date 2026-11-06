#include "doser/posix_tz.h"

#include <cctype>

#include "doser/civil_time.h"

namespace doser::tz {
namespace {

bool parse_name(const char*& p) {
    if (*p == '<') {
        const char* q = p + 1;
        while (*q && *q != '>') ++q;
        if (*q != '>' || q - p < 4) return false;
        p = q + 1;
        return true;
    }
    const char* q = p;
    while (std::isalpha(static_cast<unsigned char>(*q))) ++q;
    if (q - p < 3) return false;
    p = q;
    return true;
}

bool parse_num(const char*& p, int max, int* out) {
    if (!std::isdigit(static_cast<unsigned char>(*p))) return false;
    int v = 0;
    while (std::isdigit(static_cast<unsigned char>(*p))) {
        v = v * 10 + (*p++ - '0');
        if (v > max) return false;
    }
    *out = v;
    return true;
}

// [+-]hh[:mm[:ss]] in seconds; max_h 24 for offsets, 167 for rule times
bool parse_time(const char*& p, int max_h, int32_t* out) {
    int sign = 1;
    if (*p == '+' || *p == '-') sign = *p++ == '-' ? -1 : 1;
    int h = 0, m = 0, s = 0;
    if (!parse_num(p, max_h, &h)) return false;
    if (*p == ':') {
        ++p;
        if (!parse_num(p, 59, &m)) return false;
        if (*p == ':') {
            ++p;
            if (!parse_num(p, 59, &s)) return false;
        }
    }
    *out = sign * (h * 3600 + m * 60 + s);
    return true;
}

bool parse_rule(const char*& p, Rule* r) {
    int a = 0, b = 0, c = 0;
    if (*p == 'M') {
        ++p;
        if (!parse_num(p, 12, &a) || a < 1 || *p++ != '.') return false;
        if (!parse_num(p, 5, &b) || b < 1 || *p++ != '.') return false;
        if (!parse_num(p, 6, &c)) return false;
        r->kind = Rule::MonthWeekDay;
        r->month = static_cast<uint8_t>(a);
        r->week = static_cast<uint8_t>(b);
        r->wday = static_cast<uint8_t>(c);
    } else if (*p == 'J') {
        ++p;
        if (!parse_num(p, 365, &a) || a < 1) return false;
        r->kind = Rule::Julian1;
        r->day = static_cast<uint16_t>(a);
    } else {
        if (!parse_num(p, 365, &a)) return false;
        r->kind = Rule::Julian0;
        r->day = static_cast<uint16_t>(a);
    }
    r->time = 7200;
    if (*p == '/') {
        ++p;
        if (!parse_time(p, 167, &r->time)) return false;
    }
    return true;
}

bool leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

// Local wall time (as if UTC) at which the rule fires in the given year
int64_t rule_local(const Rule& r, int year) {
    int64_t day = 0;
    const int64_t jan1 = civil::days_from_civil(year, 1, 1);
    switch (r.kind) {
        case Rule::Julian1:  // Feb 29 is never counted
            day = jan1 + r.day - 1 + (leap(year) && r.day > 59 ? 1 : 0);
            break;
        case Rule::Julian0:
            day = jan1 + r.day;
            break;
        case Rule::MonthWeekDay: {
            const int64_t first = civil::days_from_civil(year, r.month, 1);
            // 1970-01-01 was a Thursday (wday 4)
            const int wd_first = static_cast<int>(((first % 7) + 7 + 4) % 7);
            day = first + (r.wday - wd_first + 7) % 7 + (r.week - 1) * 7;
            const int next_month_year = r.month == 12 ? year + 1 : year;
            const unsigned next_month = r.month == 12 ? 1 : r.month + 1u;
            const int64_t month_end = civil::days_from_civil(next_month_year, next_month, 1);
            while (day >= month_end) day -= 7;  // week 5 = last
            break;
        }
    }
    return day * 86400 + r.time;
}

}  // namespace

bool parse(const char* s, Zone* z) {
    Zone r;
    const char* p = s;
    if (!p || !parse_name(p)) return false;
    int32_t off = 0;
    if (!parse_time(p, 24, &off)) return false;
    r.std_offset = -off;  // POSIX: positive = west of Greenwich
    if (*p) {
        if (!parse_name(p)) return false;
        r.has_dst = true;
        r.dst_offset = r.std_offset + 3600;
        if (*p && *p != ',') {
            if (!parse_time(p, 24, &off)) return false;
            r.dst_offset = -off;
        }
        if (*p == ',') {
            ++p;
            if (!parse_rule(p, &r.start) || *p++ != ',' || !parse_rule(p, &r.end)) return false;
        } else {
            r.start = Rule{Rule::MonthWeekDay, 3, 2, 0, 0, 7200};
            r.end = Rule{Rule::MonthWeekDay, 11, 1, 0, 0, 7200};
        }
        if (*p) return false;
    }
    *z = r;
    return true;
}

int32_t offset_at(const Zone& z, int64_t utc) {
    if (!z.has_dst) return z.std_offset;
    const int year = civil::from_unix(utc + z.std_offset).year;
    // DST starts at a time given in standard time, ends at one given in DST
    const int64_t start = rule_local(z.start, year) - z.std_offset;
    const int64_t end = rule_local(z.end, year) - z.dst_offset;
    const bool dst = start < end ? (utc >= start && utc < end) : !(utc >= end && utc < start);
    return dst ? z.dst_offset : z.std_offset;
}

int64_t to_utc(const Zone& z, int64_t local, Mapping* how) {
    const int64_t u_std = local - z.std_offset;
    const int64_t u_dst = local - z.dst_offset;
    const bool std_ok = offset_at(z, u_std) == z.std_offset;
    const bool dst_ok = z.has_dst && offset_at(z, u_dst) == z.dst_offset;
    Mapping m = Mapping::Unique;
    int64_t r = u_std;
    if (std_ok && dst_ok && u_std != u_dst) {
        m = Mapping::Ambiguous;
        r = u_std < u_dst ? u_std : u_dst;
    } else if (dst_ok && !std_ok) {
        r = u_dst;
    } else if (!std_ok && !dst_ok) {
        // Gap: before the change the standard offset applied, so interpreting
        // the time with it lands the same distance after the change.
        m = Mapping::Gap;
        r = z.dst_offset > z.std_offset ? u_std : u_dst;
    }
    if (how) *how = m;
    return r;
}

}  // namespace doser::tz
