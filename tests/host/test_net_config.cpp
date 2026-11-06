// Host tests for the network settings (IPv4 parsing, validation, JSON,
// LAN/WLAN selection policy).
#include <cstdio>
#include <cstring>
#include <string>

#include "lgal/net_config.h"

using namespace lgal;
using namespace lgal::netcfg;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

static uint32_t ip(const char* s) {
    uint32_t v = 0xdeadbeef;
    return parse_ipv4(s, &v) ? v : 0xdeadbeef;
}

static std::string fmt(uint32_t v) {
    char b[16];
    fmt_ipv4(v, b);
    return b;
}

static void test_ipv4() {
    std::printf("Net config: IPv4\n");
    CHECK(ip("192.168.1.50") == 0xC0A80132u);
    CHECK(ip("0.0.0.0") == 0);
    CHECK(ip("255.255.255.255") == 0xFFFFFFFFu);
    CHECK(ip("10.0.0.1") == 0x0A000001u);
    const char* bad[] = {"",         "1.2.3",     "1.2.3.4.5", "256.1.1.1", "1.2.3.04", "01.2.3.4", "1..3.4",
                         "1.2.3.4 ", " 1.2.3.4",  "a.b.c.d",   "1.2.3.-4",  "1.2.3.",   ".1.2.3",   "1234.1.1.1",
                         "1.2.3.4x", "1.2.3.256", "1,2,3,4"};
    for (const char* s : bad) {
        uint32_t v = 7;
        CHECK(!parse_ipv4(s, &v));
        CHECK(v == 7);
    }
    CHECK(!parse_ipv4(nullptr, nullptr));
    CHECK(fmt(0xC0A80132u) == "192.168.1.50");
    CHECK(fmt(0) == "0.0.0.0");
    CHECK(fmt(0xFFFFFFFFu) == "255.255.255.255");
    // round trip over a spread of values
    for (uint64_t x = 0; x <= 0xFFFFFFFFull; x += 0x01010101ull * 3 + 7) {
        char b[16];
        fmt_ipv4(uint32_t(x), b);
        uint32_t back = 1;
        CHECK(parse_ipv4(b, &back) && back == uint32_t(x));
    }
}

static void test_netmask() {
    std::printf("Net config: netmask\n");
    CHECK(valid_netmask(ip("255.255.255.0")));
    CHECK(valid_netmask(ip("255.0.0.0")));
    CHECK(valid_netmask(ip("255.255.255.252")));
    CHECK(valid_netmask(ip("255.255.240.0")));
    CHECK(!valid_netmask(ip("255.255.255.254")));  // /31
    CHECK(!valid_netmask(ip("255.255.255.255")));  // /32
    CHECK(!valid_netmask(ip("254.0.0.0")));        // /7
    CHECK(!valid_netmask(0));
    CHECK(!valid_netmask(ip("255.0.255.0")));
    CHECK(!valid_netmask(ip("0.255.255.255")));
    int ok = 0;
    for (int p = 0; p <= 32; ++p) {
        const uint32_t m = p ? ~uint32_t(0) << (32 - p) : 0;
        if (valid_netmask(m)) ++ok;
    }
    CHECK(ok == 23);  // /8../30
}

static IpConfig stat(const char* a, const char* m, const char* g = "", const char* d1 = "", const char* d2 = "") {
    IpConfig c;
    c.dhcp = false;
    c.ip = *a ? ip(a) : 0;
    c.netmask = *m ? ip(m) : 0;
    c.gateway = *g ? ip(g) : 0;
    c.dns1 = *d1 ? ip(d1) : 0;
    c.dns2 = *d2 ? ip(d2) : 0;
    return c;
}

static const char* verr(const IpConfig& c, const char* prefix = "lan") {
    Error e;
    return validate(c, prefix, &e) ? "ok" : e.field;
}

