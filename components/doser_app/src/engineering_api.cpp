#include "doser/engineering_api.h"

#include <cstring>

#include "doser/history.h"
#include "doser/i2c_devices.h"
#include "doser/motion.h"
#include "driver/temperature_sensor.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lgal/auth.h"
#include "lgal/http.h"
#include "lgal/json.h"
#include "lgal/json_reader.h"
#include "lgal/net.h"
#include "lgal/net_config.h"
#include "lgal/settings.h"
#include "lgal/sysapi.h"
#include "nvs.h"

namespace doser::engineering {

namespace {
using lgal::Json;
namespace http = lgal::http;

constexpr const char* TAG = "engineering";

// Internal temperature sensor of the ESP32-S3, installed on first use
temperature_sensor_handle_t s_tsens = nullptr;
bool s_tsens_failed = false;

bool chip_temp(float* c) {
    if (!s_tsens && !s_tsens_failed) {
        temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
        if (temperature_sensor_install(&cfg, &s_tsens) != ESP_OK || temperature_sensor_enable(s_tsens) != ESP_OK) {
            ESP_LOGW(TAG, "chip temperature sensor not available");
            s_tsens_failed = true;
            s_tsens = nullptr;
        }
    }
    return s_tsens && temperature_sensor_get_celsius(s_tsens, c) == ESP_OK;
}

esp_err_t get_telemetry(httpd_req_t* req) {
    if (!http::authorize_engineering(req)) return ESP_OK;
    Json j;
    j.obj()
        .kv("uptime_s", esp_timer_get_time() / 1000000)
        .kv("reset_reason", lgal::sysapi::reset_reason_str(esp_reset_reason()))
        .kv("tasks", static_cast<int64_t>(uxTaskGetNumberOfTasks()));
    float t = 0;
    if (chip_temp(&t)) j.kv("chip_temp_c", static_cast<double>(t), 1);
    else j.null("chip_temp_c");

    const size_t free_b = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    j.obj("heap")
        .kv("free", static_cast<int64_t>(free_b))
        .kv("min_free", static_cast<int64_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)))
        .kv("largest_block", static_cast<int64_t>(largest))
        .kv("total", static_cast<int64_t>(heap_caps_get_total_size(MALLOC_CAP_INTERNAL)))
        .kv("fragmentation_pct", free_b ? 100.0 * (1.0 - static_cast<double>(largest) / free_b) : 0.0, 1)
        .end();
    j.obj("psram")
        .kv("free", static_cast<int64_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)))
        .kv("total", static_cast<int64_t>(heap_caps_get_total_size(MALLOC_CAP_SPIRAM)))
        .end();

    nvs_stats_t ns{};
    j.obj("nvs");
    if (nvs_get_stats(nullptr, &ns) == ESP_OK) {
        j.kv("used_entries", static_cast<int64_t>(ns.used_entries))
            .kv("free_entries", static_cast<int64_t>(ns.free_entries))
            .kv("total_entries", static_cast<int64_t>(ns.total_entries))
            .kv("namespaces", static_cast<int64_t>(ns.namespace_count));
    }
    j.end();

    const history::Usage u = history::usage();
    j.obj("storage")
        .kv("total", static_cast<int64_t>(u.fs_total))
        .kv("used", static_cast<int64_t>(u.fs_used))
        .kv("history_days", u.days)
        .kv("history_bytes", static_cast<int64_t>(u.bytes))
        .kv("history_dropped", static_cast<int64_t>(u.dropped))
        .end();
    j.end();
    return http::send_json(req, j.str().c_str());
}

void factory_task(void*) {
    vTaskDelay(pdMS_TO_TICKS(500));  // let the response go out
    factory_reset();
}

esp_err_t post_reset(httpd_req_t* req) {
    if (!http::authorize_engineering(req)) return ESP_OK;
    char what[12] = "";
    http::query_str(req, "what", what, sizeof(what));
    if (std::strcmp(what, "network") == 0) {
        ESP_LOGW(TAG, "network settings reset via API");
        const esp_err_t err = lgal::net::set_config(lgal::netcfg::Config{}, 1000);
        if (err != ESP_OK)
            return http::send_error(req, "500 Internal Server Error", "internal_error", esp_err_to_name(err));
        return http::send_json(req, "{\"status\":\"ok\"}");
    }
    // the factory reset is the admin's (maintenance, with password), not here
    return http::send_error(req, "400 Bad Request", "invalid_param", "what=network");
}

// Factory reset for the admin (maintenance page): session + password again
esp_err_t post_factory_reset(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    std::string text;
    lgal::JsonReader doc;
    lgal::JsonToken tokens[8];
    if (!http::read_json(req, 256, &text, &doc, tokens, 8)) return ESP_OK;
    char pw[72] = "";
    if (!doc.root()["password"].get(pw, sizeof(pw)))
        return http::send_error(req, "400 Bad Request", "invalid_param", "password");
    const bool ok = lgal::auth::confirm_password(req, pw);
    std::memset(pw, 0, sizeof(pw));
    if (!ok) return ESP_OK;
    ESP_LOGW(TAG, "factory reset by the admin");
    if (xTaskCreate(factory_task, "factory", 4096, nullptr, 5, nullptr) != pdPASS)
        return http::send_error(req, "500 Internal Server Error", "internal_error", "no task");
    return http::send_json(req, "{\"status\":\"rebooting\"}");
}
}  // namespace

void factory_reset() {
    ESP_LOGW(TAG, "factory reset: stopping pumps, clearing EEPROM");
    motion::lock(true);  // everything stops, no new moves
    vTaskDelay(pdMS_TO_TICKS(100));
    if (eeprom::present()) {
        const esp_err_t err = eeprom::erase();
        if (err != ESP_OK) ESP_LOGE(TAG, "EEPROM erase: %s", esp_err_to_name(err));
    }
    lgal::settings::factory_reset();
}

esp_err_t register_handlers(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_STATE;
    http::route(server, "/api/v1/engineering/telemetry", HTTP_GET, get_telemetry);
    http::route(server, "/api/v1/engineering/reset", HTTP_POST, post_reset);
    http::route(server, "/api/v1/system/factory_reset", HTTP_POST, post_factory_reset);
    return ESP_OK;
}

}  // namespace doser::engineering
