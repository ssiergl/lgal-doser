#include "lgal/ws.h"

#include <cstring>
#include <string>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lgal/auth.h"
#include "lgal/http.h"
#include "lgal/settings.h"
#include "lgal/timesync.h"
#include "lgal/ws_core.h"

namespace lgal::ws {

namespace {
constexpr const char* TAG = "ws";
constexpr int kMaxTypes = 8;
constexpr int kMaxCloseHandlers = 4;
constexpr int kTokens = 32;

struct Client {
    int fd = -1;
    auth::Principal who;
};

struct Route {
    const char* type = nullptr;
    MessageHandler fn = nullptr;
};

httpd_handle_t s_server = nullptr;
SemaphoreHandle_t s_mx = nullptr;
TaskHandle_t s_task = nullptr;
Client s_clients[kMaxClients];
bool s_new_client = false;  // send the status right away
StatusWriter s_writer = nullptr;
Route s_routes[kMaxTypes];
CloseHandler s_close[kMaxCloseHandlers];

struct Guard {
    Guard() { xSemaphoreTake(s_mx, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(s_mx); }
};

int find_fd(int fd) {
    for (int i = 0; i < kMaxClients; ++i)
        if (s_clients[i].fd == fd) return i;
    return -1;
}

bool header(httpd_req_t* req, const char* name, char* out, size_t len) {
    const size_t n = httpd_req_get_hdr_value_len(req, name);
    return n > 0 && n < len && httpd_req_get_hdr_value_str(req, name, out, len) == ESP_OK;
}

// Before the upgrade: Origin, login, free slot. An error response is sent
// and ESP_FAIL closes the connection.
esp_err_t pre_handshake(httpd_req_t* req) {
    char origin[96], host[96];
    const bool has_origin = httpd_req_get_hdr_value_len(req, "Origin") > 0;
    if (has_origin && (!header(req, "Origin", origin, sizeof(origin)) || !header(req, "Host", host, sizeof(host)) ||
                       !origin_allowed(origin, host))) {
        ESP_LOGW(TAG, "Handshake from a foreign origin rejected");
        http::send_error(req, "403 Forbidden", "forbidden_origin", "WebSocket only from the device's own pages");
        return ESP_FAIL;
    }
    auth::Principal who;
    if (!auth::principal(req, &who)) {
        http::send_error(req, "401 Unauthorized", "unauthorized", "Login required");
        return ESP_FAIL;
    }
    if (!settings::setup_completed()) {  // live values and priming only after the first setup
        http::send_error(req, "409 Conflict", "setup_required", "Complete the first setup first");
        return ESP_FAIL;
    }
    const int fd = httpd_req_to_sockfd(req);
    {
        Guard g;
        int slot = find_fd(fd);
        if (slot < 0) slot = find_fd(-1);
        if (slot >= 0) {
            s_clients[slot].fd = fd;
            s_clients[slot].who = who;
            s_new_client = true;
        }
        if (slot < 0) {
            http::send_error(req, "503 Service Unavailable", "too_many_clients", "Too many WebSocket connections");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Client %d connected (fd %d)", slot, fd);
    }
    notify();
    return ESP_OK;
}

esp_err_t send_text(httpd_req_t* req, const std::string& s) {
    httpd_ws_frame_t f{};
    f.type = HTTPD_WS_TYPE_TEXT;
    f.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(s.data()));
    f.len = s.size();
    f.final = true;
    return httpd_ws_send_frame(req, &f);
}

esp_err_t reply_error(httpd_req_t* req, const char* code) {
    Json j;
    j.obj().kv("type", "error").kv("ok", false).kv("error", code).end();
    return send_text(req, j.str());
}

// Data frames after the upgrade (control frames are handled by the server)
esp_err_t on_frame(httpd_req_t* req) {
    if (req->method == HTTP_GET) return ESP_OK;  // handshake
    httpd_ws_frame_t f{};
    if (httpd_ws_recv_frame(req, &f, 0) != ESP_OK) return ESP_FAIL;
    if (f.len > size_t(kMaxMessage)) {
        ESP_LOGW(TAG, "Message too long (%u bytes), closing", unsigned(f.len));
        return ESP_FAIL;
    }
    char buf[kMaxMessage + 1];
    f.payload = reinterpret_cast<uint8_t*>(buf);
    if (f.len && httpd_ws_recv_frame(req, &f, f.len) != ESP_OK) return ESP_FAIL;
    buf[f.len] = '\0';
    if (f.type != HTTPD_WS_TYPE_TEXT) return ESP_OK;

    int client = -1;
    auth::Principal who;
    {
        Guard g;
        client = find_fd(httpd_req_to_sockfd(req));
        if (client >= 0) who = s_clients[client].who;
    }
    if (client < 0) return ESP_FAIL;
    if (!auth::still_valid(who)) {
        reply_error(req, "unauthorized");
        return ESP_FAIL;
    }

    JsonReader doc;
    JsonToken tok[kTokens];
    if (!doc.parse(buf, f.len, tok, kTokens)) return reply_error(req, "invalid_json");
    const JsonValue msg = doc.root();
    char type[24];
    if (!msg["type"].get(type, sizeof(type))) return reply_error(req, "invalid_json");
    int64_t id = 0;
    const bool has_id = msg["id"].get(&id);

    MessageHandler fn = nullptr;
    const char* rtype = nullptr;
    for (const Route& r : s_routes)
        if (r.type && std::strcmp(r.type, type) == 0) {
            fn = r.fn;
            rtype = r.type;
        }
    const bool ping = std::strcmp(type, "ping") == 0;
    if (!fn && !ping) return reply_error(req, "unknown_type");

    Json j;
    j.obj().kv("type", ping ? "ping" : rtype);
    if (has_id) j.kv("id", id);
    const char* err = ping ? nullptr : fn(client, msg, j);
    j.kv("ok", err == nullptr);
    if (err) j.kv("error", err);
    j.end();
    return send_text(req, j.str());
}

// ---------------------------------------------------------------------------
// Push

struct Push {
    std::string text;
};

// HTTP server task: send to all open WebSocket clients
void send_work(void* arg) {
    Push* p = static_cast<Push*>(arg);
    int fds[kMaxClients];
    {
        Guard g;
        for (int i = 0; i < kMaxClients; ++i) fds[i] = s_clients[i].fd;
    }
    httpd_ws_frame_t f{};
    f.type = HTTPD_WS_TYPE_TEXT;
    f.payload = reinterpret_cast<uint8_t*>(p->text.data());
    f.len = p->text.size();
    f.final = true;
    for (int fd : fds) {
        if (fd < 0 || httpd_ws_get_fd_info(s_server, fd) != HTTPD_WS_CLIENT_WEBSOCKET) continue;
        if (httpd_ws_send_frame_async(s_server, fd, &f) != ESP_OK) {
            ESP_LOGW(TAG, "Send to fd %d failed, closing", fd);
            httpd_sess_trigger_close(s_server, fd);
        }
    }
    delete p;
}

void recheck_logins() {
    int fds[kMaxClients];
    auth::Principal who[kMaxClients];
    {
        Guard g;
        for (int i = 0; i < kMaxClients; ++i) {
            fds[i] = s_clients[i].fd;
            who[i] = s_clients[i].who;
        }
    }
    for (int i = 0; i < kMaxClients; ++i) {
        if (fds[i] < 0 || auth::still_valid(who[i])) continue;
        ESP_LOGI(TAG, "Client %d: login no longer valid, closing", i);
        httpd_sess_trigger_close(s_server, fds[i]);
    }
}

void push_task(void*) {
    uint32_t last_hash = 0, seq = 0;
    int64_t last_sent = 0, last_check = 0;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kPushIntervalMs));
        bool fresh = false;
        int n = 0;
        {
            Guard g;
            fresh = s_new_client;
            s_new_client = false;
            for (const Client& c : s_clients) n += c.fd >= 0;
        }
        if (!n) {
            last_hash = 0;
            continue;
        }
        const int64_t now = esp_timer_get_time() / 1000;
        if (now - last_check >= kRecheckMs) {
            last_check = now;
            recheck_logins();
        }
        Json body;
        body.obj();
        if (s_writer) s_writer(body);
        body.end();
        const uint32_t h = fnv1a(body.c_str(), body.str().size());
        if (!fresh && h == last_hash && now - last_sent < kKeepaliveMs) continue;
        last_hash = h;
        last_sent = now;

