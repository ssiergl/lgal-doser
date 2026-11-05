#include "doser/safe_init.h"

#include <atomic>

#include "doser/pins.h"
#include "driver/gpio.h"
#include "esp_log.h"

namespace doser {

namespace {
constexpr const char* TAG = "doser_hal";
std::atomic<bool> s_enabled{false};
}  // namespace

void safe_init() {
    // Set the level first, then switch to output -> no glitch on EN
    gpio_set_level(pins::kDriverEnable, 1);
    gpio_config_t en{};
    en.pin_bit_mask = 1ULL << pins::kDriverEnable;
    en.mode = GPIO_MODE_OUTPUT;
    en.pull_up_en = GPIO_PULLUP_DISABLE;  // external 10k pull-up present
    en.pull_down_en = GPIO_PULLDOWN_DISABLE;
    en.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&en);
    gpio_set_level(pins::kDriverEnable, 1);
    s_enabled = false;

    uint64_t step_mask = 0;
    for (auto pin : pins::kStep) {
        gpio_set_level(pin, 0);
        step_mask |= 1ULL << pin;
    }
    gpio_config_t step{};
    step.pin_bit_mask = step_mask;
    step.mode = GPIO_MODE_OUTPUT;
    step.pull_up_en = GPIO_PULLUP_DISABLE;
    step.pull_down_en = GPIO_PULLDOWN_DISABLE;
    step.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&step);
    for (auto pin : pins::kStep) gpio_set_level(pin, 0);

    ESP_LOGI(TAG, "Treiber deaktiviert (EN=HIGH), STEP-Pins LOW");
}

void set_drivers_enabled(bool enabled) {
    gpio_set_level(pins::kDriverEnable, enabled ? 0 : 1);
    s_enabled = enabled;
}

bool drivers_enabled() { return s_enabled; }

}  // namespace doser
