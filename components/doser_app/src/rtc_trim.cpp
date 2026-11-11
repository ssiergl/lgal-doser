#include "doser/rtc_trim.h"

#include <sys/time.h>

#include "doser/i2c_devices.h"
#include "doser/nvs_blob.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace doser::rtc_trim {

namespace {
constexpr const char* TAG = "rtc_trim";
constexpr const char* kNs = "hw";
constexpr const char* kKey = "rtctrim";
constexpr uint16_t kVersion = 1;
constexpr uint32_t kNtp = 1, kManual = 2;

TaskHandle_t s_task = nullptr;
SemaphoreHandle_t s_mx = nullptr;
State s_state;
int32_t s_offset = 0;
int64_t s_measured_at = 0;
bool s_battery = false;  // fixed at boot

struct Lock {
    Lock() { xSemaphoreTake(s_mx, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(s_mx); }
};

int64_t now_utc() {
    timeval tv{};
    gettimeofday(&tv, nullptr);
    return tv.tv_sec;
}

void save(const State& s) {
    if (nvs_blob::save(kNs, kKey, kVersion, s) != ESP_OK) ESP_LOGW(TAG, "state not saved");
}

bool measure(int32_t* off) {
    if (rtc::edge_offset(off) != ESP_OK) return false;
    Lock l;
    s_offset = *off;
    s_measured_at = now_utc();
    return true;
}

void handle_ntp() {
    int32_t off = 0;
    int8_t aging = 0;
    if (!measure(&off) || rtc::aging(&aging) != ESP_OK) {
        // no second edge: oscillator stopped -> just set it
        if (rtc::write_aligned() == ESP_OK) {
            Lock l;
            s_state.ever_set = 1;
            s_state.anchored = 0;
            save(s_state);
        }
        return;
    }
    State s;
    {
        Lock l;
        s = s_state;
    }
    const int64_t now = now_utc();
    const Decision d = decide(s, now, off, aging);
    if (d.trim && d.aging != aging) {
        if (rtc::set_aging(d.aging) != ESP_OK) return;
        ESP_LOGI(TAG, "drift %+.2f ppm -> aging %d -> %d", static_cast<double>(d.ppm), aging, d.aging);
    }
    if (d.set_rtc) {
        if (rtc::write_aligned() != ESP_OK) return;
        ESP_LOGI(TAG, "RTC set (was %+ld ms off)", static_cast<long>(off));
        if (!measure(&off)) return;
    }
    if (!d.set_rtc && !d.anchor && !d.trim) return;
    apply(&s, d, now, off, aging);
    Lock l;
    s.automatic = s_state.automatic;  // may have changed meanwhile
    s_state = s;
    save(s_state);
}

void handle_manual() {
    if (rtc::write_aligned() != ESP_OK) return;
    Lock l;
    s_state.ever_set = 1;
    s_state.anchored = 0;  // a hand-set system clock is no reference
    save(s_state);
}

void task(void*) {
    for (;;) {
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, portMAX_DELAY);
        if (!rtc::present()) continue;
        if (bits & kManual) handle_manual();
        else if (bits & kNtp) handle_ntp();
    }
}
}  // namespace

void init() {
    s_mx = xSemaphoreCreateMutex();
    nvs_blob::load(kNs, kKey, kVersion, &s_state);
    // a brand-new module also reports a stopped oscillator: only warn when
    // the firmware had set the clock before
    s_battery = rtc::lost_at_boot() && s_state.ever_set;
    if (s_battery) ESP_LOGW(TAG, "RTC lost its time while off - check the backup battery");
    if (xTaskCreate(task, "rtc_trim", 3072, nullptr, 2, &s_task) != pdPASS) ESP_LOGE(TAG, "task not started");
}

void on_ntp() {
    if (s_task) xTaskNotify(s_task, kNtp, eSetBits);
}

void on_manual() {
    if (s_task) xTaskNotify(s_task, kManual, eSetBits);
}

bool battery_warning() { return s_battery; }

Status status() {
    Status st{};
    if (!s_mx) return st;
    Lock l;
    st.state = s_state;
    st.offset_ms = s_offset;
    st.measured_at = s_measured_at;
    return st;
}

esp_err_t set_automatic(bool on) {
    Lock l;
    s_state.automatic = on ? 1 : 0;
    s_state.anchored = 0;  // a new measurement starts with the next NTP sync
    return nvs_blob::save(kNs, kKey, kVersion, s_state);
}

}  // namespace doser::rtc_trim
