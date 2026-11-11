#pragma once
// Changes made in the core (network, MQTT, device settings, clock, password,
// API keys) for the application's event history. The core does not know the
// history; the application installs a hook. It runs in the caller's task.

#include "esp_http_server.h"

namespace lgal::events {

enum class By : uint8_t { Device, Browser, ApiKey };

// code: "network", "mqtt", "settings", "time_set", "password", "key_added", "key_removed"
using Hook = void (*)(const char* code, By by);
void set_hook(Hook h);
void note(const char* code, By by);
// Source from the request's login (session or API key). Not with the auth lock held.
void note(const char* code, httpd_req_t* req);

}  // namespace lgal::events
