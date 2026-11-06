#include "lgal/settings.h"

#include <cstring>

#include "esp_log.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace lgal::settings {

namespace {
constexpr const char* TAG = "settings";
constexpr const char* kNs = "lgal";
constexpr const char* kKeySetupDone = "setup_done";
constexpr const char* kKeyLanguage = "lang";
char s_lang[4] = "";  // cache, empty = not loaded yet
}  // namespace

esp_err_t init() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Reformatting NVS (%s)", esp_err_to_name(err));
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    return err;
}

bool setup_completed() {
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return false;
    uint8_t v = 0;
    nvs_get_u8(h, kKeySetupDone, &v);
    nvs_close(h);
    return v != 0;
}

esp_err_t set_setup_completed(bool done) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNs, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, kKeySetupDone, done ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

bool language_supported(const char* code) {
    if (!code) return false;
    for (const char* l : kLanguages)
        if (std::strcmp(l, code) == 0) return true;
    return false;
}

const char* language() {
    if (!s_lang[0]) {
        char v[sizeof(s_lang)] = "";
        size_t len = sizeof(v);
        nvs_handle_t h;
        if (nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
            if (nvs_get_str(h, kKeyLanguage, v, &len) != ESP_OK) v[0] = 0;
            nvs_close(h);
        }
        std::strcpy(s_lang, language_supported(v) ? v : kLanguages[0]);
    }
    return s_lang;
}

esp_err_t set_language(const char* code) {
    if (!language_supported(code)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNs, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, kKeyLanguage, code);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        std::strcpy(s_lang, code);
        ESP_LOGI(TAG, "Language set to %s", code);
    }
    return err;
}

void factory_reset() {
    ESP_LOGW(TAG, "Factory reset: erasing NVS and data partition");
    nvs_flash_deinit();
    nvs_flash_erase();
    const esp_partition_t* p =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
    if (p) esp_partition_erase_range(p, 0, p->size);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

}  // namespace lgal::settings
