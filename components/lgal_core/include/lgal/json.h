#pragma once
// Minimal JSON writer for API responses (no parser, no allocation
// per field except the result string).
//
//   lgal::Json j;
//   j.obj().kv("a", 1).arr("list").v(true).v("x").end().end();
//   http::send_json(req, j.c_str());

#include <cstdint>
#include <string>

namespace lgal {

class Json {
public:
    Json() { s_.reserve(512); }

    Json& obj(const char* key = nullptr) { return open(key, '{'); }
    Json& arr(const char* key = nullptr) { return open(key, '['); }
    Json& end();

    Json& kv(const char* key, const char* v) { return key_(key).str_(v); }
    Json& kv(const char* key, const std::string& v) { return key_(key).str_(v.c_str()); }
    Json& kv(const char* key, bool v) { return key_(key).raw_(v ? "true" : "false"); }
    // Integer overloads use the fundamental types only: int64_t is `long` on
    // 64-bit Linux but `long long` on the ESP32 and macOS, so an int64_t
    // overload next to `long` would collide on one of them.
    Json& kv(const char* key, int v) { return kv(key, static_cast<long long>(v)); }
    Json& kv(const char* key, unsigned v) { return kv(key, static_cast<long long>(v)); }
    Json& kv(const char* key, long v) { return kv(key, static_cast<long long>(v)); }
    Json& kv(const char* key, unsigned long v) { return kv(key, static_cast<long long>(v)); }
    Json& kv(const char* key, unsigned long long v) { return kv(key, static_cast<long long>(v)); }
    Json& kv(const char* key, long long v);
    Json& kv(const char* key, double v, int decimals = 2);
    Json& null(const char* key) { return key_(key).raw_("null"); }

    Json& v(const char* x) { return key_(nullptr).str_(x); }
    Json& v(bool x) { return key_(nullptr).raw_(x ? "true" : "false"); }
    Json& v(int64_t x);
    Json& v(int x) { return v(static_cast<int64_t>(x)); }

    const char* c_str() const { return s_.c_str(); }
    const std::string& str() const { return s_; }

private:
    Json& open(const char* key, char c);
    Json& key_(const char* key);
    Json& str_(const char* v);
    Json& raw_(const char* v);

    std::string s_;
    std::string stack_;  // open brackets
    bool first_ = true;
};

}  // namespace lgal