static void test_validate_ip() {
    std::printf("Net config: IP validation\n");
    IpConfig dhcp;
    dhcp.ip = 0xFFFFFFFFu;  // ignored with DHCP
    CHECK(!std::strcmp(verr(dhcp), "ok"));
    CHECK(!std::strcmp(verr(stat("192.168.1.50", "255.255.255.0", "192.168.1.1", "192.168.1.1", "9.9.9.9")), "ok"));
    CHECK(!std::strcmp(verr(stat("192.168.1.50", "255.255.255.0")), "ok"));  // no gateway, no DNS
    CHECK(!std::strcmp(verr(stat("10.1.2.3", "255.0.0.0", "10.0.0.1")), "ok"));
    CHECK(!std::strcmp(verr(stat("", "255.255.255.0")), "lan.ip"));
    CHECK(!std::strcmp(verr(stat("127.0.0.5", "255.0.0.0")), "lan.ip"));
    CHECK(!std::strcmp(verr(stat("224.0.0.1", "255.255.255.0")), "lan.ip"));
    CHECK(!std::strcmp(verr(stat("192.168.1.0", "255.255.255.0")), "lan.ip"));    // network address
    CHECK(!std::strcmp(verr(stat("192.168.1.255", "255.255.255.0")), "lan.ip"));  // broadcast
    CHECK(!std::strcmp(verr(stat("192.168.1.50", "")), "lan.netmask"));
    CHECK(!std::strcmp(verr(stat("192.168.1.50", "255.255.0.255")), "lan.netmask"));
    CHECK(!std::strcmp(verr(stat("192.168.1.50", "255.255.255.0", "192.168.2.1")), "lan.gateway"));
    CHECK(!std::strcmp(verr(stat("192.168.1.50", "255.255.255.0", "192.168.1.50")), "lan.gateway"));
    CHECK(!std::strcmp(verr(stat("192.168.1.50", "255.255.255.0", "192.168.1.255")), "lan.gateway"));
    CHECK(!std::strcmp(verr(stat("192.168.1.50", "255.255.255.0", "192.168.1.0")), "lan.gateway"));
    CHECK(!std::strcmp(verr(stat("192.168.1.50", "255.255.255.0", "", "0.1.2.3")), "lan.dns1"));
    CHECK(!std::strcmp(verr(stat("192.168.1.50", "255.255.255.0", "", "", "239.1.1.1")), "lan.dns2"));
    CHECK(!std::strcmp(verr(stat("192.168.1.50", "", ""), "wlan"), "wlan.netmask"));
    // DNS outside the subnet is fine
    CHECK(!std::strcmp(verr(stat("192.168.1.50", "255.255.255.0", "192.168.1.1", "1.1.1.1")), "ok"));
}

static void test_validate_config() {
    std::printf("Net config: config validation\n");
    Config c;
    Error e;
    CHECK(validate(c, &e));  // factory default: auto, DHCP, no Wi-Fi
    CHECK(!wlan_configured(c));
    c.mode = Mode::Wlan;
    CHECK(!validate(c, &e) && !std::strcmp(e.code, "wlan_not_configured"));
    std::strcpy(c.ssid, "Reef");
    CHECK(validate(c, &e));  // open network
    std::strcpy(c.password, "short");
    CHECK(!validate(c, &e) && !std::strcmp(e.field, "wlan.password"));
    std::strcpy(c.password, "12345678");
    CHECK(validate(c, &e));
    c.password[3] = '\t';
    CHECK(!validate(c, &e) && !std::strcmp(e.field, "wlan.password"));
    std::memset(c.password, 'x', kPasswordMax);
    c.password[kPasswordMax] = 0;
    CHECK(validate(c, &e));
    c.mode = Mode(7);
    CHECK(!validate(c, &e) && !std::strcmp(e.field, "mode"));
    c.mode = Mode::Lan;
    c.wlan = stat("10.0.0.300", "255.0.0.0");
    c.wlan.ip = 0;
    CHECK(!validate(c, &e) && !std::strcmp(e.field, "wlan.ip"));
}

static bool parse_json(const char* text, Config* c, Error* e) {
    JsonToken tok[64];
    JsonReader doc;
    if (!doc.parse(text, std::strlen(text), tok, 64)) return false;
    return parse(doc.root(), c, e);
}

static std::string to_json(const Config& c) {
    Json j;
    j.obj();
    write(j, c);
    j.end();
    return j.str();
}

