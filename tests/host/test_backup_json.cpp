// Host tests for the full backup document (write -> parse round trip,
// secrets, partial documents and validation errors).
#include <cstdio>
#include <cstring>
#include <string>

#include "doser/backup_json.h"

using namespace doser;
namespace bk = doser::backup;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

static lgal::JsonToken g_tok[2048];
static const int32_t kToday = 20764;

static void sample(bk::Data* d) {
    *d = bk::Data{};
    d->parts = bk::kAllParts;
    std::strcpy(d->language, "de");
    std::strcpy(d->timezone, "CET-1CEST,M3.5.0,M10.5.0/3");
    std::strcpy(d->ntp_server, "pool.ntp.org");
    d->led = {true, 22 * 60 + 30, 6 * 60, 10};
    d->net.mode = lgal::netcfg::Mode::Auto;
    d->net.lan.dhcp = false;
    lgal::netcfg::parse_ipv4("192.168.1.50", &d->net.lan.ip);
    lgal::netcfg::parse_ipv4("255.255.255.0", &d->net.lan.netmask);
    lgal::netcfg::parse_ipv4("192.168.1.1", &d->net.lan.gateway);
    std::strcpy(d->net.ssid, "Reef");
    std::strcpy(d->net.password, "test-wlan-pw");
    d->wlan_password = true;
    d->mqtt.enabled = true;
    std::strcpy(d->mqtt.host, "broker.local");
    d->mqtt.tls = lgal::mqttcfg::Tls::Custom;
    std::strcpy(d->mqtt.username, "doser");
    std::strcpy(d->mqtt.password, "test-mqtt-pw");
    std::strcpy(d->mqtt.base_topic, "aq/doser");
    d->mqtt.control = true;
    d->mqtt_password = true;
    d->mqtt_ca = "-----BEGIN CERTIFICATE-----\nMIIBtest\n-----END CERTIFICATE-----\n";
    std::strcpy(d->pumps[0].name, "KH \"Balling\"");
    d->pumps[0].mode = dose::Mode::Auto;
    d->pumps[0].speed_tier = 2;
    d->pumps[0].ul_per_rev = 342.25f;
    d->pumps[0].retract_mrev = -1;
    d->pumps[0].daily_limit_ul = 80000;
    d->pumps[0].capacity_ul = 2000000;
    d->pumps[0].calibrated_at = 1793000000;
    d->pumps[3].mode = dose::Mode::Disabled;
    d->pumps[4].mode = dose::Mode::ManualOnly;
    auto& s = d->schedules[0];
    s.kind = sched::Kind::Spread;
    s.count = 12;
    s.start_min = 8 * 60;
    s.end_min = 20 * 60;
    s.amount_ul = 60000;
    auto& t = d->schedules[2];
    t.kind = sched::Kind::Times;
    t.time_count = 2;
    t.times[0] = {10 * 60, 0, 5000};
    t.times[1] = {22 * 60, 0, 2500};
    d->locks.count = 1;
    d->locks.rules[0].a = 0;
    d->locks.rules[0].b = 1;
    d->locks.rules[0].gap_min = 5;
    d->dosing.tier_rpm[2] = 450;
    d->dosing.limit_mode = dose::LimitMode::Rolling24h;
    d->scheduler.grid_min = 10;
    d->hw[1].driver.run_ma = 700;
    d->hw[1].driver.hold_ma = 100;
    d->hw[1].driver.invert = true;
    d->hw[1].driver.microsteps = 32;
}

static void fake_auth(lgal::Json& j) { j.kv("password", "00").kv("keys", "11"); }

static std::string doc_of(const bk::Data& d) {
    lgal::Json j;
    bk::write(j, d, {"1.0.0", "lgal-doser-test", 1794000000}, fake_auth);
    return j.str();
}

static bool parse_str(const std::string& s, const bk::Data& base, bk::Data* out, bk::Error* err, lgal::JsonReader* r) {
    if (!r->parse(s.c_str(), s.size(), g_tok, 2048)) return false;
    return bk::parse(r->root(), base, kToday, out, err);
}

static void replace(std::string* s, const char* from, const char* to) {
    const size_t p = s->find(from);
    if (p == std::string::npos) {
        std::printf("  (pattern not found: %s)\n", from);
        return;
    }
    s->replace(p, std::strlen(from), to);
}

