#pragma once
// Release manifest of the fixed update source (pure logic, host-testable:
// tests/host/test_ota_manifest.cpp).
//
// The release build publishes one manifest next to the firmware files, e.g.
// https://github.com/<owner>/<repo>/releases/latest/download/manifest.json:
//
//   {"version":"0.2.0",
//    "notes":"https://github.com/<owner>/<repo>/releases/tag/v0.2.0",
//    "builds":[{"variant":"N16R8","url":"lgal_doser-N16R8.bin",
//               "sha256":"<64 hex digits>","size":1234567}, …]}
//
// "url" is absolute or relative to the manifest address; "notes" and "size"
// are optional. The device picks the build of its own variant (flash size)
// and checks the SHA-256 of the download before switching to it.

#include <cstddef>
#include <cstdint>
#include <string>

namespace lgal::ota {

constexpr size_t kMaxManifest = 4096;  // bytes
constexpr size_t kMaxVersion = 31;
constexpr size_t kMaxReleaseUrl = 512;

// "1.2.3", "v1.2.3", "1.2.3-rc.1" (pre-release), "1.2.3+build" (ignored)
struct Version {
    uint32_t major = 0, minor = 0, patch = 0;
    std::string pre;  // "" = release
};
bool parse_version(const char* s, Version* out);
// < 0: a older than b, 0: same, > 0: a newer. A pre-release is older than
// its release; pre-releases compare by their dot-separated parts.
int compare_versions(const Version& a, const Version& b);

struct Release {
    std::string version;  // as published (checked with parse_version)
    std::string notes;    // release page (http/https), "" = none
    std::string url;      // firmware file, absolute
    uint8_t sha256[32] = {};
    uint32_t size = 0;    // 0 = not given
};

// Release of the given variant from a manifest. nullptr = ok; otherwise
// "manifest_invalid", "invalid_version", "no_build" (variant missing),
// "invalid_sha256" or "invalid_url".
const char* parse_manifest(const char* json, size_t len, const char* variant, const char* manifest_url,
                           Release* out);

// ref relative to base (RFC 3986 without dot segments): absolute http(s)
// addresses are kept, "/path" replaces the path, anything else replaces
// the last path segment. "" if base is no http(s) address.
std::string resolve_url(const char* base, const char* ref);

// Build variant of the module by flash size: 8 -> "N8R8", 16 -> "N16R8",
// 32 -> "N32R8" (all with 8 MB octal PSRAM), nullptr otherwise.
const char* variant_for_flash_mb(uint32_t mb);

}  // namespace lgal::ota
