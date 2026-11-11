#include "lgal/ota.h"

#include <atomic>
#include <cctype>
#include <cstring>
#include <memory>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lgal/http.h"
#include "lgal/json.h"
#include "lgal/json_reader.h"
#include "lgal/led.h"
#include "lgal/mqtt.h"
#include "nvs.h"
#include "sdkconfig.h"

namespace lgal::ota {

namespace {
constexpr const char* TAG = "ota";
constexpr size_t kChunk = 4096;
constexpr size_t kHeaderLen =
    sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
constexpr const char* kNs = "ota";
constexpr int kMaxRedirects = 5;
constexpr int kTimeoutMs = 15000;

Guard s_guard;
LockHook s_lock;
std::atomic<bool> s_busy{false};
std::function<bool()> s_health;
std::atomic<bool> s_allow_http{false};

// URL update progress (s_mux)
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
struct Progress {
    State state = State::Idle;
    char source[8] = "";
    bool http = false;  // plain http (no transport security)
    uint32_t received = 0;
    int32_t total = -1;  // -1 = unknown (no Content-Length)
    const char* error = nullptr;
    char detail[64] = "";
};
Progress s_prog;

void restart_cb(void*) { esp_restart(); }

void schedule_restart(uint32_t ms) {
    esp_timer_create_args_t args{};
    args.callback = restart_cb;
    args.name = "ota_reboot";
    esp_timer_handle_t t;
    if (esp_timer_create(&args, &t) == ESP_OK) esp_timer_start_once(t, uint64_t(ms) * 1000);
}

// Checks whether the image matches this device.
bool check_header(const uint8_t* data, std::string& err, const char*& code) {
    const auto* img = reinterpret_cast<const esp_image_header_t*>(data);
    if (img->magic != ESP_IMAGE_HEADER_MAGIC) {
        code = "not_esp_image";
        err = "Not an ESP firmware image (magic)";
        return false;
    }
    if (img->chip_id != ESP_CHIP_ID_ESP32S3) {
        code = "wrong_chip";
        err = "Firmware is not built for ESP32-S3";
        return false;
    }
    const auto* desc = reinterpret_cast<const esp_app_desc_t*>(
        data + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
    if (desc->magic_word != ESP_APP_DESC_MAGIC_WORD) {
        code = "no_app_desc";
        err = "App description missing";
        return false;
    }
    const esp_app_desc_t* own = esp_app_get_description();
    if (strncmp(desc->project_name, own->project_name, sizeof(desc->project_name)) != 0) {
        code = "wrong_project";
        err = std::string("Wrong project: ") + std::string(desc->project_name, strnlen(desc->project_name, 32));
        return false;
    }
    ESP_LOGI(TAG, "New image: %s %s (%s %s), running: %s", desc->project_name, desc->version, desc->date,
             desc->time, own->version);
    return true;
}

// Writes an image arriving in chunks to the inactive slot: the header is
// checked before the first flash write, esp_ota_end verifies the hash.
class Writer {
public:
    explicit Writer(const esp_partition_t* target) : target_(target) {}
    ~Writer() {
        if (begun_ && !ended_) esp_ota_abort(handle_);
    }
    bool feed(const uint8_t* data, size_t n) {
        if (!begun_) {
            const size_t take = std::min(kHeaderLen - head_fill_, n);
            memcpy(head_ + head_fill_, data, take);
            head_fill_ += take;
            if (head_fill_ < kHeaderLen) return true;
            if (!check_header(head_, err, code)) return false;
            const esp_err_t e = esp_ota_begin(target_, OTA_WITH_SEQUENTIAL_WRITES, &handle_);
            if (e != ESP_OK) return fail("flash_error", std::string("esp_ota_begin: ") + esp_err_to_name(e));
            begun_ = true;
            if (esp_ota_write(handle_, head_, kHeaderLen) != ESP_OK) return fail("flash_error", "Flash write error");
            data += take;
            n -= take;
        }
        if (n && esp_ota_write(handle_, data, n) != ESP_OK) return fail("flash_error", "Flash write error");
        return true;
    }
    // Verifies the image and makes it the boot partition
    bool finish() {
        if (!begun_) return fail("file_too_short", "File too short");
        ended_ = true;
        esp_err_t e = esp_ota_end(handle_);  // checks image integrity (hash)
        if (e == ESP_OK) e = esp_ota_set_boot_partition(target_);
        if (e == ESP_ERR_OTA_VALIDATE_FAILED) return fail("image_invalid", "Image verification failed");
        if (e != ESP_OK) return fail("flash_error", esp_err_to_name(e));
        return true;
    }
    bool fail(const char* c, std::string e) {
        code = c;
        err = std::move(e);
        return false;
    }
    const char* code = "upload_failed";
    std::string err;

private:
    const esp_partition_t* target_;
    esp_ota_handle_t handle_ = 0;
    bool begun_ = false, ended_ = false;
    size_t head_fill_ = 0;
    uint8_t head_[kHeaderLen];
};

void finish(bool ok) {
    led::set(led::State::Ota, false);
    if (!ok && s_lock) s_lock(false);
    s_busy = false;
}

// Everything that has to pass before flashing starts. nullptr = go (s_busy taken).
const char* begin_update(std::string& reason) {
    bool expected = false;
    if (!s_busy.compare_exchange_strong(expected, true)) return "ota_in_progress";
    if (s_guard && !s_guard(reason)) {
        s_busy = false;
        return "guard";
    }
    if (!esp_ota_get_next_update_partition(nullptr)) {
        s_busy = false;
        return "no_ota_partition";
    }
    return nullptr;
}

esp_err_t post_firmware(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    std::string reason;
    if (const char* e = begin_update(reason)) {
        if (!std::strcmp(e, "ota_in_progress"))
            return http::send_error(req, "409 Conflict", e, "An update is already in progress");
        if (!std::strcmp(e, "guard")) return http::send_error(req, "409 Conflict", reason.c_str(), "Update not allowed right now");
        return http::send_error(req, "500 Internal Server Error", e, "No OTA partition");
    }
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (req->content_len < kHeaderLen || req->content_len > target->size) {
        s_busy = false;
        return http::send_error(req, "413 Content Too Large", "invalid_size", "Invalid file size");
    }

    if (s_lock) s_lock(true);
    led::set(led::State::Ota, true);
    ESP_LOGI(TAG, "OTA start: %u bytes -> %s", (unsigned)req->content_len, target->label);

    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[kChunk]);
    if (!buf) {
        finish(false);
        return http::send_error(req, "500 Internal Server Error", "no_memory", "Out of memory");
    }
    auto w = std::make_unique<Writer>(target);
    size_t received = 0;
    int timeouts = 0;
    bool ok = true;
    while (received < req->content_len) {
        const int n = httpd_req_recv(req, reinterpret_cast<char*>(buf.get()), std::min(kChunk, req->content_len - received));
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 5) continue;
        if (n <= 0) {
            ok = w->fail("transfer_aborted", "Transfer aborted");
            break;
        }
        timeouts = 0;
        if (!w->feed(buf.get(), n)) {
            ok = false;
            break;
        }
        received += n;
    }
    if (ok) ok = w->finish();
    if (!ok) {
        ESP_LOGE(TAG, "OTA failed: %s", w->err.c_str());
        const std::string code = w->code, err = w->err;
        w.reset();
        finish(false);
        return http::send_error(req, "400 Bad Request", code.c_str(), err.c_str());
    }

