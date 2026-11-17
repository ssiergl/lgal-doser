// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// Host tests for the WebSocket helpers (origin check, change hash, status push).
#include <cstdio>
#include <cstring>

#include "lgal/json.h"
#include "lgal/json_reader.h"
#include "lgal/ws_core.h"

using namespace lgal;
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

// The push must be valid JSON: the web UI drops a message it cannot parse,
// and the live view then only follows the slow REST reload
static void test_status_message() {
    std::printf("WS: status message\n");
    const std::string m = status_message(7, 1794704081, true, "{\"pumps\":[{\"pump\":1,\"activity\":\"idle\"}]}");
    CHECK(m == "{\"type\":\"status\",\"seq\":7,\"utc\":1794704081,\"time_valid\":true,"
               "\"pumps\":[{\"pump\":1,\"activity\":\"idle\"}]}");
    JsonReader doc;
    JsonToken tok[32];
    CHECK(doc.parse(m.data(), m.size(), tok, 32));
    bool valid = false;
    int64_t seq = 0;
    CHECK(doc.root()["time_valid"].get(&valid) && valid);
    CHECK(doc.root()["seq"].get(&seq) && seq == 7);
    CHECK(doc.root()["pumps"].valid());

    // empty body: head only, still closed
    const std::string e = status_message(8, 0, false, "{}");
    CHECK(e == "{\"type\":\"status\",\"seq\":8,\"utc\":0,\"time_valid\":false}");
    CHECK(doc.parse(e.data(), e.size(), tok, 32));
}

int main() {
    test_origin();
    test_hash();
    test_status_message();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
