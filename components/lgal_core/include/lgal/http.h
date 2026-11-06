#pragma once
// HTTP server (port 80) and shared helpers for all API handlers.
//
// Authentication M0 (preliminary): HTTP Basic, user "admin", password =
// the device's setup password. Replaced by session login + API key in M4.

#include <cstddef>
#include <cstdint>
#include <string>

#include "esp_err.h"
#include "esp_http_server.h"
#include "lgal/json_reader.h"

namespace lgal::http {

esp_err_t start();
httpd_handle_t handle();

// Checks the login. On failure 401 is sent and false is returned;
// the handler must then return ESP_OK immediately.
bool authorize(httpd_req_t* req);

esp_err_t send_json(httpd_req_t* req, const char* json, const char* status = "200 OK");
// Error response {"error":{"code":"...","message":"..."}}.
// code: fixed, machine-readable identifier (snake_case) – the web UI
// translates based on the code. message: English plain text for logs/developers.
esp_err_t send_error(httpd_req_t* req, const char* status, const char* code, const char* message);

// Query parameter (?key=value, URL-decoded). false if missing,
// invalid or longer than the buffer.
bool query_str(httpd_req_t* req, const char* key, char* out, size_t len);
bool query_int(httpd_req_t* req, const char* key, int64_t* out);
// Decimal number ("1.25") times scale, rounded – e.g. ml -> µl with scale 1000
bool query_decimal(httpd_req_t* req, const char* key, int64_t scale, int64_t* out);

// Request body as JSON (Content-Type is not checked). On failure an error is
// sent (400 body_missing / invalid_json, 413 body_too_large, 408 timeout) and
// false is returned; the handler must then return ESP_OK immediately.
// `body` holds the text the reader points into and must outlive it.
bool read_json(httpd_req_t* req, size_t max_len, std::string* body, JsonReader* doc, JsonToken* tokens,
               int max_tokens);

// Registers a handler (errors are logged).
void route(httpd_handle_t server, const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t*));

}  // namespace lgal::http
