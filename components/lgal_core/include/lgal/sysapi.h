#pragma once
// System endpoints (all require login):
//   GET    /                        home page (set_home_page, e.g. the web UI), otherwise
//                                   the maintenance page; factory state -> /setup
//   GET    /recovery                maintenance/emergency page (in the firmware image)
//   GET    /api/v1/system/info      version, partition, OTA status, network, memory
//   GET    /api/v1/system/log       RAM log (text)
//   POST   /api/v1/system/reboot    reboot
//   GET    /api/v1/system/coredump  last core dump (ELF), 404 if none
//   DELETE /api/v1/system/coredump  delete core dump
//   GET    /api/v1/system/language  current and available languages
//   POST   /api/v1/system/language  ?lang=en|de  language of the web UI
//   GET    /api/v1/system/time      time, source, time zone, NTP server
//   PUT    /api/v1/system/time      {"timezone"?, "ntp_server"?, "utc"? | "local"?} POSIX TZ
//                                   string, NTP host ("" = off), manual time (tertiary
//                                   source) as UTC seconds or as device wall time
//                                   "YYYY-MM-DDTHH:MM[:SS]"
//   GET    /i18n.js                 shared translation base of the pages
//   GET    /flags.js                flag icons of the languages (pages and web UI)

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_system.h"

namespace lgal::sysapi {

esp_err_t register_handlers(httpd_handle_t server);

// "POWERON", "SW", "PANIC", ... (as in /api/v1/system/info)
const char* reset_reason_str(esp_reset_reason_t r);

// Link to a device-specific page, shown on the maintenance page.
// id: fixed key the page uses to translate the name; name: English.
void add_link(const char* id, const char* name, const char* path);
// Page for "/" (called after the setup check and login)
void set_home_page(esp_err_t (*send)(httpd_req_t* req));

}  // namespace lgal::sysapi
