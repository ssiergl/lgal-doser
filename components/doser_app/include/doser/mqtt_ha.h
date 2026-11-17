// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
// MQTT publishing of the pump states, Home Assistant discovery and commands
// (M6). Topics and payloads: doser/ha_core.h; connection: lgal/mqtt.h.
// "<base>/event": every history event once, oldest first, with "replay"
// for events buffered before the current connection (doser/event_cursor.h).

#include <cstdint>

namespace doser::mqtt_ha {

// Registers the MQTT callbacks and starts the publishing task. Call before
// lgal::mqtt::init(), after dosing and scheduler.
void init();

// Alarm state for "<base>/state" (main.cpp, from the status LED evaluation)
void set_alarms(bool critical, bool warning, uint16_t conditions);
// 24 V supply in V for "<base>/state" (board with sense input); sent again
// from a change of 0.2 V on
void set_supply(float volts);

// Update check or progress changed: "<base>/update" is sent soon (any task)
void update_changed();

}  // namespace doser::mqtt_ha