    ESP_LOGI(TAG, "OTA successful, rebooting in 1 s");
    http::send_json(req, "{\"status\":\"ok\",\"reboot\":true}");
    // pumps stay locked, LED stays on OTA until reboot
    led::set(led::State::Ota, true);
    schedule_restart(1000);
    return ESP_OK;
}

// --- update from a URL ---------------------------------------------------------

bool scheme_http(const char* url) { return strncasecmp(url, "http://", 7) == 0; }
bool scheme_https(const char* url) { return strncasecmp(url, "https://", 8) == 0; }

// nullptr = acceptable; otherwise "invalid_url" or "http_not_allowed"
const char* check_url(const char* url) {
    const size_t len = strnlen(url, kMaxUrl + 1);
    if (len == 0 || len > kMaxUrl) return "invalid_url";
    for (size_t i = 0; i < len; ++i)
        if (static_cast<unsigned char>(url[i]) <= ' ' || url[i] == 0x7F) return "invalid_url";
    const bool http = scheme_http(url);
    if (!http && !scheme_https(url)) return "invalid_url";
    const char* host = url + (http ? 7 : 8);
    if (!*host || *host == '/' || *host == ':' || *host == '?' || *host == '#') return "invalid_url";
    if (http && !s_allow_http) return "http_not_allowed";
    return nullptr;
}

void publish_state();

void set_progress(State st, const char* error = nullptr, const char* detail = nullptr) {
    portENTER_CRITICAL(&s_mux);
    s_prog.state = st;
    if (error) s_prog.error = error;
    if (detail) std::snprintf(s_prog.detail, sizeof(s_prog.detail), "%s", detail);
    portEXIT_CRITICAL(&s_mux);
    publish_state();
}

void write_status(Json& j) {
    portENTER_CRITICAL(&s_mux);
    const Progress p = s_prog;
    portEXIT_CRITICAL(&s_mux);
    j.obj().kv("state", state_name(p.state));
    if (p.state != State::Idle) {
        j.kv("source", p.source).kv("http", p.http).kv("received", static_cast<int64_t>(p.received));
        if (p.total >= 0) j.kv("total", static_cast<int64_t>(p.total));
        else j.null("total");
    }
    if (p.state == State::Failed && p.error) j.kv("error", p.error).kv("detail", p.detail);
    j.end();
}

// "<base>/ota/state" (not retained), whenever the state changes
void publish_state() {
    if (!mqtt::connected()) return;
    Json j;
    write_status(j);
    mqtt::publish(mqtt::topic("ota/state"), j.c_str(), int(j.str().size()), false, 1);
}

struct UrlJob {
    std::string url;
};

void fail_url(const char* code, const char* detail) {
    ESP_LOGE(TAG, "URL update failed: %s (%s)", code, detail);
    finish(false);
    set_progress(State::Failed, code, detail);
}

void url_task(void* arg) {
    std::unique_ptr<UrlJob> job(static_cast<UrlJob*>(arg));
    publish_state();
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[kChunk]);
    if (!buf || !target) {
        fail_url("no_memory", "Out of memory");
        vTaskDelete(nullptr);
        return;
    }
    esp_http_client_config_t cfg{};
    cfg.url = job->url.c_str();
    cfg.timeout_ms = kTimeoutMs;
    cfg.buffer_size = kChunk;
    cfg.buffer_size_tx = 2048;  // redirect targets with signed query strings are long
    cfg.crt_bundle_attach = esp_crt_bundle_attach;  // https: server certificate checked
    cfg.disable_auto_redirect = true;               // every hop is checked below
    cfg.keep_alive_enable = false;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        fail_url("no_memory", "HTTP client");
        vTaskDelete(nullptr);
        return;
    }

    const char* code = nullptr;
    char detail[64] = "";
    int64_t length = -1;
    for (int hop = 0;; ++hop) {
        const esp_err_t e = esp_http_client_open(client, 0);
        if (e != ESP_OK) {
            code = "download_failed";
            std::snprintf(detail, sizeof(detail), "%s", esp_err_to_name(e));
            break;
        }
        length = esp_http_client_fetch_headers(client);
        const int status = esp_http_client_get_status_code(client);
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            if (hop >= kMaxRedirects) {
                code = "too_many_redirects";
                break;
            }
            esp_http_client_set_redirection(client);
            esp_http_client_close(client);
            char next[kMaxUrl + 1] = "";
            esp_http_client_get_url(client, next, sizeof(next));
            if (scheme_http(next) && !s_allow_http) {  // no silent downgrade to http
                code = "http_not_allowed";
                std::snprintf(detail, sizeof(detail), "redirect to http");
                break;
            }
            if (scheme_http(next)) {
                portENTER_CRITICAL(&s_mux);
                s_prog.http = true;
                portEXIT_CRITICAL(&s_mux);
            }
            continue;
        }
        if (status != 200) {
            code = "http_status";
            std::snprintf(detail, sizeof(detail), "HTTP %d", status);
        } else if (length > 0 && (length < int64_t(kHeaderLen) || length > int64_t(target->size))) {
            code = "invalid_size";
            std::snprintf(detail, sizeof(detail), "%lld bytes", static_cast<long long>(length));
        }
        break;
    }

    if (!code) {
        if (s_lock) s_lock(true);
        led::set(led::State::Ota, true);
        portENTER_CRITICAL(&s_mux);
        s_prog.total = length > 0 ? static_cast<int32_t>(length) : -1;
        portEXIT_CRITICAL(&s_mux);
        ESP_LOGI(TAG, "URL update: %lld bytes -> %s", static_cast<long long>(length), target->label);
        auto w = std::make_unique<Writer>(target);
        size_t received = 0;
        int64_t last_pub = 0;
        bool ok = true;
        while (true) {
            const int n = esp_http_client_read(client, reinterpret_cast<char*>(buf.get()), kChunk);
            if (n < 0) {
                ok = w->fail("download_failed", "Transfer aborted");
                break;
            }
            if (n == 0) {
                if (!esp_http_client_is_complete_data_received(client)) ok = w->fail("download_failed", "Transfer aborted");
                break;
            }
            if (received + n > target->size) {
                ok = w->fail("invalid_size", "Image larger than the slot");
                break;
            }
            if (!w->feed(buf.get(), n)) {
                ok = false;
                break;
            }
            received += n;
            portENTER_CRITICAL(&s_mux);
            s_prog.received = static_cast<uint32_t>(received);
            portEXIT_CRITICAL(&s_mux);
            const int64_t now = esp_timer_get_time();
            if (now - last_pub > 5000000) {  // MQTT progress every 5 s
                last_pub = now;
                publish_state();
            }
        }
        if (ok) ok = w->finish();
        if (!ok) {
            code = w->code;
            std::snprintf(detail, sizeof(detail), "%s", w->err.c_str());
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (code) {
        fail_url(code, detail);
    } else {
        ESP_LOGI(TAG, "URL update successful, rebooting in 2 s");
        set_progress(State::Rebooting);
        led::set(led::State::Ota, true);  // pumps stay locked until the reboot
        schedule_restart(2000);
    }
    vTaskDelete(nullptr);
}

// --- settings --------------------------------------------------------------------

void load_settings() {
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t v = 0;
    if (nvs_get_u8(h, "http", &v) == ESP_OK) s_allow_http = v != 0;
    nvs_close(h);
}

esp_err_t save_settings(const Settings& s) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNs, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, "http", s.allow_http);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) s_allow_http = s.allow_http;
    return err;
}

