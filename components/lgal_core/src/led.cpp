// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lgal/led.h"

#include <atomic>
#include <cmath>

#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace lgal::led {

namespace {
constexpr const char* TAG = "led";
constexpr ledc_mode_t kMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kTimer = LEDC_TIMER_0;
constexpr ledc_timer_bit_t kRes = LEDC_TIMER_12_BIT;
constexpr uint32_t kMaxDuty = (1u << 12) - 1;
constexpr ledc_channel_t kCh[3] = {LEDC_CHANNEL_0, LEDC_CHANNEL_1, LEDC_CHANNEL_2};
constexpr uint32_t kFrameMs = 20;

struct Rgb {
    float r, g, b;
};

Config s_cfg{};
std::atomic<uint32_t> s_flags{0};
std::atomic<uint8_t> s_idle_brightness{100};
int64_t s_boot_start_us = 0;

// Only pure colours, so that every state stays clear also when the LED channels differ: the
// colour says what it is about, the pattern how urgent it is.
constexpr Rgb kOff{0, 0, 0};
constexpr Rgb kRed{1, 0, 0};
constexpr Rgb kGreen{0, 1, 0};
constexpr Rgb kBlue{0, 0, 1};
constexpr Rgb kMagenta{1, 0, 1};
// The green chip of the light is far brighter than the red one (series resistors red 1 kΩ,
// green/blue 470 Ω): with full green, yellow and white turn green. Set by eye on board d5v1:
// yellow = 15 % green duty, white = 30 % green and blue duty (values before the gamma
// correction: 0.15^(1/2.2) = 0.42, 0.30^(1/2.2) = 0.58). The other colours are right at 100 %.
constexpr Rgb kYellow{1, 0.42f, 0};
constexpr Rgb kWhite{1, 0.58f, 0.58f};

Rgb scale(Rgb c, float f) { return {c.r * f, c.g * f, c.b * f}; }

// 1..0..1 pulse with period in ms: starts bright, so that even a dose of 2 s shows
float pulse(int64_t t_ms, uint32_t period_ms) {
    float phase = static_cast<float>(t_ms % period_ms) / static_cast<float>(period_ms);
    return 0.5f + 0.5f * cosf(phase * 2.0f * static_cast<float>(M_PI));
}

// starts with the light on
bool blink(int64_t t_ms, uint32_t period_ms) { return (t_ms % period_ms) < (period_ms / 2); }

// t_ms: time since the state is shown, so that a pulse or blink starts at its beginning
Rgb render(State s, int64_t t_ms) {
    switch (s) {
        case State::ButtonReset: return kRed;
        case State::ButtonAp: return kWhite;
        case State::Boot: {
            float f = static_cast<float>((esp_timer_get_time() - s_boot_start_us) / 1000) / 1500.0f;
            return scale(kWhite, f > 1.0f ? 1.0f : f);
        }
        case State::Critical: return blink(t_ms, 250) ? kRed : kOff;
        case State::Ota: return blink(t_ms, 500) ? kWhite : kOff;
        case State::Service: return scale(kMagenta, 0.1f + 0.9f * pulse(t_ms, 1500));
        case State::Dosing: return scale(kBlue, 0.1f + 0.9f * pulse(t_ms, 1200));
        case State::SetupAp: return scale(kWhite, 0.1f + 0.9f * pulse(t_ms, 3000));
        case State::Warning: return blink(t_ms, 2000) ? kYellow : kOff;
        case State::IdleLan:
        case State::IdleWlan: return scale(kGreen, s_idle_brightness / 100.0f);
        default: return kOff;
    }
}

uint32_t to_duty(float v, float gain) {
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    // gamma 2.2 for perceptually linear brightness
    float lin = powf(v, 2.2f) * gain;
    return static_cast<uint32_t>(lin * kMaxDuty + 0.5f);
}

void apply(Rgb c) {
    const float gains[3] = {s_cfg.gain_red, s_cfg.gain_green, s_cfg.gain_blue};
    const float vals[3] = {c.r, c.g, c.b};
    for (int i = 0; i < 3; ++i) {
        ledc_set_duty(kMode, kCh[i], to_duty(vals[i], gains[i]));
        ledc_update_duty(kMode, kCh[i]);
    }
}

void task(void*) {
    State shown = State::Count;
    int64_t since_ms = 0;
    while (true) {
        int64_t t_ms = esp_timer_get_time() / 1000;
        uint32_t flags = s_flags.load();
        State s = State::Count;
        for (uint8_t i = 0; i < static_cast<uint8_t>(State::Count); ++i) {
            if (flags & (1u << i)) {
                s = static_cast<State>(i);
                break;
            }
        }
        if (s != shown) {
            shown = s;
            since_ms = t_ms;
        }
        apply(s == State::Count ? kOff : render(s, t_ms - since_ms));
        vTaskDelay(pdMS_TO_TICKS(kFrameMs));
    }
}
}  // namespace

esp_err_t init(const Config& cfg) {
    s_cfg = cfg;
    s_boot_start_us = esp_timer_get_time();

    ledc_timer_config_t tc{};
    tc.speed_mode = kMode;
    tc.duty_resolution = kRes;
    tc.timer_num = kTimer;
    tc.freq_hz = 5000;
    tc.clk_cfg = LEDC_AUTO_CLK;
    if (esp_err_t err = ledc_timer_config(&tc); err != ESP_OK) return err;

    const gpio_num_t pins[3] = {cfg.red, cfg.green, cfg.blue};
    for (int i = 0; i < 3; ++i) {
        ledc_channel_config_t cc{};
        cc.gpio_num = pins[i];
        cc.speed_mode = kMode;
        cc.channel = kCh[i];
        cc.timer_sel = kTimer;
        cc.duty = 0;
        cc.hpoint = 0;
        if (esp_err_t err = ledc_channel_config(&cc); err != ESP_OK) return err;
    }

    set(State::Boot, true);
    if (xTaskCreate(task, "led", 3072, nullptr, 2, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "RGB LED ready");
    return ESP_OK;
}

void set(State s, bool active) {
    uint32_t bit = 1u << static_cast<uint8_t>(s);
    if (active) {
        s_flags.fetch_or(bit);
    } else {
        s_flags.fetch_and(~bit);
    }
}

bool is_set(State s) { return s_flags.load() & (1u << static_cast<uint8_t>(s)); }

void set_idle_brightness(uint8_t percent) { s_idle_brightness = percent > 100 ? 100 : percent; }

uint8_t idle_brightness() { return s_idle_brightness; }

const char* shown_state() {
    static constexpr const char* kNames[] = {"button_reset", "button_ap", "boot",     "critical", "ota",     "service",
                                             "dosing",       "setup_ap",  "warning",  "idle_lan", "idle_wlan"};
    static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<size_t>(State::Count));
    const uint32_t flags = s_flags.load();
    for (uint8_t i = 0; i < static_cast<uint8_t>(State::Count); ++i)
        if (flags & (1u << i)) return kNames[i];
    return "off";
}

}  // namespace lgal::led
