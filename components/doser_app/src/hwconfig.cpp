#include "doser/hwconfig.h"

#include <cstdio>
#include <type_traits>

#include "esp_log.h"
#include "nvs.h"

namespace doser::hwconfig {

namespace {
constexpr const char* TAG = "hwconfig";
constexpr const char* kNs = "hw";
constexpr uint16_t kVersion = 1;

template <typename T>
struct Blob {
    uint16_t version;
    T value;
};

template <typename T>
bool load_blob(nvs_handle_t h, const char* key, T* out) {
    static_assert(std::is_trivially_copyable_v<T>);
    Blob<T> b{};
    size_t len = sizeof(b);
    if (nvs_get_blob(h, key, &b, &len) != ESP_OK || len != sizeof(b) || b.version != kVersion) return false;
    *out = b.value;
    return true;
}

template <typename T>
esp_err_t save_blob(const char* key, const T& v) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNs, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    Blob<T> b{kVersion, v};
    err = nvs_set_blob(h, key, &b, sizeof(b));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
}  // namespace

void load(motion::PumpConfig (&pumps)[5]) {
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "Keine gespeicherten Hardware-Parameter, verwende Standardwerte");
        return;
    }
    char key[12];
    for (int i = 0; i < 5; ++i) {
        snprintf(key, sizeof(key), "pump%d", i);
        load_blob(h, key, &pumps[i]);
    }
    nvs_close(h);
}

esp_err_t save_pump(int pump, const motion::PumpConfig& cfg) {
    char key[12];
    snprintf(key, sizeof(key), "pump%d", pump);
    return save_blob(key, cfg);
}

}  // namespace doser::hwconfig
