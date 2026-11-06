#include "lgal/http.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "esp_log.h"
#include "lgal/device.h"
#include "lgal/json.h"

namespace lgal::http {

namespace {
constexpr const char* TAG = "http";
httpd_handle_t s_server = nullptr;

int b64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool b64_decode(const char* in, std::string& out) {
    out.clear();
    uint32_t acc = 0;
    int bits = 0;
    for (; *in && *in != '='; ++in) {
        int v = b64_value(*in);
        if (v < 0) return false;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((acc >> bits) & 0xFF));
        }
    }
    return true;
}

// constant-time comparison (against timing attacks)
bool equal_ct(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

esp_err_t send_401(httpd_req_t* req) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"LG Aquatic Life\", charset=\"UTF-8\"");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"error\":{\"code\":\"unauthorized\",\"message\":\"Login required\"}}");
}
}  // namespace

esp_err_t start() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.max_uri_handlers = 48;
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.core_id = 0;          // network core; core 1 later belongs to the dosing engine
    cfg.recv_wait_timeout = 15;
    cfg.send_wait_timeout = 15;
    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err == ESP_OK) ESP_LOGI(TAG, "HTTP server running on port 80");
    return err;
}

httpd_handle_t handle() { return s_server; }

bool authorize(httpd_req_t* req) {
    char hdr[160];
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) == ESP_OK &&
        strncmp(hdr, "Basic ", 6) == 0) {
        std::string creds;
        if (b64_decode(hdr + 6, creds)) {
            const std::string expected = std::string("admin:") + device::setup_password();
            if (equal_ct(creds, expected)) return true;
        }
    }
    send_401(req);
    return false;
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

bool read_json(httpd_req_t* req, size_t max_len, std::string* body, JsonReader* doc, JsonToken* tokens,
               int max_tokens) {
    const size_t len = req->content_len;
    if (len == 0) {
        send_error(req, "400 Bad Request", "body_missing", "JSON body required");
        return false;
    }
    if (len > max_len) {
        send_error(req, "413 Content Too Large", "body_too_large", "JSON body too large");
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
