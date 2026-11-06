#pragma once
// Minimal JSON reader for API request bodies (counterpart to json.h).
//
// parse() validates the complete document (RFC 8259, max nesting depth
// kMaxDepth) and indexes it into a caller-provided token array: no
// allocation, the text is not copied and must outlive the reader. Strings
// are unescaped on access. Unknown members are simply not looked at.
//
//   lgal::JsonToken tok[64];
//   lgal::JsonReader doc;
//   if (!doc.parse(body, len, tok, 64)) -> doc.error(), doc.error_pos()
//   int64_t n;
//   if (!doc.root()["count"].get(&n)) -> missing or not an integer
//
// Missing members / out-of-range indices yield an invalid value; every
// getter on an invalid value returns false, so lookups can be chained.

#include <cstddef>
#include <cstdint>

namespace lgal {

enum class JsonType : uint8_t { Invalid = 0, Null, Bool, Number, String, Array, Object };

struct JsonToken {
    JsonType type;
    uint8_t reserved;
    uint16_t children;  // Array: elements, Object: members
    uint32_t start;     // text range; String without the quotes
    uint32_t end;
    uint32_t next;      // index of the first token after this subtree
};

class JsonReader;

class JsonValue {
public:
    JsonValue() = default;

    bool valid() const { return doc_ != nullptr; }
    JsonType type() const;
    bool is_null() const { return type() == JsonType::Null; }
    // Elements (array) or members (object), 0 otherwise
    int size() const;

    // Object member by name (first match), invalid if missing / not an object
    JsonValue operator[](const char* key) const;
    // Array element, invalid if out of range / not an array
    JsonValue operator[](int index) const;
    // Object member by position: name and value
    JsonValue key_at(int index) const;
    JsonValue value_at(int index) const;

    bool get(bool* out) const;
    // Integer without fraction or exponent, within int64
    bool get(int64_t* out) const;
    // Decimal number times scale, rounded half away from zero ("1.25" with
    // scale 1000 -> 1250). No exponent, at most 15 integer digits.
    bool get_decimal(int64_t scale, int64_t* out) const;
    // Unescaped UTF-8 string, NUL-terminated. false if not a string, longer
    // than len - 1 bytes or containing \u0000.
    bool get(char* out, size_t len) const;
    // String equal to s (after unescaping)
    bool equals(const char* s) const;

private:
    friend class JsonReader;
    JsonValue(const JsonReader* doc, int index) : doc_(doc), index_(index) {}
    const JsonToken* tok() const;

    const JsonReader* doc_ = nullptr;
    int index_ = 0;
};

class JsonReader {
public:
    static constexpr int kMaxDepth = 16;

    bool parse(const char* text, size_t len, JsonToken* tokens, int max_tokens);
    JsonValue root() const { return count_ ? JsonValue(this, 0) : JsonValue(); }

    // After a failed parse: "syntax", "too_deep", "too_many_tokens",
    // "too_large", "trailing_data", "empty"
    const char* error() const { return error_; }
    size_t error_pos() const { return error_pos_; }
    int tokens_used() const { return count_; }

private:
    friend class JsonValue;
    bool value(int depth);
    bool string();
    bool number();
    bool literal(const char* word);
    void ws();
    int add(JsonType t, size_t start);
    bool fail(const char* e);

    const char* text_ = nullptr;
    size_t len_ = 0;
    size_t pos_ = 0;
    JsonToken* tok_ = nullptr;
    int max_ = 0;
    int count_ = 0;
    const char* error_ = nullptr;
    size_t error_pos_ = 0;
};

}  // namespace lgal
