#include "lgal/device_name.h"

#include <cstring>

namespace lgal::devname {

bool valid_name(const char* s) {
    if (!s) return false;
    const size_t n = std::strlen(s);
    if (n > kNameMax) return false;
    bool text = false;
    for (size_t i = 0; i < n; ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x20 || c == 0x7f) return false;
        if (c != ' ') text = true;
    }
    return n == 0 || text;
}

bool valid_host(const char* s) {
    if (!s) return false;
    const size_t n = std::strlen(s);
    if (n == 0 || n > kHostMax || s[0] == '-' || s[n - 1] == '-') return false;
    for (size_t i = 0; i < n; ++i) {
        const char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    return true;
}

namespace {

// Latin-1 letter in UTF-8 (lead byte 0xC3) -> ASCII, nullptr = no letter
const char* latin1(unsigned char b) {
    if (b == 0x9f) return "ss";
    if (b < 0xa0) b = static_cast<unsigned char>(b + 0x20);  // upper -> lower case
    switch (b) {
        case 0xa4: return "ae";
        case 0xb6: return "oe";
        case 0xbc: return "ue";
        case 0xa0: case 0xa1: case 0xa2: case 0xa3: case 0xa5: return "a";
        case 0xa6: return "ae";
        case 0xa7: return "c";
        case 0xa8: case 0xa9: case 0xaa: case 0xab: return "e";
        case 0xac: case 0xad: case 0xae: case 0xaf: return "i";
        case 0xb1: return "n";
        case 0xb2: case 0xb3: case 0xb4: case 0xb5: case 0xb8: return "o";
        case 0xb9: case 0xba: case 0xbb: return "u";
        case 0xbd: case 0xbf: return "y";
        default: return nullptr;
    }
}

}  // namespace

std::string slug(const char* name) {
    std::string out;
    if (!name) return out;
    bool dash = false;  // a separator is pending
    const auto put = [&](const char* s) {
        if (dash && !out.empty()) out += '-';
        dash = false;
        out += s;
    };
    for (size_t i = 0; name[i];) {
        const auto c = static_cast<unsigned char>(name[i]);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            const char s[2] = {static_cast<char>(c), 0};
            put(s);
            ++i;
        } else if (c >= 'A' && c <= 'Z') {
            const char s[2] = {static_cast<char>(c + 32), 0};
            put(s);
            ++i;
        } else if (c == 0xc3 && name[i + 1]) {
            if (const char* s = latin1(static_cast<unsigned char>(name[i + 1]))) put(s);
            else dash = true;
            i += 2;
        } else {
            dash = true;
            ++i;
            while ((static_cast<unsigned char>(name[i]) & 0xc0) == 0x80) ++i;  // rest of a UTF-8 sequence
        }
    }
    if (out.size() > kHostMax) out.resize(kHostMax);
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

std::string effective(const char* name, const char* host, const std::string& fallback) {
    if (host && host[0]) return host;
    std::string s = slug(name);
    return s.empty() ? fallback : s;
}

}  // namespace lgal::devname
