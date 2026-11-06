#pragma once
// HTTP server (port 80) and shared helpers for all API handlers.
//
// Authentication M0 (preliminary): HTTP Basic, user "admin", password =
// the device's setup password. Replaced by session login + API key in M4.

#include "esp_err.h"
#include "esp_http_server.h"

namespace lgal::http {

esp_err_t start();
httpd_handle_t handle();

// Checks the login. On failure 401 is sent and false is returned;
// the handler must then return ESP_OK immediately.
bool authorize(httpd_req_t* req);

esp_err_t send_json(httpd_req_t* req, const char* json, const char* status = "200 OK");
esp_err_t send_error(httpd_req_t* req, const char* status, const char* message);

}  // namespace lgal::http
