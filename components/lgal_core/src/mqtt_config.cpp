#include "lgal/mqtt_config.h"

#include <cstring>

#include "lgal/net_config.h"

namespace lgal::mqttcfg {

namespace {
bool fail(Error* err, const char* code, const char* field) {
    if (err) {
        err->code = code;
        err->field = field;
    }
    return false;
}

bool get_str(const JsonValue& v, char* out, size_t len) { return v.type() == JsonType::String && v.get(out, len); }
bool get_bool(const JsonValue& v, bool* out) { return v.type() == JsonType::Bool && v.get(out); }
}  // namespace

const char* tls_name(Tls t) {
    switch (t) {
        case Tls::Off: return "off";
        case Tls::Bundle: return "bundle";
        case Tls::Custom: return "custom";
    }
    return "off";
}

uint16_t port(const Config& c) {
    if (c.port) return c.port;
    return c.tls == Tls::Off ? 1883 : 8883;
}

bool valid_topic(const char* t, int max) {
    if (!t || !*t) return false;
    const size_t n = std::strlen(t);
    if (n > size_t(max) || t[0] == '/' || t[n - 1] == '/') return false;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = static_cast<unsigned char>(t[i]);
        if (c <= ' ' || c >= 0x7f || c == '+' || c == '#') return false;
        if (c == '/' && t[i + 1] == '/') return false;
    }
    return true;
}

bool valid_host(const char* h) {
    if (!h || !*h) return false;
    const size_t n = std::strlen(h);
    if (n > size_t(kHostMax)) return false;
    uint32_t ip;
    if (netcfg::parse_ipv4(h, &ip)) return true;
    // labels of 1..63 characters, no '-' at the start or end of a label
    size_t label = 0;
    bool digits_only = true;
    for (size_t i = 0; i <= n; ++i) {
        const char c = h[i];
        if (c == '.' || c == '\0') {
            if (label == 0 || label > 63 || h[i - 1] == '-') return false;
            label = 0;
            continue;
        }
        const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool digit = c >= '0' && c <= '9';
        if (!alpha && !digit && c != '-') return false;
        if (c == '-' && label == 0) return false;
        if (!digit) digits_only = false;
        ++label;
    }
    return !digits_only;  // "1.2.3" is a broken address, not a name
}

bool validate(const Config& c, bool ca_present, Error* err) {
    if (c.tls > Tls::Custom) return fail(err, "invalid_param", "tls");
    if (c.host[0] && !valid_host(c.host)) return fail(err, "invalid_param", "host");
    if (c.enabled && !c.host[0]) return fail(err, "invalid_param", "host");
    if (c.base_topic[0] && !valid_topic(c.base_topic, kTopicMax)) return fail(err, "invalid_param", "base_topic");
    if (!valid_topic(c.discovery_prefix, kPrefixMax)) return fail(err, "invalid_param", "discovery_prefix");
    if (std::strlen(c.username) > size_t(kUserMax)) return fail(err, "invalid_param", "username");
    if (std::strlen(c.password) > size_t(kPasswordMax)) return fail(err, "invalid_param", "password");
    if (c.password[0] && !c.username[0]) return fail(err, "invalid_param", "username");
    if (c.enabled && c.tls == Tls::Custom && !ca_present) return fail(err, "ca_missing", "tls");
    return true;
}

bool parse(const JsonValue& v, bool ca_present, Config* out, Error* err) {
    if (v.type() != JsonType::Object) return fail(err, "invalid_param", "body");
    Config c = *out;
    const JsonValue en = v["enabled"];
    if (en.valid() && !get_bool(en, &c.enabled)) return fail(err, "invalid_param", "enabled");
    const JsonValue host = v["host"];
    if (host.valid() && !get_str(host, c.host, sizeof(c.host))) return fail(err, "invalid_param", "host");
    const JsonValue p = v["port"];
    if (p.valid()) {
        int64_t n;
        if (p.type() != JsonType::Number || !p.get(&n) || n < 0 || n > 65535) return fail(err, "invalid_param", "port");
        c.port = static_cast<uint16_t>(n);
    }
    const JsonValue tls = v["tls"];
    if (tls.valid()) {
        if (tls.equals("off")) c.tls = Tls::Off;
        else if (tls.equals("bundle")) c.tls = Tls::Bundle;
        else if (tls.equals("custom")) c.tls = Tls::Custom;
        else return fail(err, "invalid_param", "tls");
    }
    const JsonValue user = v["username"];
    if (user.valid() && !get_str(user, c.username, sizeof(c.username))) return fail(err, "invalid_param", "username");
    const JsonValue pw = v["password"];
    if (pw.valid() && !get_str(pw, c.password, sizeof(c.password))) return fail(err, "invalid_param", "password");
    if (!c.username[0]) c.password[0] = 0;  // no user, no password
    const JsonValue base = v["base_topic"];
    if (base.valid() && !get_str(base, c.base_topic, sizeof(c.base_topic)))
        return fail(err, "invalid_param", "base_topic");
    const JsonValue disc = v["discovery"];
    if (disc.valid() && !get_bool(disc, &c.discovery)) return fail(err, "invalid_param", "discovery");
    const JsonValue prefix = v["discovery_prefix"];
    if (prefix.valid() && !get_str(prefix, c.discovery_prefix, sizeof(c.discovery_prefix)))
        return fail(err, "invalid_param", "discovery_prefix");
    const JsonValue ctl = v["control"];
    if (ctl.valid() && !get_bool(ctl, &c.control)) return fail(err, "invalid_param", "control");
    if (!validate(c, ca_present, err)) return false;
    *out = c;
    return true;
}

void write(Json& j, const Config& c) {
    j.kv("enabled", c.enabled).kv("host", c.host).kv("port", static_cast<int>(c.port));
    j.kv("tls", tls_name(c.tls)).kv("username", c.username).kv("password_set", c.password[0] != 0);
    j.kv("base_topic", c.base_topic).kv("discovery", c.discovery).kv("discovery_prefix", c.discovery_prefix);
    j.kv("control", c.control);
}

bool looks_like_pem(const char* s, unsigned len) {
    if (!s || len == 0 || len >= kCaMax) return false;
    for (unsigned i = 0; i < len; ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0 || (c < ' ' && c != '\n' && c != '\r' && c != '\t') || c >= 0x7f) return false;
    }
    static const char kBegin[] = "-----BEGIN CERTIFICATE-----";
    static const char kEnd[] = "-----END CERTIFICATE-----";
    const char* b = nullptr;
    for (unsigned i = 0; i + sizeof(kBegin) - 1 <= len; ++i)
        if (std::memcmp(s + i, kBegin, sizeof(kBegin) - 1) == 0) {
            b = s + i;
            break;
        }
    if (!b) return false;
    for (const char* q = b; q + sizeof(kEnd) - 1 <= s + len; ++q)
        if (std::memcmp(q, kEnd, sizeof(kEnd) - 1) == 0) return true;
    return false;
}

}  // namespace lgal::mqttcfg
