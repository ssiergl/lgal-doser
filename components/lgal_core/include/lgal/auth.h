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

namespace lgal::auth {

esp_err_t init();  // after settings::init and device::init
esp_err_t register_handlers(httpd_handle_t server);

enum class Result { Ok, Unauthorized, EngineeringLocked, Throttled };
// Used by http::authorize / http::authorize_engineering
Result check(httpd_req_t* req, bool engineering);

bool password_set();

}  // namespace lgal::auth
