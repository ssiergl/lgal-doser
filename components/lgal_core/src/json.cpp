#include "lgal/json.h"

#include <cinttypes>
#include <cmath>
#include <cstdio>

namespace lgal {

Json& Json::open(const char* key, char c) {
    key_(key);
    s_ += c;
    stack_ += (c == '{') ? '}' : ']';
    first_ = true;
    return *this;
}

Json& Json::end() {
    if (!stack_.empty()) {
        s_ += stack_.back();
        stack_.pop_back();
    }
    first_ = false;
    return *this;
}

Json& Json::key_(const char* key) {
    if (!first_) s_ += ',';
    first_ = false;
    if (key) {
        str_(key);
        s_ += ':';
    }
    return *this;
}

Json& Json::str_(const char* v) {
    if (!v) {
        s_ += "null";
        return *this;
    }
    s_ += '"';
    for (const char* p = v; *p; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        switch (c) {
            case '"': s_ += "\\\""; break;
            case '\\': s_ += "\\\\"; break;
            case '\n': s_ += "\\n"; break;
            case '\r': s_ += "\\r"; break;
            case '\t': s_ += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    snprintf(b, sizeof(b), "\\u%04x", c);
                    s_ += b;
                } else {
                    s_ += static_cast<char>(c);
                }
        }
    }
    s_ += '"';
    return *this;
}

Json& Json::raw_(const char* v) {
    s_ += v;
    return *this;
}

Json& Json::kv(const char* key, long long v) {
    char b[24];
    snprintf(b, sizeof(b), "%lld", v);
    return key_(key).raw_(b);
}

Json& Json::kv(const char* key, double v, int decimals) {
    if (!std::isfinite(v)) return null(key);
    char b[32];
    snprintf(b, sizeof(b), "%.*f", decimals, v);
    return key_(key).raw_(b);
}

Json& Json::v(int64_t x) {
    char b[24];
    snprintf(b, sizeof(b), "%" PRId64, x);
    return key_(nullptr).raw_(b);
}

}  // namespace lgal
