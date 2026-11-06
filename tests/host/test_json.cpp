// Host tests for the JSON reader (lgal_core) and the round trip with the
// JSON writer.
#include <cstdio>
#include <cstring>
#include <string>

#include "lgal/json.h"
#include "lgal/json_reader.h"

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

static JsonToken g_tok[64];

static bool parse(JsonReader& r, const char* s, int max = 64) { return r.parse(s, std::strlen(s), g_tok, max); }

static const char* err(const char* s, int max = 64) {
    JsonReader r;
    return parse(r, s, max) ? nullptr : r.error();
}

static void test_valid_documents() {
    const char* ok[] = {
        "0", "-0", "1.5", "-12.25e+3", "1E2", "true", "false", "null", "\"\"", "\"a\\\"b\"", "[]", "{}",
        " \t\r\n[ 1 , 2 ] ", "{\"a\":{\"b\":[[],{}]}}", "\"\\u00e4\\ud83d\\ude00\"", "\"\\/\\b\\f\\n\\r\\t\\\\\"",
    };
    for (const char* s : ok) {
        const char* e = err(s);
        if (e) std::printf("  rejected %s: %s\n", s, e);
        CHECK(e == nullptr);
    }
}

static void test_invalid_documents() {
    struct Case {
        const char* text;
        const char* error;
    };
    const Case bad[] = {
        {"", "empty"},
        {"   ", "empty"},
        {"01", "trailing_data"},
        {"1.", "syntax"},
        {".5", "syntax"},
        {"-", "syntax"},
        {"1e", "syntax"},
        {"+1", "syntax"},
        {"tru", "syntax"},
        {"nul", "syntax"},
        {"[1,]", "syntax"},
        {"[1 2]", "syntax"},
        {"{\"a\" 1}", "syntax"},
        {"{\"a\":1,}", "syntax"},
        {"{a:1}", "syntax"},
        {"{\"a\":1", "syntax"},
        {"[", "syntax"},
        {"\"abc", "syntax"},
        {"\"a\nb\"", "syntax"},       // raw control character
        {"\"\\x\"", "syntax"},        // unknown escape
        {"\"\\u12\"", "syntax"},      // short \u
        {"\"\\udc00\"", "syntax"},    // lone low surrogate
        {"\"\\ud83d\"", "syntax"},    // high surrogate without low
        {"\"\\ud83d\\u0041\"", "syntax"},
        {"[] []", "trailing_data"},
        {"{}x", "trailing_data"},
        {"[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]", "too_deep"},  // 17 levels
    };
    for (const auto& c : bad) {
        const char* e = err(c.text);
        if (!e || std::strcmp(e, c.error) != 0) std::printf("  %s -> %s (expected %s)\n", c.text, e ? e : "ok", c.error);
        CHECK(e && std::strcmp(e, c.error) == 0);
    }
    CHECK(err("[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]") == nullptr);  // 16 levels
    CHECK(std::strcmp(err("[1,2,3]", 3), "too_many_tokens") == 0);
    CHECK(err("[1,2,3]", 4) == nullptr);

    JsonReader r;
    CHECK(!parse(r, "{\"a\": [1, x]}"));
    CHECK(r.error_pos() == 10 && !r.root().valid());
    CHECK(!r.parse(nullptr, 0, g_tok, 64) && std::strcmp(r.error(), "too_large") == 0);
}

