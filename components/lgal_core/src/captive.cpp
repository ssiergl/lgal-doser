#include "lgal/captive.h"

#include <cstdio>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lgal/dns_reply.h"
#include "lgal/http.h"
#include "lgal/net.h"
#include "lwip/sockets.h"

namespace lgal::captive {

namespace {
constexpr const char* TAG = "captive";

void dns_task(void*) {
    const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (s < 0 || bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "DNS socket failed (errno %d)", errno);
        if (s >= 0) close(s);
        vTaskDelete(nullptr);
        return;
    }
    timeval tv{1, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    static uint8_t q[dns::kMaxPacket], out[dns::kMaxPacket];
    while (true) {
        sockaddr_in from{};
        socklen_t flen = sizeof(from);
        const int n = recvfrom(s, q, sizeof(q), 0, reinterpret_cast<sockaddr*>(&from), &flen);
        if (n <= 0) continue;
        // only clients of the open setup AP; nothing for LAN/Wi-Fi neighbours
        if (!net::is_setup_ap_client(from.sin_addr.s_addr)) continue;
        const uint32_t ip = ntohl(net::setup_ap_ip());
        if (!ip) continue;
        const size_t len = dns::answer(q, size_t(n), ip, out, sizeof(out));
        if (len) sendto(s, out, len, 0, reinterpret_cast<sockaddr*>(&from), flen);
    }
}

esp_err_t not_found(httpd_req_t* req, httpd_err_code_t) {
    if (net::is_setup_ap_client(http::client_ipv4(req))) {
        const uint32_t ip = ntohl(net::setup_ap_ip());
        char loc[32];
        std::snprintf(loc, sizeof(loc), "http://%u.%u.%u.%u/", unsigned(ip >> 24), unsigned((ip >> 16) & 255),
                      unsigned((ip >> 8) & 255), unsigned(ip & 255));
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", loc);
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        return httpd_resp_send(req, nullptr, 0);
    }
    return http::send_error(req, "404 Not Found", "not_found", "Not found");
}
}  // namespace

esp_err_t start() {
    return xTaskCreate(dns_task, "captive_dns", 3072, nullptr, 2, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t register_handlers(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_STATE;
    return httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, not_found);
}

}  // namespace lgal::captive
