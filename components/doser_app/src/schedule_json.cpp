#include "doser/schedule_json.h"

#include <cstring>

namespace doser::sched_json {

namespace {
using lgal::Json;
using lgal::JsonType;
using lgal::JsonValue;

constexpr const char* kWeekdays[7] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};

bool fail(Error* e, const char* field, int entry = -1, const char* code = "invalid_param") {
    if (e) {
        e->code = code;
        e->field = field;
        e->entry = entry;
    }
    return false;
}

void put2(char* p, unsigned v) {
    p[0] = static_cast<char>('0' + v / 10 % 10);
    p[1] = static_cast<char>('0' + v % 10);
}

double ml(uint32_t ul) { return ul / 1000.0; }

// ml (decimal) -> µl, 1 µl .. UINT32_MAX
bool get_ul(const JsonValue& v, uint32_t* out) {
    int64_t ul = 0;
    if (!v.get_decimal(1000, &ul) || ul < 1 || ul > static_cast<int64_t>(UINT32_MAX)) return false;
    *out = static_cast<uint32_t>(ul);
    return true;
}

bool get_time(const JsonValue& v, uint16_t* minute) {
    char buf[8];
    return v.get(buf, sizeof(buf)) && parse_hhmm(buf, minute);
}

bool get_u16(const JsonValue& v, int64_t lo, int64_t hi, uint16_t* out) {
    int64_t n = 0;
    if (!v.get(&n) || n < lo || n > hi) return false;
    *out = static_cast<uint16_t>(n);
    return true;
}

bool parse_days(const JsonValue& v, int32_t today, sched::DayRule* d, Error* err) {
    *d = sched::DayRule{};
    if (!v.valid() || v.is_null()) return true;  // daily
    if (v.type() != JsonType::Object) return fail(err, "days");
    const JsonValue mode = v["mode"];
    if (!mode.valid() || mode.equals("daily")) return true;
    if (mode.equals("weekdays")) {
        const JsonValue list = v["weekdays"];
        if (list.type() != JsonType::Array) return fail(err, "weekdays");
        d->weekdays = 0;
        for (int i = 0; i < list.size(); ++i) {
            int k = 0;
            while (k < 7 && !list[i].equals(kWeekdays[k])) ++k;
            if (k == 7) return fail(err, "weekdays");
            d->weekdays |= static_cast<uint8_t>(1u << k);
        }
        return true;  // empty list -> invalid_days from validate
    }
    if (mode.equals("interval")) {
        d->mode = sched::DayMode::Interval;
        int64_t every = 0;
        if (!v["every"].get(&every) || every < 1 || every > 255) return fail(err, "every");
        d->interval = static_cast<uint8_t>(every);
        const JsonValue anchor = v["anchor"];
        if (anchor.valid() && !anchor.is_null()) {
            char buf[12];
            if (!anchor.get(buf, sizeof(buf)) || !parse_date(buf, &d->anchor_day)) return fail(err, "anchor");
        } else if (today >= 0) {
            d->anchor_day = today;
        } else {
            return fail(err, "anchor", -1, "time_invalid");
        }
        return true;
    }
    return fail(err, "mode");
}

}  // namespace

