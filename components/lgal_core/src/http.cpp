#include "lgal/http.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "esp_log.h"
#include "lgal/auth.h"
#include "lgal/json.h"
#include "lgal/ws.h"
#include "lwip/sockets.h"

namespace lgal::http {

namespace {
constexpr const char* TAG = "http";
httpd_handle_t s_server = nullptr;

// Browsers asking for a page get a redirect to the login page, API clients JSON
bool wants_html(httpd_req_t* req) {
    char accept[128];
    return httpd_req_get_hdr_value_str(req, "Accept", accept, sizeof(accept)) == ESP_OK &&
           std::strstr(accept, "text/html") != nullptr;
}

void redirect_login(httpd_req_t* req, bool engineering) {
    // static: httpd keeps the header pointer until the response is sent (single server task)
    static char location[3 * CONFIG_HTTPD_MAX_URI_LEN + 32];
    size_t o = static_cast<size_t>(snprintf(location, sizeof(location), "/login?%snext=", engineering ? "eng=1&" : ""));
    static constexpr char kHex[] = "0123456789ABCDEF";
    for (const char* p = req->uri; *p && o + 4 < sizeof(location); ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (std::isalnum(c) || std::strchr("/-_.~", c)) {
            location[o++] = static_cast<char>(c);
        } else {
            location[o++] = '%';
            location[o++] = kHex[c >> 4];
            location[o++] = kHex[c & 15];
        }
    }
    location[o] = '\0';
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, nullptr, 0);
}

bool deny(httpd_req_t* req, auth::Result r) {
    if (r == auth::Result::Throttled) {
        send_error(req, "429 Too Many Requests", "too_many_attempts", "Too many failed attempts");
    } else if (wants_html(req)) {
        redirect_login(req, r == auth::Result::EngineeringLocked);
    } else if (r == auth::Result::EngineeringLocked) {
        send_error(req, "403 Forbidden", "engineering_locked", "Engineering area is locked");
    } else {
        send_error(req, "401 Unauthorized", "unauthorized", "Login required");
    }
    return false;
}
// Every closed connection (also LRU purge): WebSocket clients are forgotten
// (priming of that client stops). The hook has to close the socket itself.
void on_close(httpd_handle_t, int fd) {
    ws::socket_closed(fd);
    close(fd);
}
}  // namespace

esp_err_t start() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.max_uri_handlers = 112;
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.core_id = 0;          // network core; core 1 later belongs to the dosing engine
    cfg.recv_wait_timeout = 15;
    cfg.send_wait_timeout = 15;
    cfg.close_fn = on_close;
    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err == ESP_OK) ESP_LOGI(TAG, "HTTP server running on port 80");
    return err;
}

httpd_handle_t handle() { return s_server; }

bool authorize(httpd_req_t* req) {
    const auth::Result r = auth::check(req, false);
    return r == auth::Result::Ok || deny(req, r);
}

bool authorize_engineering(httpd_req_t* req) {
    const auth::Result r = auth::check(req, true);
    return r == auth::Result::Ok || deny(req, r);
}

uint32_t client_ipv4(httpd_req_t* req) {
    const int fd = httpd_req_to_sockfd(req);
    struct sockaddr_storage addr {};
    socklen_t len = sizeof(addr);
    if (fd < 0 || getpeername(fd, reinterpret_cast<struct sockaddr*>(&addr), &len) != 0) return 0;
    uint32_t ip = 0;
    if (addr.ss_family == AF_INET) {
        ip = reinterpret_cast<struct sockaddr_in*>(&addr)->sin_addr.s_addr;
    } else if (addr.ss_family == AF_INET6) {
        // IPv4-mapped (::ffff:a.b.c.d); pure IPv6 clients keep their low 32 bits
        std::memcpy(&ip, &reinterpret_cast<struct sockaddr_in6*>(&addr)->sin6_addr.s6_addr[12], 4);
    }
    return ip;
}

esp_err_t send_json(httpd_req_t* req, const char* json, const char* status) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

