// Mutation fuzzer for every parser that sees input from outside the device:
// API request bodies (JSON reader, backup, network, MQTT, schedule, lock
// rules), query strings, Cookie/Authorization headers, the WebSocket Origin
// check, the OTA manifest from the update server and DNS packets in the
// setup access point. Meant to run under AddressSanitizer and
// UndefinedBehaviorSanitizer (tests/fuzz/run.sh), which turn every
// out-of-bounds access, overflow or undefined shift into a crash.
//
// No libFuzzer needed (Apple clang ships none): seeds are valid documents,
// each round mutates one (bit flips, special bytes, inserts, deletes,
// duplicated or spliced ranges, truncation) with a fixed-seed PRNG, so a
// crash is reproducible from the printed round number.
//
//   fuzz_parsers [rounds] [seed]
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <strings.h>
#include <vector>

#include "doser/backup_json.h"
#include "doser/history_json.h"
#include "doser/posix_tz.h"
#include "doser/schedule_json.h"
#include "lgal/auth_core.h"
#include "lgal/device_name.h"
#include "lgal/dns_reply.h"
#include "lgal/json.h"
#include "lgal/json_reader.h"
#include "lgal/mqtt_config.h"
#include "lgal/net_config.h"
#include "lgal/ota_image.h"
#include "lgal/ota_manifest.h"
#include "lgal/ws_core.h"

using namespace doser;
namespace bk = doser::backup;
using Bytes = std::vector<uint8_t>;

namespace {

// --- PRNG ------------------------------------------------------------------
uint64_t g_rng = 1;
uint32_t rnd() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return uint32_t(g_rng >> 11);
}
uint32_t rnd(uint32_t n) { return n ? rnd() % n : 0; }

// --- mutation ----------------------------------------------------------------
const char kSpecial[] = "{}[]\":,\\/0123456789-+.eEtfn u\x00\x01\x7f\x80\xc3\xff\n\r\t;=%&?#@";
const char* const kTokens[] = {"\\u0000", "\\ud800", "\\uDFFF", "\\\"", "null", "true", "1e999", "-0",
                               "99999999999999999999", "0.0000000000000001", "-9223372036854775808",
                               "\"\"", "[]", "{}", "[[[[[[[[[[[[[[[[[[", "]]]]]]]]]]]]]]]]]",
                               "\xef\xbf\xbf", "%00", "../", "http://", "https://a/", "65535", "4294967296"};

void mutate(Bytes& b, const std::vector<Bytes>& pool) {
    const int ops = 1 + int(rnd(6));
    for (int k = 0; k < ops; ++k) {
        const size_t n = b.size();
        switch (rnd(10)) {
            case 0:
                if (n) b[rnd(uint32_t(n))] ^= uint8_t(1u << rnd(8));
                break;
            case 1:
                if (n) b[rnd(uint32_t(n))] = uint8_t(kSpecial[rnd(sizeof(kSpecial) - 1)]);
                break;
            case 2:
                b.insert(b.begin() + rnd(uint32_t(n + 1)), uint8_t(kSpecial[rnd(sizeof(kSpecial) - 1)]));
                break;
            case 3: {
                const char* t = kTokens[rnd(sizeof(kTokens) / sizeof(kTokens[0]))];
                b.insert(b.begin() + rnd(uint32_t(n + 1)), t, t + std::strlen(t));
                break;
            }
            case 4:
                if (n) {
                    const size_t at = rnd(uint32_t(n)), len = 1 + rnd(uint32_t(std::min<size_t>(n - at, 64)));
                    b.erase(b.begin() + at, b.begin() + at + len);
                }
                break;
            case 5:
                if (n) {
                    const size_t at = rnd(uint32_t(n)), len = 1 + rnd(uint32_t(std::min<size_t>(n - at, 64)));
                    const Bytes part(b.begin() + at, b.begin() + at + len);
                    b.insert(b.begin() + rnd(uint32_t(n + 1)), part.begin(), part.end());
                }
                break;
            case 6:
                if (n) b.resize(rnd(uint32_t(n)));
                break;
            case 7:
                if (n) b[rnd(uint32_t(n))] = uint8_t(rnd(256));
                break;
            case 8:
                if (!pool.empty()) {  // splice a range of another seed
                    const Bytes& o = pool[rnd(uint32_t(pool.size()))];
                    if (!o.empty()) {
                        const size_t at = rnd(uint32_t(o.size())),
                                     len = 1 + rnd(uint32_t(std::min<size_t>(o.size() - at, 128)));
                        b.insert(b.begin() + rnd(uint32_t(n + 1)), o.begin() + at, o.begin() + at + len);
                    }
                }
                break;
            default:
                if (n >= 2) std::swap(b[rnd(uint32_t(n))], b[rnd(uint32_t(n))]);
                break;
        }
    }
}

