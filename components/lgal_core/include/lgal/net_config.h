#pragma once
// Network settings and LAN/WLAN selection (spec 2). No ESP dependencies
// (host tests: tests/host/test_net_config.cpp).
//
// JSON form (GET/PUT /api/v1/net/settings, see netapi.h):
//   {"mode":"auto|lan|wlan",
//    "lan": {"dhcp":true,"ip":"192.168.1.50","netmask":"255.255.255.0",
//            "gateway":"192.168.1.1","dns1":"192.168.1.1","dns2":""},
//    "wlan":{"ssid":"Reef","password":"...","dhcp":true, ...same IP fields}}
// PUT accepts partial documents: missing members keep their value. The
// Wi-Fi password is never sent back ("password_set" instead); an omitted
// password keeps the stored one. Empty address strings mean "none".

#include <cstdint>

#include "lgal/json.h"
#include "lgal/json_reader.h"

namespace lgal::netcfg {

// IPv4 address in host byte order: "a.b.c.d" = a<<24 | b<<16 | c<<8 | d
bool parse_ipv4(const char* s, uint32_t* out);  // strict dotted decimal
void fmt_ipv4(uint32_t ip, char (&out)[16]);
// Contiguous mask with prefix /8../30
bool valid_netmask(uint32_t mask);

struct IpConfig {
    bool dhcp = true;
    uint32_t ip = 0;
    uint32_t netmask = 0;
    uint32_t gateway = 0;  // 0 = none
    uint32_t dns1 = 0;     // 0 = none
    uint32_t dns2 = 0;
};

// auto: LAN preferred, WLAN after the LAN has been down for kFallbackMs
// and off again as soon as the LAN is back. lan/wlan: only that interface.
enum class Mode : uint8_t { Auto = 0, Lan = 1, Wlan = 2 };
const char* mode_name(Mode m);

inline constexpr int kSsidMax = 32;
inline constexpr int kPasswordMax = 63;  // WPA2 passphrase 8..63, empty = open network

struct Config {
    Mode mode = Mode::Auto;
    IpConfig lan;
    IpConfig wlan;
    char ssid[kSsidMax + 1] = "";
    char password[kPasswordMax + 1] = "";
};

struct Error {
    const char* code = nullptr;   // "invalid_param", "wlan_not_configured"
    const char* field = nullptr;  // e.g. "lan.gateway"
};

// prefix: "lan" or "wlan" for the field name in errors
bool validate(const IpConfig& c, const char* prefix, Error* err);
bool validate(const Config& c, Error* err);
bool wlan_configured(const Config& c);

// IP members of an "lan"/"wlan" object ("dhcp", "ip", ...) onto *c, without
// validation. Missing members keep their value, v may be invalid (= nothing).
bool parse_ip(const JsonValue& v, const char* prefix, IpConfig* c, Error* err);
// Applies the members present in v onto *out (start with the current
// configuration) and validates the result.
bool parse(const JsonValue& v, Config* out, Error* err);
// Members into the currently open object; never contains the password
void write(Json& j, const Config& c);
void write_ip(Json& j, const IpConfig& c);

inline constexpr int64_t kFallbackMs = 30000;

// Decides which interfaces run. Call regularly with the current state.
class Policy {
public:
    struct Input {
        bool eth_present = false;  // W5500 answered at boot
        bool lan_up = false;       // LAN link with an IP address
        bool wlan_configured = false;
    };
    struct Output {
        bool lan = false;   // Ethernet driver running
        bool wlan = false;  // Wi-Fi client wanted
    };
    Output update(Mode mode, const Input& in, int64_t now_ms);
    // ms the LAN has been down (auto mode), 0 when up
    int64_t lan_down_ms(int64_t now_ms) const;

private:
    int64_t lan_down_since_ = -1;
    bool wlan_ = false;
};

}  // namespace lgal::netcfg
