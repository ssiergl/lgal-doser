#include "lgal/device.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_mac.h"
#include "psa/crypto.h"
#include "sdkconfig.h"

namespace lgal::device {

namespace {
constexpr const char* TAG = "device";
// Without 0/O/1/l/i – easy to read and type. 31 characters, 12 digits ≈ 59 bits.
constexpr char kAlphabet[] = "23456789abcdefghjkmnpqrstuvwxyz";
static_assert(sizeof(kAlphabet) - 1 == 31, "alphabet");

std::string s_mac, s_short, s_host, s_ssid, s_pass;

bool sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
    size_t olen = 0;
    return psa_hash_compute(PSA_ALG_SHA_256, data, len, out, 32, &olen) == PSA_SUCCESS && olen == 32;
}

// HMAC-SHA256 per RFC 2104, using the PSA hash only (independent of PSA key handling)
bool hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* msg, size_t msg_len, uint8_t out[32]) {
    uint8_t k[64] = {};
    if (key_len > 64) {
        if (!sha256(key, key_len, k)) return false;
    } else {
        memcpy(k, key, key_len);
    }
    uint8_t buf[64 + 64];
    if (msg_len > 64) return false;
    for (int i = 0; i < 64; ++i) buf[i] = k[i] ^ 0x36;
    memcpy(buf + 64, msg, msg_len);
    uint8_t inner[32];
    if (!sha256(buf, 64 + msg_len, inner)) return false;
    for (int i = 0; i < 64; ++i) buf[i] = k[i] ^ 0x5c;
    memcpy(buf + 64, inner, 32);
    return sha256(buf, 64 + 32, out);
}
}  // namespace

void init() {
    uint8_t mac[6] = {};
    esp_efuse_mac_get_default(mac);

    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    s_mac = tmp;
    snprintf(tmp, sizeof(tmp), "%02x%02x", mac[4], mac[5]);
    s_short = tmp;
    s_host = std::string("lgal-") + CONFIG_LGAL_DEVICE_TYPE + "-" + s_short;
    snprintf(tmp, sizeof(tmp), "LG-Aquatic-Life-Setup-%02X%02X", mac[4], mac[5]);
    s_ssid = tmp;

    psa_crypto_init();
    uint8_t msg[7 + 6];
    memcpy(msg, "LGAL-AP", 7);
    memcpy(msg + 7, mac, 6);
    const char* secret = CONFIG_LGAL_AP_SECRET;
    uint8_t digest[32];
    if (!hmac_sha256(reinterpret_cast<const uint8_t*>(secret), strlen(secret), msg, sizeof(msg), digest)) {
        ESP_LOGE(TAG, "HMAC failed");
        memset(digest, 0, sizeof(digest));
    }
    s_pass.clear();
    for (int i = 0; i < 12; ++i) s_pass += kAlphabet[digest[i] % 31];
}

const std::string& mac_str() { return s_mac; }
const std::string& short_id() { return s_short; }
const std::string& hostname() { return s_host; }
const std::string& setup_ssid() { return s_ssid; }
const std::string& setup_password() { return s_pass; }

}  // namespace lgal::device
