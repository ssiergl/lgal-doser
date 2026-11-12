#pragma once
// Firmware OTA by HTTP upload, from the fixed update source or from a URL,
// with image check and rollback protection.
//
//   POST /api/v1/ota/firmware   body = raw .bin (application/octet-stream)
//   GET  /api/v1/ota/release    last check of the update source:
//                               {"source","channel","board","variant","current","auto_check","checking",
//                                "checked_at"(null = not yet),"latest"(null = unknown),
//                                "available","notes"?,"error"?,"detail"?}
//   POST /api/v1/ota/release/check    fetch the manifest now -> 202 {"status":"started"}
//   POST /api/v1/ota/release/install  install the newer release of the source -> 202
//   POST /api/v1/ota/url        {"url":"https://…/firmware.bin"} -> 202 {"status":"started"};
//                               the device downloads the image itself (engineering)
//   GET  /api/v1/ota/status     {"state":"idle|running|failed|rebooting","source":"api|mqtt",
//                               "http","received","total" (null = unknown),"error"?,"detail"?}
//   GET  /api/v1/ota/settings   {"source","channel","auto_check","allow_http"}
//   PUT  /api/v1/ota/settings   {"auto_check"?,"channel"?}; "source"? and "allow_http"? need
//                               engineering
//   MQTT "<base>/ota/state"     progress and result (not retained)
//
// Update source: the address of the release manifest (ota_manifest.h),
// factory setting CONFIG_LGAL_OTA_SOURCE. With auto_check the device fetches
// it shortly after the start and then daily; it only reports a newer
// version, installing always needs a request.
// Channel: while the source is the factory one, the admin may choose
// "beta" (CONFIG_LGAL_OTA_SOURCE_BETA: newest release candidate or
// release) instead of "stable"; "channel" is null when there is no choice
// (own source from the engineering menu, or no beta address). Back to
// stable never installs an older version: the device stays on the release
// candidate until a newer release is out. The firmware file of the own
// board and variant must match the SHA-256 of the manifest before it is
// activated.
//
// URLs: https with the server certificate checked against the CA bundle.
// Plain http only with allow_http (off from the factory, the WebUI warns);
// a redirect from https to http is refused unless http is allowed. Up to
// 5 redirects. Errors: invalid_url, http_not_allowed, download_failed,
// http_status, too_many_redirects, invalid_size, sha256_mismatch, those of
// the manifest (manifest_invalid, no_build, …) and those of the upload.
//
// Flow: ask guard (e.g. "no dosing active") -> lock pumps ->
// check header (chip, flash size, project name, board) -> write to inactive slot ->
// verify image -> switch boot partition -> reboot.
// The new image starts as PENDING_VERIFY and is only marked valid after
// CONFIG_LGAL_OTA_CONFIRM_DELAY_S of stable runtime + health check.
// A crash or watchdog before that -> the bootloader rolls back.

#include <cstddef>
#include <cstdint>
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
    std::string source;       // release manifest, "" = no update source
    bool beta = false;        // beta channel instead of source (see channel())
    bool auto_check = true;   // fetch it daily (only reports)
    bool allow_http = false;  // plain http for updates
};
Settings settings();
// "stable" or "beta" while the channel can be chosen, otherwise nullptr
const char* channel();

// Last check of the update source
struct ReleaseInfo {
    bool checking = false;
    bool checked = false;       // since the start
    int64_t checked_at = 0;     // UTC, 0 = unknown (clock not set)
    std::string latest;         // version of the source, "" = unknown
    bool available = false;     // newer than the running firmware
    std::string notes;          // release page, "" = none
    const char* error = nullptr;
};
ReleaseInfo release_info();
const char* running_version();
// Called after every check and on every change of the update state (any
// task): e.g. to republish the Home Assistant update entity.
void set_change_hook(std::function<void()> hook);
// Installs the newer release of the update source in the background
// (source e.g. "api"). nullptr = started; otherwise "no_source",
// "ota_in_progress", "no_ota_partition", "no_memory" or "guard" (*reason).
const char* start_release_install(const char* source, std::string* reason = nullptr);

enum class State : uint8_t { Idle, Running, Failed, Rebooting };
const char* state_name(State s);
// Download progress (as GET /api/v1/ota/status)
struct Status {
    State state = State::Idle;
    uint32_t received = 0;
    int32_t total = -1;  // -1 = unknown
};
Status status();

// Starts an update from url in the background (source e.g. "api").
// nullptr = started; otherwise "invalid_url", "http_not_allowed",
// "ota_in_progress", "no_ota_partition", "no_memory" or "guard" (then
// *reason holds the guard's code, e.g. "pump_running").
const char* start_url(const char* url, const char* source, std::string* reason = nullptr);

esp_err_t register_handlers(httpd_handle_t server);

// Starts verification of a freshly flashed image (no-op otherwise).
void start_rollback_guard(std::function<bool()> health_check);

}  // namespace lgal::ota
