#include "doser/backup_json.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "doser/schedule_json.h"
#include "doser/tmc2209_proto.h"

namespace doser::backup {

using lgal::Json;
using lgal::JsonValue;

namespace {
constexpr const char* kParts[] = {"system", "network", "pumps", "engineering", "auth", "mqtt"};
constexpr int kPartCount = sizeof(kParts) / sizeof(kParts[0]);
constexpr uint16_t kMaxRpm = 600;

double ml(int64_t ul) { return static_cast<double>(ul) / 1000.0; }

bool fail(Error* err, const char* code, const char* field) {
    if (err) {
        err->code = code;
        std::snprintf(err->field, sizeof(err->field), "%s", field ? field : "");
    }
    return false;
}

bool failf(Error* err, const char* fmt, int index, const char* member) {
    if (err) {
        err->code = "invalid_param";
        std::snprintf(err->field, sizeof(err->field), fmt, index, member ? member : "");
    }
    return false;
}

bool get_bool(const JsonValue& v, bool* out) { return v.get(out); }

bool get_int(const JsonValue& v, int64_t lo, int64_t hi, int64_t* out) {
    return v.get(out) && *out >= lo && *out <= hi;
}

// amount in ml with up to three decimals -> µl
bool get_ul(const JsonValue& v, int64_t hi_ul, uint32_t* out) {
    int64_t x = 0;
    if (!v.get_decimal(1000, &x) || x < 0 || x > hi_ul) return false;
    *out = static_cast<uint32_t>(x);
    return true;
}

void write_hm(Json& j, const char* key, uint16_t minute) {
    char b[6];
    sched_json::fmt_hhmm(minute, b);
    j.kv(key, b);
}

// --- write -----------------------------------------------------------------

void write_system(Json& j, const Data& d) {
    j.obj("system").kv("language", d.language).kv("timezone", d.timezone).kv("ntp_server", d.ntp_server);
    j.obj("led_night").kv("enabled", d.led.enabled);
    write_hm(j, "start", d.led.start_min);
    write_hm(j, "end", d.led.end_min);
    j.kv("brightness", static_cast<int>(d.led.brightness)).end();
    j.end();
}

void write_network(Json& j, const Data& d) {
    j.obj("network");
    lgal::netcfg::write(j, d.net);
    if (d.wlan_password) j.kv("wlan_password", d.net.password);
    j.end();
}

void write_pumps(Json& j, const Data& d) {
    j.arr("pumps");
    for (int i = 0; i < 5; ++i) {
        const auto& p = d.pumps[i];
        j.obj().kv("pump", i + 1).kv("name", p.name).kv("mode", dose::mode_str(p.mode)).kv("tier", static_cast<int>(p.speed_tier));
        j.kv("daily_limit_ml", ml(p.daily_limit_ul), 3).kv("retract_mrev", static_cast<int>(p.retract_mrev));
        j.kv("capacity_ml", ml(p.capacity_ul), 3);
        j.kv("ul_per_rev", static_cast<double>(p.ul_per_rev), 3).kv("calibrated_at", p.calibrated_at);
        j.obj("schedule");
        sched_json::write_schedule(j, d.schedules[i]);
        j.end().end();
    }
    j.end();
    j.obj("locks");
    sched_json::write_locks(j, d.locks);
    j.end();
}

void write_engineering(Json& j, const Data& d) {
    const auto& e = d.dosing;
    j.obj("engineering").obj("dosing");
    j.kv("retract_mrev", static_cast<int>(e.retract_mrev)).kv("calib_ml", ml(e.calib_target_ul), 3);
    j.arr("tier_rpm");
    for (uint16_t r : e.tier_rpm) j.v(static_cast<int>(r));
    j.end();
    j.kv("prime_max_s", static_cast<int>(e.prime_max_s)).kv("prime_heartbeat_ms", static_cast<int>(e.prime_heartbeat_ms));
    j.kv("min_dose_ml", ml(e.min_dose_ul), 3).kv("tube_life_h", static_cast<int>(e.tube_life_h));
    j.kv("limit_mode", dose::limit_mode_str(e.limit_mode)).end();
    j.obj("scheduler").kv("grid_min", static_cast<int>(d.scheduler.grid_min));
    j.kv("max_delay_min", static_cast<int>(d.scheduler.max_delay_min)).end();
    j.obj("hardware").arr("pumps");
    for (const auto& c : d.hw) {
        j.obj().kv("run_ma", static_cast<int>(c.driver.run_ma)).kv("hold_ma", static_cast<int>(c.driver.hold_ma));
        j.kv("microsteps", static_cast<int>(c.driver.microsteps)).kv("invert", c.driver.invert);
        j.kv("full_steps", static_cast<int>(c.full_steps)).kv("accel", static_cast<int64_t>(c.accel));
        j.kv("v_start", static_cast<int64_t>(c.v_start)).end();
    }
    j.end().end().end();
}

// --- parse -----------------------------------------------------------------

bool parse_hm(const JsonValue& v, uint16_t* out) {
    char b[8];
    return v.get(b, sizeof(b)) && sched_json::parse_hhmm(b, out);
}

bool parse_system(const JsonValue& v, Data* d, Error* err) {
    if (!v["language"].get(d->language, sizeof(d->language)) || !d->language[0])
        return fail(err, "invalid_param", "system.language");
    if (!v["timezone"].get(d->timezone, sizeof(d->timezone)) || !d->timezone[0])
        return fail(err, "invalid_param", "system.timezone");
    if (!v["ntp_server"].get(d->ntp_server, sizeof(d->ntp_server))) return fail(err, "invalid_param", "system.ntp_server");
    const JsonValue n = v["led_night"];
    int64_t b = 0;
    if (!get_bool(n["enabled"], &d->led.enabled)) return fail(err, "invalid_param", "system.led_night.enabled");
    if (!parse_hm(n["start"], &d->led.start_min)) return fail(err, "invalid_param", "system.led_night.start");
    if (!parse_hm(n["end"], &d->led.end_min)) return fail(err, "invalid_param", "system.led_night.end");
    if (!get_int(n["brightness"], 0, 100, &b)) return fail(err, "invalid_param", "system.led_night.brightness");
    d->led.brightness = static_cast<uint8_t>(b);
    return true;
}

bool parse_network(const JsonValue& v, Data* d, Error* err) {
    char old_ssid[sizeof(d->net.ssid)];
    std::memcpy(old_ssid, d->net.ssid, sizeof(old_ssid));
    const JsonValue pw = v["wlan_password"];
    // without the password, keep the stored one only for the same network
    lgal::netcfg::Error ne;
    if (!lgal::netcfg::parse(v, &d->net, &ne)) return fail(err, ne.code ? ne.code : "invalid_param", ne.field);
    d->wlan_password = pw.valid();
    if (pw.valid()) {
        if (!pw.get(d->net.password, sizeof(d->net.password))) return fail(err, "invalid_param", "network.wlan_password");
        if (!d->net.ssid[0]) d->net.password[0] = 0;
        if (!lgal::netcfg::validate(d->net, &ne)) return fail(err, ne.code ? ne.code : "invalid_param", ne.field);
    } else if (std::strcmp(old_ssid, d->net.ssid) != 0) {
        d->net.password[0] = 0;
    }
    return true;
}

void write_mqtt(Json& j, const Data& d) {
    j.obj("mqtt");
    lgal::mqttcfg::write(j, d.mqtt);
    if (d.mqtt_password) j.kv("password", d.mqtt.password);
    j.kv("ca_pem", d.mqtt_ca).end();
}

bool parse_mqtt(const JsonValue& v, Data* d, Error* err) {
    if (v.type() != lgal::JsonType::Object) return fail(err, "invalid_param", "mqtt");
    const JsonValue ca = v["ca_pem"];
    if (ca.valid()) {
        std::string pem(lgal::mqttcfg::kCaMax, '\0');
        if (ca.type() != lgal::JsonType::String || !ca.get(pem.data(), pem.size()))
            return fail(err, "invalid_param", "mqtt.ca_pem");
        pem.resize(std::strlen(pem.c_str()));
        if (!pem.empty() && !lgal::mqttcfg::looks_like_pem(pem.data(), static_cast<unsigned>(pem.size())))
            return fail(err, "invalid_certificate", "mqtt.ca_pem");
        d->mqtt_ca = pem;
    }
    // without the password, keep the stored one only for the same broker and user
    const lgal::mqttcfg::Config old = d->mqtt;
    lgal::mqttcfg::Error me;
    if (!lgal::mqttcfg::parse(v, !d->mqtt_ca.empty(), &d->mqtt, &me)) {
        if (err) {
            err->code = me.code ? me.code : "invalid_param";
            std::snprintf(err->field, sizeof(err->field), "mqtt.%s", me.field ? me.field : "");
        }
        return false;
    }
    d->mqtt_password = v["password"].valid();
    if (!d->mqtt_password && (std::strcmp(old.host, d->mqtt.host) != 0 || std::strcmp(old.username, d->mqtt.username) != 0))
        d->mqtt.password[0] = 0;
    return true;
}

bool parse_pump(const JsonValue& v, int i, int32_t today, Data* d, Error* err) {
    auto& p = d->pumps[i];
    char buf[24];
    int64_t x = 0;
    p = dose::PumpSettings{};
    if (!v["name"].get(p.name, sizeof(p.name))) return failf(err, "pumps[%d].%s", i, "name");
    if (!v["mode"].get(buf, sizeof(buf)) || !dose::parse_mode(buf, &p.mode)) return failf(err, "pumps[%d].%s", i, "mode");
    if (!get_int(v["tier"], 0, dose::kTiers - 1, &x)) return failf(err, "pumps[%d].%s", i, "tier");
    p.speed_tier = static_cast<uint8_t>(x);
    if (!get_ul(v["daily_limit_ml"], 10000000, &p.daily_limit_ul)) return failf(err, "pumps[%d].%s", i, "daily_limit_ml");
    if (!get_int(v["retract_mrev"], -1, 2000, &x)) return failf(err, "pumps[%d].%s", i, "retract_mrev");
    p.retract_mrev = static_cast<int16_t>(x);
    if (!get_ul(v["capacity_ml"], 100000000, &p.capacity_ul)) return failf(err, "pumps[%d].%s", i, "capacity_ml");
    int64_t milli = 0;
    if (!v["ul_per_rev"].get_decimal(1000, &milli) || milli < 0 || milli > 100000000)
        return failf(err, "pumps[%d].%s", i, "ul_per_rev");
    p.ul_per_rev = static_cast<float>(milli) / 1000.0f;
    if (!v["calibrated_at"].get(&p.calibrated_at) || p.calibrated_at < 0) return failf(err, "pumps[%d].%s", i, "calibrated_at");
    sched_json::Error se;
    if (!sched_json::parse_schedule(v["schedule"], today, &d->schedules[i], &se)) {
        if (err) {
            err->code = se.code ? se.code : "invalid_param";
            std::snprintf(err->field, sizeof(err->field), "pumps[%d].schedule%s%s", i, se.field ? "." : "", se.field ? se.field : "");
        }
        return false;
    }
    return true;
}

bool parse_pumps(const JsonValue& root, int32_t today, Data* d, Error* err) {
    const JsonValue arr = root["pumps"];
    bool seen[5] = {};
    for (int k = 0; k < 5; ++k) {
        const JsonValue v = arr[k];
        int64_t n = 0;
        if (!v.valid() || !get_int(v["pump"], 1, 5, &n) || seen[n - 1]) return failf(err, "pumps[%d].%s", k, "pump");
        seen[n - 1] = true;
        if (!parse_pump(v, static_cast<int>(n - 1), today, d, err)) return false;
    }
    if (arr[5].valid()) return fail(err, "invalid_param", "pumps");
    sched_json::Error se;
    if (!sched_json::parse_locks(root["locks"], &d->locks, &se)) return fail(err, se.code ? se.code : "invalid_param", "locks");
    return true;
}

bool parse_engineering(const JsonValue& v, Data* d, Error* err) {
    auto& e = d->dosing;
    const JsonValue g = v["dosing"];
    int64_t x = 0;
    char buf[24];
    if (!get_int(g["retract_mrev"], 0, 2000, &x)) return fail(err, "invalid_param", "engineering.dosing.retract_mrev");
    e.retract_mrev = static_cast<uint16_t>(x);
    if (!get_ul(g["calib_ml"], 200000, &e.calib_target_ul)) return fail(err, "invalid_param", "engineering.dosing.calib_ml");
    for (int t = 0; t < dose::kTiers; ++t) {
        if (!get_int(g["tier_rpm"][t], 1, kMaxRpm, &x)) return fail(err, "invalid_param", "engineering.dosing.tier_rpm");
        e.tier_rpm[t] = static_cast<uint16_t>(x);
    }
    if (!get_int(g["prime_max_s"], 1, 3600, &x)) return fail(err, "invalid_param", "engineering.dosing.prime_max_s");
    e.prime_max_s = static_cast<uint16_t>(x);
    if (!get_int(g["prime_heartbeat_ms"], 1, 60000, &x)) return fail(err, "invalid_param", "engineering.dosing.prime_heartbeat_ms");
    e.prime_heartbeat_ms = static_cast<uint16_t>(x);
    if (!get_ul(g["min_dose_ml"], 1000000, &e.min_dose_ul)) return fail(err, "invalid_param", "engineering.dosing.min_dose_ml");
    if (!get_int(g["tube_life_h"], 0, 65535, &x)) return fail(err, "invalid_param", "engineering.dosing.tube_life_h");
    e.tube_life_h = static_cast<uint16_t>(x);
    if (!g["limit_mode"].get(buf, sizeof(buf)) || !dose::parse_limit_mode(buf, &e.limit_mode))
        return fail(err, "invalid_param", "engineering.dosing.limit_mode");
    if (const char* f = dose::validate(e, kMaxRpm)) {
        if (err) {
            err->code = "invalid_param";
            std::snprintf(err->field, sizeof(err->field), "engineering.dosing.%s", f);
        }
        return false;
    }

    const JsonValue s = v["scheduler"];
    if (!get_int(s["grid_min"], 1, 60, &x)) return fail(err, "invalid_param", "engineering.scheduler.grid_min");
    d->scheduler.grid_min = static_cast<uint16_t>(x);
    if (!get_int(s["max_delay_min"], 0, 1440, &x)) return fail(err, "invalid_param", "engineering.scheduler.max_delay_min");
    d->scheduler.max_delay_min = static_cast<uint16_t>(x);
    if (const char* f = sched::validate(d->scheduler)) {
        if (err) {
            err->code = "invalid_param";
            std::snprintf(err->field, sizeof(err->field), "engineering.scheduler.%s", f);
        }
        return false;
    }

    const JsonValue hp = v["hardware"]["pumps"];
    for (int i = 0; i < 5; ++i) {
        const JsonValue h = hp[i];
        auto& c = d->hw[i];
        if (!get_int(h["run_ma"], 50, 1500, &x)) return failf(err, "engineering.hardware.pumps[%d].%s", i, "run_ma");
        c.driver.run_ma = static_cast<uint16_t>(x);
        if (!get_int(h["hold_ma"], 0, c.driver.run_ma, &x)) return failf(err, "engineering.hardware.pumps[%d].%s", i, "hold_ma");
        c.driver.hold_ma = static_cast<uint16_t>(x);
        if (!get_int(h["microsteps"], 1, 256, &x) || tmc::mres_for(static_cast<uint16_t>(x)) == 0xFF)
            return failf(err, "engineering.hardware.pumps[%d].%s", i, "microsteps");
        c.driver.microsteps = static_cast<uint16_t>(x);
        if (!get_bool(h["invert"], &c.driver.invert)) return failf(err, "engineering.hardware.pumps[%d].%s", i, "invert");
        if (!get_int(h["full_steps"], 1, 1000, &x)) return failf(err, "engineering.hardware.pumps[%d].%s", i, "full_steps");
        c.full_steps = static_cast<uint16_t>(x);
        if (!get_int(h["accel"], 0, 10000000, &x)) return failf(err, "engineering.hardware.pumps[%d].%s", i, "accel");
        c.accel = static_cast<uint32_t>(x);
        if (!get_int(h["v_start"], 0, 1000000, &x)) return failf(err, "engineering.hardware.pumps[%d].%s", i, "v_start");
        c.v_start = static_cast<uint32_t>(x);
    }
    if (hp[5].valid()) return fail(err, "invalid_param", "engineering.hardware.pumps");
    return true;
}
}  // namespace

const char* part_str(Part p) {
    for (int i = 0; i < kPartCount; ++i)
        if (p == (1 << i)) return kParts[i];
    return "";
}

bool parse_parts(const char* s, uint8_t* out) {
    uint8_t m = 0;
    while (s && *s) {
        const char* comma = std::strchr(s, ',');
        const size_t n = comma ? size_t(comma - s) : std::strlen(s);
        int found = -1;
        for (int i = 0; i < kPartCount; ++i)
            if (std::strlen(kParts[i]) == n && std::strncmp(s, kParts[i], n) == 0) found = i;
        if (found < 0) return false;
        m = static_cast<uint8_t>(m | (1u << found));
        if (comma && !comma[1]) return false;
        s = comma ? comma + 1 : nullptr;
    }
    *out = m;
    return true;
}

void write(Json& j, const Data& d, const Meta& m, void (*write_auth)(Json&)) {
    j.obj().kv("format", kFormat).kv("version", kVersion).kv("firmware", m.firmware).kv("device", m.device);
    j.kv("exported_utc", m.utc);
    if (d.parts & kSystem) write_system(j, d);
    if (d.parts & kNetwork) write_network(j, d);
    if (d.parts & kPumps) write_pumps(j, d);
    if (d.parts & kEngineering) write_engineering(j, d);
    if (d.parts & kMqtt) write_mqtt(j, d);
    if ((d.parts & kAuth) && write_auth) {
        j.obj("auth");
        write_auth(j);
        j.end();
    }
    j.end();
}

bool parse(const JsonValue& v, const Data& base, int32_t today, Data* out, Error* err) {
    if (!v["format"].equals(kFormat)) return fail(err, "invalid_format", "format");
    int64_t ver = 0;
    if (!v["version"].get(&ver) || ver < 1) return fail(err, "invalid_format", "version");
    if (ver > kVersion) return fail(err, "unsupported_version", "version");
    *out = base;
    out->parts = 0;
    out->wlan_password = false;
    out->mqtt_password = false;
    if (v["system"].valid()) {
        if (!parse_system(v["system"], out, err)) return false;
        out->parts |= kSystem;
    }
    if (v["network"].valid()) {
        if (!parse_network(v["network"], out, err)) return false;
        out->parts |= kNetwork;
    }
    if (v["pumps"].valid() || v["locks"].valid()) {
        if (!parse_pumps(v, today, out, err)) return false;
        out->parts |= kPumps;
    }
    if (v["engineering"].valid()) {
        if (!parse_engineering(v["engineering"], out, err)) return false;
        out->parts |= kEngineering;
    }
    if (v["mqtt"].valid()) {
        if (!parse_mqtt(v["mqtt"], out, err)) return false;
        out->parts |= kMqtt;
    }
    if (v["auth"].valid()) out->parts |= kAuth;
    return true;
}

}  // namespace doser::backup
