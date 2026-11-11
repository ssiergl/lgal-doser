#pragma once
// MQTT broker settings (M6). No ESP dependencies (host tests:
// tests/host/test_mqtt_config.cpp).
//
// JSON form (GET/PUT /api/v1/mqtt, see mqtt.h):
//   {"enabled":true,"host":"broker.local","port":1883,"tls":"off|bundle|custom",
//    "username":"doser","password":"...","base_topic":"lg-aquatic-life/doser/lgal-doser-eeff",
//    "discovery":true,"discovery_prefix":"homeassistant","control":false}
// PUT accepts partial documents: missing members keep their value. The
// password is never sent back ("password_set" instead); an omitted password
// keeps the stored one, "" clears it. port 0 = default (1883, TLS 8883),
// base_topic "" = kDefaultPrefix + device type + "/" + factory host name of the device
// (spec: lg-aquatic-life/<device-type>/<device-id>/...).

#include <cstdint>

#include "lgal/json.h"
#include "lgal/json_reader.h"

namespace lgal::mqttcfg {

// off: plain TCP; bundle: TLS, server checked against the built-in public CA
// list; custom: TLS, server checked against an uploaded CA certificate
enum class Tls : uint8_t { Off = 0, Bundle = 1, Custom = 2 };
const char* tls_name(Tls t);

inline constexpr int kHostMax = 64;
inline constexpr int kUserMax = 64;
inline constexpr int kPasswordMax = 64;
inline constexpr int kTopicMax = 64;
inline constexpr int kPrefixMax = 32;
inline constexpr unsigned kCaMax = 8192;  // PEM text incl. terminator
inline constexpr const char* kDefaultPrefix = "lg-aquatic-life/";  // LG Aquatic Life topic tree

struct Config {
    bool enabled = false;
    char host[kHostMax + 1] = "";
    uint16_t port = 0;
    Tls tls = Tls::Off;
    char username[kUserMax + 1] = "";
    char password[kPasswordMax + 1] = "";
    char base_topic[kTopicMax + 1] = "";
    bool discovery = true;  // Home Assistant MQTT discovery
    char discovery_prefix[kPrefixMax + 1] = "homeassistant";
    bool control = false;   // accept commands (dosing, schedule, stop)
};

uint16_t port(const Config& c);  // with default
// Topic for publishing: printable ASCII without '+', '#' and spaces, no
// empty level ("a//b", leading or trailing '/'), 1..max characters
bool valid_topic(const char* t, int max);
// Host name (letters, digits, '-', '.') or IPv4 address
bool valid_host(const char* h);

struct Error {
    const char* code = nullptr;   // "invalid_param", "ca_missing"
    const char* field = nullptr;  // e.g. "host"
};

// ca_present: a CA certificate is stored (required for tls "custom")
bool validate(const Config& c, bool ca_present, Error* err);
// Applies the members present in v onto *out (start with the current
// configuration) and validates the result.
bool parse(const JsonValue& v, bool ca_present, Config* out, Error* err);
// Members into the currently open object; never contains the password
void write(Json& j, const Config& c);

// Basic shape of a PEM certificate list (the device parses it with mbedtls):
// at least one BEGIN/END CERTIFICATE block, text only, below kCaMax
bool looks_like_pem(const char* s, unsigned len);

}  // namespace lgal::mqttcfg