static void test_json() {
    std::printf("Net config: JSON\n");
    Config c;
    Error e;
    CHECK(to_json(c) ==
          "{\"mode\":\"auto\",\"lan\":{\"dhcp\":true,\"ip\":\"\",\"netmask\":\"\",\"gateway\":\"\",\"dns1\":\"\","
          "\"dns2\":\"\"},\"wlan\":{\"ssid\":\"\",\"password_set\":false,\"dhcp\":true,\"ip\":\"\",\"netmask\":\"\","
          "\"gateway\":\"\",\"dns1\":\"\",\"dns2\":\"\"}}");

    CHECK(parse_json("{\"lan\":{\"dhcp\":false,\"ip\":\"192.168.1.50\",\"netmask\":\"255.255.255.0\","
                     "\"gateway\":\"192.168.1.1\",\"dns1\":\"192.168.1.1\",\"dns2\":\"\"},"
                     "\"wlan\":{\"ssid\":\"Reef \\u00fc\",\"password\":\"secret-pass\"}}",
                     &c, &e));
    CHECK(!c.lan.dhcp && c.lan.ip == 0xC0A80132u && c.lan.gateway == 0xC0A80101u && c.lan.dns2 == 0);
    CHECK(!std::strcmp(c.ssid, "Reef \xc3\xbc") && !std::strcmp(c.password, "secret-pass"));
    const std::string out = to_json(c);
    CHECK(out.find("secret-pass") == std::string::npos);
    CHECK(out.find("\"password_set\":true") != std::string::npos);
    CHECK(out.find("\"ip\":\"192.168.1.50\"") != std::string::npos);

    // partial update keeps the rest, omitted password stays
    CHECK(parse_json("{\"mode\":\"wlan\",\"wlan\":{\"dhcp\":true}}", &c, &e));
    CHECK(c.mode == Mode::Wlan && !std::strcmp(c.password, "secret-pass") && c.lan.ip == 0xC0A80132u);
    // round trip of the written form (password_set is ignored on input)
    Config back;
    std::strcpy(back.password, "secret-pass");
    CHECK(parse_json(to_json(c).c_str(), &back, &e));
    CHECK(to_json(back) == to_json(c));
    CHECK(!std::strcmp(back.password, "secret-pass"));

    // back to DHCP: addresses are kept for the form, validation skipped
    CHECK(parse_json("{\"lan\":{\"dhcp\":true,\"gateway\":\"\"}}", &c, &e));
    CHECK(c.lan.dhcp && c.lan.ip == 0xC0A80132u && c.lan.gateway == 0);
    CHECK(parse_json("{\"lan\":{\"ip\":null}}", &c, &e) && c.lan.ip == 0);

    // empty SSID clears the password; then mode wlan is refused
    CHECK(parse_json("{\"mode\":\"auto\",\"wlan\":{\"ssid\":\"\"}}", &c, &e));
    CHECK(!c.ssid[0] && !c.password[0]);
    CHECK(!parse_json("{\"mode\":\"wlan\"}", &c, &e) && !std::strcmp(e.code, "wlan_not_configured"));

    // errors leave the configuration unchanged
    const std::string before = to_json(c);
    struct {
        const char* json;
        const char* field;
    } bad[] = {
        {"[]", "body"},
        {"{\"mode\":\"eth\"}", "mode"},
        {"{\"mode\":1}", "mode"},
        {"{\"lan\":true}", "lan"},
        {"{\"lan\":{\"dhcp\":\"no\"}}", "lan.dhcp"},
        {"{\"lan\":{\"dhcp\":false,\"ip\":\"192.168.1.50\",\"netmask\":\"\"}}", "lan.netmask"},
        {"{\"lan\":{\"ip\":\"192.168.1\"}}", "lan.ip"},
        {"{\"lan\":{\"gateway\":5}}", "lan.gateway"},
        {"{\"lan\":{\"dns1\":\"192.168.001.1\"}}", "lan.dns1"},
        {"{\"wlan\":{\"dns2\":\"x\"}}", "wlan.dns2"},
        {"{\"wlan\":{\"ssid\":\"123456789012345678901234567890123\"}}", "wlan.ssid"},
        {"{\"wlan\":{\"ssid\":7}}", "wlan.ssid"},
        {"{\"wlan\":{\"ssid\":\"Reef\",\"password\":\"1234567\"}}", "wlan.password"},
        {"{\"wlan\":{\"ssid\":\"Reef\",\"password\":\"1234567890123456789012345678901234567890123456789012345678901234\"}}",
         "wlan.password"},
        {"{\"wlan\":{\"ssid\":\"Reef\",\"password\":\"abc\\u0000defgh\"}}", "wlan.password"},
        {"{\"wlan\":{\"ssid\":\"Reef\",\"password\":\"\\u00fc1234567\"}}", "wlan.password"},
    };
    for (const auto& b : bad) {
        Error be;
        const bool ok = parse_json(b.json, &c, &be);
        CHECK(!ok);
        CHECK(be.field && !std::strcmp(be.field, b.field));
        if (!ok && be.field && std::strcmp(be.field, b.field)) std::printf("    %s -> %s\n", b.json, be.field);
    }
    CHECK(to_json(c) == before);
    // the SSID may hold 32 bytes and any UTF-8
    CHECK(parse_json("{\"wlan\":{\"ssid\":\"12345678901234567890123456789012\"}}", &c, &e));
    CHECK(std::strlen(c.ssid) == 32);
}

