// Host tests for the MQTT broker settings (validation, JSON, PEM check).
#include <cstdio>
#include <cstring>
#include <string>

#include "lgal/mqtt_config.h"

using namespace lgal;
using namespace lgal::mqttcfg;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

static bool same(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }

// Applies a JSON document onto *c; returns the error code or "" on success
static std::string apply(const char* text, Config* c, bool ca = false, const char** field = nullptr) {
    JsonReader r;
    JsonToken tok[64];
    if (!r.parse(text, std::strlen(text), tok, 64)) return "json";
    Error e;
    if (parse(r.root(), ca, c, &e)) return "";
    if (field) *field = e.field;
    return e.code ? e.code : "?";
}

static void test_topic_host() {
    std::printf("MQTT config: topics and hosts\n");
    CHECK(valid_topic("lgal-doser-eeff", kTopicMax));
    CHECK(valid_topic("home/aquarium/doser", kTopicMax));
    CHECK(valid_topic("a", kTopicMax));
    const char* bad_topics[] = {"", "/a", "a/", "a//b", "a+b", "a/#", "a b", "a\tb", "\x7f", "ä"};
    for (const char* t : bad_topics) CHECK(!valid_topic(t, kTopicMax));
    CHECK(!valid_topic(nullptr, kTopicMax));
    std::string longt(kTopicMax, 'x');
    CHECK(valid_topic(longt.c_str(), kTopicMax));
    longt += 'x';
    CHECK(!valid_topic(longt.c_str(), kTopicMax));

    const char* hosts[] = {"broker", "broker.local", "mqtt.example.com", "192.168.1.10", "a-b.c-d", "h1", "1a"};
    for (const char* h : hosts) CHECK(valid_host(h));
    const char* bad_hosts[] = {"",        "-a",          "a-",      "a..b",   ".a",       "a.",     "a_b",
                               "a b",     "1.2.3",       "256.1.1.1", "a/b",  "http://a", "a:1883", "ä.de"};
    for (const char* h : bad_hosts) CHECK(!valid_host(h));
    std::string label(63, 'a');
    CHECK(valid_host(label.c_str()));
    label += 'a';
    CHECK(!valid_host(label.c_str()));
}

static void test_defaults_and_port() {
    std::printf("MQTT config: defaults\n");
    Config c;
    CHECK(!c.enabled && c.tls == Tls::Off && c.discovery && !c.control);
    CHECK(same(c.discovery_prefix, "homeassistant"));
    CHECK(validate(c, false, nullptr));  // disabled without host is fine
    CHECK(port(c) == 1883);
    c.tls = Tls::Bundle;
    CHECK(port(c) == 8883);
    c.port = 1234;
    CHECK(port(c) == 1234);
    CHECK(same(tls_name(Tls::Off), "off") && same(tls_name(Tls::Bundle), "bundle") &&
          same(tls_name(Tls::Custom), "custom"));
}