        const auto ts = timesync::status();
        Json head;
        head.obj().kv("type", "status").kv("seq", int64_t(++seq)).kv("utc", ts.utc).kv("time_valid", ts.valid);
        Push* p = new Push;
        p->text = head.str();  // "{...}" without the closing brace
        p->text.pop_back();
        if (body.str().size() > 2) p->text += ',';
        p->text.append(body.str(), 1, std::string::npos);
        if (httpd_queue_work(s_server, send_work, p) != ESP_OK) delete p;
    }
}
}  // namespace

void set_status_writer(StatusWriter w) { s_writer = w; }

bool on_message(const char* type, MessageHandler h) {
    for (Route& r : s_routes)
        if (!r.type) {
            r.type = type;
            r.fn = h;
            return true;
        }
    ESP_LOGE(TAG, "No room for message type %s", type);
    return false;
}

bool on_close(CloseHandler h) {
    for (CloseHandler& c : s_close)
        if (!c) {
            c = h;
            return true;
        }
    return false;
}

void notify() {
    if (s_task) xTaskNotifyGive(s_task);
}

void socket_closed(int fd) {
    if (!s_mx) return;
    int client = -1;
    {
        Guard g;
        client = find_fd(fd);
        if (client >= 0) s_clients[client] = Client{};
    }
    if (client < 0) return;
    ESP_LOGI(TAG, "Client %d disconnected", client);
    for (CloseHandler c : s_close)
        if (c) c(client);
}

esp_err_t register_handlers(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_STATE;
    s_server = server;
    if (!s_mx) s_mx = xSemaphoreCreateMutex();
    if (!s_mx) return ESP_ERR_NO_MEM;
    httpd_uri_t u{};
    u.uri = "/api/v1/ws";
    u.method = HTTP_GET;
    u.handler = on_frame;
    u.is_websocket = true;
    u.ws_pre_handshake_cb = pre_handshake;
    esp_err_t err = httpd_register_uri_handler(server, &u);
    if (err != ESP_OK) return err;
    if (!s_task && xTaskCreatePinnedToCore(push_task, "ws_push", 4096, nullptr, 3, &s_task, 0) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}

}  // namespace lgal::ws