// Structure-aware variant for JSON targets: replace one scalar (string,
// number, literal) with a hostile value, so most inputs stay valid JSON and
// reach the field checks behind the reader.
const char* const kValues[] = {
    "0", "-1", "1", "255", "256", "65535", "65536", "2147483647", "2147483648", "-2147483649",
    "4294967295", "4294967296", "9223372036854775807", "-9223372036854775808", "99999999999999999999",
    "0.0001", "0.5", "1.0005", "-0.0", "1e3", "999999999999999.9", "1000000000000000", "null", "true",
    "false", "[]", "{}", "[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30]",
    "[{}]", "[\"\"]", "\"\"", "\" \"", "\"x\"", "\"\\u0000\"", "\"\\ud800\"", "\"\\\"\"",
    "\"\u00e4\u00f6\"", "\"../../etc\"", "\"%s%n\"", "\"a/+/#\"", "\"$SYS\"", "\"255.255.255.255\"",
    "\"0.0.0.0\"", "\"1.2.3\"", "\"1.2.3.4.5\"", "\"24:00\"", "\"23:59\"", "\"00:00\"", "\"7:5\"",
    "\"2026-02-29\"", "\"2028-02-29\"", "\"1969-12-31\"", "\"9999-12-31\"", "\"auto\"", "\"manual\"",
    "\"custom\"", "\"bundle\"", "\"spread\"", "\"times\"", "\"N16R8\"", "\"http://a\"",
    "\"https://\"", "\"CET-1CEST,M3.5.0,M10.5.0/3\"", "\"<+14>-14\"", "\"UTC25\"",
    "\"-----BEGIN CERTIFICATE-----\\n-----END CERTIFICATE-----\"",
};

struct Span {
    size_t at, len;
};

std::vector<Span> scalars(const Bytes& b) {
    std::vector<Span> out;
    for (size_t i = 0; i < b.size();) {
        const uint8_t c = b[i];
        if (c == '"') {
            size_t k = i + 1;
            while (k < b.size() && b[k] != '"') k += b[k] == '\\' ? 2 : 1;
            if (k >= b.size()) break;
            out.push_back({i, k + 1 - i});
            i = k + 1;
        } else if (c == '-' || (c >= '0' && c <= '9') || c == 't' || c == 'f' || c == 'n') {
            size_t k = i;
            while (k < b.size() && b[k] != ',' && b[k] != '}' && b[k] != ']' && b[k] != ' ' && b[k] != '\n') ++k;
            out.push_back({i, k - i});
            i = k;
        } else {
            ++i;
        }
    }
    return out;
}

void mutate_json(Bytes& b, const std::vector<Bytes>& pool) {
    const int ops = 1 + int(rnd(3));
    for (int k = 0; k < ops; ++k) {
        const auto sp = scalars(b);
        if (sp.empty()) break;
        const Span s = sp[rnd(uint32_t(sp.size()))];
        std::string v;
        if (rnd(8) == 0)
            v = "\"" + std::string(1 + rnd(400), char('a' + rnd(26))) + "\"";  // long string
        else
            v = kValues[rnd(sizeof(kValues) / sizeof(kValues[0]))];
        b.erase(b.begin() + s.at, b.begin() + s.at + s.len);
        b.insert(b.begin() + s.at, v.begin(), v.end());
    }
    if (rnd(6) == 0) mutate(b, pool);  // and now and then raw bytes on top
}

// --- targets -----------------------------------------------------------------
lgal::JsonToken g_tok[3072];

// Touch every value with every getter (small buffers too)
void walk(const lgal::JsonValue& v, int depth) {
    if (depth > 40) std::abort();  // the reader promises kMaxDepth
    bool bv;
    int64_t iv;
    char small[4], big[256];
    v.get(&bv);
    v.get(&iv);
    v.get_decimal(1000, &iv);
    v.get_decimal(1, &iv);
    v.get(small, sizeof(small));
    v.get(big, sizeof(big));
    v.equals("x");
    (void)v["pump"].valid();
    (void)v[0].valid();
    (void)v[-1].valid();
    (void)v[1 << 20].valid();
    const int n = v.size();
    if (n < 0) std::abort();
    for (int i = 0; i < n; ++i) {
        if (v.type() == lgal::JsonType::Object) {
            v.key_at(i).get(big, sizeof(big));
            walk(v.value_at(i), depth + 1);
        } else {
            walk(v[i], depth + 1);
        }
    }
    (void)v.key_at(n).valid();
    (void)v.value_at(-1).valid();
}

