#pragma once

namespace doser {

// Puts all pump-related outputs into a safe state.
// Must be called first thing in app_main():
//  - EN (GPIO21) driven HIGH -> all drivers without current
//  - STEP pins as output LOW
//  - UART pins stay high-impedance
void safe_init();

// Control driver EN (true = drivers on). Only for use by the dosing engine.
void set_drivers_enabled(bool enabled);
bool drivers_enabled();

}  // namespace doser
