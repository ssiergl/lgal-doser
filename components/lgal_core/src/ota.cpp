#include "lgal/ota.h"

#include <atomic>
#include <cstring>
#include <memory>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lgal/http.h"
#include "lgal/led.h"
#include "sdkconfig.h"

namespace lgal::ota {

namespace {
constexpr const char* TAG = "ota";
constexpr size_t kChunk = 4096;
constexpr size_t kHeaderLen =
    sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);

Guard s_guard;
LockHook s_lock;
std::atomic<bool> s_busy{false};
std::function<bool()> s_health;

void restart_cb(void*) { esp_restart(); }

void schedule_restart(uint32_t ms) {
    esp_timer_create_args_t args{};
    args.callback = restart_cb;
    args.name = "ota_reboot";
    esp_timer_handle_t t;
    if (esp_timer_create(&args, &t) == ESP_OK) esp_timer_start_once(t, uint64_t(ms) * 1000);
}

// Checks whether the uploaded image matches this device.
bool check_header(const uint8_t* data, std::string& err) {
    const auto* img = reinterpret_cast<const esp_image_header_t*>(data);
    if (img->magic != ESP_IMAGE_HEADER_MAGIC) {
        err = "keine ESP-Firmware (Magic)";
        return false;
    }
    if (img->chip_id != ESP_CHIP_ID_ESP32S3) {
        err = "Firmware ist nicht für ESP32-S3";
        return false;
    }
    const auto* desc = reinterpret_cast<const esp_app_desc_t*>(
        data + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
    if (desc->magic_word != ESP_APP_DESC_MAGIC_WORD) {
        err = "App-Beschreibung fehlt";
        return false;
    }
    const esp_app_desc_t* own = esp_app_get_description();
    if (strncmp(desc->project_name, own->project_name, sizeof(desc->project_name)) != 0) {
        err = std::string("falsches Projekt: ") + std::string(desc->project_name, strnlen(desc->project_name, 32));
        return false;
    }
    ESP_LOGI(TAG, "New image: %s %s (%s %s), running: %s", desc->project_name, desc->version, desc->date,
             desc->time, own->version);
    return true;
}

void finish(bool ok) {
    led::set(led::State::Ota, false);
    if (!ok && s_lock) s_lock(false);
    s_busy = false;
}

esp_err_t post_firmware(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;

    bool expected = false;
    if (!s_busy.compare_exchange_strong(expected, true)) {
        return http::send_error(req, "409 Conflict", "OTA laeuft bereits");
    }
    std::string reason;
    if (s_guard && !s_guard(reason)) {
        s_busy = false;
        return http::send_error(req, "409 Conflict", reason.c_str());
    }

    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (!target) {
        s_busy = false;
        return http::send_error(req, "500 Internal Server Error", "keine OTA-Partition");
    }
    if (req->content_len < kHeaderLen || req->content_len > target->size) {
        s_busy = false;
        return http::send_error(req, "413 Content Too Large", "ungueltige Dateigroesse");
    }

    if (s_lock) s_lock(true);
    led::set(led::State::Ota, true);
    ESP_LOGI(TAG, "OTA start: %u bytes -> %s", (unsigned)req->content_len, target->label);

    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[kChunk]);
    if (!buf) {
        finish(false);
        return http::send_error(req, "500 Internal Server Error", "kein Speicher");
    }

    esp_ota_handle_t handle = 0;
    bool begun = false;
    size_t received = 0;
    size_t head_fill = 0;  // bytes collected for the header check
    uint8_t head[kHeaderLen];
    int timeouts = 0;
    std::string err;

    while (received < req->content_len) {
        int n = httpd_req_recv(req, reinterpret_cast<char*>(buf.get()),
                               std::min(kChunk, req->content_len - received));
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 5) continue;
        if (n <= 0) {
            err = "Uebertragung abgebrochen";
            break;
        }
        timeouts = 0;

        if (!begun) {
            // Collect the header, only then start writing flash
            size_t take = std::min(kHeaderLen - head_fill, static_cast<size_t>(n));
            memcpy(head + head_fill, buf.get(), take);
            head_fill += take;
            if (head_fill < kHeaderLen) {
                received += n;
                continue;
            }
            if (!check_header(head, err)) break;
            esp_err_t e = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &handle);
            if (e != ESP_OK) {
                err = std::string("esp_ota_begin: ") + esp_err_to_name(e);
                break;
            }
            begun = true;
            // Write the collected header; the rest of the current chunk follows below
            if (esp_ota_write(handle, head, kHeaderLen) != ESP_OK) {
                err = "Flash-Schreibfehler";
                break;
            }
            size_t rest = static_cast<size_t>(n) - take;
            if (rest && esp_ota_write(handle, buf.get() + take, rest) != ESP_OK) {
                err = "Flash-Schreibfehler";
                break;
            }
        } else if (esp_ota_write(handle, buf.get(), n) != ESP_OK) {
            err = "Flash-Schreibfehler";
            break;
        }
        received += n;
    }

    if (err.empty() && !begun) err = "Datei zu kurz";

    if (!err.empty()) {
        if (begun) esp_ota_abort(handle);
        ESP_LOGE(TAG, "OTA failed: %s", err.c_str());
        finish(false);
        return http::send_error(req, "400 Bad Request", err.c_str());
    }

    esp_err_t e = esp_ota_end(handle);  // checks image integrity (hash)
    if (e == ESP_OK) e = esp_ota_set_boot_partition(target);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "OTA finalize failed: %s", esp_err_to_name(e));
        finish(false);
        return http::send_error(req, "400 Bad Request",
                                e == ESP_ERR_OTA_VALIDATE_FAILED ? "Image ungueltig" : esp_err_to_name(e));
    }

    ESP_LOGI(TAG, "OTA successful, rebooting in 1 s");
    http::send_json(req, "{\"status\":\"ok\",\"reboot\":true}");
    // pumps stay locked, LED stays on OTA until reboot
    led::set(led::State::Ota, true);
    schedule_restart(1000);
    return ESP_OK;
}

void rollback_task(void*) {
    vTaskDelay(pdMS_TO_TICKS(CONFIG_LGAL_OTA_CONFIRM_DELAY_S * 1000));
    if (!s_health || s_health()) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "New firmware confirmed (valid)");
    } else {
        ESP_LOGE(TAG, "Health check failed -> rolling back to previous firmware");
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
    vTaskDelete(nullptr);
}
}  // namespace

void set_guard(Guard guard) { s_guard = std::move(guard); }
void set_lock_hook(LockHook hook) { s_lock = std::move(hook); }
bool in_progress() { return s_busy; }

esp_err_t register_handlers(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_STATE;
    httpd_uri_t u{};
    u.uri = "/api/v1/ota/firmware";
    u.method = HTTP_POST;
    u.handler = post_firmware;
    return httpd_register_uri_handler(server, &u);
}

void start_rollback_guard(std::function<bool()> health_check) {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK || state != ESP_OTA_IMG_PENDING_VERIFY) {
        return;
    }
    ESP_LOGW(TAG, "Firmware in %s is new (PENDING_VERIFY), verifying in %d s", running->label,
             CONFIG_LGAL_OTA_CONFIRM_DELAY_S);
    s_health = std::move(health_check);
    xTaskCreate(rollback_task, "ota_verify", 4096, nullptr, 5, nullptr);
}

}  // namespace lgal::ota
