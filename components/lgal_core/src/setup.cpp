#include "lgal/setup.h"

#include <cstdio>

#include "esp_log.h"
#include "lgal/auth.h"
#include "lgal/device.h"
#include "lgal/http.h"
#include "lgal/json.h"
#include "lgal/net.h"
#include "lgal/settings.h"

extern const char setup_html_start[] asm("_binary_setup_html_start");
extern const char setup_html_end[] asm("_binary_setup_html_end");

namespace lgal::setup {

namespace {
constexpr const char* TAG = "setup";

// Addresses at which the device is reachable after the setup
void write_urls(Json& j) {
    j.arr("urls");
    char buf[64];
    std::snprintf(buf, sizeof(buf), "http://%s.local/", device::hostname().c_str());
    j.v(buf);
    const net::Status st = net::status();
    const uint32_t ip = st.active == net::Iface::Lan ? st.eth_ip : st.active == net::Iface::Wlan ? st.wlan_ip : 0;
    if (ip) {
        std::snprintf(buf, sizeof(buf), "http://%u.%u.%u.%u/", unsigned(ip & 255), unsigned((ip >> 8) & 255),
                      unsigned((ip >> 16) & 255), unsigned(ip >> 24));  // network byte order
        j.v(buf);
    }
    j.end();
}

esp_err_t get_page(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, setup_html_start, setup_html_end - setup_html_start - 1);
}

esp_err_t get_status(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    Json j;
    j.obj()
        .kv("completed", settings::setup_completed())
        .kv("password_set", auth::password_set())
        .kv("connected", net::status().active != net::Iface::None)
        .kv("hostname", device::hostname());
    write_urls(j);
    j.end();
    return http::send_json(req, j.c_str());
}

esp_err_t post_complete(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    if (!auth::password_set())
        return http::send_error(req, "409 Conflict", "password_not_set", "Set an admin password first");
    if (net::status().active == net::Iface::None)
        return http::send_error(req, "409 Conflict", "not_connected", "Neither LAN nor Wi-Fi is connected");
    const esp_err_t err = settings::set_setup_completed(true);
    if (err != ESP_OK) return http::send_error(req, "500 Internal Server Error", "storage_failed", esp_err_to_name(err));
    ESP_LOGI(TAG, "Setup completed, setup AP off in %d s", kApOffDelayS);
    net::stop_setup_ap_in(kApOffDelayS);
    Json j;
    j.obj().kv("status", "ok").kv("ap_off_in_s", kApOffDelayS);
    write_urls(j);
    j.end();
    return http::send_json(req, j.c_str());
}
}  // namespace

esp_err_t register_handlers(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_STATE;
    http::route(server, "/setup", HTTP_GET, get_page);
    http::route(server, "/api/v1/setup", HTTP_GET, get_status);
    http::route(server, "/api/v1/setup/complete", HTTP_POST, post_complete);
    return ESP_OK;
}

}  // namespace lgal::setup
