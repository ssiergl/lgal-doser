#include "lgal/timesync.h"

#include <sys/time.h>

#include <cstdlib>
#include <ctime>
#include <string>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"

namespace lgal::timesync {

namespace {
constexpr const char* TAG = "time";
constexpr int64_t kMinValid = 1735689600;  // 2025-01-01: everything before is "no time"
constexpr int64_t kRtcResyncUs = 3600LL * 1000000;

RtcHooks s_rtc;
std::string s_tz;
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
    if (s_rtc.write) s_rtc_ok = s_rtc.write(tv->tv_sec);
    char buf[32];
    time_t t = tv->tv_sec;
    tm lt{};
    localtime_r(&t, &lt);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &lt);
    ESP_LOGI(TAG, "NTP time %s, RTC %s", buf, s_rtc_ok ? "updated" : "unavailable");
}

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

void init(const RtcHooks& rtc, const char* tz_posix, const char* ntp_server) {
    s_rtc = rtc;
    s_tz = tz_posix ? tz_posix : "UTC0";
    setenv("TZ", s_tz.c_str(), 1);
    tzset();

    if (!load_from_rtc("boot")) ESP_LOGW(TAG, "RTC has no valid time - waiting for NTP or manual input");

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(ntp_server);
    cfg.sync_cb = on_ntp;
    cfg.start = true;
    cfg.wait_for_sync = false;
    if (esp_netif_sntp_init(&cfg) != ESP_OK) ESP_LOGE(TAG, "SNTP start failed");

    esp_timer_create_args_t args{};
    args.callback = periodic;
    args.name = "rtc_resync";
    if (esp_timer_create(&args, &s_timer) == ESP_OK) esp_timer_start_periodic(s_timer, kRtcResyncUs);
    ESP_LOGI(TAG, "Time zone %s, NTP %s", s_tz.c_str(), ntp_server);
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
    if (s_rtc.write) s_rtc_ok = s_rtc.write(utc);
    ESP_LOGI(TAG, "Time set manually, RTC %s", s_rtc_ok ? "updated" : "unavailable");
    return true;
}

const char* timezone() { return s_tz.c_str(); }

}  // namespace lgal::timesync
