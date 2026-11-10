#pragma once
// Firmware OTA by HTTP upload or from a URL, with image check and rollback
// protection.
//
//   POST /api/v1/ota/firmware   body = raw .bin (application/octet-stream)
//   POST /api/v1/ota/url        {"url":"https://…/firmware.bin"} -> 202 {"status":"started"};
//                               the device downloads the image itself
//   GET  /api/v1/ota/status     {"state":"idle|running|failed|rebooting","source":"api|mqtt",
//                               "http","received","total" (null = unknown),"error"?,"detail"?}
//   GET  /api/v1/ota/settings   {"allow_http","mqtt_update"}
//   PUT  /api/v1/ota/settings   {"allow_http"?,"mqtt_update"?}
//   MQTT "<base>/ota/set"       URL as payload (only with mqtt_update); progress
//                               and result on "<base>/ota/state" (not retained)
//
// URLs: https with the server certificate checked against the CA bundle.
// Plain http only with allow_http (off from the factory, the WebUI warns);
// a redirect from https to http is refused unless http is allowed. Up to
// 5 redirects. Errors: invalid_url, http_not_allowed, download_failed,
// http_status, too_many_redirects, invalid_size and those of the upload.
//
// Flow: ask guard (e.g. "no dosing active") -> lock pumps ->
// check header (chip, project name) -> write to inactive slot ->
// verify image -> switch boot partition -> reboot.
// The new image starts as PENDING_VERIFY and is only marked valid after
// CONFIG_LGAL_OTA_CONFIRM_DELAY_S of stable runtime + health check.
// A crash or watchdog before that -> the bootloader rolls back.

#include <cstddef>
#include <functional>
#include <string>
#include "esp_err.h"
#include "esp_http_server.h"

namespace lgal::ota {

// Returns false (+ error code, e.g. "pump_running") if no update is allowed right now.
using Guard = std::function<bool(std::string& reason)>;
// Called with true before flashing and with false after an abort
// (lock/unlock pumps).
using LockHook = std::function<void(bool locked)>;

void set_guard(Guard guard);
void set_lock_hook(LockHook hook);
bool in_progress();

constexpr size_t kMaxUrl = 512;
struct Settings {
    bool allow_http = false;   // plain http for URL updates
    bool mqtt_update = false;  // "<base>/ota/set" accepted
};
Settings settings();

enum class State : uint8_t { Idle, Running, Failed, Rebooting };
const char* state_name(State s);

// Starts an update from url in the background (source "api" or "mqtt").
// nullptr = started; otherwise "invalid_url", "http_not_allowed",
// "ota_in_progress", "no_ota_partition", "no_memory" or "guard" (then
// *reason holds the guard's code, e.g. "pump_running").
const char* start_url(const char* url, const char* source, std::string* reason = nullptr);

esp_err_t register_handlers(httpd_handle_t server);

// Starts verification of a freshly flashed image (no-op otherwise).
void start_rollback_guard(std::function<bool()> health_check);

}  // namespace lgal::ota
