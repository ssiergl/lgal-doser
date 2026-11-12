#include "lgal/ota_manifest.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <strings.h>
#include <vector>

#include "lgal/json_reader.h"

namespace lgal::ota {

namespace {

constexpr int kMaxTokens = 160;  // ~ 12 per build, room for a dozen

bool is_ident(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-';
}

// Up to 6 digits, at least one
bool number(const char*& p, uint32_t* out) {
    uint32_t v = 0;
    int n = 0;
    while (*p >= '0' && *p <= '9') {
        if (++n > 6) return false;
        v = v * 10 + uint32_t(*p++ - '0');
    }
    *out = v;
    return n > 0;
}

// Dot-separated identifiers, none empty
bool idents(const char* s, size_t n) {
    if (!n || s[0] == '.' || s[n - 1] == '.') return false;
    for (size_t i = 0; i < n; ++i)
        if (!is_ident(s[i]) || (s[i] == '.' && s[i + 1] == '.')) return false;
    return true;
}

bool all_digits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

int compare_part(const std::string& a, const std::string& b) {
    const bool na = all_digits(a), nb = all_digits(b);
    if (na && nb) {  // numerically: shorter (without leading zeros) is smaller
        const size_t za = a.find_first_not_of('0'), zb = b.find_first_not_of('0');
        const std::string ta = za == std::string::npos ? "" : a.substr(za);
        const std::string tb = zb == std::string::npos ? "" : b.substr(zb);
        if (ta.size() != tb.size()) return ta.size() < tb.size() ? -1 : 1;
        return ta.compare(tb) < 0 ? -1 : ta.compare(tb) > 0 ? 1 : 0;
    }
    if (na != nb) return na ? -1 : 1;  // numeric parts sort before words
    const int c = a.compare(b);
    return c < 0 ? -1 : c > 0 ? 1 : 0;
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> parts;
    size_t i = 0;
    for (size_t dot; (dot = s.find('.', i)) != std::string::npos; i = dot + 1) parts.push_back(s.substr(i, dot - i));
    parts.push_back(s.substr(i));
    return parts;
}

bool http_url(const std::string& s) {
    return strncasecmp(s.c_str(), "http://", 7) == 0 || strncasecmp(s.c_str(), "https://", 8) == 0;
}

// Absolute http(s) address with a host, printable ASCII, not too long
bool valid_url(const std::string& s) {
    if (s.size() > kMaxReleaseUrl || !http_url(s)) return false;
    for (char c : s)
        if (static_cast<unsigned char>(c) <= ' ' || static_cast<unsigned char>(c) >= 0x7F) return false;
    const char h = s[s.find("://") + 3];
    return h && h != '/' && h != ':' && h != '?' && h != '#';
}

int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

bool parse_version(const char* s, Version* out) {
    if (!s) return false;
    const char* p = s;
    if (*p == 'v' || *p == 'V') ++p;
    Version v;
    if (!number(p, &v.major) || *p++ != '.' || !number(p, &v.minor) || *p++ != '.' || !number(p, &v.patch))
        return false;
    if (*p == '-') {
        const char* q = ++p;
        while (*p && *p != '+') ++p;
        if (!idents(q, size_t(p - q))) return false;
        v.pre.assign(q, size_t(p - q));
    }
    if (*p == '+') {
        const char* q = ++p;
        p += std::strlen(p);
        if (!idents(q, size_t(p - q))) return false;
    }
    if (*p || size_t(p - s) > kMaxVersion) return false;
    *out = v;
    return true;
}

int compare_versions(const Version& a, const Version& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    if (a.pre.empty() || b.pre.empty()) return a.pre.empty() == b.pre.empty() ? 0 : a.pre.empty() ? 1 : -1;
    const std::vector<std::string> pa = split(a.pre), pb = split(b.pre);
    for (size_t i = 0; i < pa.size() && i < pb.size(); ++i)
        if (const int c = compare_part(pa[i], pb[i])) return c;
    return pa.size() == pb.size() ? 0 : pa.size() < pb.size() ? -1 : 1;
}

std::string resolve_url(const char* base, const char* ref) {
    const std::string b(base ? base : ""), r(ref ? ref : "");
    if (r.empty()) return "";
    if (http_url(r)) return r;
    if (!http_url(b)) return "";
    const size_t host = b.find("://") + 3;
    if (r.compare(0, 2, "//") == 0) return b.substr(0, host - 2) + r;  // same scheme
    const size_t path = std::min(b.find_first_of("/?#", host), b.size());
    if (r[0] == '/') return b.substr(0, path) + r;
    std::string dir = b.substr(0, std::min(b.find_first_of("?#", host), b.size()));
    const size_t slash = dir.rfind('/');
    if (slash == std::string::npos || slash < host) return dir + "/" + r;
    return dir.substr(0, slash + 1) + r;
}

const char* parse_manifest(const char* json, size_t len, const char* board, const char* variant,
                           const char* manifest_url, Release* out) {
    if (!json || len == 0 || len > kMaxManifest) return "manifest_invalid";
    std::unique_ptr<JsonToken[]> tok(new JsonToken[kMaxTokens]);
    JsonReader doc;
    if (!doc.parse(json, len, tok.get(), kMaxTokens) || doc.root().type() != JsonType::Object)
        return "manifest_invalid";
    const JsonValue root = doc.root();

    char buf[kMaxReleaseUrl + 1];
    Release rel;
    Version v;
    if (!root["version"].get(buf, kMaxVersion + 1)) return "manifest_invalid";
    if (!parse_version(buf, &v)) return "invalid_version";
    rel.version = buf;
    // only a web address: the UI shows it as a link
    if (root["notes"].get(buf, sizeof(buf)) && valid_url(buf)) rel.notes = buf;

    const JsonValue builds = root["builds"];
    if (builds.type() != JsonType::Array) return "manifest_invalid";
    JsonValue b;
    for (int i = 0; i < builds.size(); ++i) {
        if (builds[i]["board"].equals(board) && builds[i]["variant"].equals(variant)) {
            b = builds[i];
            break;
        }
    }
    if (!b.valid()) return "no_build";

    char sha[65];
    if (!b["sha256"].get(sha, sizeof(sha)) || std::strlen(sha) != 64) return "invalid_sha256";
    for (int i = 0; i < 32; ++i) {
        const int hi = hex(sha[2 * i]), lo = hex(sha[2 * i + 1]);
        if (hi < 0 || lo < 0) return "invalid_sha256";
        rel.sha256[i] = uint8_t(hi << 4 | lo);
    }
    if (!b["url"].get(buf, sizeof(buf))) return "invalid_url";
    rel.url = resolve_url(manifest_url, buf);
    if (!valid_url(rel.url)) return "invalid_url";
    const JsonValue vs = b["size"];
    if (vs.valid()) {
        int64_t n = 0;
        if (!vs.get(&n) || n <= 0 || n > (int64_t(1) << 26)) return "manifest_invalid";
        rel.size = uint32_t(n);
    }
    *out = std::move(rel);
    return nullptr;
}

const char* variant_for_flash_mb(uint32_t mb) {
    switch (mb) {
        case 8: return "N8R8";
        case 16: return "N16R8";
        case 32: return "N32R8";
        default: return nullptr;
    }
}

}  // namespace lgal::ota
