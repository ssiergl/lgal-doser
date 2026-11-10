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

}  // namespace doser::mqtt_ha
