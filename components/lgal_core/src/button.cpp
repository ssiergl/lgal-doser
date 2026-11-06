#include "lgal/button.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lgal/led.h"

namespace lgal::button {

namespace {
constexpr const char* TAG = "button";
constexpr uint32_t kPollMs = 20;
constexpr uint32_t kDebounceMs = 60;

Config s_cfg;

bool pressed() { return gpio_get_level(s_cfg.gpio) == 0; }

void task(void*) {
    bool armed = false;  // only active after the first release
    bool down = false;
    uint32_t stable_ms = 0;
    uint32_t held_ms = 0;
    bool raw_prev = pressed();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(kPollMs));
        bool raw = pressed();
        if (raw != raw_prev) {
            raw_prev = raw;
            stable_ms = 0;
            continue;
        }
        stable_ms += kPollMs;
        if (stable_ms < kDebounceMs) continue;

        if (!armed) {
            if (!raw) {
                armed = true;
                ESP_LOGI(TAG, "Button ready");
            }
            continue;
        }

        if (raw && !down) {
            down = true;
            held_ms = 0;
        } else if (raw && down) {
            held_ms += kPollMs;
            led::set(led::State::ButtonAp, held_ms >= s_cfg.ap_hold_ms && held_ms < s_cfg.reset_hold_ms);
            led::set(led::State::ButtonReset, held_ms >= s_cfg.reset_hold_ms);
        } else if (!raw && down) {
            down = false;
            led::set(led::State::ButtonAp, false);
            led::set(led::State::ButtonReset, false);
            if (held_ms >= s_cfg.reset_hold_ms) {
                ESP_LOGW(TAG, "Button %u ms: factory reset", (unsigned)held_ms);
                if (s_cfg.on_factory_reset) s_cfg.on_factory_reset();
            } else if (held_ms >= s_cfg.ap_hold_ms) {
                ESP_LOGI(TAG, "Button %u ms: setup AP", (unsigned)held_ms);
                if (s_cfg.on_setup_ap) s_cfg.on_setup_ap();
            }
        }
    }
}
}  // namespace

esp_err_t init(const Config& cfg) {
    s_cfg = cfg;
    gpio_config_t io{};
    io.pin_bit_mask = 1ULL << cfg.gpio;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    if (esp_err_t err = gpio_config(&io); err != ESP_OK) return err;
    if (xTaskCreate(task, "button", 4096, nullptr, 3, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

}  // namespace lgal::button
