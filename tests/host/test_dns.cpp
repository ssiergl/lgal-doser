// Host tests for the captive portal DNS answers.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "lgal/dns_reply.h"

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

using Bytes = std::vector<uint8_t>;

static void put16(Bytes& b, uint16_t v) {
    b.push_back(uint8_t(v >> 8));
    b.push_back(uint8_t(v));
}

// Standard query as a resolver sends it
static Bytes query(const char* name, uint16_t qtype, uint16_t flags = 0x0100, uint16_t qclass = 1, bool edns = false) {
    Bytes b;
    put16(b, 0xBEEF);  // id
    put16(b, flags);
    put16(b, 1);
    put16(b, 0);
    put16(b, 0);
    put16(b, edns ? 1 : 0);
    const char* p = name;
    while (*p) {
        const char* dot = std::strchr(p, '.');
        const size_t n = dot ? size_t(dot - p) : std::strlen(p);
        b.push_back(uint8_t(n));
        b.insert(b.end(), p, p + n);
        p += n;
        if (*p == '.') ++p;
    }
    b.push_back(0);
    put16(b, qtype);
    put16(b, qclass);
    if (edns) {  // OPT record
        b.push_back(0);
        put16(b, 41);
        put16(b, 1232);
        put16(b, 0);
        put16(b, 0);
        put16(b, 0);
    }
    return b;
}

static uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }

static size_t run(const Bytes& q, uint8_t* out, size_t cap = dns::kMaxPacket) {
    return dns::answer(q.data(), q.size(), 0xC0A80401u, out, cap);
}

static void test_a() {
    std::printf("DNS: A answer\n");
    const Bytes q = query("captive.apple.com", 1);
    uint8_t out[dns::kMaxPacket];
    const size_t n = run(q, out);
    CHECK(n == q.size() + 16);
    CHECK(rd16(out) == 0xBEEF);
    CHECK(rd16(out + 2) == 0x8500);  // QR, AA, RD
    CHECK(rd16(out + 4) == 1 && rd16(out + 6) == 1 && rd16(out + 8) == 0 && rd16(out + 10) == 0);
    CHECK(std::memcmp(out + 12, q.data() + 12, q.size() - 12) == 0);  // question copied
    const uint8_t* r = out + q.size();
    CHECK(rd16(r) == 0xC00C && rd16(r + 2) == 1 && rd16(r + 4) == 1);
    CHECK(r[6] == 0 && r[7] == 0 && r[8] == 0 && r[9] == 60);
    CHECK(rd16(r + 10) == 4);
    CHECK(r[12] == 192 && r[13] == 168 && r[14] == 4 && r[15] == 1);

    // RD not set -> not set in the reply; class ANY answered
    const Bytes q2 = query("x", 1, 0x0000, 255);
    CHECK(run(q2, out) == q2.size() + 16 && rd16(out + 2) == 0x8400 && rd16(out + 6) == 1);
    // EDNS additional record is not echoed
    const Bytes q3 = query("connectivitycheck.gstatic.com", 1, 0x0100, 1, true);
    const size_t n3 = run(q3, out);
    CHECK(n3 == q3.size() - 11 + 16);
    CHECK(rd16(out + 10) == 0 && rd16(out + 6) == 1);
    // root name
    const Bytes q4 = query("", 1);
    CHECK(q4.size() == 17);
    CHECK(run(q4, out) == 33);
}

static void test_other_types() {
    std::printf("DNS: other types\n");
    uint8_t out[dns::kMaxPacket];
    for (uint16_t t : {28, 16, 65, 255}) {  // AAAA, TXT, HTTPS, ANY
        const Bytes q = query("www.msftconnecttest.com", t);
        CHECK(run(q, out) == q.size());
        CHECK(rd16(out + 2) == 0x8500 && rd16(out + 6) == 0);
    }
    const Bytes ch = query("x", 1, 0x0100, 3);  // class CHAOS
    CHECK(run(ch, out) == ch.size() && rd16(out + 6) == 0);
}

static void test_rejects() {
    std::printf("DNS: rejects\n");
    uint8_t out[dns::kMaxPacket];
    Bytes q = query("a.example", 1);
    CHECK(dns::answer(q.data(), 11, 1, out, sizeof(out)) == 0);  // short header
    CHECK(dns::answer(nullptr, 30, 1, out, sizeof(out)) == 0);
    CHECK(dns::answer(q.data(), q.size(), 1, nullptr, 512) == 0);
    for (size_t cut = 12; cut < q.size(); ++cut) CHECK(dns::answer(q.data(), cut, 1, out, sizeof(out)) == 0);
    CHECK(run(query("a.example", 1, 0x8100), out) == 0);  // response
    CHECK(run(query("a.example", 1, 0x2100), out) == 0);  // opcode 4 (NOTIFY)
    Bytes two = q;
    two[5] = 2;
    CHECK(run(two, out) == 0);
    Bytes ans = q;
    ans[7] = 1;
    CHECK(run(ans, out) == 0);
    Bytes comp = q;
    comp[12] = 0xC0;
    CHECK(run(comp, out) == 0);
    Bytes over = q;
    over[12] = 60;  // label runs past the end
    CHECK(run(over, out) == 0);
    // name too long (> 254 bytes)
    std::string longname;
    for (int i = 0; i < 5; ++i) longname += std::string(60, 'a') + ".";
    longname.pop_back();
    CHECK(run(query(longname.c_str(), 1), out) == 0);
    std::string okname;
    for (int i = 0; i < 4; ++i) okname += std::string(61, 'b') + ".";
    okname.pop_back();  // 4*62 = 248 bytes encoded
    CHECK(run(query(okname.c_str(), 1), out) > 0);
    // too little room
    CHECK(run(q, out, q.size() + 15) == 0);
    CHECK(run(q, out, q.size() + 16) == q.size() + 16);
    const Bytes aaaa = query("a.example", 28);
    CHECK(run(aaaa, out, aaaa.size() - 1) == 0);
}

int main() {
    test_a();
    test_other_types();
    test_rejects();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
