// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace doser {

// Puts all pump-related outputs into a safe state.
// Must be called first thing in app_main():
//  - EN (GPIO21) driven HIGH -> all drivers without current
//  - STEP pins as output LOW
//  - UART pins stay high-impedance
void safe_init();

// Board without 24 V (doser/supply.h): releases EN (input, no pull). Driven
// HIGH it would back-feed the unpowered 3.3 V rail of the drivers; the
// external pull-up holds them off as soon as the rail has power again.
void release_driver_enable();

// Control driver EN (true = drivers on). Only for use by the dosing engine.
void set_drivers_enabled(bool enabled);
bool drivers_enabled();

}  // namespace doser
