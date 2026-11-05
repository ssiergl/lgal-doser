#pragma once
// Button reset/setup (GPIO0, active low).
//   hold >= 3 s and release   -> setup AP (temporary)
//   hold >= 10 s and release  -> factory reset
// The action is only triggered on release; while holding, the LED
// shows which threshold has been reached. A press only counts after
// the button has been released once since startup (protection against a stuck
// button against reset loops).

#include <functional>
#include "driver/gpio.h"
#include "esp_err.h"

namespace lgal::button {

struct Config {
    gpio_num_t gpio;
    uint32_t ap_hold_ms = 3000;
    uint32_t reset_hold_ms = 10000;
    std::function<void()> on_setup_ap;
    std::function<void()> on_factory_reset;
};

esp_err_t init(const Config& cfg);

}  // namespace lgal::button
