#pragma once
// First setup (spec 2): wizard page and completion. While setup_completed
// is false the setup AP stays on and "/" leads to /setup.
//
//   GET  /setup                   wizard (language, admin password, network, time)
//   GET  /api/v1/setup            {"completed","password_set","connected","hostname","urls"}
//   POST /api/v1/setup/complete   needs an admin password and a working LAN/Wi-Fi
//                                 connection (409 password_not_set / not_connected);
//                                 sets setup_completed and switches the setup AP off
//                                 after kApOffDelayS
//
// The steps themselves use the existing endpoints (system/language,
// auth/password, net/*, system/time).

#include "esp_err.h"
#include "esp_http_server.h"

namespace lgal::setup {

inline constexpr int kApOffDelayS = 20;

esp_err_t register_handlers(httpd_handle_t server);

}  // namespace lgal::setup
