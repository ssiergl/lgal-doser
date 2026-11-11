#include "lgal/timesync.h"

#include <sys/time.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

namespace lgal::timesync {

namespace {
constexpr const char* TAG = "time";
constexpr int64_t kMinValid = 1735689600;  // 2025-01-01: everything before is "no time"
constexpr int64_t kRtcResyncUs = 3600LL * 1000000;

constexpr const char* kNs = "lgal";
constexpr const char* kKeyTz = "tz";
constexpr const char* kKeyNtp = "ntp";

RtcHooks s_rtc;
bool (*s_tz_valid)(const char*) = nullptr;
// Two buffers: timezone() hands out the active one, a change writes the other
char s_tz_buf[2][kMaxTz + 1] = {"UTC0", ""};
std::atomic<int> s_tz_idx{0};
char s_ntp[kMaxNtp + 1] = "";  // the SNTP client keeps a pointer: only changed while stopped
SemaphoreHandle_t s_mx = nullptr;
bool s_sntp_running = false;
volatile Source s_source = Source::None;
volatile int64_t s_last_ntp = 0;
volatile bool s_rtc_ok = false;
esp_timer_handle_t s_timer = nullptr;

int64_t now_utc() {
    timeval tv{};
    gettimeofday(&tv, nullptr);
    return tv.tv_sec;
}

void set_system(int64_t utc) {
    timeval tv{};
    tv.tv_sec = static_cast<time_t>(utc);
    settimeofday(&tv, nullptr);
}

bool load_from_rtc(const char* why) {
    if (!s_rtc.read) return false;
    int64_t utc = 0;
    if (!s_rtc.read(&utc) || utc < kMinValid) {
        s_rtc_ok = false;
        return false;
    }
    s_rtc_ok = true;
    set_system(utc);
    if (s_source != Source::Ntp && s_source != Source::Manual) s_source = Source::Rtc;
    ESP_LOGI(TAG, "System time from RTC (%s)", why);
    return true;
}

void on_ntp(timeval* tv) {
    s_last_ntp = tv->tv_sec;
    s_source = Source::Ntp;
    if (s_rtc.write) s_rtc_ok = s_rtc.write(tv->tv_sec, true);
    char buf[32];
    time_t t = tv->tv_sec;
    tm lt{};
    localtime_r(&t, &lt);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &lt);
    ESP_LOGI(TAG, "NTP time %s, RTC %s", buf, s_rtc_ok ? "updated" : "unavailable");
}

bool load_str(const char* key, char* out, size_t len) {
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return false;
    size_t n = len;
    const bool ok = nvs_get_str(h, key, out, &n) == ESP_OK;
    nvs_close(h);
    return ok;
}

esp_err_t save_str(const char* key, const char* v) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNs, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, key, v);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

void apply_tz(const char* tz) {
    const int next = 1 - s_tz_idx.load();
    std::strncpy(s_tz_buf[next], tz, kMaxTz);
    s_tz_buf[next][kMaxTz] = 0;
    s_tz_idx.store(next);
    setenv("TZ", s_tz_buf[next], 1);
    tzset();
}

void start_sntp() {
    if (s_sntp_running) {
        esp_netif_sntp_deinit();
        s_sntp_running = false;
    }
    if (!s_ntp[0]) {
        ESP_LOGI(TAG, "NTP off");
        return;
    }
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(s_ntp);
    cfg.sync_cb = on_ntp;
    cfg.start = true;
    cfg.wait_for_sync = false;
    if (esp_netif_sntp_init(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "SNTP start failed");
        return;
    }
    s_sntp_running = true;
}

struct Guard {
    Guard() { xSemaphoreTake(s_mx, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(s_mx); }
};

void periodic(void*) {
    // Without fresh NTP time the RTC (±2 ppm) is more accurate than the ESP clock.
    const int64_t now = now_utc();
    if (s_last_ntp == 0 || now - s_last_ntp > 2 * 3600) load_from_rtc("hourly resync");
}
}  // namespace

const char* source_str(Source s) {
    switch (s) {
        case Source::Rtc: return "rtc";
        case Source::Ntp: return "ntp";
        case Source::Manual: return "manual";
        default: return "none";
    }
}