static void test_parse() {
    std::printf("MQTT config: parse\n");
    Config c;
    CHECK(apply(R"({"enabled":true,"host":"broker.local","port":1884,"tls":"bundle","username":"doser",
                    "password":"secret","base_topic":"aq/doser","discovery":false,"discovery_prefix":"ha",
                    "control":true})",
                &c) == "");
    CHECK(c.enabled && same(c.host, "broker.local") && c.port == 1884 && c.tls == Tls::Bundle);
    CHECK(same(c.username, "doser") && same(c.password, "secret") && same(c.base_topic, "aq/doser"));
    CHECK(!c.discovery && same(c.discovery_prefix, "ha") && c.control);
    // partial: password kept when omitted
    CHECK(apply(R"({"port":0})", &c) == "");
    CHECK(c.port == 0 && same(c.password, "secret") && same(c.host, "broker.local"));
    // "" clears the password
    CHECK(apply(R"({"password":""})", &c) == "");
    CHECK(c.password[0] == 0 && same(c.username, "doser"));
    // no user -> no password
    CHECK(apply(R"({"password":"pw"})", &c) == "" && same(c.password, "pw"));
    CHECK(apply(R"({"username":""})", &c) == "" && c.password[0] == 0);
    // custom TLS needs a CA when enabled
    const char* field = nullptr;
    Config before = c;
    CHECK(apply(R"({"tls":"custom"})", &c, false, &field) == "ca_missing" && same(field, "tls"));
    CHECK(c.tls == before.tls);  // unchanged on error
    CHECK(apply(R"({"tls":"custom"})", &c, true) == "" && c.tls == Tls::Custom);
    CHECK(apply(R"({"enabled":false,"tls":"custom"})", &c, false) == "");  // disabled: allowed
    // errors keep the configuration
    struct {
        const char* doc;
        const char* field;
    } bad[] = {
        {R"({"enabled":"yes"})", "enabled"},
        {R"({"enabled":true,"host":""})", "host"},
        {R"({"host":"a b"})", "host"},
        {R"({"host":5})", "host"},
        {R"({"port":-1})", "port"},
        {R"({"port":65536})", "port"},
        {R"({"port":"1883"})", "port"},
        {R"({"tls":"on"})", "tls"},
        {R"({"base_topic":"a/#"})", "base_topic"},
        {R"({"base_topic":"/x"})", "base_topic"},
        {R"({"discovery_prefix":""})", "discovery_prefix"},
        {R"({"discovery_prefix":"0123456789012345678901234567890123"})", "discovery_prefix"},
        {R"({"discovery":1})", "discovery"},
        {R"({"control":"true"})", "control"},
        {R"({"username":"0123456789012345678901234567890123456789012345678901234567890123456789"})", "username"},
        {R"({"username":"u","password":"0123456789012345678901234567890123456789012345678901234567890123456789"})",
         "password"},
        {R"([1])", "body"},
    };
    for (const auto& b : bad) {
        Config x = c;
        const char* f = nullptr;
        CHECK(apply(b.doc, &x, true, &f) == "invalid_param");
        CHECK(same(f, b.field));
        CHECK(std::memcmp(&x, &c, sizeof(x)) == 0);
    }
    // base_topic "" = default
    CHECK(apply(R"({"base_topic":""})", &c) == "" && c.base_topic[0] == 0);
}

static void test_write() {
    std::printf("MQTT config: write\n");
    Config c;
    std::strcpy(c.host, "h");
    std::strcpy(c.username, "u");
    std::strcpy(c.password, "very-secret");
    Json j;
    j.obj();
    write(j, c);
    j.end();
    const std::string s = j.c_str();
    CHECK(s.find("very-secret") == std::string::npos);
    CHECK(s.find("\"password_set\":true") != std::string::npos);
    CHECK(s.find("\"tls\":\"off\"") != std::string::npos);
    CHECK(s.find("\"port\":0") != std::string::npos);
    // round trip (without password)
    Config back;
    CHECK(apply(j.c_str(), &back) == "");
    CHECK(same(back.host, "h") && same(back.username, "u") && back.password[0] == 0);
}

static void test_pem() {
    std::printf("MQTT config: PEM\n");
    const char* ok = "-----BEGIN CERTIFICATE-----\nMIIB\n-----END CERTIFICATE-----\n";
    CHECK(looks_like_pem(ok, unsigned(std::strlen(ok))));
    const std::string two = std::string(ok) + ok;
    CHECK(looks_like_pem(two.c_str(), unsigned(two.size())));
    const char* no_end = "-----BEGIN CERTIFICATE-----\nMIIB\n";
    CHECK(!looks_like_pem(no_end, unsigned(std::strlen(no_end))));
    const char* key = "-----BEGIN PRIVATE KEY-----\nMIIB\n-----END PRIVATE KEY-----\n";
    CHECK(!looks_like_pem(key, unsigned(std::strlen(key))));
    const char* end_first = "-----END CERTIFICATE-----\n-----BEGIN CERTIFICATE-----\n";
    CHECK(!looks_like_pem(end_first, unsigned(std::strlen(end_first))));
    std::string bin(ok);
    bin[30] = '\0';
    CHECK(!looks_like_pem(bin.data(), unsigned(bin.size())));
    std::string big(ok);
    big.append(kCaMax, 'A');
    CHECK(!looks_like_pem(big.c_str(), unsigned(big.size())));
    CHECK(!looks_like_pem(nullptr, 0) && !looks_like_pem("", 0));
}

int main() {
    test_topic_host();
    test_defaults_and_port();
    test_parse();
    test_write();
    test_pem();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
