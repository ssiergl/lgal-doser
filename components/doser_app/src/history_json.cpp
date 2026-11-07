#include "doser/history_json.h"

#include <cstdio>
#include <cstring>

#include "doser/schedule_core.h"
#include "doser/schedule_json.h"

namespace doser::history_json {

using history::Event;
using history::Kind;

namespace {
double ml(int64_t ul) { return static_cast<double>(ul) / 1000.0; }
}  // namespace

void fmt_local_s(int64_t local, char (&out)[20]) {
    char m[17];
    sched_json::fmt_local(local, m);
    std::memcpy(out, m, 16);
    const int64_t s = local - sched::day_of(local) * int64_t(86400);
    const unsigned sec = static_cast<unsigned>(s % 60);
    out[16] = ':';
    out[17] = static_cast<char>('0' + sec / 10);
    out[18] = static_cast<char>('0' + sec % 10);
    out[19] = '\0';
}

void write_event(lgal::Json& j, const Event& e, int64_t local) {
    char loc[20];
    fmt_local_s(local, loc);
    j.obj().kv("utc", static_cast<int64_t>(e.utc)).kv("local", loc).kv("kind", history::kind_str(e.kind));
    j.kv("pump", static_cast<int>(e.pump));
    if (e.source != history::Source::None) j.kv("source", history::source_str(e.source));
    j.kv("code", history::code_str(e.code));
    switch (e.kind) {
        case Kind::Dose:
        case Kind::Prime:
        case Kind::CalibRun:
            j.kv("target_ml", ml(e.target_ul), 3).kv("done_ml", ml(e.done_ul), 3);
            break;
        case Kind::Skip:
            if (e.target_ul) j.kv("target_ml", ml(e.target_ul), 3);
            j.kv("count", static_cast<int64_t>(e.value));
            break;
        case Kind::Container:
            j.kv("remaining_ml", ml(e.value), 3).kv("capacity_ml", ml(e.target_ul), 3);
            break;
        case Kind::Calibration:
            j.kv("target_ml", ml(e.target_ul), 3).kv("measured_ml", ml(e.done_ul), 3).kv("ul_per_rev", e.value / 1000.0, 2);
            break;
        case Kind::Tube:
            j.kv("hours", static_cast<int64_t>(e.value));
            break;
        case Kind::Alarm:
            break;
    }
    j.end();
}

bool parse_kinds(const char* s, uint16_t* out) {
    uint16_t m = 0;
    while (s && *s) {
        const char* comma = std::strchr(s, ',');
        const size_t n = comma ? size_t(comma - s) : std::strlen(s);
        char word[16];
        if (n == 0 || n >= sizeof(word)) return false;
        std::memcpy(word, s, n);
        word[n] = '\0';
        Kind k;
        if (!history::parse_kind(word, &k)) return false;
        m = static_cast<uint16_t>(m | (1u << static_cast<uint8_t>(k)));
        if (comma && !comma[1]) return false;  // trailing comma
        s = comma ? comma + 1 : nullptr;
    }
    *out = m;
    return true;
}

const char* csv_header() { return "utc,local,kind,pump,source,code,target_ml,done_ml,value\r\n"; }

size_t csv_line(const Event& e, int64_t local, char* out, size_t len) {
    char loc[20];
    fmt_local_s(local, loc);
    char value[24] = "";
    switch (e.kind) {
        case Kind::Container: std::snprintf(value, sizeof(value), "%.3f", ml(e.value)); break;
        case Kind::Calibration: std::snprintf(value, sizeof(value), "%.2f", e.value / 1000.0); break;
        case Kind::Tube:
        case Kind::Skip: std::snprintf(value, sizeof(value), "%ld", long(e.value)); break;
        default: break;
    }
    const int n = std::snprintf(out, len, "%lu,%s,%s,%u,%s,%s,%.3f,%.3f,%s\r\n", static_cast<unsigned long>(e.utc), loc,
                                history::kind_str(e.kind), unsigned(e.pump), history::source_str(e.source),
                                history::code_str(e.code), ml(e.target_ul), ml(e.done_ul), value);
    return n > 0 && size_t(n) < len ? size_t(n) : 0;
}

void Daily::reset(int32_t today, int n_days) {
    *this = Daily{};
    days = n_days < 1 ? 1 : n_days > kMaxDays ? kMaxDays : n_days;
    first_day = today - days + 1;
}

void Daily::add(const Event& e, int64_t local) {
    if (e.pump < 1 || e.pump > 5) return;
    const int32_t i = sched::day_of(local) - first_day;
    if (i < 0 || i >= days) return;
    const int p = e.pump - 1;
    if (e.kind == Kind::Dose) {
        dosed_ul[i][p] += e.done_ul;
        if (doses[i][p] < UINT16_MAX) ++doses[i][p];
    } else if (e.kind == Kind::Skip) {
        const uint32_t n = e.value > 0 ? uint32_t(e.value) : 1;
        skipped[i][p] = static_cast<uint16_t>(skipped[i][p] + n > UINT16_MAX ? UINT16_MAX : skipped[i][p] + n);
    }
}

void Daily::write(lgal::Json& j) const {
    j.arr("days");
    for (int i = 0; i < days; ++i) {
        char d[11];
        sched_json::fmt_date(first_day + i, d);
        j.obj().kv("date", d);
        j.arr("ml");
        for (int p = 0; p < 5; ++p) j.v(ml(dosed_ul[i][p]), 3);
        j.end().arr("doses");
        for (int p = 0; p < 5; ++p) j.v(static_cast<int>(doses[i][p]));
        j.end().arr("skipped");
        for (int p = 0; p < 5; ++p) j.v(static_cast<int>(skipped[i][p]));
        j.end().end();
    }
    j.end();
}

}  // namespace doser::history_json
