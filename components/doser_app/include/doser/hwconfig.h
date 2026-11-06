#pragma once
// Persistent hardware parameters (NVS namespace "hw"). In M6 these values
// are maintained via the engineering mode; until then via /hwtest.

#include "doser/motion.h"
#include "esp_err.h"

namespace doser::hwconfig {

void load(motion::PumpConfig (&pumps)[5]);
esp_err_t save_pump(int pump, const motion::PumpConfig& cfg);

}  // namespace doser::hwconfig
