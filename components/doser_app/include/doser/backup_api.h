#pragma once
// Full backup and restore (M6, engineering area only; format: backup_json.h).
//
//   GET  /api/v1/backup?secrets=0|1     download of the whole configuration;
//                                       secrets=1 adds the Wi-Fi and MQTT password,
//                                       the admin password record and the API key hashes
//   POST /api/v1/backup?parts=system,network,pumps,engineering,mqtt,auth
//                                       JSON body = a backup document. Restores the
//                                       chosen parts that the document contains
//                                       (default: all of them). Everything is
//                                       validated first; 409 pump_busy while a pump
//                                       runs. Response {"status":"ok","applied":[..],
//                                       "network_changed","signed_out"}; on a failure
//                                       while applying the error names "part" and
//                                       lists what was "applied" before.
//
// Restoring "auth" replaces the admin password and API keys and signs out every
// session (also the one that restored). "network" is applied 2 s after the
// response. Container contents, counters and the history are not touched.

#include "esp_err.h"
#include "esp_http_server.h"

namespace doser::backup_api {

esp_err_t register_handlers(httpd_handle_t server);

}  // namespace doser::backup_api
