#pragma once
// Firmware OTA via HTTP upload with image check and rollback protection.
//
//   POST /api/v1/ota/firmware   body = raw .bin (application/octet-stream)
//
// Flow: ask guard (e.g. "no dosing active") -> lock pumps ->
// check header (chip, project name) -> write to inactive slot ->
// verify image -> switch boot partition -> reboot.
// The new image starts as PENDING_VERIFY and is only marked valid after
// CONFIG_LGAL_OTA_CONFIRM_DELAY_S of stable runtime + health check.
// A crash or watchdog before that -> the bootloader rolls back.

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

esp_err_t register_handlers(httpd_handle_t server);

// Starts verification of a freshly flashed image (no-op otherwise).
void start_rollback_guard(std::function<bool()> health_check);

}  // namespace lgal::ota
