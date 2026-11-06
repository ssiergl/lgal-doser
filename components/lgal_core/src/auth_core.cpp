#include "lgal/auth_core.h"

#include <cstring>

#include "lgal/sha256.h"

namespace lgal::auth {

// ---------------------------------------------------------------------------
const char* check_policy(const char* password) {
    const size_t n = password ? std::strlen(password) : 0;
    if (n < kMinPassword) return "password_too_short";
    if (n > kMaxPassword) return "password_too_long";
    return nullptr;
}

void make_record(PasswordRecord* r, const char* password, const uint8_t salt[16], uint32_t iterations) {
    std::memset(r, 0, sizeof(*r));
    r->magic = kPasswordMagic;
    r->iterations = iterations;
    std::memcpy(r->salt, salt, sizeof(r->salt));
    crypto::pbkdf2_sha256(password, std::strlen(password), r->salt, sizeof(r->salt), iterations, r->hash,
                          sizeof(r->hash));
}

bool is_set(const PasswordRecord& r) {
    return r.magic == kPasswordMagic && r.iterations >= 1 && r.iterations <= 1000000;
}

bool verify(const PasswordRecord& r, const char* password) {
    if (!is_set(r) || !password) return false;
    const size_t n = std::strlen(password);
    if (n > kMaxPassword) return false;
    uint8_t h[32];
    crypto::pbkdf2_sha256(password, n, r.salt, sizeof(r.salt), r.iterations, h, sizeof(h));
    return crypto::equal_ct(h, r.hash, sizeof(h));
}

void derive_password(const char* secret, const char* label, const uint8_t mac[6], size_t chars, char* out) {
    static constexpr char kAlphabet[] = "23456789abcdefghjkmnpqrstuvwxyz";
    static_assert(sizeof(kAlphabet) - 1 == 31);
    uint8_t msg[16 + 6];
    const size_t ll = std::strlen(label) < 16 ? std::strlen(label) : 16;
    std::memcpy(msg, label, ll);
    std::memcpy(msg + ll, mac, 6);
    uint8_t d[32];
    crypto::hmac_sha256(secret, std::strlen(secret), msg, ll + 6, d);
    if (chars > sizeof(d)) chars = sizeof(d);
    for (size_t i = 0; i < chars; ++i) out[i] = kAlphabet[d[i] % 31];
    out[chars] = '\0';
}

// ---------------------------------------------------------------------------
void encode_token(const uint8_t random[kTokenRandom], char out[kTokenChars + 1]) {
    static constexpr char kB32[] = "abcdefghijklmnopqrstuvwxyz234567";
    // 20 bytes = 160 bits = 32 groups of 5 bits
    for (size_t i = 0; i < kTokenChars; ++i) {
        const size_t bit = i * 5;
        const unsigned v = (unsigned(random[bit / 8]) << 8 | (bit / 8 + 1 < kTokenRandom ? random[bit / 8 + 1] : 0));
        out[i] = kB32[(v >> (11 - bit % 8)) & 0x1F];
    }
    out[kTokenChars] = '\0';
}

// ---------------------------------------------------------------------------
bool Sessions::alive(const Entry& e, int64_t now) const {
    return e.token[0] && now - e.last <= kSessionIdle && now - e.created <= kSessionMax;
}

void Sessions::create(const uint8_t random[kTokenRandom], int64_t now, char token[kTokenChars + 1]) {
    int slot = 0;
    for (int i = 0; i < kMaxSessions; ++i) {
        if (!alive(e_[i], now)) {
            slot = i;
            break;
        }
        if (e_[i].last < e_[slot].last) slot = i;  // least recently used
    }
    Entry& e = e_[slot];
    encode_token(random, e.token);
    e.created = e.last = now;
    e.eng_last = -1;
    std::memcpy(token, e.token, sizeof(e.token));
}

int Sessions::find(const char* token, int64_t now) {
    if (!token || std::strlen(token) != kTokenChars) return -1;
    int found = -1;
    for (int i = 0; i < kMaxSessions; ++i) {
        // compare all entries (constant time per entry, no early exit)
        if (e_[i].token[0] && crypto::equal_ct(e_[i].token, token, kTokenChars) && found < 0) found = i;
    }
    if (found < 0) return -1;
    if (!alive(e_[found], now)) {
        e_[found] = Entry{};
        return -1;
    }
    e_[found].last = now;
    return found;
}

void Sessions::remove(const char* token) {
    if (!token || std::strlen(token) != kTokenChars) return;
    for (auto& e : e_)
        if (e.token[0] && crypto::equal_ct(e.token, token, kTokenChars)) e = Entry{};
}

void Sessions::clear() {
    for (auto& e : e_) e = Entry{};
}

int Sessions::count(int64_t now) const {
    int n = 0;
    for (const auto& e : e_) n += alive(e, now) ? 1 : 0;
    return n;
}

void Sessions::set_engineering(int index, bool on, int64_t now) {
    if (index < 0 || index >= kMaxSessions) return;
    e_[index].eng_last = on ? now : -1;
}

bool Sessions::engineering(int index, int64_t now) {
    if (index < 0 || index >= kMaxSessions) return false;
    Entry& e = e_[index];
    if (e.eng_last < 0) return false;
    if (now - e.eng_last > kEngineeringIdle) {
        e.eng_last = -1;
        return false;
    }
    e.eng_last = now;
    return true;
}

int64_t Sessions::engineering_left(int index, int64_t now) const {
    if (index < 0 || index >= kMaxSessions || e_[index].eng_last < 0) return 0;
    const int64_t left = kEngineeringIdle - (now - e_[index].eng_last);
    return left > 0 ? left : 0;
}

// ---------------------------------------------------------------------------
namespace {
bool name_ok(const char* name) {
    const size_t n = name ? std::strlen(name) : 0;
    if (n == 0 || n >= kKeyNameLen) return false;
    for (size_t i = 0; i < n; ++i)
        if (static_cast<unsigned char>(name[i]) < 0x20 || name[i] == 0x7F) return false;
    return true;
}
}  // namespace

int add_key(KeyStore& s, const char* name, const uint8_t random[kTokenRandom], int64_t created_utc,
            char out[kKeyChars + 1]) {
    if (!name_ok(name)) return -1;
    int slot = -1;
    for (int i = 0; i < kMaxKeys && slot < 0; ++i)
        if (!s.keys[i].name[0]) slot = i;
    if (slot < 0) return -1;
    std::memcpy(out, kKeyPrefix, 5);
    encode_token(random, out + 5);
    ApiKey& k = s.keys[slot];
    std::memset(&k, 0, sizeof(k));
    std::strncpy(k.name, name, kKeyNameLen - 1);
    crypto::sha256(out, kKeyChars, k.hash);
    k.created = created_utc;
    return slot;
}

int find_key(const KeyStore& s, const char* key) {
    if (!key || std::strlen(key) != kKeyChars || std::strncmp(key, kKeyPrefix, 5) != 0) return -1;
    uint8_t h[32];
    crypto::sha256(key, kKeyChars, h);
    int found = -1;
    for (int i = 0; i < kMaxKeys; ++i)
        if (s.keys[i].name[0] && crypto::equal_ct(s.keys[i].hash, h, sizeof(h)) && found < 0) found = i;
    return found;
}

bool remove_key(KeyStore& s, int slot) {
    if (slot < 0 || slot >= kMaxKeys || !s.keys[slot].name[0]) return false;
    std::memset(&s.keys[slot], 0, sizeof(ApiKey));
    return true;
}

int key_count(const KeyStore& s) {
    int n = 0;
    for (const auto& k : s.keys) n += k.name[0] ? 1 : 0;
    return n;
}

// ---------------------------------------------------------------------------
const Throttle::Slot* Throttle::find(uint32_t client) const {
    for (const auto& s : s_)
        if (s.last >= 0 && s.client == client) return &s;
    return nullptr;
}

int64_t Throttle::wait(uint32_t client, int64_t now) const {
    const Slot* s = find(client);
    if (!s) s = &overflow_;
    return s->last >= 0 && s->until > now ? s->until - now : 0;
}

void Throttle::failed(uint32_t client, int64_t now) {
    Slot* slot = const_cast<Slot*>(find(client));
    if (!slot) {
        // a free slot or one quiet for kForget, else the shared overflow record
        for (auto& s : s_) {
            if (s.last < 0 || (s.until <= now && now - s.last >= kForget)) {
                s = Slot{};
                s.client = client;
                slot = &s;
                break;
            }
        }
        if (!slot) {
            slot = &overflow_;
            if (slot->last >= 0 && slot->until <= now && now - slot->last >= kForget) *slot = Slot{};
        }
    }
    ++slot->failures;
    slot->last = now;
    if (slot->failures > kFreeAttempts) {
        const int n = slot->failures - kFreeAttempts - 1;
        const int64_t w = n >= 9 ? kMaxWait : (int64_t(1) << n);
        slot->until = now + (w < kMaxWait ? w : kMaxWait);
    }
}

void Throttle::succeeded(uint32_t client) {
    // the overflow record stays: it may belong to someone else
    for (auto& s : s_)
        if (s.last >= 0 && s.client == client) s = Slot{};
}

// ---------------------------------------------------------------------------
bool cookie_value(const char* header, const char* name, char* out, size_t len) {
    if (!header || !name || !out || len == 0) return false;
    const size_t nl = std::strlen(name);
    const char* p = header;
    while (*p) {
        while (*p == ' ' || *p == ';') ++p;
        const char* eq = std::strchr(p, '=');
        const char* end = std::strchr(p, ';');
        if (!end) end = p + std::strlen(p);
        if (eq && eq < end && size_t(eq - p) == nl && std::strncmp(p, name, nl) == 0) {
            const size_t vl = size_t(end - eq - 1);
            if (vl + 1 > len) return false;
            std::memcpy(out, eq + 1, vl);
            out[vl] = '\0';
            return true;
        }
        p = end;
    }
    return false;
}

bool bearer_token(const char* header, char* out, size_t len) {
    if (!header || !out || std::strncmp(header, "Bearer ", 7) != 0) return false;
    const char* v = header + 7;
    while (*v == ' ') ++v;
    size_t n = std::strlen(v);
    while (n && v[n - 1] == ' ') --n;
    if (n == 0 || n + 1 > len) return false;
    std::memcpy(out, v, n);
    out[n] = '\0';
    return true;
}

}  // namespace lgal::auth
