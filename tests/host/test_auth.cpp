// Host tests for SHA-256/HMAC/PBKDF2 and the authentication core.
// Reference values computed with Python's hashlib/hmac/base64.
#include <cstdio>
#include <cstring>
#include <string>

#include "lgal/auth_core.h"
#include "lgal/setup_core.h"
#include "lgal/sha256.h"

using namespace lgal;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

static std::string hex(const uint8_t* p, size_t n) {
    static const char d[] = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}

static std::string sha(const std::string& m) {
    uint8_t h[32];
    crypto::sha256(m.data(), m.size(), h);
    return hex(h, 32);
}

static void test_sha256() {
    CHECK(sha("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // padding boundaries
    CHECK(sha(std::string(55, 'x')) == "d5e285683cd4efc02d021a5c62014694958901005d6f71e89e0989fac77e4072");
    CHECK(sha(std::string(56, 'x')) == "04c26261370ee7541549d16dee320c723e3fd14671e66a099afe0a377c16888e");
    CHECK(sha(std::string(64, 'x')) == "7ce100971f64e7001e8fe5a51973ecdfe1ced42befe7ee8d5fd6219506b5393c");
    // one million 'a' in uneven pieces (exercises the buffering)
    crypto::Sha256 s;
    const std::string chunk(997, 'a');
    size_t left = 1000000;
    while (left) {
        const size_t n = left < chunk.size() ? left : chunk.size();
        s.update(chunk.data(), n);
        left -= n;
    }
    uint8_t h[32];
    s.finish(h);
    CHECK(hex(h, 32) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    // the object is reusable after finish
    s.update("abc", 3);
    s.finish(h);
    CHECK(hex(h, 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

static void test_hmac_pbkdf2() {
    uint8_t out[64];
    const std::string k1(20, '\x0b');
    crypto::hmac_sha256(k1.data(), k1.size(), "Hi There", 8, out);
    CHECK(hex(out, 32) == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    crypto::hmac_sha256("Jefe", 4, "what do ya want for nothing?", 28, out);
    CHECK(hex(out, 32) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    const std::string k6(131, '\xaa');
    const char* m6 = "Test Using Larger Than Block-Size Key - Hash Key First";
    crypto::hmac_sha256(k6.data(), k6.size(), m6, std::strlen(m6), out);
    CHECK(hex(out, 32) == "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
    // reused key schedule gives the same result
    const crypto::HmacSha256 mac("Jefe", 4);
    mac.mac("what do ya want for nothing?", 28, out);
    mac.mac("what do ya want for nothing?", 28, out);
    CHECK(hex(out, 32) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");

    crypto::pbkdf2_sha256("passwd", 6, "salt", 4, 1, out, 64);
    CHECK(hex(out, 64) ==
          "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"
          "49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783");
    crypto::pbkdf2_sha256("password", 8, "salt", 4, 4096, out, 32);
    CHECK(hex(out, 32) == "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
    crypto::pbkdf2_sha256("Password", 8, "NaCl", 4, 80000, out, 64);
    CHECK(hex(out, 64) ==
          "4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56"
          "a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d");

    CHECK(crypto::equal_ct("abcd", "abcd", 4) && !crypto::equal_ct("abcd", "abce", 4));
}

static void test_password() {
    uint8_t salt[16];
    for (int i = 0; i < 16; ++i) salt[i] = static_cast<uint8_t>(i);
    auth::PasswordRecord r{};
    CHECK(!auth::is_set(r) && !auth::verify(r, ""));
    auth::make_record(&r, "aquarium-2026", salt);
    CHECK(auth::is_set(r) && r.iterations == auth::kPbkdf2Iterations);
    CHECK(hex(r.hash, 32) == "6d1878bd882493dc6e5aa6bfe06fc6e23482326afc38c3ec1a08b6821ceb8462");
    CHECK(auth::verify(r, "aquarium-2026"));
    CHECK(!auth::verify(r, "aquarium-2027") && !auth::verify(r, "") && !auth::verify(r, nullptr));
    CHECK(!auth::verify(r, std::string(65, 'a').c_str()));
    auth::PasswordRecord bad = r;
    bad.hash[31] ^= 1;
    CHECK(!auth::verify(bad, "aquarium-2026"));
    bad = r;
    bad.magic = 0;
    CHECK(!auth::verify(bad, "aquarium-2026"));

    CHECK(auth::check_policy("1234567") && !std::strcmp(auth::check_policy("1234567"), "password_too_short"));
    CHECK(auth::check_policy("12345678") == nullptr);
    CHECK(auth::check_policy(std::string(64, 'a').c_str()) == nullptr);
    CHECK(!std::strcmp(auth::check_policy(std::string(65, 'a').c_str()), "password_too_long"));
    CHECK(auth::check_policy(nullptr) != nullptr);
}

static void test_device_secret() {
    uint8_t raw[32];
    for (int i = 0; i < 32; ++i) raw[i] = uint8_t(i * 8 + 7);
    char hex[auth::kDeviceSecretChars + 1];
    auth::hex_device_secret(raw, hex);
    CHECK(std::strcmp(hex, "070f171f272f373f474f575f676f777f878f979fa7afb7bfc7cfd7dfe7eff7ff") == 0);
    CHECK(auth::valid_device_secret(hex));
    // what tools/provision.py writes for the master "test-master-secret" and A0:B1:C2:D3:E4:F5
    CHECK(auth::valid_device_secret("0ba9da55f69e9b4e63d0919fd75787aeec5077944585eff083bb3448e97909bb"));
    CHECK(!auth::valid_device_secret(nullptr));
    CHECK(!auth::valid_device_secret(""));
    CHECK(!auth::valid_device_secret(std::string(63, 'a').c_str()));
    CHECK(!auth::valid_device_secret(std::string(65, 'a').c_str()));
    CHECK(!auth::valid_device_secret(std::string(64, 'A').c_str()));
    CHECK(!auth::valid_device_secret((std::string(63, 'a') + "g").c_str()));
}

static void test_derive() {
    // same values as tools/ap_password.py with --secret test-secret
    const uint8_t mac[6] = {0xA0, 0xB1, 0xC2, 0xD3, 0xE4, 0xF5};
    char out[17];
    auth::derive_password("test-secret", "LGAL-AP", mac, 12, out);
    CHECK(std::strcmp(out, "jr5rf4tqe4ax") == 0);
    auth::derive_password("test-secret", "LGAL-ENG", mac, 16, out);
    CHECK(std::strcmp(out, "nufkq664m949efry") == 0);
    // a provisioned device: same values as tools/ap_password.py with --master-file (test-master-secret)
    auth::derive_password("0ba9da55f69e9b4e63d0919fd75787aeec5077944585eff083bb3448e97909bb", "LGAL-AP", mac, 12, out);
    CHECK(std::strcmp(out, "79ng72jc87k4") == 0);
    auth::derive_password("0ba9da55f69e9b4e63d0919fd75787aeec5077944585eff083bb3448e97909bb", "LGAL-ENG", mac, 16, out);
    CHECK(std::strcmp(out, "brd7jfepbw37w8u4") == 0);
}

static void test_tokens() {
    uint8_t r[20];
    for (int i = 0; i < 20; ++i) r[i] = static_cast<uint8_t>(i);
    char t[33];
    auth::encode_token(r, t);
    CHECK(std::strcmp(t, "aaaqeayeaudaocajbifqydiob4ibceqt") == 0);
    std::memset(r, 0xFF, sizeof(r));
    auth::encode_token(r, t);
    CHECK(std::strcmp(t, "77777777777777777777777777777777") == 0);
}

static void rnd(uint8_t* r, int seed) {
    for (int i = 0; i < 20; ++i) r[i] = static_cast<uint8_t>(seed * 37 + i * 11);
}

static void test_sessions() {
    auth::Sessions s;
    uint8_t r[20];
    char a[33], b[33];
    rnd(r, 1);
    s.create(r, 100, a);
    rnd(r, 2);
    s.create(r, 100, b);
    CHECK(std::strcmp(a, b) != 0 && s.count(100) == 2);
    const int ia = s.find(a, 200);
    CHECK(ia >= 0 && s.find(b, 200) >= 0 && s.find(b, 200) != ia);
    CHECK(s.find("x", 200) < 0 && s.find(nullptr, 200) < 0);
    char wrong[33];
    std::strcpy(wrong, a);
    wrong[31] = wrong[31] == 'a' ? 'b' : 'a';
    CHECK(s.find(wrong, 200) < 0);

    // idle timeout, touched by find
    CHECK(s.find(a, 200 + auth::kSessionIdle) >= 0);              // still valid, touches
    CHECK(s.find(b, 200 + auth::kSessionIdle + 1) < 0);           // b idle too long
    CHECK(s.count(200 + auth::kSessionIdle + 1) == 1);
    // absolute timeout despite activity
    int64_t t = 200 + auth::kSessionIdle;
    while (t < 100 + auth::kSessionMax) {
        CHECK(s.find(a, t) >= 0);
        t += auth::kSessionIdle / 2;
    }
    CHECK(s.find(a, 101 + auth::kSessionMax) < 0);

    // logout
    rnd(r, 3);
    s.create(r, 1000, a);
    s.remove(a);
    CHECK(s.find(a, 1000) < 0);

    // full table: the least recently used session is evicted
    s.clear();
    char tok[auth::kMaxSessions + 1][33];
    for (int i = 0; i < auth::kMaxSessions; ++i) {
        rnd(r, 10 + i);
        s.create(r, 2000 + i, tok[i]);
    }
    CHECK(s.find(tok[0], 3000) >= 0);  // 0 is now the most recent, 1 the oldest
    rnd(r, 99);
    s.create(r, 3001, tok[auth::kMaxSessions]);
    CHECK(s.find(tok[1], 3002) < 0);
    CHECK(s.find(tok[0], 3002) >= 0 && s.find(tok[auth::kMaxSessions], 3002) >= 0);
    CHECK(s.count(3002) == auth::kMaxSessions);
}

static void test_engineering() {
    auth::Sessions s;
    uint8_t r[20];
    char a[33];
    rnd(r, 5);
    s.create(r, 0, a);
    int i = s.find(a, 10);
    CHECK(!s.engineering(i, 10) && s.engineering_left(i, 10) == 0);
    s.set_engineering(i, true, 10);
    CHECK(s.engineering(i, 10 + auth::kEngineeringIdle));      // touched
    CHECK(s.engineering_left(i, 20 + auth::kEngineeringIdle) == auth::kEngineeringIdle - 10);
    CHECK(!s.engineering(i, 11 + 2 * auth::kEngineeringIdle));  // auto-lock
    CHECK(!s.engineering(i, 12 + 2 * auth::kEngineeringIdle));  // stays locked
    s.set_engineering(i, true, 5000);
    s.set_engineering(i, false, 5001);
    CHECK(!s.engineering(i, 5002));
    CHECK(!s.engineering(-1, 0) && !s.engineering(auth::kMaxSessions, 0));
    // a new session in the same slot starts locked
    s.set_engineering(i, true, 6000);
    s.remove(a);
    rnd(r, 6);
    s.create(r, 6001, a);
    i = s.find(a, 6002);
    CHECK(!s.engineering(i, 6003));
}

static void test_keys() {
    auth::KeyStore ks;
    uint8_t r[20];
    for (int i = 0; i < 20; ++i) r[i] = static_cast<uint8_t>(i);
    char key[auth::kKeyChars + 1];
    const int slot = auth::add_key(ks, "Home Assistant", r, 1793968215, key);
    CHECK(slot == 0 && std::strcmp(key, "lgal_aaaqeayeaudaocajbifqydiob4ibceqt") == 0);
    CHECK(hex(ks.keys[0].hash, 32) == "d64986b20d35459cde3a498b909d2c3c72cad9e99ebe94a14f87ca1a16d7023d");
    CHECK(std::strcmp(ks.keys[0].name, "Home Assistant") == 0 && ks.keys[0].created == 1793968215);
    CHECK(auth::find_key(ks, key) == 0);
    char other[auth::kKeyChars + 1];
    std::strcpy(other, key);
    other[10] = 'z';
    CHECK(auth::find_key(ks, other) < 0);
    CHECK(auth::find_key(ks, key + 5) < 0);            // without prefix
    CHECK(auth::find_key(ks, "lgal_") < 0 && auth::find_key(ks, nullptr) < 0);

    CHECK(auth::add_key(ks, "", r, 0, other) < 0);
    CHECK(auth::add_key(ks, std::string(24, 'n').c_str(), r, 0, other) < 0);
    CHECK(auth::add_key(ks, "tab\there", r, 0, other) < 0);
    char k2[auth::kKeyChars + 1], k3[auth::kKeyChars + 1], k4[auth::kKeyChars + 1];
    rnd(r, 7);
    CHECK(auth::add_key(ks, "App", r, 0, k2) == 1);
    rnd(r, 8);
    CHECK(auth::add_key(ks, std::string(23, 'n').c_str(), r, 0, k3) == 2);
    rnd(r, 9);
    CHECK(auth::add_key(ks, "Script", r, 0, k4) == 3);
    rnd(r, 10);
    CHECK(auth::add_key(ks, "Fifth", r, 0, other) < 0 && auth::key_count(ks) == 4);
    CHECK(auth::find_key(ks, k3) == 2 && auth::find_key(ks, k4) == 3);

    CHECK(auth::remove_key(ks, 1) && !auth::remove_key(ks, 1) && !auth::remove_key(ks, 9));
    CHECK(auth::find_key(ks, k2) < 0 && auth::key_count(ks) == 3);
    rnd(r, 11);
    CHECK(auth::add_key(ks, "New", r, 0, other) == 1);  // reuses the free slot
    CHECK(auth::find_key(ks, other) == 1 && auth::find_key(ks, key) == 0);
}

static void test_throttle() {
    auth::Throttle t;
    const uint32_t ip = 0x0A00002A, ip2 = 0x0A00002B;
    for (int i = 0; i < auth::kFreeAttempts; ++i) {
        CHECK(t.wait(ip, 100) == 0);
        t.failed(ip, 100);
    }
    CHECK(t.wait(ip, 100) == 0);  // 5 failures are free
    t.failed(ip, 100);
    CHECK(t.wait(ip, 100) == 1);
    t.failed(ip, 101);
    CHECK(t.wait(ip, 101) == 2);
    t.failed(ip, 103);
    CHECK(t.wait(ip, 103) == 4);
    CHECK(t.wait(ip2, 103) == 0);  // other client unaffected
    for (int i = 0; i < 20; ++i) t.failed(ip, 200);
    CHECK(t.wait(ip, 200) == auth::kMaxWait);
    CHECK(t.wait(ip, 200 + auth::kMaxWait) == 0);
    t.succeeded(ip);
    CHECK(t.wait(ip, 201) == 0);
    t.failed(ip, 202);
    CHECK(t.wait(ip, 202) == 0);  // counting starts again

    // many clients: a recent record is never pushed out
    auth::Throttle u;
    for (int i = 0; i < 7; ++i) u.failed(ip, 300);
    CHECK(u.wait(ip, 300) == 2);
    for (uint32_t c = 1; c <= auth::kThrottleSlots; ++c) u.failed(1000 + c, 301);
    CHECK(u.wait(ip, 301) == 1);  // ip keeps its record
    CHECK(u.wait(1001, 301) == 0);
    CHECK(u.wait(1000 + auth::kThrottleSlots, 301) == 0);  // in the shared overflow record (1 failure)

    // rotating addresses: they share the overflow record and get throttled together
    auth::Throttle r;
    int64_t now = 1000, allowed = 0;
    for (int i = 0; i < 3600; ++i) {  // one attempt per second for an hour, 16 addresses
        const uint32_t c = 5000 + uint32_t(i % 16);
        if (r.wait(c, now) == 0) {
            ++allowed;
            r.failed(c, now);
        }
        now += 1;
    }
    // at most kMaxWait apart per record after the ramp: 9 records ~ 25 each
    // (without the fix all 3600 attempts got through)
    CHECK(allowed <= (auth::kThrottleSlots + 1) * (auth::kFreeAttempts + 10 + 3600 / auth::kMaxWait));
    CHECK(r.wait(9999, now) > 0);                   // a new address is throttled as well

    // records are forgotten after kForget quiet seconds
    auth::Throttle f;
    for (uint32_t c = 1; c <= auth::kThrottleSlots; ++c) f.failed(c, 10);
    f.failed(77, 20);  // overflow
    CHECK(f.wait(77, 20) == 0);
    f.failed(78, 10 + auth::kForget);  // reuses the slot of client 1
    for (int i = 0; i < auth::kFreeAttempts; ++i) f.failed(78, 10 + auth::kForget);
    CHECK(f.wait(78, 10 + auth::kForget) == 1);
    CHECK(f.wait(1, 10 + auth::kForget) == 0);
}

static void test_headers() {
    char v[40];
    CHECK(auth::cookie_value("lgal_session=abc", "lgal_session", v, sizeof(v)) && !std::strcmp(v, "abc"));
    CHECK(auth::cookie_value("a=1; lgal_session=xyz; b=2", "lgal_session", v, sizeof(v)) && !std::strcmp(v, "xyz"));
    CHECK(auth::cookie_value("a=1;lgal_session=q", "lgal_session", v, sizeof(v)) && !std::strcmp(v, "q"));
    CHECK(!auth::cookie_value("xlgal_session=1", "lgal_session", v, sizeof(v)));
    CHECK(!auth::cookie_value("lgal_session_old=1", "lgal_session", v, sizeof(v)));
    CHECK(!auth::cookie_value("a=1", "lgal_session", v, sizeof(v)));
    CHECK(!auth::cookie_value("lgal_session=0123456789", "lgal_session", v, 5));  // too long
    CHECK(auth::cookie_value("lgal_session=", "lgal_session", v, sizeof(v)) && v[0] == 0);
    CHECK(!auth::cookie_value(nullptr, "x", v, sizeof(v)));

    CHECK(auth::bearer_token("Bearer lgal_abc", v, sizeof(v)) && !std::strcmp(v, "lgal_abc"));
    CHECK(auth::bearer_token("Bearer   tok  ", v, sizeof(v)) && !std::strcmp(v, "tok"));
    CHECK(!auth::bearer_token("Basic YWRtaW46eA==", v, sizeof(v)));
    CHECK(!auth::bearer_token("Bearer ", v, sizeof(v)));
    CHECK(!auth::bearer_token("Bearer 0123456789", v, 5));
}

static void test_setup_gate() {
    // the setup and what its steps need
    for (const char* u : {"/setup", "/setup?x=1", "/api/v1/setup", "/api/v1/setup/complete", "/login?next=/setup",
                          "/api/v1/auth/status", "/api/v1/net/scan", "/api/v1/system/time",
                          "/api/v1/system/language?lang=de", "/api/v1/ota/url", "/recovery", "/i18n.js"})
        CHECK(setup::open_during_setup(u));
    // no way to the pumps or the web UI
    for (const char* u : {"/", "/ui/app.js", "/ws", "/api/v1/pumps", "/api/v1/pumps/prime", "/api/v1/hw/pump/run",
                          "/api/v1/engineering/dosing", "/api/v1/schedule/status", "/api/v1/history", "/api/v1/backup",
                          "/api/v1/mqtt", "/setupx", "/network", "/api/v1/setupx", "/recoveryx", ""})
        CHECK(!setup::open_during_setup(u));
    CHECK(!setup::open_during_setup(nullptr));
}

int main() {
    std::printf("Auth: SHA-256\n");
    test_sha256();
    std::printf("Auth: HMAC and PBKDF2\n");
    test_hmac_pbkdf2();
    std::printf("Auth: password records\n");
    test_password();
    std::printf("Auth: derived device passwords\n");
    test_device_secret();
    test_derive();
    std::printf("Auth: tokens\n");
    test_tokens();
    std::printf("Auth: sessions\n");
    test_sessions();
    std::printf("Auth: engineering unlock\n");
    test_engineering();
    std::printf("Auth: API keys\n");
    test_keys();
    std::printf("Auth: login throttling\n");
    test_throttle();
    std::printf("Auth: headers\n");
    test_headers();
    std::printf("Setup gate\n");
    test_setup_gate();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
