// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

#include "doser/supply.h"

#include <atomic>

#include "doser/pins.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

namespace doser::supply {

namespace {
constexpr const char* TAG = "supply";
constexpr int kSamples = 16;

adc_oneshot_unit_handle_t s_unit = nullptr;
adc_cali_handle_t s_cali = nullptr;
adc_channel_t s_channel{};
Tracker s_tracker;  // init() and poll() only
State s_boot = State::None;
std::atomic<State> s_state{State::None};
std::atomic<uint32_t> s_mv{0};

// Supply voltage in mV, mean of kSamples; 0 when no sample could be read
uint32_t measure() {
    int sum = 0, n = 0;
    for (int i = 0; i < kSamples; ++i) {
        int raw = 0, mv = 0;
        if (adc_oneshot_read(s_unit, s_channel, &raw) != ESP_OK) continue;
        if (adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) continue;
        sum += mv;
        ++n;
    }
    return n ? static_cast<uint32_t>(sum / n) * pins::kSupplyDivider : 0;
}

esp_err_t start_adc() {
    adc_unit_t unit{};
    esp_err_t err = adc_oneshot_io_to_channel(pins::kSupplySense, &unit, &s_channel);
    if (err != ESP_OK) return err;
    adc_oneshot_unit_init_cfg_t ucfg{};
    ucfg.unit_id = unit;
    ucfg.ulp_mode = ADC_ULP_MODE_DISABLE;
    err = adc_oneshot_new_unit(&ucfg, &s_unit);
    if (err != ESP_OK) return err;
    adc_oneshot_chan_cfg_t ccfg{};
    ccfg.atten = ADC_ATTEN_DB_12;  // up to about 3.1 V at the pin = 34 V
    ccfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    err = adc_oneshot_config_channel(s_unit, s_channel, &ccfg);
    if (err == ESP_OK) {
        adc_cali_curve_fitting_config_t cal{};
        cal.unit_id = unit;
        cal.chan = s_channel;
        cal.atten = ADC_ATTEN_DB_12;
        cal.bitwidth = ADC_BITWIDTH_DEFAULT;
        err = adc_cali_create_scheme_curve_fitting(&cal, &s_cali);
    }
    if (err != ESP_OK) {
        adc_oneshot_del_unit(s_unit);
        s_unit = nullptr;
    }
    return err;
}
}  // namespace

bool init(uint8_t board_rev) {
    if (pins::kSupplySense == GPIO_NUM_NC || board_rev < pins::kSupplySenseRev) return false;
    const esp_err_t err = start_adc();
    if (err != ESP_OK) {
        // without a reading the firmware starts as on a board without the input
        ESP_LOGE(TAG, "24 V sense (GPIO%d) unavailable: %s", pins::kSupplySense, esp_err_to_name(err));
        return false;
    }
    const uint32_t mv = measure();
    s_boot = classify(mv);
    s_tracker = Tracker(s_boot);
    s_mv = mv;
    s_state = s_boot;
    ESP_LOGI(TAG, "24 V supply: %.1f V (%s)", mv / 1000.0, state_name(s_boot));
    return true;
}

bool present() { return s_unit != nullptr; }

bool poll() {
    if (!s_unit) return false;
    const uint32_t mv = measure();
    s_mv = mv;
    if (!s_tracker.update(mv)) return false;
    s_state = s_tracker.state();
    return true;
}

State state() { return s_state; }
State boot_state() { return s_boot; }
uint32_t millivolts() { return s_mv; }
bool returned() { return s_tracker.returned(); }

}  // namespace doser::supply