// ---------------------------------------------------------------------------
bool parse_schedule(const JsonValue& v, int32_t today, sched::PumpSchedule* out, Error* err) {
    sched::PumpSchedule s;
    if (v.type() != JsonType::Object) return fail(err, "body");
    char buf[20];
    const JsonValue kind = v["kind"];
    if (kind.valid() && (!kind.get(buf, sizeof(buf)) || !sched::parse_kind(buf, &s.kind))) return fail(err, "kind");
    const JsonValue en = v["enabled"];
    if (en.valid()) {
        bool b = true;
        if (!en.get(&b)) return fail(err, "enabled");
        s.enabled = b ? 1 : 0;
    }
    const JsonValue pl = v["power_loss"];
    if (pl.valid() && (!pl.get(buf, sizeof(buf)) || !sched::parse_power_loss(buf, &s.power_loss)))
        return fail(err, "power_loss");
    const JsonValue cu = v["catch_up_min"];
    if (cu.valid() && !get_u16(cu, 0, UINT16_MAX, &s.catch_up_min)) return fail(err, "catch_up_min");

    if (s.kind == sched::Kind::Spread) {
        if (!get_time(v["start"], &s.start_min)) return fail(err, "start");
        if (!get_u16(v["count"], 1, sched::kMaxSlotsPerDay, &s.count)) return fail(err, "count");
        if (s.count > 1 && !get_time(v["end"], &s.end_min)) return fail(err, "end");
        if (s.count == 1) s.end_min = s.start_min;
        if (!get_ul(v["ml"], &s.amount_ul)) return fail(err, "ml");
    } else if (s.kind == sched::Kind::Times) {
        const JsonValue list = v["times"];
        if (list.type() != JsonType::Array || list.size() < 1 || list.size() > sched::kMaxTimes) return fail(err, "times");
        for (int i = 0; i < list.size(); ++i) {
            const JsonValue e = list[i];
            sched::TimeDose t;
            if (e.type() != JsonType::Object || !get_time(e["time"], &t.minute)) return fail(err, "time", i);
            if (!get_ul(e["ml"], &t.ul)) return fail(err, "ml", i);
            s.times[i] = t;
        }
        s.time_count = static_cast<uint8_t>(list.size());
        // ascending by time (insertion sort); equal times stay and fail validation
        for (int i = 1; i < s.time_count; ++i)
            for (int k = i; k > 0 && s.times[k].minute < s.times[k - 1].minute; --k) {
                const sched::TimeDose t = s.times[k];
                s.times[k] = s.times[k - 1];
                s.times[k - 1] = t;
            }
    }
    if (!parse_days(v["days"], today, &s.days, err)) return false;
    *out = s;
    return true;
}

void write_schedule(Json& j, const sched::PumpSchedule& s) {
    char t[6];
    j.kv("kind", sched::kind_str(s.kind)).kv("enabled", s.enabled != 0);
    if (s.kind == sched::Kind::Spread) {
        fmt_hhmm(s.start_min, t);
        j.kv("start", t);
        fmt_hhmm(s.end_min, t);
        j.kv("end", t).kv("count", s.count).kv("ml", ml(s.amount_ul), 3);
    } else if (s.kind == sched::Kind::Times) {
        j.arr("times");
        for (int i = 0; i < s.time_count && i < sched::kMaxTimes; ++i) {
            fmt_hhmm(s.times[i].minute, t);
            j.obj().kv("time", t).kv("ml", ml(s.times[i].ul), 3).end();
        }
        j.end();
    }
    j.obj("days");
    if (s.days.mode == sched::DayMode::Interval) {
        char d[11];
        fmt_date(s.days.anchor_day, d);
        j.kv("mode", "interval").kv("every", s.days.interval).kv("anchor", d);
    } else if ((s.days.weekdays & 0x7F) == 0x7F) {
        j.kv("mode", "daily");
    } else {
        j.kv("mode", "weekdays").arr("weekdays");
        for (int k = 0; k < 7; ++k)
            if (s.days.weekdays & (1u << k)) j.v(kWeekdays[k]);
        j.end();
    }
    j.end();
    j.kv("power_loss", sched::power_loss_str(s.power_loss)).kv("catch_up_min", s.catch_up_min);
}

// ---------------------------------------------------------------------------
bool parse_locks(const JsonValue& v, sched::Locks* out, Error* err) {
    sched::Locks l;
    if (v.type() != JsonType::Object) return fail(err, "body");
    const JsonValue m = v["manual"];
    if (m.valid()) {
        char buf[12];
        if (!m.get(buf, sizeof(buf)) || !sched::parse_manual_during_lock(buf, &l.manual)) return fail(err, "manual");
    }
    const JsonValue rules = v["rules"];
    if (rules.valid()) {
        if (rules.type() != JsonType::Array || rules.size() > sched::kMaxLocks) return fail(err, "rules");
        for (int i = 0; i < rules.size(); ++i) {
            const JsonValue r = rules[i];
            const JsonValue pumps = r["pumps"];
            int64_t a = 0, b = 0;
            if (pumps.size() != 2 || !pumps[0].get(&a) || !pumps[1].get(&b) || a < 1 || a > sched::kPumps || b < 1 ||
                b > sched::kPumps) {
                fail(err, "pumps");
                if (err) err->rule = i;
                return false;
            }
            uint16_t gap = 0;
            if (!get_u16(r["gap_min"], 0, UINT16_MAX, &gap)) {
                fail(err, "gap_min");
                if (err) err->rule = i;
                return false;
            }
            l.rules[i] = sched::LockRule{static_cast<uint8_t>(a - 1), static_cast<uint8_t>(b - 1), gap};
        }
        l.count = static_cast<uint8_t>(rules.size());
    }
    *out = l;
    return true;
}

