#pragma once
// Web UI (M5): single page application embedded in the firmware image
// (www/, packed and gzip-compressed by pack.py at build time).
//
//   GET /        the app (login required; factory state -> /setup, see sysapi)
//   GET /ui/...  its files (no login: code only, no device data). ETag +
//                "Cache-Control: no-cache", unchanged files answer 304.
//
// The maintenance page stays reachable at /recovery.

#include "esp_err.h"
#include "esp_http_server.h"

namespace webui {

esp_err_t register_handlers(httpd_handle_t server);

}  // namespace webui
