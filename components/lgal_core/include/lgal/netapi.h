#pragma once
// Network endpoints (all require login):
//   GET  /api/v1/net/status          active interface, LAN, Wi-Fi, setup AP
//   GET  /api/v1/net/settings        settings (format: lgal/net_config.h)
//   PUT  /api/v1/net/settings        partial update; applied 1 s after the response
//   POST /api/v1/net/scan            start a Wi-Fi scan
//   GET  /api/v1/net/scan            {"state":"idle|running|done|failed","networks":[...]}
//   POST /api/v1/net/wlan/test       {"ssid","password"?, "dhcp", "ip", ...} connection test
//                                    without saving; omitted password = stored one
//                                    (same SSID)
//   GET  /api/v1/net/wlan/test       {"state":"idle|running|ok|failed","ip","reason"}
//
// Wi-Fi failure reasons ("reason"): timeout, no_ap_found, wrong_password,
// security_mismatch, connection_failed.

#include "esp_err.h"
#include "esp_http_server.h"

namespace lgal::netapi {

esp_err_t register_handlers(httpd_handle_t server);

}  // namespace lgal::netapi
