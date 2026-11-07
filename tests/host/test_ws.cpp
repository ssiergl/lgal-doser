// Host tests for the WebSocket helpers (origin check, change hash).
#include <cstdio>
#include <cstring>

#include "lgal/ws_core.h"

using namespace lgal::ws;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

static void test_origin() {
    std::printf("WS: origin\n");
    // no Origin header: not a browser
    CHECK(origin_allowed(nullptr, "lgal-doser-e4f5.local"));
    CHECK(origin_allowed(nullptr, nullptr));
    // own pages
    CHECK(origin_allowed("http://lgal-doser-e4f5.local", "lgal-doser-e4f5.local"));
    CHECK(origin_allowed("http://LGAL-Doser-E4F5.local", "lgal-doser-e4f5.local"));
    CHECK(origin_allowed("http://192.168.1.88", "192.168.1.88"));
    CHECK(origin_allowed("http://192.168.4.1:80", "192.168.4.1:80"));
    CHECK(origin_allowed("https://doser.example", "doser.example"));  // reverse proxy with TLS
    // foreign or malformed
    CHECK(!origin_allowed("http://evil.example", "192.168.1.88"));
    CHECK(!origin_allowed("http://192.168.1.88.evil.example", "192.168.1.88"));
    CHECK(!origin_allowed("http://192.168.1.8", "192.168.1.88"));
    CHECK(!origin_allowed("http://192.168.1.88", "192.168.1.8"));
    CHECK(!origin_allowed("http://192.168.1.88:8080", "192.168.1.88"));
    CHECK(!origin_allowed("http://192.168.1.88", "192.168.1.88:8080"));
    CHECK(!origin_allowed("null", "192.168.1.88"));
    CHECK(!origin_allowed("", "192.168.1.88"));
    CHECK(!origin_allowed("ftp://192.168.1.88", "192.168.1.88"));
    CHECK(!origin_allowed("192.168.1.88", "192.168.1.88"));
    CHECK(!origin_allowed("http://", ""));
    CHECK(!origin_allowed("http://192.168.1.88", ""));
    CHECK(!origin_allowed("http://192.168.1.88", nullptr));
    CHECK(!origin_allowed("http://192.168.1.88/", "192.168.1.88"));
}

static void test_hash() {
    std::printf("WS: hash\n");
    CHECK(fnv1a("", 0) == 2166136261u);
    CHECK(fnv1a("a", 1) == 0xE40C292Cu);
    CHECK(fnv1a("foobar", 6) == 0xBF9CF968u);
    const char* a = "{\"pumps\":[{\"pump\":1,\"done_ml\":1.250}]}";
    const char* b = "{\"pumps\":[{\"pump\":1,\"done_ml\":1.251}]}";
    CHECK(fnv1a(a, std::strlen(a)) != fnv1a(b, std::strlen(b)));
}

int main() {
    test_origin();
    test_hash();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
