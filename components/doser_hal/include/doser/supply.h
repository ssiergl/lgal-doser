#pragma once
// 24 V sense input (doser/pins.h kSupplySense, board d5v1 from v1.1 on):
// ADC oneshot, 12 dB attenuation, calibrated (curve fitting), 16 samples per
// reading, times the divider ratio. States and thresholds: doser/supply_core.h.
// On a board revision without the input the pin is never touched and the
// state stays None.

#include <cstdint>

#include "doser/supply_core.h"

namespace doser::supply {

// Starts the input for this board revision (hwconfig::board_rev()) and takes
// the boot measurement; false = no sense input (state None).
bool init(uint8_t board_rev);
bool present();
// New reading, from one task only (main loop); true = the debounced state changed
bool poll();
State state();       // any task
State boot_state();  // state of the boot measurement
uint32_t millivolts();  // last reading, 0 without sense input
bool returned();        // Tracker::returned()

}  // namespace doser::supply
