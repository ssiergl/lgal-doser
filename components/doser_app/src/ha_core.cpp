#include "doser/ha_core.h"

#include <cstdio>
#include <cstring>

#include "doser/alarms.h"
#include "doser/civil_time.h"

namespace doser::ha {

namespace {

// Entity names (German / English)
struct Names {
    const char* pump;  // "Pumpe %d"
    const char* state;
    const char* total;
    const char* planned;
    const char* remaining;
    const char* days;
    const char* next;
    const char* tube;
    const char* schedule;
    const char* amount;
    const char* dose;
    const char* critical;
    const char* warning;
    const char* stop;
    const char* firmware;
};
constexpr Names kDe = {"Pumpe %d",       "Status",       "Dosiert gesamt", "Geplant heute", "Vorrat",
                       "Reicht noch",    "Nächste Dosierung", "Schlauch-Betriebsstunden",
                       "Zeitplan",       "Dosiermenge",  "Dosieren",       "Alarm",         "Warnung",
                       "Alle stoppen",   "Firmware"};
constexpr Names kEn = {"Pump %d",   "State",      "Total dosed", "Planned today",   "Container", "Days left",
                       "Next dose", "Tube hours", "Schedule", "Dose amount", "Dose",
                       "Alarm",     "Warning",    "Stop all", "Firmware"};

double ml(int64_t ul) { return static_cast<double>(ul) / 1000.0; }

class Writer {
public:
    Writer(lgal::Json& j, const DeviceInfo& d) : j_(j), d_(d) {}

