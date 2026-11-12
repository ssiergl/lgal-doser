#include "lgal/device.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lgal/auth_core.h"
#include "lgal/device_name.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

namespace lgal::device {

namespace {
constexpr const char* TAG = "device";
constexpr const char* kNs = "lgal";
constexpr const char* kFactory = "fctry";  // factory partition, untouched by the factory reset
constexpr const char* kKeySecret = "secret";
constexpr const char* kKeyName = "dev_name";
constexpr const char* kKeyHost = "dev_host";

std::string s_mac, s_short, s_default, s_ssid, s_pass, s_eng;
bool s_created = false;
// written by the HTTP task, read everywhere: copies under the lock
SemaphoreHandle_t s_mx = nullptr;
std::string s_name, s_own, s_host;

struct Guard {
    Guard() { xSemaphoreTake(s_mx, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(s_mx); }
};

std::string read_str(nvs_handle_t h, const char* key, size_t max) {
    char buf[64] = "";
    size_t len = sizeof(buf);
    if (nvs_get_str(h, key, buf, &len) != ESP_OK || std::strlen(buf) > max) return {};
    return buf;
}

std::string random_secret() {
    uint8_t raw[32];
    esp_fill_random(raw, sizeof(raw));  // true random: lgal::entropy::init() runs before
    char hex[auth::kDeviceSecretChars + 1];
    auth::hex_device_secret(raw, hex);
    return hex;
}

// Device secret from the factory partition (tools/provision.py). Without
// one the device creates a random secret and keeps it there; *created
// tells the caller to show the passwords once.
std::string load_secret(bool* created) {
    *created = true;
    esp_err_t err = nvs_flash_init_partition(kFactory);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {  // no NVS data at all: formatting loses nothing
        ESP_LOGW(TAG, "Formatting the factory partition");
        err = nvs_flash_erase_partition(kFactory);
        if (err == ESP_OK) err = nvs_flash_init_partition(kFactory);
    }
    nvs_handle_t h;
    if (err == ESP_OK) err = nvs_open_from_partition(kFactory, kNs, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Factory partition not usable (%s): passwords only valid until the restart",
                 esp_err_to_name(err));
        return random_secret();
    }
    char buf[auth::kDeviceSecretChars + 1] = "";
    size_t len = sizeof(buf);
    err = nvs_get_str(h, kKeySecret, buf, &len);
    std::string secret;
    if (err == ESP_OK && auth::valid_device_secret(buf)) {
        *created = false;
        secret = buf;
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        secret = random_secret();
        err = nvs_set_str(h, kKeySecret, secret.c_str());
        if (err == ESP_OK) err = nvs_commit(h);
        if (err == ESP_OK) ESP_LOGW(TAG, "No device secret provisioned: created a random one");
        else ESP_LOGE(TAG, "Could not save the device secret (%s)", esp_err_to_name(err));
    } else {
        // never overwrite a provisioned value: provision the device again
        ESP_LOGE(TAG, "Device secret in the factory partition is invalid (%s): passwords only valid until the "
                      "restart", esp_err_to_name(err));
        secret = random_secret();
    }
    nvs_close(h);
    return secret;
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
    s_default = std::string("lgal-") + CONFIG_LGAL_DEVICE_TYPE + "-" + s_short;
    snprintf(tmp, sizeof(tmp), "LG-Aquatic-Life-Setup-%02X%02X", mac[4], mac[5]);
    s_ssid = tmp;

    const std::string secret = load_secret(&s_created);
    char pw[17];
    auth::derive_password(secret.c_str(), "LGAL-AP", mac, 12, pw);
    s_pass = pw;
    auth::derive_password(secret.c_str(), "LGAL-ENG", mac, 16, pw);
    s_eng = pw;

    s_mx = xSemaphoreCreateMutex();
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
        s_name = read_str(h, kKeyName, devname::kNameMax);
        s_own = read_str(h, kKeyHost, devname::kHostMax);
        nvs_close(h);
    }
    if (!devname::valid_name(s_name.c_str())) s_name.clear();
    if (!s_own.empty() && !devname::valid_host(s_own.c_str())) s_own.clear();
    s_host = devname::effective(s_name.c_str(), s_own.c_str(), s_default);
}

const std::string& mac_str() { return s_mac; }
const std::string& short_id() { return s_short; }
const std::string& default_hostname() { return s_default; }

std::string hostname() {
    Guard g;
    return s_host;
}

std::string name() {
    Guard g;
    return s_name;
}

std::string own_hostname() {
    Guard g;
    return s_own;
}

esp_err_t set_names(const std::string& name, const std::string& host) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNs, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = name.empty() ? nvs_erase_key(h, kKeyName) : nvs_set_str(h, kKeyName, name.c_str());
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = host.empty() ? nvs_erase_key(h, kKeyHost) : nvs_set_str(h, kKeyHost, host.c_str());
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) return err;
    Guard g;
    s_name = name;
    s_own = host;
    s_host = devname::effective(s_name.c_str(), s_own.c_str(), s_default);
    return ESP_OK;
}

const std::string& setup_ssid() { return s_ssid; }
const std::string& setup_password() { return s_pass; }
const std::string& engineering_key() { return s_eng; }
bool secret_created() { return s_created; }

}  // namespace lgal::device
