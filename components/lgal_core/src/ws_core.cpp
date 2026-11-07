#include "lgal/ws_core.h"

#include <cstring>

namespace lgal::ws {

namespace {
char lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }

bool equal_nocase(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b)
        if (lower(*a) != lower(*b)) return false;
    return *a == *b;
}
}  // namespace

bool origin_allowed(const char* origin, const char* host) {
    if (!origin) return true;
    if (!host || !*host) return false;
    const char* rest = nullptr;
    if (std::strncmp(origin, "http://", 7) == 0) rest = origin + 7;
    else if (std::strncmp(origin, "https://", 8) == 0) rest = origin + 8;
    else return false;  // includes "null" (sandboxed frames, file://)
    return equal_nocase(rest, host);
}

uint32_t fnv1a(const char* s, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        h ^= static_cast<uint8_t>(s[i]);
        h *= 16777619u;
    }
    return h;
}

}  // namespace lgal::ws