esp_err_t send_error(httpd_req_t* req, const char* status, const char* code, const char* message) {
    Json j;
    j.obj().obj("error").kv("code", code).kv("message", message ? message : code).end().end();
    return send_json(req, j.c_str(), status);
}

namespace {
int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
}  // namespace

bool query_str(httpd_req_t* req, const char* key, char* out, size_t len) {
    if (!out || len == 0) return false;
    const size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen == 0 || qlen > 512) return false;
    char q[513];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) return false;
    char raw[257];
    if (httpd_query_key_value(q, key, raw, sizeof(raw)) != ESP_OK) return false;
    // URL decoding (%XX, '+') so that names with umlauts/spaces arrive intact
    size_t o = 0;
    for (size_t i = 0; raw[i]; ++i) {
        char c = raw[i];
        if (c == '+') {
            c = ' ';
        } else if (c == '%' && hex_value(raw[i + 1]) >= 0 && hex_value(raw[i + 2]) >= 0) {
            c = static_cast<char>(hex_value(raw[i + 1]) * 16 + hex_value(raw[i + 2]));
            i += 2;
            if (c == '\0') return false;  // %00 would cut the value short
        }
        if (o + 1 >= len) return false;  // too long: rather reject than truncate
        out[o++] = c;
    }
    out[o] = '\0';
    return true;
}

bool query_decimal(httpd_req_t* req, const char* key, int64_t scale, int64_t* out) {
    char buf[24];
    if (!query_str(req, key, buf, sizeof(buf)) || buf[0] == '\0') return false;
    char* end = nullptr;
    const double v = strtod(buf, &end);
    if (!end || *end != '\0' || !(v == v) || v > 9.0e15 / scale || v < -9.0e15 / scale) return false;
    const double scaled = v * static_cast<double>(scale);
    *out = static_cast<int64_t>(scaled < 0 ? scaled - 0.5 : scaled + 0.5);
    return true;
}

bool query_int(httpd_req_t* req, const char* key, int64_t* out) {
    char buf[24];
    if (!query_str(req, key, buf, sizeof(buf)) || buf[0] == '\0') return false;
    char* end = nullptr;
    const long long v = strtoll(buf, &end, 10);
    if (!end || *end != '\0') return false;
    *out = v;
    return true;
}

bool read_body(httpd_req_t* req, size_t max_len, std::string* body) {
    const size_t len = req->content_len;
    if (len == 0) {
        send_error(req, "400 Bad Request", "body_missing", "Body required");
        return false;
    }
    if (len > max_len) {
        send_error(req, "413 Content Too Large", "body_too_large", "Body too large");
        return false;
    }
    body->resize(len);
    size_t got = 0;
    int timeouts = 0;
    while (got < len) {
        const int n = httpd_req_recv(req, body->data() + got, len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) continue;  // slow client: recv_wait_timeout each
        if (n <= 0) {
            send_error(req, "408 Request Timeout", "timeout", "Body not received");
            return false;
        }
        got += static_cast<size_t>(n);
    }
    return true;
}

bool read_json(httpd_req_t* req, size_t max_len, std::string* body, JsonReader* doc, JsonToken* tokens,
               int max_tokens) {
    if (!read_body(req, max_len, body)) return false;
    if (!doc->parse(body->data(), body->size(), tokens, max_tokens)) {
        char msg[64];
        snprintf(msg, sizeof(msg), "%s at offset %u", doc->error(), static_cast<unsigned>(doc->error_pos()));
        send_error(req, "400 Bad Request", "invalid_json", msg);
        return false;
    }
    return true;
}

void route(httpd_handle_t server, const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t*)) {
    httpd_uri_t u{};
    u.uri = uri;
    u.method = method;
    u.handler = handler;
    const esp_err_t err = httpd_register_uri_handler(server, &u);
    if (err != ESP_OK) ESP_LOGE(TAG, "Route %s: %s", uri, esp_err_to_name(err));
}

}  // namespace lgal::http
