#pragma once
// Persistent core settings (NVS namespace "lgal").

#include "esp_err.h"

namespace lgal::settings {

// Initialize NVS (on layout change NVS is reformatted).
esp_err_t init();

bool setup_completed();
esp_err_t set_setup_completed(bool done);

// Factory reset: erase NVS completely and the data partition "storage",
// then reboot. Does not return.
[[noreturn]] void factory_reset();

}  // namespace lgal::settings
