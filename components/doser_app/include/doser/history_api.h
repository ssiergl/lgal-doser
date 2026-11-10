#pragma once
// REST API of the event history (M6, functional spec chapter 9). Instants as
// UTC seconds plus the device-local "YYYY-MM-DD HH:MM:SS". All require login.
//
//   GET    /api/v1/history        ?pump=1..5&kind=dose,skip,...&days=1..60
//                                 (or from=&to= UTC seconds)&limit=1..500&offset=
//                                 {"events":[...], "more":bool}, newest first.
//                                 Event fields: see history_json.h
//   GET    /api/v1/history.csv    same filters, no limit (attachment)
//   GET    /api/v1/history/daily  ?days=1..60 (default 14)
//                                 {"today","keep_days","days":[{"date","ml":[5],
//                                 "doses":[5],"skipped":[5]}]} oldest first;
//                                 "days" empty without a valid clock
//   DELETE /api/v1/history        delete everything (engineering area)
//   GET    /api/v1/history/settings  {"keep_days","min_days","max_days"}
//   POST   /api/v1/history/settings  ?keep_days=1..60 retention (engineering area;
//                                 a shorter value deletes older days within a minute)
//
// kinds: dose, prime, calib_run, skip, container, calibration, tube, alarm
// codes: results (success, aborted, fault, locked, heartbeat_lost, ...), skip
//        reasons (missed, daily_limit, delay_timeout, ...),
//        container/calibration/tube changes (set, refilled) and alarms
//        (container_low, tube_worn, rtc_fault); unknown: other

#include "esp_err.h"
#include "esp_http_server.h"

namespace doser::history_api {

esp_err_t register_handlers(httpd_handle_t server);

}  // namespace doser::history_api
