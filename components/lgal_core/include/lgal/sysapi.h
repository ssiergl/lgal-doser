#pragma once
// System endpoints (all require login):
//   GET    /                        recovery/emergency page (in the firmware image)
//   GET    /api/v1/system/info      version, partition, OTA status, network, memory
//   GET    /api/v1/system/log       RAM log (text)
//   POST   /api/v1/system/reboot    reboot
//   GET    /api/v1/system/coredump  last core dump (ELF), 404 if none
//   DELETE /api/v1/system/coredump  delete core dump
//   GET    /api/v1/system/language  current and available languages
//   POST   /api/v1/system/language  ?lang=en|de  language of the web UI
//   GET    /i18n.js                 shared translation base of the pages

#include "esp_err.h"
#include "esp_http_server.h"

namespace lgal::sysapi {

esp_err_t register_handlers(httpd_handle_t server);

// Link to a device-specific page, shown on the maintenance page.
// id: fixed key the page uses to translate the name; name: English.
void add_link(const char* id, const char* name, const char* path);

}  // namespace lgal::sysapi
