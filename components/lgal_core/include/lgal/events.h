// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
// Changes made in the core (network, MQTT, device settings, clock, password,
// API keys) for the application's event history. The core does not know the
// history; the application installs a hook. It runs in the caller's task.

#include "esp_http_server.h"

namespace lgal::events {

enum class By : uint8_t { Device, Browser, ApiKey };

// What a "settings" event changed (its value; 0 = not recorded)
enum Area : int32_t { kLanguage = 1, kTime = 2, kLed = 3, kName = 4 };

// code: "network", "mqtt", "settings", "time_set", "password", "key_added", "key_removed"
using Hook = void (*)(const char* code, By by, int32_t value);
void set_hook(Hook h);
void note(const char* code, By by, int32_t value = 0);
// Source from the request's login (session or API key). Not with the auth lock held.
void note(const char* code, httpd_req_t* req, int32_t value = 0);

}  // namespace lgal::events
