#include "lgal/sha256.h"

#include <cstring>

namespace lgal::crypto {

namespace {
constexpr uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline uint32_t load_be(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}
inline void store_be(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}
}  // namespace

void Sha256::reset() {
    static constexpr uint32_t kInit[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::memcpy(h_, kInit, sizeof(h_));
    total_ = 0;
    used_ = 0;
}

void Sha256::block(const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) w[i] = load_be(p + 4 * i);
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
        const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
    h_[5] += f;
    h_[6] += g;
    h_[7] += h;
}

void Sha256::update(const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    total_ += len;
    if (used_) {
        const size_t n = len < 64 - used_ ? len : 64 - used_;
        std::memcpy(buf_ + used_, p, n);
        used_ += n;
        p += n;
        len -= n;
        if (used_ < 64) return;
        block(buf_);
        used_ = 0;
    }
    for (; len >= 64; p += 64, len -= 64) block(p);
    if (len) {
        std::memcpy(buf_, p, len);
        used_ = len;
    }
}

void Sha256::finish(uint8_t out[kSha256Len]) {
    const uint64_t bits = total_ * 8;
    const uint8_t pad = 0x80;
    update(&pad, 1);
    const uint8_t zero[64] = {};
    update(zero, (used_ <= 56 ? 56 : 120) - used_);
    uint8_t len_be[8];
    for (int i = 0; i < 8; ++i) len_be[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
    update(len_be, 8);
    for (int i = 0; i < 8; ++i) store_be(out + 4 * i, h_[i]);
    reset();
}

void sha256(const void* data, size_t len, uint8_t out[kSha256Len]) {
    Sha256 s;
    s.update(data, len);
    s.finish(out);
}

// ---------------------------------------------------------------------------
HmacSha256::HmacSha256(const void* key, size_t key_len) {
    uint8_t k[64] = {};
    if (key_len > 64) sha256(key, key_len, k);
    else std::memcpy(k, key, key_len);
    uint8_t pad[64];
    for (int i = 0; i < 64; ++i) pad[i] = k[i] ^ 0x36;
    inner_.update(pad, 64);
    for (int i = 0; i < 64; ++i) pad[i] = k[i] ^ 0x5c;
    outer_.update(pad, 64);
    std::memset(k, 0, sizeof(k));
    std::memset(pad, 0, sizeof(pad));
}

void HmacSha256::mac(const void* msg, size_t len, uint8_t out[kSha256Len]) const { mac(msg, len, nullptr, 0, out); }

void HmacSha256::mac(const void* a, size_t a_len, const void* b, size_t b_len, uint8_t out[kSha256Len]) const {
    Sha256 in = inner_;
    in.update(a, a_len);
    if (b_len) in.update(b, b_len);
    uint8_t ih[kSha256Len];
    in.finish(ih);
    Sha256 o = outer_;
    o.update(ih, sizeof(ih));
    o.finish(out);
}

void hmac_sha256(const void* key, size_t key_len, const void* msg, size_t msg_len, uint8_t out[kSha256Len]) {
    HmacSha256(key, key_len).mac(msg, msg_len, out);
}

void pbkdf2_sha256(const void* password, size_t pw_len, const void* salt, size_t salt_len, uint32_t iterations,
                   uint8_t* out, size_t out_len) {
    const HmacSha256 prf(password, pw_len);
    uint8_t u[kSha256Len], t[kSha256Len];
    for (uint32_t blk = 1; out_len > 0; ++blk) {
        uint8_t be[4];  // U1 = PRF(P, S || INT(i))
        store_be(be, blk);
        prf.mac(salt, salt_len, be, sizeof(be), u);
        std::memcpy(t, u, sizeof(t));
        for (uint32_t i = 1; i < iterations; ++i) {
            prf.mac(u, sizeof(u), u);
            for (size_t k = 0; k < sizeof(t); ++k) t[k] ^= u[k];
        }
        const size_t n = out_len < sizeof(t) ? out_len : sizeof(t);
        std::memcpy(out, t, n);
        out += n;
        out_len -= n;
    }
    std::memset(u, 0, sizeof(u));
    std::memset(t, 0, sizeof(t));
}

bool equal_ct(const void* a, const void* b, size_t len) {
    const volatile uint8_t* x = static_cast<const volatile uint8_t*>(a);
    const volatile uint8_t* y = static_cast<const volatile uint8_t*>(b);
    uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i) diff |= x[i] ^ y[i];
    return diff == 0;
}

}  // namespace lgal::crypto
