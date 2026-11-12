#include "doser/hwconfig.h"

#include <cstdio>
#include <type_traits>

#include "doser/pins.h"
#include "esp_log.h"
#include "nvs.h"

namespace doser::hwconfig {

namespace {
constexpr const char* TAG = "hwconfig";
constexpr const char* kNs = "hw";
constexpr uint16_t kVersion = 1;
uint8_t s_fitted = pins::kBoardMask;         // in effect since startup
uint8_t s_fitted_saved = pins::kBoardMask;   // in effect after the next restart

bool valid_mask(uint8_t m) { return m != 0 && (m & ~pins::kBoardMask) == 0; }

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

void load(motion::PumpConfig (&pumps)[kMaxPumps]) {
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "No stored hardware parameters, using defaults");
        return;
    }
    char key[12];
    for (int i = 0; i < kMaxPumps; ++i) {
        snprintf(key, sizeof(key), "pump%d", i);
        load_blob(h, key, &pumps[i]);
    }
    uint8_t m = pins::kBoardMask;
    if (load_blob(h, "fitted", &m) && valid_mask(m)) s_fitted = s_fitted_saved = m;
    nvs_close(h);
    for (int i = 0; i < pins::kPumpCount; ++i)
        if (!fitted(i)) ESP_LOGI(TAG, "Pump %d: not fitted", i + 1);
}

int board_pumps() { return pins::kPumpCount; }
uint8_t board_mask() { return pins::kBoardMask; }

uint8_t fitted_mask() { return s_fitted; }
bool fitted(int pump) { return pump >= 0 && pump < kMaxPumps && (s_fitted >> pump & 1); }
uint8_t stored_fitted_mask() { return s_fitted_saved; }

esp_err_t save_fitted(uint8_t mask) {
    if (!valid_mask(mask)) return ESP_ERR_INVALID_ARG;
    const esp_err_t err = save_blob("fitted", mask);
    if (err == ESP_OK) s_fitted_saved = mask;
    return err;
}

esp_err_t save_pump(int pump, const motion::PumpConfig& cfg) {
    char key[12];
    snprintf(key, sizeof(key), "pump%d", pump);
    return save_blob(key, cfg);
}

}  // namespace doser::hwconfig
