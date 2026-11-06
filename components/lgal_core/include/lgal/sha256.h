#pragma once
// Portable SHA-256, HMAC-SHA256 (RFC 2104) and PBKDF2-HMAC-SHA256 (RFC 8018).
// No ESP dependencies so the password handling can be host tested; fast
// enough for logins (one compression ≈ a few µs on the ESP32-S3).

#include <cstddef>
#include <cstdint>

namespace lgal::crypto {

constexpr size_t kSha256Len = 32;

class Sha256 {
public:
    Sha256() { reset(); }
    void reset();
    void update(const void* data, size_t len);
    void finish(uint8_t out[kSha256Len]);

private:
    void block(const uint8_t* p);
    uint32_t h_[8];
    uint8_t buf_[64];
    uint64_t total_ = 0;
    size_t used_ = 0;
};

void sha256(const void* data, size_t len, uint8_t out[kSha256Len]);

// HMAC with the key schedule computed once (reused for many messages)
class HmacSha256 {
public:
    HmacSha256(const void* key, size_t key_len);
    void mac(const void* msg, size_t len, uint8_t out[kSha256Len]) const;
    // MAC over the concatenation a || b
    void mac(const void* a, size_t a_len, const void* b, size_t b_len, uint8_t out[kSha256Len]) const;

private:
    Sha256 inner_, outer_;  // states after the ipad / opad block
};

void hmac_sha256(const void* key, size_t key_len, const void* msg, size_t msg_len, uint8_t out[kSha256Len]);

void pbkdf2_sha256(const void* password, size_t pw_len, const void* salt, size_t salt_len, uint32_t iterations,
                   uint8_t* out, size_t out_len);

// Constant-time comparison (no early exit)
bool equal_ct(const void* a, const void* b, size_t len);

}  // namespace lgal::crypto