static void test_access() {
    JsonReader r;
    CHECK(parse(r,
                "{\"name\":\"KH \\\"Ca\\\"\",\"count\":12,\"ml\":2.5,\"on\":true,\"off\":false,\"none\":null,"
                "\"list\":[10,{\"x\":1},[7,8],\"s\"],\"empty\":{},\"name\":\"second\"}"));
    const JsonValue root = r.root();
    CHECK(root.type() == JsonType::Object && root.size() == 9);

    char buf[32];
    CHECK(root["name"].get(buf, sizeof(buf)) && std::strcmp(buf, "KH \"Ca\"") == 0);  // first match wins
    CHECK(root["name"].equals("KH \"Ca\"") && !root["name"].equals("KH"));
    CHECK(!root["name"].get(buf, 7));  // too small: 7 bytes + NUL needed

    int64_t n = 0;
    CHECK(root["count"].get(&n) && n == 12);
    CHECK(!root["ml"].get(&n));  // fraction is not an integer
    CHECK(root["ml"].get_decimal(1000, &n) && n == 2500);
    bool b = false;
    CHECK(root["on"].get(&b) && b);
    CHECK(root["off"].get(&b) && !b);
    CHECK(!root["count"].get(&b));
    CHECK(root["none"].is_null() && root["none"].valid());

    const JsonValue list = root["list"];
    CHECK(list.type() == JsonType::Array && list.size() == 4);
    CHECK(list[0].get(&n) && n == 10);
    CHECK(list[1]["x"].get(&n) && n == 1);
    CHECK(list[2].size() == 2 && list[2][1].get(&n) && n == 8);
    CHECK(list[3].equals("s"));
    CHECK(!list[4].valid() && !list[-1].valid());

    CHECK(root["empty"].type() == JsonType::Object && root["empty"].size() == 0);
    CHECK(!root["empty"]["x"].valid());

    // missing members chain without crashing
    CHECK(!root["missing"].valid() && !root["missing"]["deeper"][3].get(&n));
    CHECK(root["missing"].type() == JsonType::Invalid && root["missing"].size() == 0);
    CHECK(!root[0].valid());       // index on an object
    CHECK(!list["x"].valid());     // key on an array

    // members by position (skipping nested values)
    CHECK(root.key_at(6).equals("list") && root.value_at(6).size() == 4);
    CHECK(root.key_at(7).equals("empty") && root.key_at(8).equals("name"));
    CHECK(root.value_at(8).equals("second"));
    CHECK(!root.key_at(9).valid() && !list.key_at(0).valid());
    CHECK(r.tokens_used() == 27);
}

static void test_numbers() {
    struct Dec {
        const char* text;
        int64_t scale;
        bool ok;
        int64_t value;
    };
    const Dec dec[] = {
        {"0", 1000, true, 0},
        {"1", 1000, true, 1000},
        {"-1.5", 1000, true, -1500},
        {"0.0204", 1000, true, 20},     // rounds down
        {"0.0205", 1000, true, 21},     // half away from zero
        {"-0.0205", 1000, true, -21},
        {"0.0199999", 1000, true, 20},
        {"2.5", 1, true, 3},
        {"2.4999", 1, true, 2},
        {"12.25", 100, true, 1225},
        {"999999999999999", 1000, true, 999999999999999000},
        {"1000000000000000", 1000, false, 0},  // 16 integer digits
        {"1e3", 1000, false, 0},               // exponent not supported
        {"1.5", 3, false, 0},                  // scale not a power of ten
        {"1.5", 0, false, 0},
    };
    for (const auto& d : dec) {
        JsonReader r;
        CHECK(parse(r, d.text));
        int64_t v = -42;
        const bool ok = r.root().get_decimal(d.scale, &v);
        if (ok != d.ok || (ok && v != d.value))
            std::printf("  decimal %s x%lld -> %d %lld\n", d.text, (long long)d.scale, ok, (long long)v);
        CHECK(ok == d.ok && (!ok || v == d.value));
    }

    struct Int {
        const char* text;
        bool ok;
        int64_t value;
    };
    const Int ints[] = {
        {"9223372036854775807", true, INT64_MAX},
        {"-9223372036854775808", true, INT64_MIN},
        {"9223372036854775808", false, 0},
        {"-9223372036854775809", false, 0},
        {"-0", true, 0},
        {"5.0", false, 0},
        {"5e1", false, 0},
    };
    for (const auto& c : ints) {
        JsonReader r;
        CHECK(parse(r, c.text));
        int64_t v = 0;
        const bool ok = r.root().get(&v);
        CHECK(ok == c.ok && (!ok || v == c.value));
    }
}

static void test_strings() {
    JsonReader r;
    char buf[16];
    CHECK(parse(r, "[\"\\u00e4\", \"\\ud83d\\ude00\", \"\\u20ac\", \"a\\u0000b\", \"\\n\\t\\/\", \"\\u0041\"]"));
    const JsonValue a = r.root();
    CHECK(a[0].get(buf, sizeof(buf)) && std::strcmp(buf, "\xC3\xA4") == 0);
    CHECK(a[1].get(buf, sizeof(buf)) && std::strcmp(buf, "\xF0\x9F\x98\x80") == 0);
    CHECK(a[2].get(buf, sizeof(buf)) && std::strcmp(buf, "\xE2\x82\xAC") == 0);
    CHECK(!a[3].get(buf, sizeof(buf)));  // embedded NUL
    CHECK(a[4].get(buf, sizeof(buf)) && std::strcmp(buf, "\n\t/") == 0);
    CHECK(a[5].equals("A"));
    CHECK(!a[1].get(buf, 4));  // 4 bytes + NUL do not fit
    CHECK(a[1].get(buf, 5));
    // raw UTF-8 is passed through
    CHECK(parse(r, "\"K\xC3\xBC" "che\""));
    CHECK(r.root().equals("K\xC3\xBC" "che"));
}

