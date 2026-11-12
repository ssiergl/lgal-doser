// Host test of the release manifest, version compare (lgal/ota_manifest.h)
// and board ID of an image (lgal/ota_image.h)
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "lgal/ota_image.h"
#include "lgal/ota_manifest.h"

using namespace lgal::ota;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(cond)) {                                                    \
            ++g_fail;                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                 \
    } while (0)

static int cmp(const char* a, const char* b) {
    Version va, vb;
    if (!parse_version(a, &va) || !parse_version(b, &vb)) return 99;
    return compare_versions(va, vb);
}

static void test_versions() {
    std::printf("Versions\n");
    Version v;
    CHECK(parse_version("0.1.0", &v) && v.major == 0 && v.minor == 1 && v.patch == 0 && v.pre.empty());
    CHECK(parse_version("v12.3.45-rc.1+g1234", &v) && v.major == 12 && v.patch == 45 && v.pre == "rc.1");
    const char* bad[] = {"", "1", "1.2", "1.2.x", "1.2.3.4", "1.2.3-", "1.2.3-a..b", "1.2.3+", "a1.2.3",
                         "1.2.3 ", "1234567.0.0", "1.2.3-rc_1", "1.0.0-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
    for (const char* s : bad) {
        const bool ok = parse_version(s, &v);
        if (ok) std::printf("  accepted '%s'\n", s);
        CHECK(!ok);
    }
    CHECK(cmp("0.1.0", "0.1.0") == 0);
    CHECK(cmp("v0.1.0", "0.1.0+other") == 0);
    CHECK(cmp("0.1.0", "0.2.0") < 0);
    CHECK(cmp("0.10.0", "0.9.9") > 0);
    CHECK(cmp("1.0.0", "0.99.99") > 0);
    CHECK(cmp("0.1.1", "0.1.0") > 0);
    // pre-releases
    CHECK(cmp("1.0.0-rc.1", "1.0.0") < 0);
    CHECK(cmp("1.0.0", "1.0.0-rc.1") > 0);
    CHECK(cmp("1.0.0-rc.2", "1.0.0-rc.10") < 0);
    CHECK(cmp("1.0.0-alpha", "1.0.0-alpha.1") < 0);
    CHECK(cmp("1.0.0-alpha.1", "1.0.0-beta") < 0);
    CHECK(cmp("1.0.0-1", "1.0.0-alpha") < 0);
    CHECK(cmp("1.0.0-rc.01", "1.0.0-rc.1") == 0);
    CHECK(cmp("0.9.0", "1.0.0-rc.1") < 0);
}

static void test_urls() {
    std::printf("Relative addresses\n");
    const char* m = "https://github.com/o/r/releases/latest/download/manifest.json";
    CHECK(resolve_url(m, "fw-N16R8.bin") == "https://github.com/o/r/releases/latest/download/fw-N16R8.bin");
    CHECK(resolve_url(m, "https://cdn.example.com/x.bin") == "https://cdn.example.com/x.bin");
    CHECK(resolve_url(m, "/o/r/x.bin") == "https://github.com/o/r/x.bin");
    CHECK(resolve_url(m, "//cdn.example.com/x.bin") == "https://cdn.example.com/x.bin");
    CHECK(resolve_url("http://host.lan/m.json?x=a/b#f", "fw.bin") == "http://host.lan/fw.bin");
    CHECK(resolve_url("https://host.lan", "fw.bin") == "https://host.lan/fw.bin");
    CHECK(resolve_url("https://host.lan?q", "/fw.bin") == "https://host.lan/fw.bin");
    CHECK(resolve_url("ftp://host/m.json", "fw.bin").empty());
    CHECK(resolve_url(m, "").empty());
}

static const char* kSha = "00112233445566778899aabbccddeeff00112233445566778899AABBCCDDEEFF";

static std::string manifest(const char* builds, const char* extra = "") {
    return std::string("{\"version\":\"0.2.0\",") + extra + "\"builds\":[" + builds + "]}";
}

static std::string build(const char* variant, const char* url = "fw.bin", const char* sha = kSha,
                         const char* more = "", const char* board = "d5v1") {
    return std::string("{\"board\":\"") + board + "\",\"variant\":\"" + variant + "\",\"url\":\"" + url +
           "\",\"sha256\":\"" + sha + "\"" + more + "}";
}

static const char* parse(const std::string& j, Release* r, const char* variant = "N16R8", const char* board = "d5v1") {
    return parse_manifest(j.data(), j.size(), board, variant, "https://example.com/rel/manifest.json", r);
}

static void test_manifest() {
    std::printf("Manifest\n");
    Release r;
    const std::string all = build("N8R8", "a.bin") + "," + build("N16R8", "b.bin", kSha, ",\"size\":1500000") + "," +
                            build("N32R8", "https://other.example.com/c.bin");
    const std::string ok = manifest(all.c_str(), "\"notes\":\"https://example.com/rel/v0.2.0\",");
    CHECK(parse(ok, &r) == nullptr);
    CHECK(r.version == "0.2.0" && r.url == "https://example.com/rel/b.bin" && r.size == 1500000);
    CHECK(r.notes == "https://example.com/rel/v0.2.0");
    CHECK(r.sha256[0] == 0x00 && r.sha256[5] == 0x55 && r.sha256[15] == 0xFF && r.sha256[31] == 0xFF);
    CHECK(parse(ok, &r, "N8R8") == nullptr && r.url == "https://example.com/rel/a.bin" && r.size == 0);
    CHECK(parse(ok, &r, "N32R8") == nullptr && r.url == "https://other.example.com/c.bin");
    CHECK(!std::strcmp(parse(ok, &r, "N4R2"), "no_build"));
    CHECK(!std::strcmp(parse(ok, &r, "N16R8", "d2v1"), "no_build"));

    // two boards (a hypothetical 2-pump board d2v1): each device takes its own
    const std::string two = build("N16R8", "lgal_doser-d5v1-N16R8.bin") + "," +
                            build("N16R8", "lgal_doser-d2v1-N16R8.bin", kSha, "", "d2v1") + "," +
                            build("N8R8", "lgal_doser-d2v1-N8R8.bin", kSha, "", "d2v1");
    CHECK(parse(manifest(two.c_str()), &r) == nullptr && r.url == "https://example.com/rel/lgal_doser-d5v1-N16R8.bin");
    CHECK(parse(manifest(two.c_str()), &r, "N16R8", "d2v1") == nullptr &&
          r.url == "https://example.com/rel/lgal_doser-d2v1-N16R8.bin");
    CHECK(parse(manifest(two.c_str()), &r, "N8R8", "d2v1") == nullptr &&
          r.url == "https://example.com/rel/lgal_doser-d2v1-N8R8.bin");
    CHECK(!std::strcmp(parse(manifest(two.c_str()), &r, "N8R8"), "no_build"));
    // a build without board (manifest of a firmware before board IDs) fits no device
    CHECK(!std::strcmp(parse(manifest("{\"variant\":\"N16R8\",\"url\":\"fw.bin\",\"sha256\":\"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\"}"), &r),
                       "no_build"));

    // notes only as a web address (shown as a link)
    Release n;
    CHECK(parse(manifest(build("N16R8").c_str(), "\"notes\":\"javascript:alert(1)\","), &n) == nullptr && n.notes.empty());

    struct {
        std::string json;
        const char* code;
    } bad[] = {
        {"", "manifest_invalid"},
        {"[]", "manifest_invalid"},
        {"{\"version\":\"0.2.0\"}", "manifest_invalid"},
        {"{\"version\":\"0.2.0\",\"builds\":{}}", "manifest_invalid"},
        {"{\"version\":2,\"builds\":[]}", "manifest_invalid"},
        {"{\"version\":\"2\",\"builds\":[]}", "invalid_version"},
        {manifest(""), "no_build"},
        {manifest(build("N16R8", "fw.bin", "0011").c_str()), "invalid_sha256"},
        {manifest(build("N16R8", "fw.bin", "zz112233445566778899aabbccddeeff00112233445566778899aabbccddeeff").c_str()),
         "invalid_sha256"},
        {manifest("{\"board\":\"d5v1\",\"variant\":\"N16R8\",\"sha256\":\"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\"}"),
         "invalid_url"},
        {manifest(build("N16R8", "https://").c_str()), "invalid_url"},
        {manifest(build("N16R8", "a b.bin").c_str()), "invalid_url"},
        {manifest(build("N16R8", "fw.bin", kSha, ",\"size\":0").c_str()), "manifest_invalid"},
        {manifest(build("N16R8", "fw.bin", kSha, ",\"size\":\"1\"").c_str()), "manifest_invalid"},
    };
    for (const auto& b : bad) {
        Release x;
        const char* e = parse(b.json, &x);
        if (!e || std::strcmp(e, b.code)) std::printf("  %s -> %s (want %s)\n", b.json.c_str(), e ? e : "ok", b.code);
        CHECK(e && !std::strcmp(e, b.code));
    }
    // too large
    std::string big = manifest(build("N16R8").c_str(), std::string("\"x\":\"" + std::string(kMaxManifest, 'a') + "\",").c_str());
    CHECK(!std::strcmp(parse(big, &r), "manifest_invalid"));
}

static void test_variants() {
    std::printf("Variants\n");
    CHECK(!std::strcmp(variant_for_flash_mb(8), "N8R8"));
    CHECK(!std::strcmp(variant_for_flash_mb(16), "N16R8"));
    CHECK(!std::strcmp(variant_for_flash_mb(32), "N32R8"));
    CHECK(variant_for_flash_mb(4) == nullptr);
}

static std::string desc(uint32_t magic, const char* board, size_t len = sizeof(BoardDesc)) {
    BoardDesc d{};
    d.magic = magic;
    std::memcpy(d.board, board, std::min(std::strlen(board), sizeof(d.board)));
    std::string s(reinterpret_cast<const char*>(&d), sizeof(d));
    return s.substr(0, len);
}

static std::string board_of(const std::string& s) {
    return image_board(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

static void test_image_board() {
    std::printf("Image board\n");
    CHECK(board_of(desc(kBoardDescMagic, "d5v1")) == "d5v1");
    CHECK(board_of(desc(kBoardDescMagic, "d2v1")) == "d2v1");
    CHECK(board_of(desc(kBoardDescMagic, "abcdefghij12345")) == "abcdefghij12345");  // 15 characters
    CHECK(board_of(desc(kBoardDescMagic, "abcdefghij123456")).empty());              // not terminated
    CHECK(board_of(desc(kBoardDescMagic, "")).empty());
    CHECK(board_of(desc(kBoardDescMagic, "D5V1")).empty());
    CHECK(board_of(desc(kBoardDescMagic, "d5 v1")).empty());
    CHECK(board_of(desc(kBoardDescMagic, "../x")).empty());
    CHECK(board_of(desc(0, "d5v1")).empty());                  // image before board IDs: no descriptor
    CHECK(board_of(desc(kBoardDescMagic, "d5v1", 31)).empty());  // too short
    CHECK(image_board(nullptr, 32).empty());
    CHECK(valid_board_id("d5v1") && !valid_board_id("") && !valid_board_id("d5-v1"));
}

int main() {
    test_versions();
    test_urls();
    test_manifest();
    test_variants();
    test_image_board();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
