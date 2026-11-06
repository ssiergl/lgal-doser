#pragma once
// Hardware diagnostics (M1, later part of the engineering mode "danger zone"):
//
//   GET  /hwtest                       test page (in the firmware image)
//   GET  /api/v1/hw/status             drivers, pumps, RTC, EEPROM, UART
//   POST /api/v1/hw/pump/run           ?pump=1..5&steps=±N&sps=N[&accel=&vstart=&force=1]
//   POST /api/v1/hw/pump/stop          ?pump=1..5|0 (all)[&mode=soft|hard]
//   POST /api/v1/hw/pump/config        ?pump=&run_ma=&hold_ma=&microsteps=&full_steps=&invert=&accel=&vstart=
//   GET  /api/v1/hw/tmc                ?pump=1..5  raw registers
//   POST /api/v1/hw/time               ?utc=<unix seconds>  (manual time -> system + RTC)
//   POST /api/v1/hw/rtc/aging          ?offset=-128..127
//   POST /api/v1/hw/eeprom/test
//
// Raw stepper test: bypasses container, schedule and log completely.

#include "esp_err.h"
#include "esp_http_server.h"

namespace doser::hwtest {

esp_err_t register_handlers(httpd_handle_t server);

}  // namespace doser::hwtest
