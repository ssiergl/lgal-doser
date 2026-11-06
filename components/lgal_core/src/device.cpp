#include "lgal/device.h"

#include <cstdio>
#include <cstring>

#include "esp_mac.h"
#include "lgal/auth_core.h"
#include "sdkconfig.h"

namespace lgal::device {

namespace {
std::string s_mac, s_short, s_host, s_ssid, s_pass, s_eng;
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

    char pw[17];
    auth::derive_password(CONFIG_LGAL_AP_SECRET, "LGAL-AP", mac, 12, pw);
    s_pass = pw;
    auth::derive_password(CONFIG_LGAL_AP_SECRET, "LGAL-ENG", mac, 16, pw);
    s_eng = pw;
}

const std::string& mac_str() { return s_mac; }
const std::string& short_id() { return s_short; }
const std::string& hostname() { return s_host; }
const std::string& setup_ssid() { return s_ssid; }
const std::string& setup_password() { return s_pass; }
const std::string& engineering_key() { return s_eng; }

}  // namespace lgal::device
