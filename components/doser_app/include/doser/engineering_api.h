#pragma once
// Engineering area: system telemetry and resets (M6, functional spec 11.2 D).
// All endpoints need the engineering unlock (lgal::http::authorize_engineering).
//
//   GET  /api/v1/engineering/telemetry
//        {"uptime_s","reset_reason","tasks","chip_temp_c" (null if unavailable),
//         "heap":{"free","min_free","largest_block","total","fragmentation_pct"},
//         "psram":{"free","total"},
//         "nvs":{"used_entries","free_entries","total_entries","namespaces"},
//         "storage":{"total","used","history_days","history_bytes","history_dropped"}}
//   POST /api/v1/engineering/reset?what=network|factory
//        network: LAN back to DHCP, WLAN and its password removed (applied
//                 after 1 s, the device may get a new address) -> {"status":"ok"}
//        factory: everything (NVS, history, EEPROM run state), then reboot
//                 -> {"status":"rebooting"}
//   The history alone is cleared with DELETE /api/v1/history.

#include "esp_err.h"
#include "esp_http_server.h"

namespace doser::engineering {

// Stops all pumps, clears the EEPROM (dosing and schedule run state), then
// erases NVS and the data partition and reboots (lgal::settings::factory_reset).
// Used by the button (10 s) and the API.
[[noreturn]] void factory_reset();

esp_err_t register_handlers(httpd_handle_t server);

}  // namespace doser::engineering
