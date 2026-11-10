#include "doser/history.h"

#include <sys/stat.h>

#include <atomic>

#include "esp_littlefs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lgal/timesync.h"
#include "nvs.h"

namespace doser::history {

namespace {
constexpr const char* TAG = "history";
constexpr const char* kLabel = "storage";
constexpr const char* kBase = "/storage";
constexpr const char* kDir = "/storage/hist";
constexpr int kQueueLen = 32;
constexpr int64_t kPruneEveryS = 3600;
constexpr const char* kNs = "history";
constexpr const char* kKeepKey = "keep";

QueueHandle_t s_q = nullptr;
SemaphoreHandle_t s_mx = nullptr;
Store* s_store = nullptr;
std::atomic<uint32_t> s_dropped{0};
std::atomic<uint16_t> s_keep{kKeepDaysDefault};
std::atomic<bool> s_prune_now{false};

struct Guard {
    Guard() { xSemaphoreTake(s_mx, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(s_mx); }
};

void task(void*) {
    int64_t last_prune = 0;
    for (;;) {
        Event e;
        if (xQueueReceive(s_q, &e, pdMS_TO_TICKS(s_prune_now.load() ? 1000 : 60000)) == pdTRUE) {
            Guard g;
            if (!s_store->append(e)) {
                s_dropped.fetch_add(1);
                ESP_LOGW(TAG, "append failed (%s pump %u)", kind_str(e.kind), e.pump);
            }
        }
        const auto ts = lgal::timesync::status();
        if (ts.valid && (ts.utc - last_prune >= kPruneEveryS || s_prune_now.exchange(false))) {
            last_prune = ts.utc;
            Guard g;
            const int n = s_store->prune(static_cast<uint32_t>(ts.utc), s_keep.load());
            if (n) ESP_LOGI(TAG, "pruned %d day files", n);
        }
    }
}
}  // namespace

esp_err_t init() {
    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = kBase;
    conf.partition_label = kLabel;
    conf.format_if_mount_failed = true;
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(err));
        return err;
    }
    struct stat st {};
    if (stat(kDir, &st) != 0 && mkdir(kDir, 0755) != 0) {
        ESP_LOGE(TAG, "cannot create %s", kDir);
        return ESP_FAIL;
    }
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
        uint16_t v = 0;
        if (nvs_get_u16(h, kKeepKey, &v) == ESP_OK && v >= kKeepDaysMin && v <= kKeepDaysMax) s_keep = v;
        nvs_close(h);
    }
    s_mx = xSemaphoreCreateMutex();
    s_q = xQueueCreate(kQueueLen, sizeof(Event));
    static Store store(kDir);
    s_store = &store;
    if (!s_mx || !s_q || xTaskCreate(task, "history", 4096, nullptr, 3, nullptr) != pdPASS) {
        s_store = nullptr;
        return ESP_ERR_NO_MEM;
    }
    const Usage u = usage();
    ESP_LOGI(TAG, "%d days, %llu bytes (fs %u of %u used)", u.days, static_cast<unsigned long long>(u.bytes),
             unsigned(u.fs_used), unsigned(u.fs_total));
    return ESP_OK;
}

bool ready() { return s_store != nullptr; }

void record(const Event& in) {
    if (!s_store) return;
    Event e = in;
    if (!e.utc) {
        const auto ts = lgal::timesync::status();
        if (!ts.valid) return;  // without a time an entry is worthless
        e.utc = static_cast<uint32_t>(ts.utc);
    }
    if (xQueueSend(s_q, &e, 0) != pdTRUE) s_dropped.fetch_add(1);
}

size_t query(const Filter& f, bool (*cb)(const Event&, void*), void* ctx) {
    if (!s_store) return 0;
    Guard g;
    return s_store->query(f, cb, ctx);
}

Usage usage() {
    Usage u;
    u.dropped = s_dropped.load();
    if (!s_store) return u;
    {
        Guard g;
        s_store->usage(&u.days, &u.bytes);
    }
    esp_littlefs_info(kLabel, &u.fs_total, &u.fs_used);
    return u;
}

uint16_t keep_days() { return s_keep.load(); }

esp_err_t set_keep_days(uint16_t days) {
    if (days < kKeepDaysMin || days > kKeepDaysMax) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNs, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u16(h, kKeepKey, days);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) return err;
    if (days < s_keep.exchange(days)) s_prune_now = true;
    ESP_LOGI(TAG, "keeping %u days", unsigned(days));
    return ESP_OK;
}

esp_err_t clear() {
    if (!s_store) return ESP_ERR_INVALID_STATE;
    Guard g;
    const int n = s_store->clear();
    ESP_LOGW(TAG, "history cleared (%d day files)", n);
    return ESP_OK;
}

}  // namespace doser::history