    // Opens a component; returns false when it is written as a removal stub
    bool open(const std::string& key, const char* platform, bool active) {
        j_.obj((d_.id + "_" + key).c_str()).kv("platform", platform);
        if (!active) {
            j_.end();
            return false;
        }
        j_.kv("unique_id", d_.id + "_" + key);
        return true;
    }
    void name(const std::string& prefix, const char* what) { j_.kv("name", prefix.empty() ? what : prefix + " " + what); }
    lgal::Json& j() { return j_; }

private:
    lgal::Json& j_;
    const DeviceInfo& d_;
};

std::string pump_label(const DeviceInfo& d, const Names& n, int p) {
    if (!d.pump_names[p].empty()) return d.pump_names[p];
    char b[16];
    std::snprintf(b, sizeof(b), n.pump, p + 1);
    return b;
}

void write_pump(Writer& w, const DeviceInfo& d, const Names& n, int p) {
    const std::string label = pump_label(d, n, p);
    char k[8];
    std::snprintf(k, sizeof(k), "p%d_", p + 1);
    const std::string key = k;
    char sfx[24];
    std::snprintf(sfx, sizeof(sfx), "/pump/%d/", p + 1);
    const std::string t = d.base + sfx;
    const std::string state = t + "state";
    lgal::Json& j = w.j();
    const bool on = d.fitted[p];  // a pump that is not fitted has no entities

    if (w.open(key + "state", "sensor", on)) {
        w.name(label, n.state);
        j.kv("state_topic", state).kv("value_template", "{{ value_json.activity }}").kv("device_class", "enum");
        j.arr("options").v("idle").v("dosing").v("priming").v("calibrating").end();
        j.kv("icon", "mdi:pump").end();
    }
    if (w.open(key + "total", "sensor", on)) {
        w.name(label, n.total);
        j.kv("state_topic", state).kv("value_template", "{{ value_json.total_ml }}");
        j.kv("unit_of_measurement", "mL").kv("device_class", "volume").kv("state_class", "total_increasing");
        j.kv("suggested_display_precision", 1).end();
    }
    if (w.open(key + "planned", "sensor", on)) {
        w.name(label, n.planned);
        j.kv("state_topic", state).kv("value_template", "{{ value_json.planned_ml }}");
        j.kv("unit_of_measurement", "mL").kv("device_class", "volume").kv("state_class", "measurement");
        j.kv("suggested_display_precision", 1).end();
    }
    if (w.open(key + "remaining", "sensor", on)) {
        w.name(label, n.remaining);
        j.kv("state_topic", state).kv("value_template", "{{ value_json.remaining_ml }}");
        j.kv("unit_of_measurement", "mL").kv("device_class", "volume_storage").kv("state_class", "measurement");
        j.kv("suggested_display_precision", 0).end();
    }
    if (w.open(key + "days", "sensor", on)) {
        w.name(label, n.days);
        j.kv("state_topic", state).kv("value_template", "{{ value_json.days }}");
        j.kv("unit_of_measurement", "d").kv("device_class", "duration").kv("state_class", "measurement");
        j.kv("suggested_display_precision", 1).end();
    }
    if (w.open(key + "next", "sensor", on)) {
        w.name(label, n.next);
        j.kv("state_topic", state).kv("value_template", "{{ value_json.next }}").kv("device_class", "timestamp").end();
    }
    if (w.open(key + "tube", "sensor", on)) {
        w.name(label, n.tube);
        j.kv("state_topic", state).kv("value_template", "{{ value_json.tube_h }}");
        j.kv("unit_of_measurement", "h").kv("device_class", "duration").kv("state_class", "total_increasing");
        j.kv("entity_category", "diagnostic").kv("suggested_display_precision", 1).kv("icon", "mdi:pipe").end();
    }
    if (w.open(key + "schedule", "switch", on && d.control)) {
        w.name(label, n.schedule);
        j.kv("state_topic", state).kv("value_template", "{{ value_json.schedule }}");
        j.kv("command_topic", t + "schedule/set").kv("icon", "mdi:calendar-clock").end();
    }
    if (w.open(key + "amount", "number", on && d.control)) {
        w.name(label, n.amount);
        j.kv("state_topic", state).kv("value_template", "{{ value_json.amount_ml }}");
        j.kv("command_topic", t + "amount/set").kv("min", 0.01, 2).kv("max", 500.0, 1).kv("step", 0.01, 2);
        j.kv("mode", "box").kv("unit_of_measurement", "mL").end();
    }
    if (w.open(key + "dose", "button", on && d.control)) {
        w.name(label, n.dose);
        j.kv("command_topic", t + "dose/set").kv("payload_press", "PRESS").kv("icon", "mdi:water-plus").end();
    }
}

int match_prefix(const char* s, int len, const char* p) {
    const int n = static_cast<int>(std::strlen(p));
    return len >= n && std::memcmp(s, p, size_t(n)) == 0 ? n : -1;
}

bool equals(const char* s, int len, const char* word) {
    return len == static_cast<int>(std::strlen(word)) && std::memcmp(s, word, size_t(len)) == 0;
}

}  // namespace

std::string discovery_topic(const std::string& prefix, const std::string& id) {
    return prefix + "/device/" + id + "/config";
}

void write_discovery(lgal::Json& j, const DeviceInfo& d) {
    const Names& n = d.german ? kDe : kEn;
    j.obj();
    j.obj("device").arr("identifiers").v(d.id.c_str()).end();
    j.kv("name", d.name).kv("manufacturer", "LG Aquatic Life").kv("model", "LGAL-Doser");
    j.kv("sw_version", d.firmware);
    if (!d.url.empty()) j.kv("configuration_url", d.url);
    j.end();
    j.obj("origin").kv("name", "LGAL Doser").kv("sw_version", d.firmware).end();
    j.kv("availability_topic", d.base + "/status");
    j.obj("components");
    Writer w(j, d);
    for (int p = 0; p < kPumps; ++p) write_pump(w, d, n, p);
    const std::string state = d.base + "/state";
    if (w.open("critical", "binary_sensor", true)) {
        w.name("", n.critical);
        j.kv("state_topic", state).kv("value_template", "{{ 'ON' if value_json.critical else 'OFF' }}");
        j.kv("device_class", "problem").kv("json_attributes_topic", state);
        j.kv("json_attributes_template", "{{ {'conditions': value_json.conditions} | tojson }}").end();
    }
    if (w.open("warning", "binary_sensor", true)) {
        w.name("", n.warning);
        j.kv("state_topic", state).kv("value_template", "{{ 'ON' if value_json.warning else 'OFF' }}");
        j.kv("device_class", "problem").end();
    }
    if (w.open("stop", "button", d.control)) {
        w.name("", n.stop);
        j.kv("command_topic", d.base + "/stop/set").kv("payload_press", "PRESS").kv("icon", "mdi:stop-circle").end();
    }
    if (w.open("firmware", "update", d.update_source)) {
        w.name("", n.firmware);
        j.kv("device_class", "firmware").kv("entity_category", "config").kv("state_topic", d.base + "/update");
        // installing only with control; it always uses the fixed update source
        if (d.control) j.kv("command_topic", d.base + "/update/set").kv("payload_install", "install");
        j.end();
    }
    j.end().end();
}

void fmt_iso_utc(int64_t utc, char (&out)[26]) {
    const civil::DateTime t = civil::from_unix(utc);
    std::snprintf(out, sizeof(out), "%04u-%02u-%02uT%02u:%02u:%02u+00:00", unsigned(t.year) % 10000u,
                  unsigned(t.month) % 100u, unsigned(t.day) % 100u, unsigned(t.hour) % 100u, unsigned(t.minute) % 100u,
                  unsigned(t.second) % 100u);
}

void write_pump_state(lgal::Json& j, const PumpState& s) {
    j.obj().kv("activity", s.activity).kv("total_ml", ml(static_cast<int64_t>(s.total_ul)), 3);
    j.kv("planned_ml", ml(s.planned_ul), 3);
    if (s.tracked) j.kv("remaining_ml", ml(s.remaining_ul), 1);
    else j.null("remaining_ml");
    if (s.days >= 0) j.kv("days", static_cast<double>(s.days), 1);
    else j.null("days");
    if (s.has_next) {
        char b[26];
        fmt_iso_utc(s.next_utc, b);
        j.kv("next", b);
    } else {
        j.null("next");
    }
    j.kv("schedule", s.automatic ? "ON" : "OFF");
    j.kv("amount_ml", ml(s.amount_ul), 3).kv("tube_h", static_cast<double>(s.tube_h), 1);
    if (s.last) j.kv("last", s.last);
    else j.null("last");
    j.end();
}

void write_update_state(lgal::Json& j, const UpdateState& s) {
    j.obj().kv("installed_version", s.installed);
    j.kv("latest_version", s.available && !s.latest.empty() ? s.latest : s.installed);
    if (!s.notes.empty()) j.kv("release_url", s.notes);
    j.kv("in_progress", s.in_progress);
    if (s.in_progress && s.percent >= 0) j.kv("update_percentage", s.percent);
    else j.null("update_percentage");
    j.end();
}

void write_device_state(lgal::Json& j, const DeviceState& s) {
    j.obj().kv("critical", s.critical).kv("warning", s.warning).arr("conditions");
    if (s.conditions & (1u << alarms::kRtcBit)) j.v("rtc_fault");
    for (int p = 0; p < kPumps; ++p)
        for (int c = 0; c < alarms::kConds; ++c) {
            const auto cond = static_cast<alarms::Cond>(c);
            if (!(s.conditions & alarms::cond_bit(p, cond))) continue;
            char b[32];
            std::snprintf(b, sizeof(b), "pump%d_%s", p + 1, alarms::cond_code(cond));
            j.v(b);
        }
    j.end().end();
}

bool parse_ml(const char* s, int len, uint32_t* ul) {
    while (len > 0 && (*s == ' ')) ++s, --len;
    while (len > 0 && s[len - 1] == ' ') --len;
    if (len <= 0 || len > 16) return false;
    uint64_t v = 0;
    int i = 0, digits = 0, decimals = -1;
    for (; i < len; ++i) {
        const char c = s[i];
        if (c == '.' || c == ',') {
            if (decimals >= 0) return false;
            decimals = 0;
            continue;
        }
        if (c < '0' || c > '9') return false;
        if (decimals >= 0 && ++decimals > 3) return false;
        v = v * 10 + uint64_t(c - '0');
        ++digits;
        if (v > uint64_t(kMaxCommandUl) * 1000) return false;
    }
    if (!digits) return false;
    for (int d = decimals < 0 ? 0 : decimals; d < 3; ++d) v *= 10;
    if (v == 0 || v > kMaxCommandUl) return false;
    *ul = static_cast<uint32_t>(v);
    return true;
}

bool parse_command(const std::string& base, const char* topic, int topic_len, const char* data, int data_len,
                   Command* out) {
    if (!topic || topic_len <= 0 || base.empty()) return false;
    const int bl = static_cast<int>(base.size());
    if (topic_len <= bl + 1 || std::memcmp(topic, base.data(), size_t(bl)) != 0 || topic[bl] != '/') return false;
    const char* t = topic + bl + 1;
    int tl = topic_len - bl - 1;
    Command c;
    if (equals(t, tl, "stop/set")) {
        c.cmd = Cmd::Stop;
        *out = c;
        return true;
    }
    if (equals(t, tl, "update/set")) {
        if (!equals(data, data_len, "install")) return false;
        c.cmd = Cmd::Install;
        *out = c;
        return true;
    }
    const int pp = match_prefix(t, tl, "pump/");
    if (pp < 0 || tl < pp + 2 || t[pp] < '1' || t[pp] > '0' + kPumps || t[pp + 1] != '/') return false;
    c.pump = t[pp] - '1';
    t += pp + 2;
    tl -= pp + 2;
    if (equals(t, tl, "dose/set")) {
        c.cmd = Cmd::Dose;
        if (equals(data, data_len, "PRESS")) c.press = true;
        else if (!parse_ml(data, data_len, &c.ul)) return false;
    } else if (equals(t, tl, "amount/set")) {
        c.cmd = Cmd::Amount;
        if (!parse_ml(data, data_len, &c.ul)) return false;
    } else if (equals(t, tl, "schedule/set")) {
        c.cmd = Cmd::Schedule;
        if (equals(data, data_len, "ON")) c.on = true;
        else if (!equals(data, data_len, "OFF")) return false;
    } else {
        return false;
    }
    *out = c;
    return true;
}

}  // namespace doser::ha