static void test_round_trip() {
    std::printf("Round trip\n");
    static bk::Data d, base, out;
    sample(&d);
    const std::string doc = doc_of(d);
    CHECK(doc.find("\"format\":\"lgal-doser-backup\"") != std::string::npos);
    CHECK(doc.find("\"wlan_password\":\"test-wlan-pw\"") != std::string::npos);
    CHECK(doc.find("\"auth\":{\"password\":\"00\"") != std::string::npos);
    lgal::JsonReader r;
    bk::Error err;
    CHECK(parse_str(doc, base, &out, &err, &r));
    CHECK(out.parts == bk::kAllParts);
    CHECK(!std::strcmp(out.language, "de") && !std::strcmp(out.timezone, d.timezone) && !std::strcmp(out.ntp_server, "pool.ntp.org"));
    CHECK(out.led.enabled && out.led.start_min == 22 * 60 + 30 && out.led.end_min == 360 && out.led.brightness == 10);
    CHECK(!out.net.lan.dhcp && out.net.lan.ip == d.net.lan.ip && out.net.lan.gateway == d.net.lan.gateway);
    CHECK(!std::strcmp(out.net.ssid, "Reef") && !std::strcmp(out.net.password, "test-wlan-pw") && out.wlan_password);
    CHECK(out.mqtt.enabled && !std::strcmp(out.mqtt.host, "broker.local") && out.mqtt.tls == lgal::mqttcfg::Tls::Custom);
    CHECK(!std::strcmp(out.mqtt.username, "doser") && !std::strcmp(out.mqtt.password, "test-mqtt-pw") && out.mqtt_password);
    CHECK(!std::strcmp(out.mqtt.base_topic, "aq/doser") && out.mqtt.control && out.mqtt.discovery);
    CHECK(out.mqtt_ca == d.mqtt_ca);
    bool pumps_ok = true;
    for (int i = 0; i < 5; ++i) {
        const auto &a = d.pumps[i], &b = out.pumps[i];
        pumps_ok = pumps_ok && !std::strcmp(a.name, b.name) && a.mode == b.mode && a.speed_tier == b.speed_tier &&
                   a.retract_mrev == b.retract_mrev && a.daily_limit_ul == b.daily_limit_ul &&
                   a.capacity_ul == b.capacity_ul &&
                   a.calibrated_at == b.calibrated_at && a.ul_per_rev == b.ul_per_rev;
    }
    CHECK(pumps_ok);
    CHECK(out.schedules[0].kind == sched::Kind::Spread && out.schedules[0].count == 12 && out.schedules[0].amount_ul == 60000);
    CHECK(out.schedules[0].start_min == 480 && out.schedules[0].end_min == 1200);
    CHECK(out.schedules[2].kind == sched::Kind::Times && out.schedules[2].time_count == 2 && out.schedules[2].times[1].ul == 2500);
    CHECK(out.schedules[1].kind == sched::Kind::None);
    CHECK(out.locks.count == 1 && out.locks.rules[0].b == 1 && out.locks.rules[0].gap_min == 5);
    CHECK(out.dosing.tier_rpm[2] == 450 && out.dosing.limit_mode == dose::LimitMode::Rolling24h && out.scheduler.grid_min == 10);
    CHECK(out.hw[1].driver.run_ma == 700 && out.hw[1].driver.hold_ma == 100 && out.hw[1].driver.invert &&
          out.hw[1].driver.microsteps == 32 && out.hw[0].driver.run_ma == 600);
    // a second write gives the same document
    out.parts = bk::kAllParts;
    CHECK(doc_of(out) == doc);
}

static void test_parts_and_secrets() {
    std::printf("Parts and secrets\n");
    static bk::Data d, base, out;
    sample(&d);
    d.wlan_password = false;
    d.parts = bk::kSystem | bk::kNetwork;
    std::string doc = doc_of(d);
    CHECK(doc.find("test-wlan-pw") == std::string::npos && doc.find("\"auth\"") == std::string::npos);
    CHECK(doc.find("\"pumps\"") == std::string::npos && doc.find("\"engineering\"") == std::string::npos);
    lgal::JsonReader r;
    bk::Error err;
    // same network: the stored password stays
    std::strcpy(base.net.ssid, "Reef");
    std::strcpy(base.net.password, "stored-pw-123");
    std::strcpy(base.pumps[1].name, "untouched");
    CHECK(parse_str(doc, base, &out, &err, &r));
    CHECK(out.parts == (bk::kSystem | bk::kNetwork) && !out.wlan_password);
    CHECK(!std::strcmp(out.net.password, "stored-pw-123") && !std::strcmp(out.pumps[1].name, "untouched"));
    // other network without a password: the stored one does not belong to it
    std::strcpy(base.net.ssid, "OldNet");
    CHECK(parse_str(doc, base, &out, &err, &r));
    CHECK(!std::strcmp(out.net.ssid, "Reef") && out.net.password[0] == 0);

    // MQTT without password: kept for the same broker and user only
    sample(&d);
    d.mqtt_password = false;
    d.parts = bk::kMqtt;
    doc = doc_of(d);
    CHECK(doc.find("test-mqtt-pw") == std::string::npos && doc.find("\"ca_pem\":\"-----BEGIN") != std::string::npos);
    base = bk::Data{};
    std::strcpy(base.mqtt.host, "broker.local");
    std::strcpy(base.mqtt.username, "doser");
    std::strcpy(base.mqtt.password, "stored-mqtt");
    CHECK(parse_str(doc, base, &out, &err, &r));
    CHECK(out.parts == bk::kMqtt && !out.mqtt_password && !std::strcmp(out.mqtt.password, "stored-mqtt"));
    std::strcpy(base.mqtt.username, "other");
    CHECK(parse_str(doc, base, &out, &err, &r));
    CHECK(!std::strcmp(out.mqtt.username, "doser") && out.mqtt.password[0] == 0);
    // a document without certificate removes it
    d.mqtt_ca.clear();
    d.mqtt.tls = lgal::mqttcfg::Tls::Bundle;
    base.mqtt_ca = "x";
    CHECK(parse_str(doc_of(d), base, &out, &err, &r) && out.mqtt_ca.empty());

    uint8_t m = 0;
    CHECK(bk::parse_parts("system,pumps,mqtt,auth", &m) && m == (bk::kSystem | bk::kPumps | bk::kMqtt | bk::kAuth));
    CHECK(bk::parse_parts("system,pumps,auth", &m) && m == (bk::kSystem | bk::kPumps | bk::kAuth));
    CHECK(bk::parse_parts("", &m) && m == 0);
    CHECK(!bk::parse_parts("system,", &m) && !bk::parse_parts("sys", &m) && !bk::parse_parts("pumps,foo", &m));
    CHECK(!std::strcmp(bk::part_str(bk::kEngineering), "engineering"));
}

