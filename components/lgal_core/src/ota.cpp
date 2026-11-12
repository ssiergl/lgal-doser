#include "lgal/ota.h"

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lgal/http.h"
#include "lgal/json.h"
#include "lgal/json_reader.h"
#include "lgal/led.h"
#include "lgal/mqtt.h"
#include "lgal/ota_image.h"
#include "lgal/ota_manifest.h"
#include "lgal/sha256.h"
#include "nvs.h"
#include "sdkconfig.h"

#ifndef LGAL_BOARD_ID
#error "LGAL_BOARD_ID is set by the top-level CMakeLists.txt"
#endif

// Board descriptor of this image, behind esp_app_desc_t (lgal/ota_image.h)
extern "C" __attribute__((used, section(".rodata_custom_desc")))
const lgal::ota::BoardDesc lgal_board_desc = {lgal::ota::kBoardDescMagic, LGAL_BOARD_ID, {}};
static_assert(sizeof(LGAL_BOARD_ID) - 1 <= lgal::ota::kMaxBoardId, "board ID too long");

namespace lgal::ota {

namespace {
constexpr const char* TAG = "ota";
constexpr size_t kChunk = 4096;
static_assert(kBoardDescOffset ==
              sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t));
constexpr size_t kHeaderLen = kBoardDescOffset + sizeof(BoardDesc);
constexpr const char* kNs = "ota";
constexpr int kMaxRedirects = 5;
constexpr int kTimeoutMs = 15000;

Guard s_guard;
LockHook s_lock;
std::atomic<bool> s_busy{false};
std::function<bool()> s_health;
std::atomic<bool> s_allow_http{false};
std::atomic<bool> s_auto_check{true};
std::function<void()> s_change;

// Update source and the last check (s_rel_mux)
std::mutex s_rel_mux;
std::string s_source;  // as set; the beta channel replaces only the factory source
bool s_beta = false;
constexpr const char* kFactorySource = CONFIG_LGAL_OTA_SOURCE;
constexpr const char* kBetaSource = CONFIG_LGAL_OTA_SOURCE_BETA;
ReleaseInfo s_rel;
char s_rel_detail[64] = "";
std::atomic<int64_t> s_next_check_us{0};  // esp_timer time of the next automatic check
constexpr int64_t kCheckTickUs = 10LL * 60 * 1000000;  // the first check ~10 min after the start
constexpr int64_t kCheckPeriodUs = 24LL * 3600 * 1000000;
constexpr int64_t kRetryUs = 3600LL * 1000000;  // after a failed check

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

// Flash size in the header of the running image: the module variant this
// device runs (N8R8/N16R8/N32R8 differ in flash size and partition table).
// 0xff = unknown.
uint8_t own_flash_size() {
    esp_image_header_t h{};
    const esp_partition_t* p = esp_ota_get_running_partition();
    if (!p || esp_partition_read(p, 0, &h, sizeof(h)) != ESP_OK || h.magic != ESP_IMAGE_HEADER_MAGIC) return 0xff;
    return h.spi_size;
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
    if (const uint8_t own = own_flash_size(); own != 0xff && img->spi_size != own) {
        code = "wrong_variant";
        err = "Firmware is built for a module with a different flash size";
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
    // other circuit board: other pins, other pump count
    if (const std::string b = image_board(data + kBoardDescOffset, sizeof(BoardDesc)); b != LGAL_BOARD_ID) {
        code = "wrong_board";
        err = b.empty() ? std::string("Firmware has no board ID")
                        : "Firmware is for board " + b + ", this device is " LGAL_BOARD_ID;
        return false;
    }
    ESP_LOGI(TAG, "New image: %s %s for %s (%s %s), running: %s", desc->project_name, desc->version, LGAL_BOARD_ID,
             desc->date, desc->time, own->version);
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

// --- download from an address ---------------------------------------------------

bool scheme_http(const char* url) { return strncasecmp(url, "http://", 7) == 0; }
bool scheme_https(const char* url) { return strncasecmp(url, "https://", 8) == 0; }

// nullptr = acceptable; otherwise "invalid_url" or (unless format_only)
// "http_not_allowed"
const char* check_url(const char* url, bool format_only = false) {
    const size_t len = strnlen(url, kMaxUrl + 1);
    if (len == 0 || len > kMaxUrl) return "invalid_url";
    for (size_t i = 0; i < len; ++i)
        if (static_cast<unsigned char>(url[i]) <= ' ' || url[i] == 0x7F) return "invalid_url";
    const bool http = scheme_http(url);
    if (!http && !scheme_https(url)) return "invalid_url";
    const char* host = url + (http ? 7 : 8);
    if (!*host || *host == '/' || *host == ':' || *host == '?' || *host == '#') return "invalid_url";
    if (http && !format_only && !s_allow_http) return "http_not_allowed";
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

void mark_http() {
    portENTER_CRITICAL(&s_mux);
    s_prog.http = true;
    portEXIT_CRITICAL(&s_mux);
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

// "<base>/ota/state" (not retained) and the change hook, whenever the state changes
void publish_state() {
    if (s_change) s_change();
    if (!mqtt::connected()) return;
    Json j;
    write_status(j);
    mqtt::publish(mqtt::topic("ota/state"), j.c_str(), int(j.str().size()), false, 1);
}

struct UrlJob {
    std::string url;
    bool verify = false;      // compare with sha256 before switching (update source)
    uint8_t sha256[crypto::kSha256Len] = {};
    uint32_t size = 0;        // expected size, 0 = unknown
};

void fail_url(const char* code, const char* detail) {
    ESP_LOGE(TAG, "Update failed: %s (%s)", code, detail);
    finish(false);
    set_progress(State::Failed, code, detail);
}

esp_http_client_handle_t make_client(const char* url) {
    esp_http_client_config_t cfg{};
    cfg.url = url;
    cfg.timeout_ms = kTimeoutMs;
    cfg.buffer_size = kChunk;
    cfg.buffer_size_tx = 2048;  // redirect targets with signed query strings are long
    cfg.crt_bundle_attach = esp_crt_bundle_attach;  // https: server certificate checked
    cfg.disable_auto_redirect = true;               // every hop is checked below
    cfg.keep_alive_enable = false;
    return esp_http_client_init(&cfg);
}

// Sends the request and follows redirects, each hop checked. nullptr = HTTP
// 200 with *length = Content-Length (<= 0 unknown); *http = a hop was plain http.
const char* open_follow(esp_http_client_handle_t client, int64_t* length, bool* http, char (&detail)[64]) {
    for (int hop = 0;; ++hop) {
        const esp_err_t e = esp_http_client_open(client, 0);
        if (e != ESP_OK) {
            std::snprintf(detail, sizeof(detail), "%s", esp_err_to_name(e));
            return "download_failed";
        }
        *length = esp_http_client_fetch_headers(client);
        const int status = esp_http_client_get_status_code(client);
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            if (hop >= kMaxRedirects) return "too_many_redirects";
            esp_http_client_set_redirection(client);
            esp_http_client_close(client);
            char next[16] = "";  // only the scheme is checked
            esp_http_client_get_url(client, next, sizeof(next));
            if (scheme_http(next)) {
                if (!s_allow_http) {  // no silent downgrade to http
                    std::snprintf(detail, sizeof(detail), "redirect to http");
                    return "http_not_allowed";
                }
                *http = true;
            }
            continue;
        }
        if (status != 200) {
            std::snprintf(detail, sizeof(detail), "HTTP %d", status);
            return "http_status";
        }
        return nullptr;
    }
}

// Downloads the image into the inactive slot and restarts on success
// (update task, s_busy taken).
void download(const UrlJob& job) {
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[kChunk]);
    esp_http_client_handle_t client = buf && target ? make_client(job.url.c_str()) : nullptr;
    if (!client) {
        fail_url("no_memory", "Out of memory");
        return;
    }
    char detail[64] = "";
    int64_t length = -1;
    bool http = false;
    const char* code = open_follow(client, &length, &http, detail);
    if (http) mark_http();
    if (!code && length > 0 &&
        (length < int64_t(kHeaderLen) || length > int64_t(target->size) || (job.size && length != job.size))) {
        code = "invalid_size";
        std::snprintf(detail, sizeof(detail), "%lld bytes", static_cast<long long>(length));
    }

    if (!code) {
        if (s_lock) s_lock(true);
        led::set(led::State::Ota, true);
        portENTER_CRITICAL(&s_mux);
        s_prog.total = length > 0 ? static_cast<int32_t>(length) : -1;
        portEXIT_CRITICAL(&s_mux);
        ESP_LOGI(TAG, "Download: %lld bytes -> %s", static_cast<long long>(length), target->label);
        auto w = std::make_unique<Writer>(target);
        crypto::Sha256 sha;
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
            if (job.verify) sha.update(buf.get(), size_t(n));
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
        if (ok && job.verify) {  // before the image can become the boot partition
            uint8_t got[crypto::kSha256Len];
            sha.finish(got);
            if (job.size && received != job.size) ok = w->fail("invalid_size", "Size differs from the manifest");
            else if (!crypto::equal_ct(got, job.sha256, sizeof(got)))
                ok = w->fail("sha256_mismatch", "SHA-256 differs from the manifest");
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
        ESP_LOGI(TAG, "Update successful, rebooting in 2 s");
        set_progress(State::Rebooting);
        led::set(led::State::Ota, true);  // pumps stay locked until the reboot
        schedule_restart(2000);
    }
}

void url_task(void* arg) {
    std::unique_ptr<UrlJob> job(static_cast<UrlJob*>(arg));
    publish_state();
    download(*job);
    vTaskDelete(nullptr);
}

// Fresh progress for an update that starts now (s_busy taken)
void start_progress(const char* source, bool http) {
    portENTER_CRITICAL(&s_mux);
    s_prog = Progress{};
    s_prog.state = State::Running;
    std::snprintf(s_prog.source, sizeof(s_prog.source), "%s", source);
    s_prog.http = http;
    portEXIT_CRITICAL(&s_mux);
}

// Gives s_busy back when the update task could not be started
const char* spawn_failed() {
    s_busy = false;
    portENTER_CRITICAL(&s_mux);
    s_prog = Progress{};
    portEXIT_CRITICAL(&s_mux);
    return "no_memory";
}

// --- update source ---------------------------------------------------------------

// The channel can be chosen while the source is the factory one
bool channel_choice(const std::string& source) { return kBetaSource[0] && !source.empty() && source == kFactorySource; }
std::string effective_source(const std::string& source, bool beta) {
    return beta && channel_choice(source) ? std::string(kBetaSource) : source;
}

std::string source_url() {
    std::lock_guard<std::mutex> l(s_rel_mux);
    return effective_source(s_source, s_beta);
}

// Variant of this build ("N16R8"): the partition table belongs to the flash size
const char* variant() { return variant_for_flash_mb(uint32_t(std::atoi(CONFIG_ESPTOOLPY_FLASHSIZE))); }

bool newer(const std::string& version) {
    Version have, got;
    return parse_version(running_version(), &have) && parse_version(version.c_str(), &got) &&
           compare_versions(got, have) > 0;
}

// Fetches and parses the manifest of the update source. nullptr = ok.
const char* fetch_manifest(const std::string& url, Release* rel, char (&detail)[64]) {
    if (url.empty()) return "no_source";
    if (const char* e = check_url(url.c_str())) return e;
    if (!variant()) return "unknown_variant";
    esp_http_client_handle_t client = make_client(url.c_str());
    if (!client) return "no_memory";
    int64_t length = -1;
    bool http = false;
    const char* code = open_follow(client, &length, &http, detail);
    std::string body;
    if (!code && length > int64_t(kMaxManifest)) code = "manifest_invalid";
    char chunk[512];
    while (!code) {
        const int n = esp_http_client_read(client, chunk, sizeof(chunk));
        if (n < 0 || (n == 0 && !esp_http_client_is_complete_data_received(client))) {
            code = "download_failed";
            std::snprintf(detail, sizeof(detail), "Transfer aborted");
        } else if (n == 0) {
            break;
        } else if (body.size() + size_t(n) > kMaxManifest) {
            code = "manifest_invalid";
            std::snprintf(detail, sizeof(detail), "larger than %u bytes", unsigned(kMaxManifest));
        } else {
            body.append(chunk, size_t(n));
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    // relative file addresses refer to the configured address, not to a redirect target
    if (!code) code = parse_manifest(body.data(), body.size(), LGAL_BOARD_ID, variant(), url.c_str(), rel);
    return code;
}

const char* start_check();

// Keeps the result of a check of url; after a failure the last known release stays.
void store_check(const std::string& url, const char* code, const char* detail, const Release& rel) {
    const time_t now = time(nullptr);
    bool available = false, stale = false;
    {
        std::lock_guard<std::mutex> l(s_rel_mux);
        s_rel.checking = false;
        stale = url != effective_source(s_source, s_beta);  // the source changed meanwhile: the result does not apply
        if (!stale) {
            s_rel.checked = true;
            s_rel.checked_at = now > 1700000000 ? int64_t(now) : 0;
            s_rel.error = code;
            std::snprintf(s_rel_detail, sizeof(s_rel_detail), "%s", code ? detail : "");
            if (!code) {
                s_rel.latest = rel.version;
                s_rel.notes = rel.notes;
                s_rel.available = newer(rel.version);
            }
            available = s_rel.available;
        }
    }
    if (stale) {
        start_check();  // the new source
        return;
    }
    s_next_check_us = esp_timer_get_time() + (code ? kRetryUs : kCheckPeriodUs);
    if (code) ESP_LOGW(TAG, "Update check failed: %s (%s)", code, detail);
    else ESP_LOGI(TAG, "Update source: %s (running %s)%s", rel.version.c_str(), running_version(), available ? ", newer" : "");
    if (s_change) s_change();
}

void check_task(void*) {
    Release rel;
    char detail[64] = "";
    const std::string url = source_url();
    const char* code = fetch_manifest(url, &rel, detail);
    store_check(url, code, detail, rel);
    vTaskDelete(nullptr);
}

// Checks again and installs the release if it is newer (s_busy taken)
void install_task(void*) {
    publish_state();
    Release rel;
    char detail[64] = "";
    const std::string url = source_url();
    const char* code = fetch_manifest(url, &rel, detail);
    store_check(url, code, detail, rel);
    if (!code && !newer(rel.version)) code = "no_update";
    if (code) {
        fail_url(code, detail);
        vTaskDelete(nullptr);
        return;
    }
    UrlJob job;
    job.url = rel.url;
    job.verify = true;
    std::memcpy(job.sha256, rel.sha256, sizeof(job.sha256));
    job.size = rel.size;
    if (scheme_http(job.url.c_str())) mark_http();
    ESP_LOGW(TAG, "Installing %s from the update source", rel.version.c_str());
    download(job);
    vTaskDelete(nullptr);
}

// nullptr = check running (also when one was already running)
const char* start_check() {
    {
        std::lock_guard<std::mutex> l(s_rel_mux);
        if (s_source.empty()) return "no_source";
        if (s_rel.checking) return nullptr;
        s_rel.checking = true;
    }
    s_next_check_us = esp_timer_get_time() + kCheckPeriodUs;
    // TLS needs a large stack; the task lives only for the check
    if (xTaskCreate(check_task, "ota_check", 8192, nullptr, 3, nullptr) != pdPASS) {
        std::lock_guard<std::mutex> l(s_rel_mux);
        s_rel.checking = false;
        return "no_memory";
    }
    if (s_change) s_change();
    return nullptr;
}

// Every 10 min: automatic check when due (esp_timer task, only starts a task)
void check_timer_cb(void*) {
    if (s_auto_check && !s_busy && esp_timer_get_time() >= s_next_check_us) start_check();
}

void write_release(Json& j) {
    const ReleaseInfo r = release_info();
    std::string detail;
    bool source;
    {
        std::lock_guard<std::mutex> l(s_rel_mux);
        detail = s_rel_detail;
        source = !s_source.empty();
    }
    j.obj().kv("source", source);
    if (const char* c = channel()) j.kv("channel", c);
    else j.null("channel");
    j.kv("board", LGAL_BOARD_ID).kv("variant", variant() ? variant() : "");
    j.kv("current", running_version());
    j.kv("auto_check", s_auto_check.load()).kv("checking", r.checking);
    if (r.checked && r.checked_at) j.kv("checked_at", r.checked_at);
    else j.null("checked_at");
    if (r.latest.empty()) j.null("latest");
    else j.kv("latest", r.latest);
    j.kv("available", r.available);
    if (!r.notes.empty()) j.kv("notes", r.notes);
    if (r.error) j.kv("error", r.error).kv("detail", detail);
    j.end();
}

// --- settings --------------------------------------------------------------------

void load_settings() {
    std::string src = kFactorySource;
    bool beta = false;
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
        uint8_t v = 0;
        if (nvs_get_u8(h, "http", &v) == ESP_OK) s_allow_http = v != 0;
        if (nvs_get_u8(h, "auto", &v) == ESP_OK) s_auto_check = v != 0;
        if (nvs_get_u8(h, "beta", &v) == ESP_OK) beta = v != 0;
        char buf[kMaxUrl + 1];
        size_t len = sizeof(buf);
        if (nvs_get_str(h, "src", buf, &len) == ESP_OK) src = buf;  // also "" = removed
        nvs_close(h);
    }
    std::lock_guard<std::mutex> l(s_rel_mux);
    s_source = src;
    s_beta = beta;
}

esp_err_t save_settings(const Settings& s) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNs, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, "http", s.allow_http);
    if (err == ESP_OK) err = nvs_set_u8(h, "auto", s.auto_check);
    if (err == ESP_OK) err = nvs_set_u8(h, "beta", s.beta);
    if (err == ESP_OK) err = nvs_set_str(h, "src", s.source.c_str());
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) return err;
    s_allow_http = s.allow_http;
    s_auto_check = s.auto_check;
    std::lock_guard<std::mutex> l(s_rel_mux);
    if (effective_source(s.source, s.beta) != effective_source(s_source, s_beta)) {  // results of the old source no longer apply
        const bool checking = s_rel.checking;
        s_rel = ReleaseInfo{};
        s_rel.checking = checking;
        s_rel_detail[0] = 0;
    }
    s_source = s.source;
    s_beta = s.beta;
    return ESP_OK;
}

void write_settings(Json& j) {
    const Settings s = settings();
    j.obj().kv("source", s.source);
    if (const char* c = channel()) j.kv("channel", c);
    else j.null("channel");
    j.kv("auto_check", s.auto_check).kv("allow_http", s.allow_http).end();
}

esp_err_t get_settings(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    Json j;
    write_settings(j);
    return http::send_json(req, j.c_str());
}

esp_err_t put_settings(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    std::string body;
    JsonReader doc;
    JsonToken tok[12];
    if (!http::read_json(req, kMaxUrl + 128, &body, &doc, tok, 12)) return ESP_OK;
    const JsonValue root = doc.root();
    const JsonValue vs = root["source"], va = root["auto_check"], vh = root["allow_http"], vc = root["channel"];
    // where firmware may come from is up to the manufacturer
    if ((vs.valid() || vh.valid()) && !http::authorize_engineering(req)) return ESP_OK;
    Settings s = settings();
    const std::string old_source = effective_source(s.source, s.beta);
    if (vs.valid()) {
        char url[kMaxUrl + 2] = "";
        if (!vs.get(url, sizeof(url)) || (url[0] && check_url(url, true)))
            return http::send_error(req, "400 Bad Request", "invalid_url", "source");
        if (s.source != url) s.beta = false;  // a new source starts on its stable channel
        s.source = url;
    }
    if (va.valid() && !va.get(&s.auto_check)) return http::send_error(req, "400 Bad Request", "invalid_param", "auto_check");
    if (vh.valid() && !vh.get(&s.allow_http)) return http::send_error(req, "400 Bad Request", "invalid_param", "allow_http");
    if (vc.valid()) {  // the admin's choice (pro view, with a warning)
        char c[8] = "";
        if (!vc.get(c, sizeof(c)) || (std::strcmp(c, "stable") && std::strcmp(c, "beta")))
            return http::send_error(req, "400 Bad Request", "invalid_param", "channel");
        if (!channel_choice(s.source)) return http::send_error(req, "409 Conflict", "no_channel", "No channel to choose (own update source)");
        s.beta = !std::strcmp(c, "beta");
    }
    const std::string new_source = effective_source(s.source, s.beta);
    if (s.allow_http && !s_allow_http) ESP_LOGW(TAG, "plain http allowed for firmware updates");
    if (save_settings(s) != ESP_OK) return http::send_error(req, "500 Internal Server Error", "storage_failed", "Could not save");
    if (new_source != old_source) {
        if (vc.valid() && !vs.valid()) ESP_LOGW(TAG, "Update channel %s", s.beta ? "beta" : "stable");
        else ESP_LOGW(TAG, "Update source %s", new_source.empty() ? "removed" : "changed");
        if (!new_source.empty()) start_check();
    }
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

esp_err_t start_error(httpd_req_t* req, const char* e, const std::string& reason) {
    if (!std::strcmp(e, "invalid_url")) return http::send_error(req, "400 Bad Request", e, "Invalid URL (http:// or https://)");
    if (!std::strcmp(e, "http_not_allowed"))
        return http::send_error(req, "400 Bad Request", e, "Plain http is not allowed (setting allow_http)");
    if (!std::strcmp(e, "no_source")) return http::send_error(req, "400 Bad Request", e, "No update source set");
    if (!std::strcmp(e, "ota_in_progress")) return http::send_error(req, "409 Conflict", e, "An update is already in progress");
    if (!std::strcmp(e, "guard")) return http::send_error(req, "409 Conflict", reason.c_str(), "Update not allowed right now");
    return http::send_error(req, "500 Internal Server Error", e, e);
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
    return e ? start_error(req, e, reason) : http::send_json(req, "{\"status\":\"started\"}", "202 Accepted");
}

esp_err_t get_release(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    Json j;
    write_release(j);
    return http::send_json(req, j.c_str());
}

esp_err_t post_check(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    const char* e = start_check();
    return e ? start_error(req, e, "") : http::send_json(req, "{\"status\":\"started\"}", "202 Accepted");
}

esp_err_t post_install(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    std::string reason;
    const char* e = start_release_install("api", &reason);
    return e ? start_error(req, e, reason) : http::send_json(req, "{\"status\":\"started\"}", "202 Accepted");
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
void set_change_hook(std::function<void()> hook) { s_change = std::move(hook); }
bool in_progress() { return s_busy; }
const char* running_version() { return esp_app_get_description()->version; }

Settings settings() {
    Settings s;
    s.allow_http = s_allow_http;
    s.auto_check = s_auto_check;
    std::lock_guard<std::mutex> l(s_rel_mux);
    s.source = s_source;
    s.beta = s_beta;
    return s;
}

const char* channel() {
    std::lock_guard<std::mutex> l(s_rel_mux);
    return channel_choice(s_source) ? (s_beta ? "beta" : "stable") : nullptr;
}

Status status() {
    portENTER_CRITICAL(&s_mux);
    const Status st{s_prog.state, s_prog.received, s_prog.total};
    portEXIT_CRITICAL(&s_mux);
    return st;
}

ReleaseInfo release_info() {
    std::lock_guard<std::mutex> l(s_rel_mux);
    return s_rel;
}

const char* start_url(const char* url, const char* source, std::string* reason) {
    if (const char* e = check_url(url)) return e;
    std::string why;
    if (const char* e = begin_update(why)) {
        if (reason) *reason = why;
        return e;
    }
    auto* job = new (std::nothrow) UrlJob{};
    if (job) job->url = url;
    start_progress(source, scheme_http(url));
    // TLS needs a large stack; the task lives only for the update
    if (!job || xTaskCreate(url_task, "ota_url", 8192, job, 4, nullptr) != pdPASS) {
        delete job;
        return spawn_failed();
    }
    ESP_LOGW(TAG, "URL update started (%s, %s)", source, scheme_http(url) ? "http" : "https");
    return nullptr;  // the task reports the state
}

const char* start_release_install(const char* source, std::string* reason) {
    if (source_url().empty()) return "no_source";
    std::string why;
    if (const char* e = begin_update(why)) {
        if (reason) *reason = why;
        return e;
    }
    start_progress(source, false);
    if (xTaskCreate(install_task, "ota_rel", 8192, nullptr, 4, nullptr) != pdPASS) return spawn_failed();
    ESP_LOGW(TAG, "Update from the source requested (%s)", source);
    return nullptr;
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
    http::route(server, "/api/v1/ota/release", HTTP_GET, get_release);
    http::route(server, "/api/v1/ota/release/check", HTTP_POST, post_check);
    http::route(server, "/api/v1/ota/release/install", HTTP_POST, post_install);
    esp_timer_create_args_t args{};
    args.callback = check_timer_cb;
    args.name = "ota_check";
    esp_timer_handle_t t;
    if (esp_timer_create(&args, &t) == ESP_OK) esp_timer_start_periodic(t, kCheckTickUs);
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
