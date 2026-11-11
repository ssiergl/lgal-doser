#include "lgal/netapi.h"

#include <cstring>
#include <string>

#include "esp_log.h"
#include "lgal/events.h"
#include "lgal/http.h"
#include "lgal/json.h"
#include "lgal/json_reader.h"
#include "lgal/net.h"
#include "lgal/net_config.h"

extern const char network_html_start[] asm("_binary_network_html_start");
extern const char network_html_end[] asm("_binary_network_html_end");

namespace lgal::netapi {

namespace {
constexpr int kApplyDelayMs = 1000;

void kv_ip(Json& j, const char* key, uint32_t ip_be) {
    if (!ip_be) {
        j.null(key);
        return;
    }
    char b[16];
    netcfg::fmt_ipv4(__builtin_bswap32(ip_be), b);  // network -> host byte order
    j.kv(key, b);
}

const char* reason_code(uint8_t r) {
    switch (r) {
        case 0: return "timeout";
        case 201:  // NO_AP_FOUND
        case 212:  // NO_AP_FOUND_IN_RSSI_THRESHOLD
            return "no_ap_found";
        case 210:  // NO_AP_FOUND_W_COMPATIBLE_SECURITY
        case 211:  // NO_AP_FOUND_IN_AUTHMODE_THRESHOLD
            return "security_mismatch";
        case 2:    // AUTH_EXPIRE
        case 15:   // 4WAY_HANDSHAKE_TIMEOUT
        case 23:   // 802_1X_AUTH_FAILED
        case 202:  // AUTH_FAIL
        case 204:  // HANDSHAKE_TIMEOUT
            return "wrong_password";
        default: return "connection_failed";
    }
}

void write_addresses(Json& j, net::Iface i) {
    netcfg::IpConfig c;
    if (!net::ip_info(i, &c)) {
        j.null("address");
        return;
    }
    j.obj("address");
    netcfg::write_ip(j, c);
    j.end();
}

esp_err_t send_input_error(httpd_req_t* req, const netcfg::Error& e) {
    Json j;
    const char* code = e.code ? e.code : "invalid_param";
    j.obj().obj("error").kv("code", code).kv("message", e.field ? e.field : code);
    if (e.field) j.kv("field", e.field);
    j.end().end();
    return http::send_json(req, j.c_str(), "400 Bad Request");
}

struct Body {
    std::string text;
    JsonReader doc;
    JsonToken tok[48];
};

bool read_body(httpd_req_t* req, Body* b) { return http::read_json(req, 1024, &b->text, &b->doc, b->tok, 48); }

esp_err_t get_page(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, network_html_start, network_html_end - network_html_start - 1);
}

esp_err_t get_status(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    const net::Status st = net::status();
    const netcfg::Config cfg = net::config();
    Json j;
    j.obj().kv("active", net::iface_name(st.active)).kv("mode", netcfg::mode_name(cfg.mode));
    j.obj("lan").kv("present", st.eth_present).kv("running", st.eth_running).kv("link", st.eth_link);
    kv_ip(j, "ip", st.eth_ip);
    write_addresses(j, net::Iface::Lan);
    j.kv("down_s", static_cast<int64_t>(st.lan_down_ms / 1000)).end();
    j.obj("wlan").kv("configured", netcfg::wlan_configured(cfg)).kv("ssid", cfg.ssid);
    j.kv("state", net::wlan_state_name(st.wlan));
    kv_ip(j, "ip", st.wlan_ip);
    if (st.wlan == net::WlanState::Connected) j.kv("rssi", net::wlan_rssi());
    if (st.wlan_reason) j.kv("reason", reason_code(st.wlan_reason)).kv("reason_id", int(st.wlan_reason));
    write_addresses(j, net::Iface::Wlan);
    j.end();
    if (cfg.mode == netcfg::Mode::Auto && netcfg::wlan_configured(cfg) && st.eth_running && !st.eth_has_ip) {
        const int64_t left = (netcfg::kFallbackMs - st.lan_down_ms + 999) / 1000;
        j.kv("fallback_in_s", left > 0 ? left : 0);
    }
    j.obj("ap").kv("active", st.ap_active).kv("clients", st.ap_clients).end();
    j.end();
    return http::send_json(req, j.c_str());
}

esp_err_t send_settings(httpd_req_t* req, const netcfg::Config& c) {
    Json j;
    j.obj();
    netcfg::write(j, c);
    j.end();
    return http::send_json(req, j.c_str());
}

esp_err_t get_settings(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    return send_settings(req, net::config());
}

esp_err_t put_settings(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    Body b;
    if (!read_body(req, &b)) return ESP_OK;
    netcfg::Config c = net::config();
    netcfg::Error e;
    if (!netcfg::parse(b.doc.root(), &c, &e)) return send_input_error(req, e);
    const esp_err_t err = net::set_config(c, kApplyDelayMs);
    std::memset(c.password, 0, sizeof(c.password));
    if (err != ESP_OK) return http::send_error(req, "500 Internal Server Error", "storage_failed", esp_err_to_name(err));
    events::note("network", req);
    return send_settings(req, net::config());
}

esp_err_t post_scan(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    const esp_err_t err = net::start_scan();
    if (err == ESP_ERR_INVALID_STATE) return http::send_error(req, "409 Conflict", "busy", "Scan already running");
    if (err != ESP_OK) return http::send_error(req, "503 Service Unavailable", "wifi_unavailable", esp_err_to_name(err));
    return http::send_json(req, "{\"state\":\"running\"}", "202 Accepted");
}

esp_err_t get_scan(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    net::ScanResult r[net::kScanMax];
    int n = 0;
    const net::ScanState s = net::scan_results(r, net::kScanMax, &n);
    static const char* const kStates[] = {"idle", "running", "done", "failed"};
    Json j;
    j.obj().kv("state", kStates[int(s)]).arr("networks");
    for (int i = 0; i < n; ++i) {
        j.obj().kv("ssid", r[i].ssid).kv("rssi", int(r[i].rssi)).kv("channel", int(r[i].channel));
        j.kv("secure", r[i].secure).end();
    }
    j.end().end();
    return http::send_json(req, j.c_str());
}

esp_err_t post_test(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    Body b;
    if (!read_body(req, &b)) return ESP_OK;
    const JsonValue root = b.doc.root();
    netcfg::Config c = net::config();
    netcfg::Error e;
    char ssid[netcfg::kSsidMax + 1];
    if (!root["ssid"].get(ssid, sizeof(ssid)) || !ssid[0]) {
        e = {"invalid_param", "wlan.ssid"};
        return send_input_error(req, e);
    }
    if (std::strcmp(ssid, c.ssid) != 0) c.password[0] = 0;  // stored password only for the same network
    std::strcpy(c.ssid, ssid);
    c.mode = netcfg::Mode::Auto;
    const JsonValue pw = root["password"];
    bool ok = !pw.valid() || pw.get(c.password, sizeof(c.password));
    if (!ok) e = {"invalid_param", "wlan.password"};
    // IP members as in the "wlan" object of the settings
    ok = ok && netcfg::parse_ip(root, "wlan", &c.wlan, &e) && netcfg::validate(c, &e);
    esp_err_t err = ESP_OK;
    if (ok) err = net::start_wlan_test(c.ssid, c.password, c.wlan);
    std::memset(c.password, 0, sizeof(c.password));
    if (!ok) return send_input_error(req, e);
    if (err != ESP_OK) return http::send_error(req, "503 Service Unavailable", "wifi_unavailable", esp_err_to_name(err));
    return http::send_json(req, "{\"state\":\"running\"}", "202 Accepted");
}

esp_err_t get_test(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    const net::TestResult r = net::wlan_test_result();
    static const char* const kStates[] = {"idle", "running", "ok", "failed"};
    Json j;
    j.obj().kv("state", kStates[int(r.state)]);
    if (r.state == net::TestState::Ok) kv_ip(j, "ip", r.ip);
    if (r.state == net::TestState::Failed) j.kv("reason", reason_code(r.reason)).kv("reason_id", int(r.reason));
    j.end();
    return http::send_json(req, j.c_str());
}
}  // namespace

esp_err_t register_handlers(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_STATE;
    http::route(server, "/network", HTTP_GET, get_page);
    http::route(server, "/api/v1/net/status", HTTP_GET, get_status);
    http::route(server, "/api/v1/net/settings", HTTP_GET, get_settings);
    http::route(server, "/api/v1/net/settings", HTTP_PUT, put_settings);
    http::route(server, "/api/v1/net/scan", HTTP_POST, post_scan);
    http::route(server, "/api/v1/net/scan", HTTP_GET, get_scan);
    http::route(server, "/api/v1/net/wlan/test", HTTP_POST, post_test);
    http::route(server, "/api/v1/net/wlan/test", HTTP_GET, get_test);
    return ESP_OK;
}

}  // namespace lgal::netapi
