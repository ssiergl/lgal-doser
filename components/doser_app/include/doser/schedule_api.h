#pragma once
// REST API of the scheduler (M3). Amounts in ml (decimal), times "HH:MM"
// local wall-clock, dates "YYYY-MM-DD". Instants come as UTC seconds plus
// the device-local "YYYY-MM-DD HH:MM" (e.g. "at" + "local", "since" +
// "since_local"), because the browser may be in another time zone.
// All require login.
//
//   GET  /schedule                              scheduler test page
//   GET  /api/v1/pumps/schedule ?pump=1..5      schedule + today's slots
//   PUT  /api/v1/pumps/schedule ?pump=1..5      JSON body, see below
//   POST /api/v1/pumps/boost    ?pump=&ml=30&days=3
//                               ?pump=&action=stop
//   GET  /api/v1/schedule/status                next slot, delay, queue, boost
//                                               and forecast of all pumps
//   GET  /api/v1/schedule/locks
//   PUT  /api/v1/schedule/locks                 JSON body, see below
//   GET  /api/v1/engineering/scheduler          super-admin parameters (engineering area)
//   POST /api/v1/engineering/scheduler ?grid_min=5&max_delay_min=30
//
// Schedule body (unknown members are ignored, so a GET result can be sent
// back after editing):
//   {"power_loss": "skip_missed" | "catch_up_window", "catch_up_min": 60,
//    "programs": [
//      {"enabled": true, "kind": "single", "time": "08:00", "ml": 2.5,
//       "days": {"mode": "daily"}},
//      {"kind": "generator", "start": "08:00", "end": "20:00", "count": 12, "ml": 24,
//       "days": {"mode": "weekdays", "weekdays": ["mon", "wed", "fri"]}},
//      {"kind": "single", "time": "21:00", "ml": 5,
//       "days": {"mode": "interval", "every": 2, "anchor": "2026-11-06"}}]}
//   "days" defaults to daily, "anchor" to today, "enabled" to true.
//   Generator: "ml" is the total per day, split over "count" doses.
//
// Locks body: {"manual": "reject" | "queue",
//              "rules": [{"pumps": [1, 2], "gap_min": 5}]}
//
// Errors: 400 with "program" (1-based) and "field" for invalid input,
// 409 "conflict" with the colliding slots in "conflicts", 409
// "exceeds_daily_limit" with "pump".

#include "esp_err.h"
#include "esp_http_server.h"

namespace doser::schedule_api {

esp_err_t register_handlers(httpd_handle_t server);

}  // namespace doser::schedule_api
