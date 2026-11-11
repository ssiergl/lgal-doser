#pragma once
// Persistent hardware parameters (NVS namespace "hw"), maintained in the
// engineering menu of the web UI (hwtest_api.h endpoints).

#include "doser/motion.h"
#include "esp_err.h"

namespace doser::hwconfig {

void load(motion::PumpConfig (&pumps)[5]);
esp_err_t save_pump(int pump, const motion::PumpConfig& cfg);

// Channel population: bit i = pump i+1 is fitted (driver, motor). A pump
// that is not fitted is never probed, never doses and is hidden in the UI
// and in Home Assistant. A change takes effect after a restart.
inline constexpr uint8_t kAllFitted = 0x1F;
uint8_t fitted_mask();          // as loaded at startup
bool fitted(int pump);          // pump 0..4
uint8_t stored_fitted_mask();   // saved, in effect after the next restart
// At least one pump; ESP_ERR_INVALID_ARG otherwise
esp_err_t save_fitted(uint8_t mask);

}  // namespace doser::hwconfig