static void test_round_trip() {
    Json j;
    j.obj().kv("text", "quote \" backslash \\ newline \n tab \t").kv("n", int64_t(-7)).kv("ml", 2.25, 3);
    j.arr("days").v("mon").v("fri").end().kv("flag", true).null("none").end();
    JsonReader r;
    CHECK(r.parse(j.c_str(), j.str().size(), g_tok, 64));
    char buf[64];
    CHECK(r.root()["text"].get(buf, sizeof(buf)) && std::strcmp(buf, "quote \" backslash \\ newline \n tab \t") == 0);
    int64_t v = 0;
    CHECK(r.root()["n"].get(&v) && v == -7);
    CHECK(r.root()["ml"].get_decimal(1000, &v) && v == 2250);
    CHECK(r.root()["days"][1].equals("fri"));
    CHECK(r.root()["none"].is_null());
}

// Mutated and truncated documents must never read outside the text
// (run with -fsanitize=address) and accepted ones must be consistent.
static void test_fuzz() {
    const std::string seed =
        "{\"power_loss\":\"skip_missed\",\"programs\":[{\"kind\":\"generator\",\"start\":\"08:00\","
        "\"count\":12,\"ml\":24.5,\"days\":{\"weekdays\":[\"mon\",\"fri\"]}},{\"t\":\"\\u00e4\\n\"}],"
        "\"x\":[-1.5e3,true,null,false]}";
    const char alphabet[] = "{}[]\":,\\u0123456789.-+eEtrufalsn \t";
    uint32_t rng = 12345;
    auto next = [&]() { return rng = rng * 1103515245u + 12345u; };
    int accepted = 0;
    bool consistent = true;
    for (int i = 0; i < 20000; ++i) {
        std::string s = seed;
        const int edits = 1 + static_cast<int>(next() >> 16) % 4;
        for (int e = 0; e < edits && !s.empty(); ++e) {
            const size_t at = (next() >> 8) % s.size();
            const char c = alphabet[(next() >> 16) % (sizeof(alphabet) - 1)];
            switch ((next() >> 20) % 3) {
                case 0: s[at] = c; break;
                case 1: s.insert(s.begin() + static_cast<long>(at), c); break;
                default: s.erase(at, 1); break;
            }
        }
        if (next() % 4 == 0) s.resize((next() >> 8) % (s.size() + 1));
        // exact-size heap copy so that ASan catches any overread
        char* buf = new char[s.size() ? s.size() : 1];
        std::memcpy(buf, s.data(), s.size());
        JsonReader r;
        if (r.parse(buf, s.size(), g_tok, 64)) {
            ++accepted;
            for (int k = 0; k < r.tokens_used(); ++k) {
                const JsonToken& tk = g_tok[k];
                if (tk.start > tk.end || tk.end > s.size() || tk.next <= static_cast<uint32_t>(k) ||
                    tk.next > static_cast<uint32_t>(r.tokens_used()))
                    consistent = false;
            }
            char tmp[256];
            int64_t v;
            const JsonValue root = r.root();
            for (int k = 0; k < root.size(); ++k) {
                root.key_at(k).get(tmp, sizeof(tmp));
                root.value_at(k).get(tmp, sizeof(tmp));
                root.value_at(k).get_decimal(1000, &v);
                root.value_at(k)[0].get(&v);
            }
        } else {
            CHECK(r.error() != nullptr && !r.root().valid());
        }
        delete[] buf;
    }
    CHECK(consistent);
    CHECK(accepted > 100);  // some mutations stay valid JSON
}

int main() {
    std::printf("JSON reader: valid documents\n");
    test_valid_documents();
    std::printf("JSON reader: invalid documents\n");
    test_invalid_documents();
    std::printf("JSON reader: access\n");
    test_access();
    std::printf("JSON reader: numbers\n");
    test_numbers();
    std::printf("JSON reader: strings\n");
    test_strings();
    std::printf("JSON reader: round trip with the writer\n");
    test_round_trip();
    std::printf("JSON reader: mutated documents\n");
    test_fuzz();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
