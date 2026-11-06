#include "lgal/json_reader.h"

#include <cstring>

namespace lgal {

namespace {

int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool hex4(const char* p, uint32_t* out) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        const int h = hex(p[i]);
        if (h < 0) return false;
        v = v << 4 | static_cast<uint32_t>(h);
    }
    *out = v;
    return true;
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

// Unescapes an already validated string body. false if the result does not
// fit (incl. NUL) or contains U+0000.
bool unescape(const char* s, size_t n, char* out, size_t cap) {
    size_t o = 0;
    auto put = [&](char c) {
        if (o + 1 >= cap) return false;
        out[o++] = c;
        return true;
    };
    for (size_t i = 0; i < n; ++i) {
        char c = s[i];
        if (c != '\\') {
            if (!put(c)) return false;
            continue;
        }
        c = s[++i];
        switch (c) {
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'u': {
                uint32_t cp = 0, lo = 0;
                hex4(s + i + 1, &cp);
                i += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF) {  // surrogate pair (validated by parse)
                    hex4(s + i + 3, &lo);
                    i += 6;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                if (cp == 0) return false;
                if (cp < 0x80) {
                    if (!put(static_cast<char>(cp))) return false;
                } else if (cp < 0x800) {
                    if (!put(static_cast<char>(0xC0 | cp >> 6)) || !put(static_cast<char>(0x80 | (cp & 0x3F))))
                        return false;
                } else if (cp < 0x10000) {
                    if (!put(static_cast<char>(0xE0 | cp >> 12)) || !put(static_cast<char>(0x80 | (cp >> 6 & 0x3F))) ||
                        !put(static_cast<char>(0x80 | (cp & 0x3F))))
                        return false;
                } else {
                    if (!put(static_cast<char>(0xF0 | cp >> 18)) || !put(static_cast<char>(0x80 | (cp >> 12 & 0x3F))) ||
                        !put(static_cast<char>(0x80 | (cp >> 6 & 0x3F))) || !put(static_cast<char>(0x80 | (cp & 0x3F))))
                        return false;
                }
                continue;
            }
            default: break;  // " \ /
        }
        if (!put(c)) return false;
    }
    out[o] = '\0';
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Parser

bool JsonReader::fail(const char* e) {
    if (!error_) {
        error_ = e;
        error_pos_ = pos_;
    }
    return false;
}

void JsonReader::ws() {
    while (pos_ < len_) {
        const char c = text_[pos_];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
        ++pos_;
    }
}

int JsonReader::add(JsonType t, size_t start) {
    if (count_ >= max_) return -1;
    JsonToken& k = tok_[count_];
    k.type = t;
    k.reserved = 0;
    k.children = 0;
    k.start = static_cast<uint32_t>(start);
    k.end = static_cast<uint32_t>(start);
    k.next = static_cast<uint32_t>(count_ + 1);
    return count_++;
}

bool JsonReader::literal(const char* word) {
    const size_t n = std::strlen(word);
    if (len_ - pos_ < n || std::memcmp(text_ + pos_, word, n) != 0) return fail("syntax");
    const int i = add(word[0] == 'n' ? JsonType::Null : JsonType::Bool, pos_);
    if (i < 0) return fail("too_many_tokens");
    pos_ += n;
    tok_[i].end = static_cast<uint32_t>(pos_);
    return true;
}

bool JsonReader::number() {
    const size_t start = pos_;
    auto digits = [&]() {
        const size_t s = pos_;
        while (pos_ < len_ && is_digit(text_[pos_])) ++pos_;
        return pos_ > s;
    };
    if (text_[pos_] == '-') ++pos_;
    if (pos_ >= len_) return fail("syntax");
    if (text_[pos_] == '0') {
        ++pos_;
    } else if (!digits()) {
        return fail("syntax");
    }
    if (pos_ < len_ && text_[pos_] == '.') {
        ++pos_;
        if (!digits()) return fail("syntax");
    }
    if (pos_ < len_ && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
        ++pos_;
        if (pos_ < len_ && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
        if (!digits()) return fail("syntax");
    }
    const int i = add(JsonType::Number, start);
    if (i < 0) return fail("too_many_tokens");
    tok_[i].end = static_cast<uint32_t>(pos_);
    return true;
}

bool JsonReader::string() {
    ++pos_;  // opening quote
    const size_t start = pos_;
    while (true) {
        if (pos_ >= len_) return fail("syntax");
        const char c = text_[pos_];
        if (c == '"') break;
        if (static_cast<unsigned char>(c) < 0x20) return fail("syntax");
        if (c != '\\') {
            ++pos_;
            continue;
        }
        if (++pos_ >= len_) return fail("syntax");
        const char e = text_[pos_];
        if (e == 'u') {
            uint32_t cp = 0, lo = 0;
            if (len_ - pos_ < 5 || !hex4(text_ + pos_ + 1, &cp)) return fail("syntax");
            pos_ += 5;
            if (cp >= 0xDC00 && cp <= 0xDFFF) return fail("syntax");  // lone low surrogate
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (len_ - pos_ < 6 || text_[pos_] != '\\' || text_[pos_ + 1] != 'u' || !hex4(text_ + pos_ + 2, &lo) ||
                    lo < 0xDC00 || lo > 0xDFFF)
                    return fail("syntax");
                pos_ += 6;
            }
        } else if (std::strchr("\"\\/bfnrt", e) && e) {
            ++pos_;
        } else {
            return fail("syntax");
        }
    }
    const int i = add(JsonType::String, start);
    if (i < 0) return fail("too_many_tokens");
    tok_[i].end = static_cast<uint32_t>(pos_);
    ++pos_;  // closing quote
    return true;
}

bool JsonReader::value(int depth) {
    ws();
    if (pos_ >= len_) return fail("syntax");
    const char c = text_[pos_];
    if (c == '"') return string();
    if (c == 't') return literal("true");
    if (c == 'f') return literal("false");
    if (c == 'n') return literal("null");
    if (c == '-' || is_digit(c)) return number();
    if (c != '{' && c != '[') return fail("syntax");

    const bool obj = c == '{';
    const char close = obj ? '}' : ']';
    if (depth >= kMaxDepth) return fail("too_deep");
    const int i = add(obj ? JsonType::Object : JsonType::Array, pos_);
    if (i < 0) return fail("too_many_tokens");
    ++pos_;
    ws();
    if (pos_ < len_ && text_[pos_] == close) {
        ++pos_;
    } else {
        while (true) {
            ws();
            if (obj) {
                if (pos_ >= len_ || text_[pos_] != '"') return fail("syntax");
                if (!string()) return false;
                ws();
                if (pos_ >= len_ || text_[pos_] != ':') return fail("syntax");
                ++pos_;
            }
            if (!value(depth + 1)) return false;
            if (tok_[i].children == UINT16_MAX) return fail("too_many_tokens");
            ++tok_[i].children;
            ws();
            if (pos_ >= len_) return fail("syntax");
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] != close) return fail("syntax");
            ++pos_;
            break;
        }
    }
    tok_[i].end = static_cast<uint32_t>(pos_);
    tok_[i].next = static_cast<uint32_t>(count_);
    return true;
}

