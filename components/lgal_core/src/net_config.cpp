#include "lgal/net_config.h"

#include <cstdio>
#include <cstring>

namespace lgal::netcfg {

bool parse_ipv4(const char* s, uint32_t* out) {
    if (!s) return false;
    uint32_t ip = 0;
    for (int part = 0; part < 4; ++part) {
        if (part > 0 && *s++ != '.') return false;
        if (*s < '0' || *s > '9') return false;
        if (s[0] == '0' && s[1] >= '0' && s[1] <= '9') return false;  // no leading zeros
        unsigned v = 0;
        int digits = 0;
        while (*s >= '0' && *s <= '9') {
            if (++digits > 3) return false;
            v = v * 10 + unsigned(*s++ - '0');
        }
        if (v > 255) return false;
        ip = (ip << 8) | v;
    }
    if (*s) return false;
    *out = ip;
    return true;
}

void fmt_ipv4(uint32_t ip, char (&out)[16]) {
    std::snprintf(out, sizeof(out), "%u.%u.%u.%u", unsigned(ip >> 24), unsigned((ip >> 16) & 255),
                  unsigned((ip >> 8) & 255), unsigned(ip & 255));
}

bool valid_netmask(uint32_t mask) {
    const uint32_t inv = ~mask;
    if (inv & (inv + 1)) return false;  // not contiguous
    int prefix = 0;
    for (uint32_t m = mask; m & 0x80000000u; m <<= 1) ++prefix;
    return prefix >= 8 && prefix <= 30;
}

const char* mode_name(Mode m) {
    switch (m) {
        case Mode::Lan: return "lan";
        case Mode::Wlan: return "wlan";
        default: return "auto";
    }
}

namespace {

// Unicast address usable by a host or server
bool unicast(uint32_t ip) {
    const uint32_t a = ip >> 24;
    return a != 0 && a != 127 && a < 224;
}

bool fail(Error* err, const char* code, const char* field) {
    if (err) {
        err->code = code;
        err->field = field;
    }
    return false;
}

const char* field_name(const char* prefix, const char* name) {
    // fixed strings so the error can point into static storage
    static const char* const kLan[] = {"lan.ip", "lan.netmask", "lan.gateway", "lan.dns1", "lan.dns2", "lan.dhcp"};
    static const char* const kWlan[] = {"wlan.ip",   "wlan.netmask", "wlan.gateway",
                                        "wlan.dns1", "wlan.dns2",    "wlan.dhcp"};
    static const char* const kNames[] = {"ip", "netmask", "gateway", "dns1", "dns2", "dhcp"};
    const bool wlan = prefix && std::strcmp(prefix, "wlan") == 0;
    for (int i = 0; i < 6; ++i)
        if (std::strcmp(kNames[i], name) == 0) return wlan ? kWlan[i] : kLan[i];
    return name;
}

}  // namespace

bool validate(const IpConfig& c, const char* prefix, Error* err) {
    if (c.dhcp) return true;
    if (!unicast(c.ip)) return fail(err, "invalid_param", field_name(prefix, "ip"));
    if (!valid_netmask(c.netmask)) return fail(err, "invalid_param", field_name(prefix, "netmask"));
    const uint32_t net = c.ip & c.netmask, bcast = net | ~c.netmask;
    if (c.ip == net || c.ip == bcast) return fail(err, "invalid_param", field_name(prefix, "ip"));
    if (c.gateway) {
        if ((c.gateway & c.netmask) != net || c.gateway == net || c.gateway == bcast || c.gateway == c.ip)
            return fail(err, "invalid_param", field_name(prefix, "gateway"));
    }
    if (c.dns1 && !unicast(c.dns1)) return fail(err, "invalid_param", field_name(prefix, "dns1"));
    if (c.dns2 && !unicast(c.dns2)) return fail(err, "invalid_param", field_name(prefix, "dns2"));
    return true;
}

bool wlan_configured(const Config& c) { return c.ssid[0] != 0; }

bool validate(const Config& c, Error* err) {
    if (c.mode != Mode::Auto && c.mode != Mode::Lan && c.mode != Mode::Wlan)
        return fail(err, "invalid_param", "mode");
    if (!validate(c.lan, "lan", err) || !validate(c.wlan, "wlan", err)) return false;
    const size_t ssid = strnlen(c.ssid, sizeof(c.ssid)), pw = strnlen(c.password, sizeof(c.password));
    if (ssid > size_t(kSsidMax)) return fail(err, "invalid_param", "wlan.ssid");
    if (pw > size_t(kPasswordMax) || (pw > 0 && pw < 8)) return fail(err, "invalid_param", "wlan.password");
    for (size_t i = 0; i < pw; ++i)  // WPA2 passphrase: printable ASCII
        if (static_cast<unsigned char>(c.password[i]) < 0x20 || static_cast<unsigned char>(c.password[i]) > 0x7e)
            return fail(err, "invalid_param", "wlan.password");
    if (c.mode == Mode::Wlan && !wlan_configured(c)) return fail(err, "wlan_not_configured", "wlan.ssid");
    return true;
}

namespace {

bool parse_addr(const JsonValue& v, const char* field, uint32_t* out, Error* err) {
    if (!v.valid()) return true;  // keep
    if (v.is_null()) {
        *out = 0;
        return true;
    }
    char buf[16];
    if (!v.get(buf, sizeof(buf))) return fail(err, "invalid_param", field);
    if (!buf[0]) {
        *out = 0;
        return true;
    }
    return parse_ipv4(buf, out) || fail(err, "invalid_param", field);
}

}  // namespace

bool parse_ip(const JsonValue& v, const char* prefix, IpConfig* c, Error* err) {
    if (!v.valid()) return true;
    if (v.type() != JsonType::Object) return fail(err, "invalid_param", prefix);
    const JsonValue dhcp = v["dhcp"];
    if (dhcp.valid() && !dhcp.get(&c->dhcp)) return fail(err, "invalid_param", field_name(prefix, "dhcp"));
    return parse_addr(v["ip"], field_name(prefix, "ip"), &c->ip, err) &&
           parse_addr(v["netmask"], field_name(prefix, "netmask"), &c->netmask, err) &&
           parse_addr(v["gateway"], field_name(prefix, "gateway"), &c->gateway, err) &&
           parse_addr(v["dns1"], field_name(prefix, "dns1"), &c->dns1, err) &&
           parse_addr(v["dns2"], field_name(prefix, "dns2"), &c->dns2, err);
}

bool parse(const JsonValue& v, Config* out, Error* err) {
    if (v.type() != JsonType::Object) return fail(err, "invalid_param", "body");
    Config c = *out;
    const JsonValue mode = v["mode"];
    if (mode.valid()) {
        if (mode.equals("auto")) c.mode = Mode::Auto;
        else if (mode.equals("lan")) c.mode = Mode::Lan;
        else if (mode.equals("wlan")) c.mode = Mode::Wlan;
        else return fail(err, "invalid_param", "mode");
    }
    if (!parse_ip(v["lan"], "lan", &c.lan, err)) return false;
    const JsonValue w = v["wlan"];
    if (!parse_ip(w, "wlan", &c.wlan, err)) return false;
    if (w.valid()) {
        const JsonValue ssid = w["ssid"];
        if (ssid.valid() && !ssid.get(c.ssid, sizeof(c.ssid))) return fail(err, "invalid_param", "wlan.ssid");
        const JsonValue pw = w["password"];
        if (pw.valid() && !pw.get(c.password, sizeof(c.password)))
            return fail(err, "invalid_param", "wlan.password");
        if (!c.ssid[0]) c.password[0] = 0;  // no network, no password
    }
    if (!validate(c, err)) return false;
    *out = c;
    return true;
}

void write_ip(Json& j, const IpConfig& c) {
    char buf[16];
    j.kv("dhcp", c.dhcp);
    const struct {
        const char* key;
        uint32_t v;
    } f[] = {{"ip", c.ip}, {"netmask", c.netmask}, {"gateway", c.gateway}, {"dns1", c.dns1}, {"dns2", c.dns2}};
    for (const auto& x : f) {
        if (x.v) {
            fmt_ipv4(x.v, buf);
            j.kv(x.key, buf);
        } else {
            j.kv(x.key, "");
        }
    }
}

void write(Json& j, const Config& c) {
    j.kv("mode", mode_name(c.mode));
    j.obj("lan");
    write_ip(j, c.lan);
    j.end();
    j.obj("wlan").kv("ssid", c.ssid).kv("password_set", c.password[0] != 0);
    write_ip(j, c.wlan);
    j.end();
}

Policy::Output Policy::update(Mode mode, const Input& in, int64_t now_ms) {
    Output o;
    if (mode == Mode::Wlan) {
        lan_down_since_ = -1;
        wlan_ = in.wlan_configured;
        o.wlan = wlan_;
        return o;
    }
    o.lan = true;
    if (in.lan_up) {
        lan_down_since_ = -1;
    } else if (lan_down_since_ < 0) {
        lan_down_since_ = now_ms;
    }
    if (mode == Mode::Lan || !in.wlan_configured) {
        wlan_ = false;
    } else if (in.lan_up) {
        wlan_ = false;  // cable back: LAN has priority
    } else if (!in.eth_present || now_ms - lan_down_since_ >= kFallbackMs) {
        wlan_ = true;
    }
    o.wlan = wlan_;
    return o;
}

int64_t Policy::lan_down_ms(int64_t now_ms) const { return lan_down_since_ < 0 ? 0 : now_ms - lan_down_since_; }

}  // namespace lgal::netcfg
