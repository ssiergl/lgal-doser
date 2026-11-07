#pragma once
// Captive portal of the setup AP (spec 2): a DNS server answers every name
// with the device address, and unknown pages requested by setup AP clients
// are redirected to the device. Phones and laptops then open the setup page
// on their own after joining the setup Wi-Fi. Clients on LAN/Wi-Fi are not
// affected (normal 404).

#include "esp_err.h"
#include "esp_http_server.h"

namespace lgal::captive {

// DNS server task (UDP 53, answers only while the setup AP is on)
esp_err_t start();
// 404 handler with the redirect for setup AP clients
esp_err_t register_handlers(httpd_handle_t server);

}  // namespace lgal::captive
