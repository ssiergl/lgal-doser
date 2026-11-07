#pragma once
// REST API of the dosing engine (M2). Amounts in ml (decimal), all require login.
//
//   GET  /dosing                              dosing test page
//   GET  /api/v1/pumps                        status + settings of all pumps
//   POST /api/v1/pumps/dose       ?pump=1..5&ml=1.5[&tier=0..2]
//                                 409 collision_lock with "retry_at" (UTC) and
//                                 "retry_local" (null = other pump still
//                                 running), or 202 "queued" if
//                                 the lock policy is "queue" (see schedule_api.h)
//   POST /api/v1/pumps/stop       ?pump=0..5          (0 = all, also drops a queued dose)
//   POST /api/v1/pumps/prime      ?pump=&action=start|beat|stop[&tier=]
//   POST /api/v1/pumps/calibrate  ?pump=&action=start[&ml=20][&tier=]
//                                 ?pump=&action=apply&measured_ml=19.6
//                                 ?pump=&action=cancel
//   POST /api/v1/pumps/settings   ?pump=&name=&mode=auto|manual_only|disabled&tier=
//                                 &daily_limit_ml=&retract_rev=0.25|off|global
//                                 (retraction: off from the factory, 0 = off, global = engineering value)
//   POST /api/v1/pumps/container  ?pump=&remaining_ml=&capacity_ml=
//   POST /api/v1/pumps/tube       ?pump=&hours=0
//   GET  /api/v1/engineering/dosing           super-admin parameters
//   POST /api/v1/engineering/dosing ?retract_rev=&calib_ml=&tier1_rpm=&tier2_rpm=&tier3_rpm=
//                                 &prime_max_s=&prime_heartbeat_ms=&min_dose_ml=&tube_life_h=
//                                 &limit_mode=calendar_day|rolling_24h
//
// The engineering endpoints need the unlocked engineering area (auth.h, spec 11.1).
//
// WebSocket /api/v1/ws (lgal/ws.h):
//   status push      "pumps":[{"pump","activity","phase","trigger"?,"target_ml"?,
//                    "done_ml"?,"rpm"?,"prime_s"?,"remaining_ml","percent"?,
//                    "used_today_ml","last"?}]
//   {"type":"prime","pump":1..5,"action":"start|beat|stop","tier"?}
//                    hold-to-run priming (dead man): beat every ~150 ms; a
//                    missing beat or a closed connection stops the pump

#include "esp_err.h"
#include "esp_http_server.h"

namespace doser::dosing_api {

esp_err_t register_handlers(httpd_handle_t server);

}  // namespace doser::dosing_api