void write_locks(Json& j, const sched::Locks& l) {
    j.kv("manual", sched::manual_during_lock_str(l.manual)).arr("rules");
    for (int i = 0; i < l.count && i < sched::kMaxLocks; ++i) {
        const auto& r = l.rules[i];
        j.obj().arr("pumps").v(r.a + 1).v(r.b + 1).end().kv("gap_min", r.gap_min).end();
    }
    j.end();
}

void write_conflict(Json& j, const sched::Conflict& c) {
    char t[6];
    auto slot = [&](const char* key, const sched::SlotRef& s) {
        fmt_hhmm(s.minute, t);
        j.obj(key).kv("pump", s.pump + 1).kv("time", t).end();
    };
    j.obj().kv("code", c.code ? c.code : "conflict");
    slot("first", c.first);
    slot("second", c.second);
    j.kv("next_day", c.next_day != 0);
    if (c.gap_min) j.kv("gap_min", c.gap_min);
    j.end();
}

// ---------------------------------------------------------------------------
bool parse_hhmm(const char* s, uint16_t* minute) {
    auto dig = [](char c) { return c >= '0' && c <= '9'; };
    if (!s || std::strlen(s) != 5 || !dig(s[0]) || !dig(s[1]) || s[2] != ':' || !dig(s[3]) || !dig(s[4])) return false;
    const int h = (s[0] - '0') * 10 + (s[1] - '0');
    const int m = (s[3] - '0') * 10 + (s[4] - '0');
    if (h > 23 || m > 59) return false;
    *minute = static_cast<uint16_t>(h * 60 + m);
    return true;
}

void fmt_hhmm(uint16_t minute, char (&out)[6]) {
    minute %= sched::kMinutesPerDay;
    put2(out, minute / 60u);
    out[2] = ':';
    put2(out + 3, minute % 60u);
    out[5] = '\0';
}

int32_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

void civil_from_days(int32_t day, int* y, unsigned* m, unsigned* d) {
    const int32_t z = day + 719468;
    const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = static_cast<int>(yoe) + era * 400 + (*m <= 2);
}

bool parse_date(const char* s, int32_t* day) {
    if (!s || std::strlen(s) != 10 || s[4] != '-' || s[7] != '-') return false;
    int v[3] = {0, 0, 0};
    const int pos[3] = {0, 5, 8}, len[3] = {4, 2, 2};
    for (int k = 0; k < 3; ++k) {
        for (int i = 0; i < len[k]; ++i) {
            const char c = s[pos[k] + i];
            if (c < '0' || c > '9') return false;
            v[k] = v[k] * 10 + (c - '0');
        }
    }
    if (v[0] < 1970 || v[0] > 2199 || v[1] < 1 || v[1] > 12 || v[2] < 1 || v[2] > 31) return false;
    const int32_t n = days_from_civil(v[0], static_cast<unsigned>(v[1]), static_cast<unsigned>(v[2]));
    int y;
    unsigned m, d;
    civil_from_days(n, &y, &m, &d);
    if (y != v[0] || static_cast<int>(m) != v[1] || static_cast<int>(d) != v[2]) return false;  // e.g. 02-30
    *day = n;
    return true;
}

void fmt_date(int32_t day, char (&out)[11]) {
    int y;
    unsigned m, d;
    civil_from_days(day, &y, &m, &d);
    if (y < 0 || y > 9999) y = 0;
    put2(out, static_cast<unsigned>(y) / 100);
    put2(out + 2, static_cast<unsigned>(y) % 100);
    out[4] = '-';
    put2(out + 5, m);
    out[7] = '-';
    put2(out + 8, d);
    out[10] = '\0';
}

void fmt_local(int64_t local, char (&out)[17]) {
    const int32_t day = sched::day_of(local);
    char d[11];
    fmt_date(day, d);
    std::memcpy(out, d, 10);
    out[10] = ' ';
    char t[6];
    fmt_hhmm(static_cast<uint16_t>((local - int64_t(day) * 86400) / 60), t);
    std::memcpy(out + 11, t, 6);
}

}  // namespace doser::sched_json