static void expect_error(const char* from, const char* to, const char* code, const char* field) {
    static bk::Data d, base, out;
    sample(&d);
    std::string doc = doc_of(d);
    replace(&doc, from, to);
    lgal::JsonReader r;
    bk::Error err;
    const bool ok = parse_str(doc, base, &out, &err, &r);
    CHECK(!ok);
    if (ok) return;
    const bool match = err.code && !std::strcmp(err.code, code) && (!field || !std::strcmp(err.field, field));
    if (!match) std::printf("  got %s / %s for %s\n", err.code ? err.code : "-", err.field, to);
    CHECK(match);
}

static void test_errors() {
    std::printf("Validation errors\n");
    expect_error("\"format\":\"lgal-doser-backup\"", "\"format\":\"other\"", "invalid_format", "format");
    expect_error("\"version\":1", "\"version\":2", "unsupported_version", "version");
    expect_error("\"version\":1", "\"version\":0", "invalid_format", "version");
    expect_error("\"brightness\":10", "\"brightness\":101", "invalid_param", "system.led_night.brightness");
    expect_error("\"start\":\"22:30\"", "\"start\":\"25:00\"", "invalid_param", "system.led_night.start");
    expect_error("\"language\":\"de\"", "\"language\":\"\"", "invalid_param", "system.language");
    expect_error("\"gateway\":\"192.168.1.1\"", "\"gateway\":\"10.0.0.1\"", "invalid_param", nullptr);
    expect_error("\"wlan_password\":\"test-wlan-pw\"", "\"wlan_password\":\"short\"", "invalid_param", "wlan.password");
    expect_error("\"tier\":2", "\"tier\":3", "invalid_param", "pumps[0].tier");
    expect_error("\"mode\":\"disabled\"", "\"mode\":\"sleeping\"", "invalid_param", "pumps[3].mode");
    expect_error("\"retract_mrev\":-1", "\"retract_mrev\":-2", "invalid_param", "pumps[0].retract_mrev");
    expect_error("\"pump\":2", "\"pump\":1", "invalid_param", "pumps[1].pump");
    expect_error("\"count\":12", "\"count\":0", "invalid_param", nullptr);
    expect_error("\"tier_rpm\":[100,200,450]", "\"tier_rpm\":[100,200,601]", "invalid_param", "engineering.dosing.tier_rpm");
    expect_error("\"grid_min\":10", "\"grid_min\":7", "invalid_param", nullptr);
    expect_error("\"run_ma\":700", "\"run_ma\":1600", "invalid_param", "engineering.hardware.pumps[1].run_ma");
    expect_error("\"hold_ma\":100", "\"hold_ma\":800", "invalid_param", "engineering.hardware.pumps[1].hold_ma");
    expect_error("\"microsteps\":32", "\"microsteps\":12", "invalid_param", "engineering.hardware.pumps[1].microsteps");
    expect_error("\"host\":\"broker.local\"", "\"host\":\"bro ker\"", "invalid_param", "mqtt.host");
    expect_error("\"base_topic\":\"aq/doser\"", "\"base_topic\":\"aq/#\"", "invalid_param", "mqtt.base_topic");
    expect_error("MIIBtest\\n-----END CERTIFICATE-----", "MIIBtest", "invalid_certificate", "mqtt.ca_pem");
    expect_error("\"ca_pem\":\"-----BEGIN CERTIFICATE-----\\nMIIBtest\\n-----END CERTIFICATE-----\\n\"", "\"ca_pem\":\"\"",
                 "ca_missing", "mqtt.tls");
}

int main() {
    test_round_trip();
    test_parts_and_secrets();
    test_errors();
    std::printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