bool JsonReader::parse(const char* text, size_t len, JsonToken* tokens, int max_tokens) {
    text_ = text;
    len_ = len;
    pos_ = 0;
    tok_ = tokens;
    max_ = max_tokens;
    count_ = 0;
    error_ = nullptr;
    error_pos_ = 0;
    if (!text || len > UINT32_MAX - 1) {
        fail("too_large");
    } else {
        ws();
        if (pos_ >= len_) {
            fail("empty");
        } else if (value(0)) {
            ws();
            if (pos_ != len_) fail("trailing_data");
        }
    }
    if (error_) count_ = 0;
    return !error_;
}

// ---------------------------------------------------------------------------
// Values

const JsonToken* JsonValue::tok() const { return doc_ ? &doc_->tok_[index_] : nullptr; }

JsonType JsonValue::type() const { return doc_ ? tok()->type : JsonType::Invalid; }

int JsonValue::size() const {
    const JsonType t = type();
    return t == JsonType::Array || t == JsonType::Object ? tok()->children : 0;
}

JsonValue JsonValue::operator[](int index) const {
    if (type() != JsonType::Array || index < 0 || index >= size()) return {};
    int i = index_ + 1;
    for (int n = 0; n < index; ++n) i = static_cast<int>(doc_->tok_[i].next);
    return JsonValue(doc_, i);
}

