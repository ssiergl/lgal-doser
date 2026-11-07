#pragma once
// RGB status LED with a fixed priority hierarchy (functional spec chapter 10).
// Each state is a flag; the active flag with the highest priority
// (smallest enum value) is always shown.

#include <cstdint>
#include "esp_err.h"
#include "driver/gpio.h"
#include "lgal/led_night.h"

namespace lgal::led {

enum class State : uint8_t {
    ButtonReset = 0,  // button held >= 10 s: steady red
    ButtonAp,         // button held >= 3 s: steady orange
    Boot,             // white, fade in
    Critical,         // prio 1: red, fast blinking
    Ota,              // prio 2: magenta/white alternating
    Service,          // prio 3: violet pulsing (calibration/priming)
    Dosing,           // prio 4: cyan pulsing
    SetupAp,          // prio 5: orange pulsing
    Warning,          // prio 6: yellow, slow blinking
    IdleLan,          // prio 7: green
    IdleWlan,         // prio 7: turquoise
    Count
};

struct Config {
    gpio_num_t red;
    gpio_num_t green;
    gpio_num_t blue;
    // Channel balance (0..1), compensates different series resistors/LED
    // efficiencies so that mixed colors are right.
    float gain_red = 1.0f;
    float gain_green = 1.0f;
    float gain_blue = 1.0f;
};

esp_err_t init(const Config& cfg);
void set(State s, bool active);
bool is_set(State s);

// Night mode: brightness of the idle indication (prio 7) in percent, 0 = off.
void set_idle_brightness(uint8_t percent);
uint8_t idle_brightness();

// Name of the state shown right now ("critical", "warning", "idle_lan", ...,
// "off" without any active state) for the API
const char* shown_state();

}  // namespace lgal::led