bool parse(const Bytes& b, lgal::JsonReader* doc, int max_tokens = 3072) {
    return doc->parse(reinterpret_cast<const char*>(b.data()), b.size(), g_tok, max_tokens);
}

std::string cstr(const Bytes& b, size_t max = 200) {
    std::string s(b.begin(), b.begin() + std::min(b.size(), max));
    return s;  // may contain NULs: c_str() stops at the first one, like a header would
}

bk::Data g_base;

void t_json(const Bytes& b) {
    lgal::JsonReader doc;
    if (parse(b, &doc)) walk(doc.root(), 0);
    lgal::JsonReader few;
    if (parse(b, &few, 7)) walk(few.root(), 0);  // token limit hit mid-document
}

void t_backup(const Bytes& b) {
    lgal::JsonReader doc;
    if (!parse(b, &doc)) return;
    auto d = std::make_unique<bk::Data>();
    bk::Error err;
    if (bk::parse(doc.root(), g_base, 20764, d.get(), &err)) {
        lgal::Json j;  // a parsed document must write again
        bk::write(j, *d, {"1.0.0", "doser", 0});
    } else if (!err.code) {
        std::abort();  // a rejection always names its reason
    }
    bk::parse(doc.root(), g_base, -1, d.get(), &err);
}

void t_config(const Bytes& b) {
    lgal::JsonReader doc;
    if (!parse(b, &doc)) return;
    const auto r = doc.root();
    lgal::netcfg::Config nc;
    lgal::netcfg::Error ne;
    if (lgal::netcfg::parse(r, &nc, &ne)) {
        lgal::Json j;
        lgal::netcfg::write(j.obj(), nc);
    }
    lgal::mqttcfg::Config mc;
    lgal::mqttcfg::Error me;
    if (lgal::mqttcfg::parse(r, true, &mc, &me)) {
        lgal::Json j;
        lgal::mqttcfg::write(j.obj(), mc);
    }
    lgal::mqttcfg::parse(r, false, &mc, &me);
    sched::PumpSchedule ps;
    sched_json::Error se;
    if (sched_json::parse_schedule(r, 20764, &ps, &se)) {
        lgal::Json j;
        sched_json::write_schedule(j.obj(), ps);
    }
    sched_json::parse_schedule(r["schedule"], -1, &ps, &se);
    sched::Locks lk;
    if (sched_json::parse_locks(r, &lk, &se)) {
        lgal::Json j;
        sched_json::write_locks(j.obj(), lk);
    }
}

void t_strings(const Bytes& b) {
    const std::string s = cstr(b);
    const char* c = s.c_str();
    char out[65];
    char tiny[2];
    uint16_t u16;
    int32_t day;
    lgal::auth::cookie_value(c, "lgal_session", out, sizeof(out));
    lgal::auth::cookie_value(c, "a", tiny, sizeof(tiny));
    lgal::auth::bearer_token(c, out, sizeof(out));
    lgal::auth::bearer_token(c, tiny, sizeof(tiny));
    lgal::auth::check_policy(c);
    lgal::ws::origin_allowed(c, "doser.local:8080");
    lgal::ws::origin_allowed("http://doser.local", c);
    lgal::ws::origin_allowed(c, c);
    sched_json::parse_hhmm(c, &u16);
    sched_json::parse_date(c, &day);
    history_json::parse_kinds(c, &u16);
    uint8_t parts;
    bk::parse_parts(c, &parts);
    tz::Zone z;
    tz::parse(c, &z);
    lgal::devname::valid_name(c);
    lgal::devname::valid_host(c);
    const std::string slug = lgal::devname::slug(c);
    if (!slug.empty() && !lgal::devname::valid_host(slug.c_str())) std::abort();  // slug must be a valid host
    lgal::devname::effective(c, c, "lgal-doser-abcdef");
    lgal::mqttcfg::valid_topic(c, 64);
    lgal::mqttcfg::valid_host(c);
    lgal::mqttcfg::looks_like_pem(c, unsigned(s.size()));
    uint32_t ip;
    lgal::netcfg::parse_ipv4(c, &ip);
    lgal::ota::Version v;
    lgal::ota::parse_version(c, &v);
    lgal::ota::resolve_url("https://github.com/x/y/releases/latest/download/manifest.json", c);
    lgal::ota::resolve_url(c, "fw.bin");
    lgal::ota::resolve_url(c, c);
}

