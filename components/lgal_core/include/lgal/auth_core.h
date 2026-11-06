#pragma once
// Authentication logic without ESP dependencies (host tests: tests/host).
//
// * Admin password: PBKDF2-HMAC-SHA256 with random salt, stored as record.
// * Browser sessions: random tokens in RAM (cookie), idle and absolute
//   timeout; a session can additionally unlock the engineering area
//   (spec 11.1, auto-lock after 15 min without engineering requests).
// * API keys (app, Home Assistant): "lgal_" + 32 characters, only the
//   SHA-256 is stored; the key is shown once when created.
// * Login throttling per client address against password guessing.
//
// Times are monotonic seconds (since boot); randomness is passed in.

#include <cstddef>
#include <cstdint>

namespace lgal::auth {

// ---------------------------------------------------------------------------
// Password
constexpr size_t kMinPassword = 8;
constexpr size_t kMaxPassword = 64;
constexpr uint32_t kPbkdf2Iterations = 10000;

struct PasswordRecord {
    uint32_t magic;  // kPasswordMagic when set
    uint32_t iterations;
    uint8_t salt[16];
    uint8_t hash[32];
};
static_assert(sizeof(PasswordRecord) == 56);
constexpr uint32_t kPasswordMagic = 0x4C475057;  // "LGPW"

// nullptr = acceptable, otherwise "password_too_short" / "password_too_long"
const char* check_policy(const char* password);
void make_record(PasswordRecord* r, const char* password, const uint8_t salt[16],
                 uint32_t iterations = kPbkdf2Iterations);
bool is_set(const PasswordRecord& r);
bool verify(const PasswordRecord& r, const char* password);

// Device passwords derived from the build secret and the base MAC:
// HMAC-SHA256(secret, label || mac) -> one character per byte from an
// unambiguous alphabet (no 0/O/1/l/i). Labels: "LGAL-AP" = setup Wi-Fi and
// password on the label (12 characters), "LGAL-ENG" = engineering key of the
// manufacturer (16 characters). Must match tools/ap_password.py.
void derive_password(const char* secret, const char* label, const uint8_t mac[6], size_t chars, char* out);

// ---------------------------------------------------------------------------
// Tokens: lower-case base32 ("a-z2-7"), 20 random bytes -> 32 characters
constexpr size_t kTokenChars = 32;
constexpr size_t kTokenRandom = 20;
void encode_token(const uint8_t random[kTokenRandom], char out[kTokenChars + 1]);

// ---------------------------------------------------------------------------
// Sessions
constexpr int kMaxSessions = 8;
constexpr int64_t kSessionIdle = 7 * 86400;       // s without request
constexpr int64_t kSessionMax = 30 * 86400;       // s after login
constexpr int64_t kEngineeringIdle = 15 * 60;     // s without engineering request

class Sessions {
public:
    // New session (evicts the least recently used one if full)
    void create(const uint8_t random[kTokenRandom], int64_t now, char token[kTokenChars + 1]);
    // Index of a valid session (and touches it), -1 if unknown or expired
    int find(const char* token, int64_t now);
    void remove(const char* token);
    void clear();
    int count(int64_t now) const;

    // Engineering unlock of a session (find() first)
    void set_engineering(int index, bool on, int64_t now);
    // Valid and touched by this call
    bool engineering(int index, int64_t now);
    int64_t engineering_left(int index, int64_t now) const;

private:
    struct Entry {
        char token[kTokenChars + 1] = "";
        int64_t created = 0, last = 0;
        int64_t eng_last = -1;  // < 0: locked
    };
    bool alive(const Entry& e, int64_t now) const;
    Entry e_[kMaxSessions];
};

// ---------------------------------------------------------------------------
// API keys
constexpr int kMaxKeys = 4;
constexpr size_t kKeyNameLen = 24;
constexpr const char* kKeyPrefix = "lgal_";
constexpr size_t kKeyChars = 5 + kTokenChars;  // incl. prefix

struct ApiKey {
    char name[kKeyNameLen];  // empty = slot free
    uint8_t hash[32];
    int64_t created;         // UTC (0 = unknown)
};

struct KeyStore {
    uint32_t version = 1;
    uint32_t reserved = 0;
    ApiKey keys[kMaxKeys] = {};
};

// Adds a key; out receives the plain key (only time it is known).
// Returns the slot or -1 ("no free slot" or invalid name).
int add_key(KeyStore& s, const char* name, const uint8_t random[kTokenRandom], int64_t created_utc,
            char out[kKeyChars + 1]);
// Slot of a presented key, -1 if unknown
int find_key(const KeyStore& s, const char* key);
bool remove_key(KeyStore& s, int slot);
int key_count(const KeyStore& s);

// ---------------------------------------------------------------------------
// Login throttling per client: kFreeAttempts failures are free, then the
// wait doubles from 1 s up to kMaxWait. Success resets the client.
// A client keeps its slot until it has been quiet for kForget seconds, so
// an attacker cannot push its own record out by rotating addresses. When
// all slots are taken, further clients share one overflow record: many
// addresses then throttle each other instead of guessing freely.
constexpr int kFreeAttempts = 5;
constexpr int64_t kMaxWait = 300;
constexpr int64_t kForget = 3600;
constexpr int kThrottleSlots = 8;

class Throttle {
public:
    // Seconds the client still has to wait (0 = may try now)
    int64_t wait(uint32_t client, int64_t now) const;
    void failed(uint32_t client, int64_t now);
    void succeeded(uint32_t client);

private:
    struct Slot {
        uint32_t client = 0;
        int failures = 0;
        int64_t until = 0, last = -1;  // last < 0: free
    };
    const Slot* find(uint32_t client) const;
    Slot s_[kThrottleSlots];
    Slot overflow_;  // shared by clients without a slot
};

// ---------------------------------------------------------------------------
// Header parsing
// Value of cookie `name` from a Cookie header ("a=1; b=2"), false if missing / too long
bool cookie_value(const char* header, const char* name, char* out, size_t len);
// Token from "Bearer <token>", false otherwise
bool bearer_token(const char* header, char* out, size_t len);

}  // namespace lgal::auth
