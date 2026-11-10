#pragma once
// Persistent hardware parameters (NVS namespace "hw"), maintained in the
// engineering menu of the web UI (hwtest_api.h endpoints).

#include "doser/motion.h"
#include "esp_err.h"

namespace doser::hwconfig {

void load(motion::PumpConfig (&pumps)[5]);
esp_err_t save_pump(int pump, const motion::PumpConfig& cfg);

}  // namespace doser::hwconfig