void t_manifest(const Bytes& b) {
    lgal::ota::Release r;
    const char* url = "https://example.invalid/releases/latest/download/manifest.json";
    for (const char* var : {"N8R8", "N16R8", "N32R8", ""}) {
        const char* e =
            lgal::ota::parse_manifest(reinterpret_cast<const char*>(b.data()), b.size(), "d5v1", var, url, &r);
        if (!e && strncasecmp(r.url.c_str(), "http://", 7) != 0 && strncasecmp(r.url.c_str(), "https://", 8) != 0)
            std::abort();  // an accepted release always has an absolute http(s) url (scheme case-insensitive)
    }
    // board descriptor of an uploaded image
    const std::string board = lgal::ota::image_board(b.data(), b.size());
    if (!board.empty() && !lgal::ota::valid_board_id(board.c_str())) std::abort();
}

void t_dns(const Bytes& b) {
    uint8_t out[lgal::dns::kMaxPacket];
    const size_t n = lgal::dns::answer(b.data(), b.size(), 0xC0A80401, out, sizeof(out));
    if (n > sizeof(out)) std::abort();
    uint8_t small[16];
    if (lgal::dns::answer(b.data(), b.size(), 0xC0A80401, small, sizeof(small)) > sizeof(small)) std::abort();
}

// --- seeds -------------------------------------------------------------------
Bytes bytes(const std::string& s) { return Bytes(s.begin(), s.end()); }

std::string backup_seed() {
    bk::Data d;
    d.parts = bk::kSystem | bk::kNetwork | bk::kPumps | bk::kEngineering | bk::kMqtt;
    std::strcpy(d.language, "de");
    std::strcpy(d.timezone, "CET-1CEST,M3.5.0,M10.5.0/3");
    std::strcpy(d.ntp_server, "pool.ntp.org");
    d.names = true;
    std::strcpy(d.name, "Riffbecken");
    d.net.lan.dhcp = false;
    lgal::netcfg::parse_ipv4("192.168.1.50", &d.net.lan.ip);
    lgal::netcfg::parse_ipv4("255.255.255.0", &d.net.lan.netmask);
    lgal::netcfg::parse_ipv4("192.168.1.1", &d.net.lan.gateway);
    std::strcpy(d.net.ssid, "Reef");
    std::strcpy(d.net.password, "test-wlan-pw");
    d.wlan_password = true;
    d.mqtt.enabled = true;
    std::strcpy(d.mqtt.host, "broker.local");
    std::strcpy(d.mqtt.base_topic, "aq/doser");
    d.mqtt_ca = "-----BEGIN CERTIFICATE-----\nMIIBtest\n-----END CERTIFICATE-----\n";
    for (int i = 0; i < 5; ++i) {
        std::snprintf(d.pumps[i].name, sizeof(d.pumps[i].name), "Pumpe \"%d\"", i + 1);
        d.pumps[i].ul_per_rev = 340.5f;
        d.hw[i] = motion::PumpConfig{};
    }
    d.fitted = 0x0F;
    d.history_keep_days = 90;
    lgal::Json j;
    bk::write(j, d, {"1.2.3", "doser-riff", 1793000000});
    return j.str();
}

std::vector<Bytes> json_seeds() {
    return {
        bytes(backup_seed()),
        bytes(R"({"mode":"auto","lan":{"dhcp":false,"ip":"10.0.0.2","netmask":"255.0.0.0","gateway":"10.0.0.1",)"
              R"("dns":"1.1.1.1"},"wlan":{"dhcp":true},"ssid":"Reef","password":"test-wlan-pw"})"),
        bytes(R"({"enabled":true,"host":"broker.local","port":8883,"tls":"custom","username":"u",)"
              R"("password":"test-mqtt-pw","base_topic":"aq/doser","ha_discovery":true,"control":true})"),
        bytes(R"({"type":"spread","daily_ml":12.5,"doses":24,"start":"08:00","end":"20:00","interval_days":1,)"
              R"("weekdays":[1,2,3,4,5,6,7],"times":[{"at":"09:30","ml":1.25},{"at":"21:00","ml":0.5}]})"),
        bytes(R"({"min_gap_min":15,"rules":[{"a":1,"b":2,"gap_min":30},{"a":3,"b":5}]})"),
        bytes(R"([1,-2.5e3,"ä😀\n",null,true,false,{"a":{"b":[[]]}}])"),
    };
}