void write_settings(Json& j) {
    j.obj().kv("allow_http", s_allow_http.load()).end();
}

esp_err_t get_settings(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    Json j;
    write_settings(j);
    return http::send_json(req, j.c_str());
}

esp_err_t put_settings(httpd_req_t* req) {
    if (!http::authorize_engineering(req)) return ESP_OK;
    std::string body;
    JsonReader doc;
    JsonToken tok[8];
    if (!http::read_json(req, 256, &body, &doc, tok, 8)) return ESP_OK;
    const JsonValue root = doc.root();
    Settings s = settings();
    const JsonValue vh = root["allow_http"];
    if (vh.valid() && !vh.get(&s.allow_http)) return http::send_error(req, "400 Bad Request", "invalid_param", "allow_http");
    if (s.allow_http && !s_allow_http) ESP_LOGW(TAG, "plain http allowed for firmware URLs");
    if (save_settings(s) != ESP_OK) return http::send_error(req, "500 Internal Server Error", "storage_failed", "Could not save");
    Json j;
    write_settings(j);
    return http::send_json(req, j.c_str());
}

esp_err_t get_status(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    Json j;
    write_status(j);
    return http::send_json(req, j.c_str());
}

esp_err_t post_url(httpd_req_t* req) {
    if (!http::authorize_engineering(req)) return ESP_OK;
    std::string body;
    JsonReader doc;
    JsonToken tok[8];
    if (!http::read_json(req, kMaxUrl + 64, &body, &doc, tok, 8)) return ESP_OK;
    char url[kMaxUrl + 2] = "";
    if (!doc.root()["url"].get(url, sizeof(url)))
        return http::send_error(req, "400 Bad Request", "invalid_url", "url required");
    std::string reason;
    const char* e = start_url(url, "api", &reason);
    if (!e) return http::send_json(req, "{\"status\":\"started\"}", "202 Accepted");
    if (!std::strcmp(e, "invalid_url")) return http::send_error(req, "400 Bad Request", e, "Invalid URL (http:// or https://)");
    if (!std::strcmp(e, "http_not_allowed"))
        return http::send_error(req, "400 Bad Request", e, "Plain http is not allowed (setting allow_http)");
    if (!std::strcmp(e, "ota_in_progress")) return http::send_error(req, "409 Conflict", e, "An update is already in progress");
    if (!std::strcmp(e, "guard")) return http::send_error(req, "409 Conflict", reason.c_str(), "Update not allowed right now");
    return http::send_error(req, "500 Internal Server Error", e, e);
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

const char* state_name(State s) {
    switch (s) {
        case State::Running: return "running";
        case State::Failed: return "failed";
        case State::Rebooting: return "rebooting";
        default: return "idle";
    }
}

void set_guard(Guard guard) { s_guard = std::move(guard); }
void set_lock_hook(LockHook hook) { s_lock = std::move(hook); }
bool in_progress() { return s_busy; }

Settings settings() {
    Settings s;
    s.allow_http = s_allow_http;
    return s;
}

const char* start_url(const char* url, const char* source, std::string* reason) {
    if (const char* e = check_url(url)) return e;
    std::string why;
    if (const char* e = begin_update(why)) {
        if (reason) *reason = why;
        return e;
    }
    auto* job = new (std::nothrow) UrlJob{url};
    portENTER_CRITICAL(&s_mux);
    s_prog = Progress{};
    s_prog.state = State::Running;
    std::snprintf(s_prog.source, sizeof(s_prog.source), "%s", source);
    s_prog.http = scheme_http(url);
    portEXIT_CRITICAL(&s_mux);
    // TLS needs a large stack; the task lives only for the update
    if (!job || xTaskCreate(url_task, "ota_url", 8192, job, 4, nullptr) != pdPASS) {
        delete job;
        s_busy = false;
        portENTER_CRITICAL(&s_mux);
        s_prog = Progress{};
        portEXIT_CRITICAL(&s_mux);
        return "no_memory";
    }
    ESP_LOGW(TAG, "URL update started (%s, %s)", source, scheme_http(url) ? "http" : "https");
    return nullptr;  // the task reports the state
}

esp_err_t register_handlers(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_STATE;
    load_settings();
    httpd_uri_t u{};
    u.uri = "/api/v1/ota/firmware";
    u.method = HTTP_POST;
    u.handler = post_firmware;
    const esp_err_t err = httpd_register_uri_handler(server, &u);
    if (err != ESP_OK) return err;
    http::route(server, "/api/v1/ota/url", HTTP_POST, post_url);
    http::route(server, "/api/v1/ota/status", HTTP_GET, get_status);
    http::route(server, "/api/v1/ota/settings", HTTP_GET, get_settings);
    http::route(server, "/api/v1/ota/settings", HTTP_PUT, put_settings);
    return ESP_OK;
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
