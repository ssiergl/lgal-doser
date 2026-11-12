#pragma once
// HTTP server (port 80) and shared helpers for all API handlers.
//
// Authentication (see auth.h): session cookie or API key. Pages without
// login redirect to /login, API requests get 401 JSON.

#include <cstddef>
#include <cstdint>
#include <string>

#include "esp_err.h"
#include "esp_http_server.h"
#include "lgal/json_reader.h"

namespace lgal::http {

esp_err_t start();
httpd_handle_t handle();

// Cross-site request forgery: a changing request (not GET/HEAD) that a
// browser sends from a page outside this device (Origin header of another
// host) gets 403 "forbidden_origin" and false; the handler must then return
// ESP_OK immediately. Clients without Origin (apps, tools) pass. authorize()
// includes it; the login endpoints, which check the session themselves,
// call it directly.
bool same_origin(httpd_req_t* req);

// Checks the login (and same_origin). On failure 401 (or a redirect to /login for pages) is
// sent and false is returned; the handler must then return ESP_OK immediately.
bool authorize(httpd_req_t* req);
// Same for the engineering area (spec 11.1): 403 "engineering_locked" if the
// session has not unlocked it.
bool authorize_engineering(httpd_req_t* req);
// Client IPv4 address (network byte order), 0 if unknown
uint32_t client_ipv4(httpd_req_t* req);

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

// Raw request body of at most max_len bytes. On failure an error is sent
// (400 body_missing, 413 body_too_large, 408 timeout) and false is returned.
bool read_body(httpd_req_t* req, size_t max_len, std::string* body);

// Request body as JSON (Content-Type is not checked). On failure an error is
// sent (400 body_missing / invalid_json, 413 body_too_large, 408 timeout) and
// false is returned; the handler must then return ESP_OK immediately.
// `body` holds the text the reader points into and must outlive it.
bool read_json(httpd_req_t* req, size_t max_len, std::string* body, JsonReader* doc, JsonToken* tokens,
               int max_tokens);

// Registers a handler (errors are logged).
void route(httpd_handle_t server, const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t*));

}  // namespace lgal::http