JsonValue JsonValue::key_at(int index) const {
    if (type() != JsonType::Object || index < 0 || index >= size()) return {};
    int i = index_ + 1;
    for (int n = 0; n < index; ++n) i = static_cast<int>(doc_->tok_[i + 1].next);
    return JsonValue(doc_, i);
}

JsonValue JsonValue::value_at(int index) const {
    const JsonValue k = key_at(index);
    return k.valid() ? JsonValue(doc_, k.index_ + 1) : JsonValue();
}

JsonValue JsonValue::operator[](const char* key) const {
    if (type() != JsonType::Object || !key) return {};
    int i = index_ + 1;
    for (int n = 0; n < size(); ++n) {
        if (JsonValue(doc_, i).equals(key)) return JsonValue(doc_, i + 1);
        i = static_cast<int>(doc_->tok_[i + 1].next);
    }
    return {};
}

bool JsonValue::get(bool* out) const {
    if (type() != JsonType::Bool) return false;
    *out = doc_->text_[tok()->start] == 't';
    return true;
}

bool JsonValue::get(int64_t* out) const {
    if (type() != JsonType::Number) return false;
    const char* p = doc_->text_ + tok()->start;
    const char* e = doc_->text_ + tok()->end;
    const bool neg = *p == '-';
    if (neg) ++p;
    const uint64_t limit = neg ? uint64_t(INT64_MAX) + 1 : uint64_t(INT64_MAX);
    uint64_t v = 0;
    for (; p < e; ++p) {
        if (!is_digit(*p)) return false;  // fraction or exponent
        const uint64_t d = static_cast<uint64_t>(*p - '0');
        if (v > (limit - d) / 10) return false;
        v = v * 10 + d;
    }
    *out = neg ? static_cast<int64_t>(0 - v) : static_cast<int64_t>(v);
    return true;
}

bool JsonValue::get_decimal(int64_t scale, int64_t* out) const {
    if (type() != JsonType::Number || scale < 1) return false;
    int frac_digits = 0;  // scale must be a power of ten
    for (int64_t s = scale; s > 1; s /= 10) {
        if (s % 10) return false;
        ++frac_digits;
    }
    const char* p = doc_->text_ + tok()->start;
    const char* e = doc_->text_ + tok()->end;
    const bool neg = *p == '-';
    if (neg) ++p;
    int64_t ip = 0;
    int n = 0;
    for (; p < e && is_digit(*p); ++p) {
        if (++n > 15) return false;
        ip = ip * 10 + (*p - '0');
    }
    int64_t fp = 0;
    bool round_up = false;
    if (p < e && *p == '.') {
        ++p;
        int k = 0;
        for (; p < e && is_digit(*p); ++p, ++k) {
            if (k < frac_digits) fp = fp * 10 + (*p - '0');
            else if (k == frac_digits) round_up = *p >= '5';
        }
        for (; k < frac_digits; ++k) fp *= 10;
    } else {
        for (int k = 0; k < frac_digits; ++k) fp *= 10;
    }
    if (p != e) return false;  // exponent
    int64_t v = 0;
    if (__builtin_mul_overflow(ip, scale, &v) || __builtin_add_overflow(v, fp + (round_up ? 1 : 0), &v)) return false;
    *out = neg ? -v : v;
    return true;
}

bool JsonValue::get(char* out, size_t len) const {
    if (type() != JsonType::String || !out || len == 0) return false;
    return unescape(doc_->text_ + tok()->start, tok()->end - tok()->start, out, len);
}

bool JsonValue::equals(const char* s) const {
    if (type() != JsonType::String || !s) return false;
    const char* raw = doc_->text_ + tok()->start;
    const size_t n = tok()->end - tok()->start;
    if (!std::memchr(raw, '\\', n)) return std::strlen(s) == n && std::memcmp(raw, s, n) == 0;
    char buf[128];
    return unescape(raw, n, buf, sizeof(buf)) && std::strcmp(buf, s) == 0;
}

}  // namespace lgal