void init(const RtcHooks& rtc, const char* default_tz, const char* default_ntp, bool (*tz_valid)(const char*)) {
    s_rtc = rtc;
    s_tz_valid = tz_valid;
    s_mx = xSemaphoreCreateMutex();

    char tz[kMaxTz + 1] = "";
    if (!load_str(kKeyTz, tz, sizeof(tz)) || !timezone_valid(tz)) {
        std::strncpy(tz, default_tz && timezone_valid(default_tz) ? default_tz : "UTC0", kMaxTz);
    }
    apply_tz(tz);
    if (!load_str(kKeyNtp, s_ntp, sizeof(s_ntp)) || !ntp_server_valid(s_ntp)) {
        std::strncpy(s_ntp, default_ntp && ntp_server_valid(default_ntp) ? default_ntp : "", kMaxNtp);
    }

    if (!load_from_rtc("boot")) ESP_LOGW(TAG, "RTC has no valid time - waiting for NTP or manual input");
    start_sntp();

    esp_timer_create_args_t args{};
    args.callback = periodic;
    args.name = "rtc_resync";
    if (esp_timer_create(&args, &s_timer) == ESP_OK) esp_timer_start_periodic(s_timer, kRtcResyncUs);
    ESP_LOGI(TAG, "Time zone %s, NTP %s", timezone(), s_ntp[0] ? s_ntp : "off");
}

Status status() {
    Status s{};
    s.utc = now_utc();
    s.valid = s.utc >= kMinValid;
    s.source = s.valid ? s_source : Source::None;
    s.last_ntp_sync = s_last_ntp;
    s.rtc_ok = s_rtc_ok;
    return s;
}

bool set_manual(int64_t utc) {
    if (utc < kMinValid) return false;
    set_system(utc);
    s_source = Source::Manual;
    if (s_rtc.write) s_rtc_ok = s_rtc.write(utc, false);
    ESP_LOGI(TAG, "Time set manually, RTC %s", s_rtc_ok ? "updated" : "unavailable");
    return true;
}

const char* timezone() { return s_tz_buf[s_tz_idx.load()]; }

const char* ntp_server() { return s_ntp; }

bool timezone_valid(const char* tz) {
    if (!tz || !tz[0] || std::strlen(tz) > size_t(kMaxTz)) return false;
    for (const char* p = tz; *p; ++p)
        if (*p < 0x21 || *p > 0x7e) return false;  // printable ASCII, no spaces
    return s_tz_valid ? s_tz_valid(tz) : true;
}

bool ntp_server_valid(const char* host) {
    if (!host) return false;
    const size_t n = std::strlen(host);
    if (n == 0) return true;  // off
    if (n > size_t(kMaxNtp) || host[0] == '.' || host[0] == '-' || host[n - 1] == '.' || host[n - 1] == '-')
        return false;
    char prev = 0;
    for (const char* p = host; *p; ++p) {
        const char c = *p;
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
        if (!ok || (c == '.' && prev == '.')) return false;
        prev = c;
    }
    return true;
}

esp_err_t set_timezone(const char* tz) {
    if (!timezone_valid(tz)) return ESP_ERR_INVALID_ARG;
    Guard g;
    const esp_err_t err = save_str(kKeyTz, tz);
    if (err != ESP_OK) return err;
    apply_tz(tz);
    ESP_LOGI(TAG, "Time zone set to %s", tz);
    return ESP_OK;
}

esp_err_t set_ntp_server(const char* host) {
    if (!ntp_server_valid(host)) return ESP_ERR_INVALID_ARG;
    Guard g;
    const esp_err_t err = save_str(kKeyNtp, host);
    if (err != ESP_OK) return err;
    if (std::strcmp(host, s_ntp) != 0) {
        if (s_sntp_running) {
            esp_netif_sntp_deinit();  // stop before the name buffer changes
            s_sntp_running = false;
        }
        std::strcpy(s_ntp, host);
        start_sntp();
        ESP_LOGI(TAG, "NTP server set to %s", host[0] ? host : "off");
    }
    return ESP_OK;
}

}  // namespace lgal::timesync