std::vector<Bytes> manifest_seeds() {
    return {bytes(R"({"name":"LGAL-Doser","version":"1.4.0","notes":"https://example.invalid/r/1.4.0",)"
                  R"("builds":[{"board":"d5v1","variant":"N16R8","url":"lgal_doser-d5v1-N16R8.bin","size":1638400,)"
                  R"("sha256":"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"},)"
                  R"({"board":"d5v1","variant":"N8R8","url":"https://example.invalid/fw-N8R8.bin",)"
                  R"("sha256":"ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100"}]})")};
}

std::vector<Bytes> string_seeds() {
    return {bytes("lgal_session=abcdefABCDEF0123456789abcdefABCDEF0123456789; theme=dark"),
            bytes("Bearer lgal_k1_0123456789abcdef0123456789abcdef"),
            bytes("http://doser.local:8080"),
            bytes("CET-1CEST,M3.5.0,M10.5.0/3"),
            bytes("<+0545>-5:45"),
            bytes("23:59"),
            bytes("2026-11-12"),
            bytes("dose,boost,alarm,manual"),
            bytes("system,pumps,engineering,mqtt"),
            bytes("Riff Becken Ä 1"),
            bytes("aq/doser/+/#"),
            bytes("v1.10.0-rc.1"),
            bytes("../../fw.bin?x=1#y"),
            bytes("-----BEGIN CERTIFICATE-----\nMIIB\n-----END CERTIFICATE-----\n")};
}

std::vector<Bytes> dns_seeds() {
    // standard query "connectivitycheck.gstatic.com" A/IN, plus AAAA with EDNS
    const uint8_t a[] = {0x12, 0x34, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0, 17, 'c', 'o', 'n', 'n', 'e', 'c', 't', 'i', 'v',
                         'i', 't', 'y', 'c', 'h', 'e', 'c', 'k', 7, 'g', 's', 't', 'a', 't', 'i', 'c', 3, 'c', 'o',
                         'm', 0, 0, 1, 0, 1};
    const uint8_t aaaa[] = {0xab, 0xcd, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 1, 4, 'l', 'g', 'a', 'l', 5, 'l', 'o', 'c',
                            'a', 'l', 0, 0, 28, 0, 1, 0, 0, 41, 0x10, 0, 0, 0, 0, 0, 0, 0};
    return {Bytes(a, a + sizeof(a)), Bytes(aaaa, aaaa + sizeof(aaaa))};
}

struct Target {
    const char* name;
    void (*fn)(const Bytes&);
    std::vector<Bytes> seeds;
    bool json;  // structure-aware mutation for most rounds
};

}  // namespace

int main(int argc, char** argv) {
    const long rounds = argc > 1 ? std::atol(argv[1]) : 200000;
    const uint64_t seed = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1;

    g_base = bk::Data{};
    std::strcpy(g_base.language, "de");
    std::strcpy(g_base.timezone, "UTC0");

    std::vector<Target> targets = {
        {"json", t_json, json_seeds(), false},
        {"backup", t_backup, json_seeds(), true},
        {"config", t_config, json_seeds(), true},
        {"strings", t_strings, string_seeds(), false},
        {"manifest", t_manifest, manifest_seeds(), true},
        {"dns", t_dns, dns_seeds(), false},
    };
    for (auto& t : targets) {
        g_rng = seed * 0x9E3779B97F4A7C15ull + 1;
        for (const auto& s : t.seeds) t.fn(s);  // seeds must pass untouched
        for (long r = 0; r < rounds; ++r) {
            Bytes b = t.seeds[rnd(uint32_t(t.seeds.size()))];
            if (t.json && r % 4 != 0)
                mutate_json(b, t.seeds);
            else
                mutate(b, t.seeds);
            if (r % 4 == 3) mutate(b, t.seeds);  // sometimes go further away
            t.fn(b);
        }
        std::printf("%-9s %ld rounds ok\n", t.name, rounds);
    }
    return 0;
}