static void test_policy() {
    std::printf("Net config: LAN/WLAN policy\n");
    using In = Policy::Input;
    const In lan_up{true, true, true}, lan_down{true, false, true}, no_eth{false, false, true};
    {
        // auto: boot without LAN -> WLAN after 30 s
        Policy p;
        auto o = p.update(Mode::Auto, lan_down, 1000);
        CHECK(o.lan && !o.wlan);
        CHECK(!p.update(Mode::Auto, lan_down, 1000 + kFallbackMs - 1).wlan);
        CHECK(p.lan_down_ms(1000 + kFallbackMs - 1) == kFallbackMs - 1);
        o = p.update(Mode::Auto, lan_down, 1000 + kFallbackMs);
        CHECK(o.lan && o.wlan);
        // cable back -> WLAN off immediately
        o = p.update(Mode::Auto, lan_up, 40000);
        CHECK(o.lan && !o.wlan && p.lan_down_ms(40000) == 0);
        // short LAN drop does not start WLAN
        CHECK(!p.update(Mode::Auto, lan_down, 50000).wlan);
        CHECK(!p.update(Mode::Auto, lan_up, 60000).wlan);
        CHECK(!p.update(Mode::Auto, lan_down, 70000).wlan);
        CHECK(!p.update(Mode::Auto, lan_down, 70000 + kFallbackMs - 1).wlan);
        CHECK(p.update(Mode::Auto, lan_down, 70000 + kFallbackMs).wlan);
        // stays on while the LAN is down
        CHECK(p.update(Mode::Auto, lan_down, 500000).wlan);
    }
    {
        // auto without Wi-Fi settings: never WLAN
        Policy p;
        const In nocfg{true, false, false};
        CHECK(!p.update(Mode::Auto, nocfg, 0).wlan);
        CHECK(!p.update(Mode::Auto, nocfg, 10 * kFallbackMs).wlan);
        // configured later: the down time already counts
        CHECK(p.update(Mode::Auto, lan_down, 10 * kFallbackMs + 1).wlan);
    }
    {
        // no Ethernet hardware: WLAN at once
        Policy p;
        auto o = p.update(Mode::Auto, no_eth, 0);
        CHECK(o.wlan && o.lan);
    }
    {
        // fixed LAN: never WLAN
        Policy p;
        CHECK(!p.update(Mode::Lan, lan_down, 0).wlan);
        auto o = p.update(Mode::Lan, no_eth, 10 * kFallbackMs);
        CHECK(o.lan && !o.wlan);
    }
    {
        // fixed WLAN: Ethernet off, WLAN if configured
        Policy p;
        auto o = p.update(Mode::Wlan, lan_up, 0);
        CHECK(!o.lan && o.wlan);
        o = p.update(Mode::Wlan, In{true, false, false}, 1);
        CHECK(!o.lan && !o.wlan);
        // switching back to auto with the LAN down starts the timer anew
        o = p.update(Mode::Auto, lan_down, 100);
        CHECK(o.lan && !o.wlan);
        CHECK(p.update(Mode::Auto, lan_down, 100 + kFallbackMs).wlan);
        // auto -> lan switches WLAN off
        CHECK(!p.update(Mode::Lan, lan_down, 200000).wlan);
    }
}

int main() {
    test_ipv4();
    test_netmask();
    test_validate_ip();
    test_validate_config();
    test_json();
    test_policy();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
