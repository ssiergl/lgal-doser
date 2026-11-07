#pragma once
// WebSocket channel for real-time updates (spec 1): GET /api/v1/ws
//
// Handshake: logged in like every API request (session cookie or API key
// header). Browsers must come from the device's own pages (Origin check,
// see ws_core.h). Errors before the upgrade: 401 unauthorized,
// 403 forbidden_origin, 503 too_many_clients.
//
// Server -> client (text frames, JSON):
//   {"type":"status","seq":n,"utc":s,"time_valid":b, ...members of the
//    status writer}   right after connecting, on every change (checked every
//                     kPushIntervalMs) and at least every kKeepaliveMs
// Client -> server: {"type":"...","id"?:n, ...}; the reply has the same
// type and id plus "ok" and on failure "error" (code):
//   {"type":"ping"}  -> {"type":"ping","ok":true}
//   other types: registered with on_message (e.g. "prime", dosing_api.h)
//   unknown type     -> {"type":"error","error":"unknown_type"}
//
// A connection ends when its session is logged out (password change,
// logout) or its API key is deleted; an open connection keeps its session
// alive like regular requests do.

#include <cstdint>

#include "esp_err.h"
#include "esp_http_server.h"
#include "lgal/json.h"
#include "lgal/json_reader.h"

namespace lgal::ws {

inline constexpr int kMaxClients = 4;
inline constexpr int kMaxMessage = 512;      // bytes per client message
inline constexpr int kPushIntervalMs = 250;  // change check
inline constexpr int kKeepaliveMs = 10000;   // status at least this often
inline constexpr int kRecheckMs = 5000;      // login of open connections

// Members of the status message, written into the open object (push task)
using StatusWriter = void (*)(Json& j);
void set_status_writer(StatusWriter w);

// Handler of a client message type, runs in the HTTP server task. client:
// 0..kMaxClients-1, stable while the connection is open. May add members to
// the reply; returns nullptr on success or an error code.
using MessageHandler = const char* (*)(int client, const JsonValue& msg, Json& reply);
bool on_message(const char* type, MessageHandler h);  // type: static string
// Called when a connection is closed (HTTP server task)
using CloseHandler = void (*)(int client);
bool on_close(CloseHandler h);

// Check the status now instead of at the next interval
void notify();

esp_err_t register_handlers(httpd_handle_t server);
// Close hook of the HTTP server (http.cpp): forgets the client
void socket_closed(int fd);

}  // namespace lgal::ws
