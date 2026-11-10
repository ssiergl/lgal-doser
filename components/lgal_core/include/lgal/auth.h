#pragma once
// Login, sessions, API keys and engineering unlock (spec 3 + 11.1).
// Logic in auth_core.h; this part stores the password and keys in NVS
// ("auth") and provides the HTTP endpoints. All responses are JSON.
//
//   GET    /login                      login/account page (no login needed)
//   GET    /api/v1/auth/status         login state (no login needed)
//   POST   /api/v1/auth/login          {"password"} -> session cookie. Before an admin
//                                      password exists: the device password (label).
//   POST   /api/v1/auth/logout
//   POST   /api/v1/auth/password       {"current", "new"} (session); signs out all
//                                      other sessions
//   POST   /api/v1/auth/recover        {"device_password", "new"}: only from a client
//                                      of the open setup Wi-Fi (button 3 s)
//   GET    /api/v1/auth/keys           API keys (session)
//   POST   /api/v1/auth/keys           {"name"} -> key, shown only once (session)
//   DELETE /api/v1/auth/keys?slot=1    (session)
//   POST   /api/v1/auth/engineering    {"key"} unlocks the engineering area for this
//                                      session (15 min after the last engineering request)
//   DELETE /api/v1/auth/engineering    lock again
//
// Requests authenticate with the session cookie or an API key (header
// "X-API-Key: lgal_..." or "Authorization: Bearer lgal_..."). With an API key
// the engineering area additionally needs "X-Engineering-Key".

#include <cstdint>

#include "esp_err.h"
#include "esp_http_server.h"
#include "lgal/auth_core.h"
#include "lgal/json.h"
#include "lgal/json_reader.h"

namespace lgal::auth {

esp_err_t init();  // after settings::init and device::init
esp_err_t register_handlers(httpd_handle_t server);

enum class Result { Ok, Unauthorized, EngineeringLocked, Throttled };
// Used by http::authorize / http::authorize_engineering
Result check(httpd_req_t* req, bool engineering);

bool password_set();

// Dangerous actions (factory reset): the request must come from a browser
// session, not an API key, and carry the admin password again (before the
// setup: the device password from the label). Failures count like failed
// logins. false = an error was sent (401 unauthorized, 403 wrong_password,
// 429 too_many_attempts); the handler must then return ESP_OK.
bool confirm_password(httpd_req_t* req, const char* password);

// Engineering backup (spec 11.1): the stored password record and API key
// hashes as hex members "password" ("" = not set) and "keys" into the open
// JSON object. No plain passwords or keys exist on the device.
void write_secrets(Json& j);
// Checks both members (format of this firmware), stores them and signs out
// every session. nullptr = done, otherwise "invalid_param" / "storage_error".
const char* restore_secrets(const JsonValue& v);

// Identity of an authenticated request, kept by long-lived connections
// (WebSocket) and checked again later: logout, a password change or a
// deleted API key end it.
struct Principal {
    char session[kTokenChars + 1] = "";  // session token, or
    int key_slot = -1;                   // API key slot
    uint8_t key_hash[32] = {};
};
bool principal(httpd_req_t* req, Principal* out);  // false = not logged in
bool still_valid(const Principal& p);

}  // namespace lgal::auth
