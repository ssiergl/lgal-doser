#pragma once
// Persistent core settings (NVS namespace "lgal").

#include "esp_err.h"

namespace lgal::settings {

// Initialize NVS (on layout change NVS is reformatted).
esp_err_t init();

bool setup_completed();
esp_err_t set_setup_completed(bool done);

// Language of the web UI (ISO 639-1). Default "en".
// The firmware itself (logs, API messages) is always English; the pages
// translate based on this setting or the selection in the browser.
inline constexpr const char* kLanguages[] = {"en", "de"};
const char* language();
bool language_supported(const char* code);
esp_err_t set_language(const char* code);  // ESP_ERR_INVALID_ARG for an unknown language

// Factory reset: erase NVS completely and the data partition "storage",
// then reboot. Does not return.
[[noreturn]] void factory_reset();

}  // namespace lgal::settings
