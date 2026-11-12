#include "lgal/auth.h"

#include <cstring>
#include <string>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lgal/auth_core.h"
#include "lgal/device.h"
#include "lgal/events.h"
#include "lgal/http.h"
#include "lgal/json.h"
#include "lgal/json_reader.h"
#include "lgal/net.h"
#include "lgal/settings.h"
#include "lgal/sha256.h"
#include "lgal/timesync.h"
#include "nvs.h"

extern const char login_html_start[] asm("_binary_login_html_start");
extern const char login_html_end[] asm("_binary_login_html_end");

namespace lgal::auth {

namespace {
constexpr const char* TAG = "auth";
constexpr const char* kNs = "auth";
constexpr const char* kCookie = "lgal_session";

SemaphoreHandle_t s_mx = nullptr;
PasswordRecord s_pw{};
KeyStore s_keys;
Sessions s_sessions;
Throttle s_throttle;

struct Guard {
    Guard() { xSemaphoreTake(s_mx, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(s_mx); }
};

int64_t now_s() { return esp_timer_get_time() / 1000000; }

void random_bytes(uint8_t* p, size_t n) { esp_fill_random(p, n); }

template <typename T>
bool load(const char* key, T* out) {
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = sizeof(T);
    const bool ok = nvs_get_blob(h, key, out, &len) == ESP_OK && len == sizeof(T);
    nvs_close(h);
    return ok;
}

template <typename T>
esp_err_t save(const char* key, const T& v) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNs, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, key, &v, sizeof(T));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

// ---------------------------------------------------------------------------
// Request helpers
bool header(httpd_req_t* req, const char* name, char* out, size_t len) {
    const size_t n = httpd_req_get_hdr_value_len(req, name);
    if (n == 0 || n + 1 > len) return false;
    return httpd_req_get_hdr_value_str(req, name, out, len) == ESP_OK;
}

// Session index from the cookie (-1 = none), call with s_mx held
int session_of(httpd_req_t* req, int64_t now) {
    char cookies[256], token[kTokenChars + 2];
    if (!header(req, "Cookie", cookies, sizeof(cookies))) return -1;
    if (!cookie_value(cookies, kCookie, token, sizeof(token))) return -1;
    return s_sessions.find(token, now);
}

// API key slot (-1 = none), call with s_mx held
int key_of(httpd_req_t* req) {
    char v[kKeyChars + 16];
    if (header(req, "X-API-Key", v, sizeof(v))) return find_key(s_keys, v);
    char auth[kKeyChars + 16];
    if (header(req, "Authorization", auth, sizeof(auth)) && bearer_token(auth, v, sizeof(v))) return find_key(s_keys, v);
    return -1;
}

bool equal_str_ct(const char* a, const std::string& b) {
    return a && std::strlen(a) == b.size() && crypto::equal_ct(a, b.data(), b.size());
}

void set_cookie(httpd_req_t* req, const char* token) {
    // static: httpd keeps the pointer until the response is sent
    static char buf[96];
    if (token) snprintf(buf, sizeof(buf), "%s=%s; Path=/; HttpOnly; SameSite=Strict", kCookie, token);
    else snprintf(buf, sizeof(buf), "%s=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0", kCookie);
    httpd_resp_set_hdr(req, "Set-Cookie", buf);
}

// New session for this client (call with s_mx held)
void login_session(httpd_req_t* req, int64_t now) {
    uint8_t r[kTokenRandom];
    random_bytes(r, sizeof(r));
    char token[kTokenChars + 1];
    s_sessions.create(r, now, token);
    set_cookie(req, token);
}

esp_err_t ok(httpd_req_t* req) { return http::send_json(req, "{\"status\":\"ok\"}"); }

esp_err_t throttled(httpd_req_t* req, int64_t wait) {
    Json j;
    j.obj().obj("error").kv("code", "too_many_attempts").kv("message", "Too many failed attempts").kv("retry_after_s", wait).end().end();
    char ra[16];
    snprintf(ra, sizeof(ra), "%lld", static_cast<long long>(wait));
    httpd_resp_set_hdr(req, "Retry-After", ra);
    return http::send_json(req, j.c_str(), "429 Too Many Requests");
}

struct Body {
    std::string text;
    JsonReader doc;
    JsonToken tok[16];
};

bool read_body(httpd_req_t* req, Body* b) { return http::read_json(req, 512, &b->text, &b->doc, b->tok, 16); }

bool str_member(const JsonValue& v, const char* key, char* out, size_t len) { return v[key].get(out, len); }

// Sets a new admin password; signs out all sessions (call with s_mx held)
const char* change_password(const char* pw) {
    if (const char* e = check_policy(pw)) return e;
    PasswordRecord r;
    uint8_t salt[16];
    random_bytes(salt, sizeof(salt));
    make_record(&r, pw, salt);
    if (save("pw", r) != ESP_OK) return "storage_error";
    s_pw = r;
    s_sessions.clear();
    ESP_LOGI(TAG, "Admin password changed, all sessions signed out");
    return nullptr;
}

esp_err_t fail(httpd_req_t* req, const char* code) {
    if (!std::strcmp(code, "storage_error")) return http::send_error(req, "500 Internal Server Error", code, "Could not save");
    if (!std::strcmp(code, "password_too_short"))
        return http::send_error(req, "400 Bad Request", code, "Password needs at least 8 characters");
    if (!std::strcmp(code, "password_too_long"))
        return http::send_error(req, "400 Bad Request", code, "Password longer than 64 characters");
    return http::send_error(req, "400 Bad Request", code, code);
}

// ---------------------------------------------------------------------------
// Handlers
esp_err_t get_page(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, login_html_start, login_html_end - login_html_start - 1);
}

esp_err_t get_status(httpd_req_t* req) {
    const int64_t now = now_s();
    const uint32_t ip = http::client_ipv4(req);
    Json j;
    {
        Guard g;
        const int s = session_of(req, now);
        const int k = s < 0 ? key_of(req) : -1;
        j.obj().kv("logged_in", s >= 0 || k >= 0);
        if (s >= 0) j.kv("via", "session");
        else if (k >= 0) j.kv("via", "api_key");
        else j.null("via");
        j.kv("password_set", is_set(s_pw))
            .kv("setup_completed", settings::setup_completed())
            .kv("engineering", s >= 0 && s_sessions.engineering_left(s, now) > 0)
            .kv("engineering_left_s", s >= 0 ? s_sessions.engineering_left(s, now) : 0)
            .kv("recovery", net::is_setup_ap_client(ip))
            .kv("device", device::hostname())
            .end();
    }
    return http::send_json(req, j.c_str());
}

esp_err_t post_login(httpd_req_t* req) {
    if (!http::same_origin(req)) return ESP_OK;
    Body b;
    if (!read_body(req, &b)) return ESP_OK;
    char pw[kMaxPassword + 2];
    if (!str_member(b.doc.root(), "password", pw, sizeof(pw)))
        return http::send_error(req, "400 Bad Request", "invalid_param", "password");
    const uint32_t ip = http::client_ipv4(req);
    const int64_t now = now_s();
    Guard g;
    if (const int64_t w = s_throttle.wait(ip, now)) return throttled(req, w);
    // Before the setup: the device password from the label
    const bool good = is_set(s_pw) ? verify(s_pw, pw) : equal_str_ct(pw, device::setup_password());
    std::memset(pw, 0, sizeof(pw));
    if (!good) {
        s_throttle.failed(ip, now);
        ESP_LOGW(TAG, "Failed login from %u.%u.%u.%u", unsigned(ip & 0xFF), unsigned(ip >> 8 & 0xFF),
                 unsigned(ip >> 16 & 0xFF), unsigned(ip >> 24));
        return http::send_error(req, "401 Unauthorized", "wrong_password", "Wrong password");
    }
    s_throttle.succeeded(ip);
    login_session(req, now);
    Json j;
    j.obj().kv("status", "ok").kv("password_set", is_set(s_pw)).end();
    return http::send_json(req, j.c_str());
}

esp_err_t post_logout(httpd_req_t* req) {
    if (!http::same_origin(req)) return ESP_OK;
    char cookies[256], token[kTokenChars + 2];
    if (header(req, "Cookie", cookies, sizeof(cookies)) && cookie_value(cookies, kCookie, token, sizeof(token))) {
        Guard g;
        s_sessions.remove(token);
    }
    set_cookie(req, nullptr);
    return ok(req);
}

esp_err_t post_password(httpd_req_t* req) {
    if (!http::same_origin(req)) return ESP_OK;
    const int64_t now = now_s();
    {
        Guard g;
        if (session_of(req, now) < 0) return http::send_error(req, "401 Unauthorized", "unauthorized", "Login required");
    }
    Body b;
    if (!read_body(req, &b)) return ESP_OK;
    char cur[kMaxPassword + 2] = "", pw[kMaxPassword + 2];
    if (!str_member(b.doc.root(), "new", pw, sizeof(pw))) return http::send_error(req, "400 Bad Request", "invalid_param", "new");
    const uint32_t ip = http::client_ipv4(req);
    Guard g;
    if (is_set(s_pw)) {
        if (const int64_t w = s_throttle.wait(ip, now)) return throttled(req, w);
        if (!str_member(b.doc.root(), "current", cur, sizeof(cur)) || !verify(s_pw, cur)) {
            s_throttle.failed(ip, now);
            return http::send_error(req, "403 Forbidden", "wrong_password", "Current password is wrong");
        }
        s_throttle.succeeded(ip);
    }
    const char* e = change_password(pw);
    std::memset(pw, 0, sizeof(pw));
    std::memset(cur, 0, sizeof(cur));
    if (e) return fail(req, e);
    login_session(req, now);  // stay signed in on this browser
    events::note("password", events::By::Browser);
    return ok(req);
}

esp_err_t post_recover(httpd_req_t* req) {
    if (!http::same_origin(req)) return ESP_OK;
    const uint32_t ip = http::client_ipv4(req);
    if (!net::is_setup_ap_client(ip))
        return http::send_error(req, "403 Forbidden", "recovery_unavailable",
                                "Only via the setup Wi-Fi (hold the button for 3 s)");
    Body b;
    if (!read_body(req, &b)) return ESP_OK;
    char dev[32], pw[kMaxPassword + 2];
    if (!str_member(b.doc.root(), "device_password", dev, sizeof(dev)))
        return http::send_error(req, "400 Bad Request", "invalid_param", "device_password");
    if (!str_member(b.doc.root(), "new", pw, sizeof(pw))) return http::send_error(req, "400 Bad Request", "invalid_param", "new");
    const int64_t now = now_s();
    Guard g;
    if (const int64_t w = s_throttle.wait(ip, now)) return throttled(req, w);
    if (!equal_str_ct(dev, device::setup_password())) {
        s_throttle.failed(ip, now);
        return http::send_error(req, "403 Forbidden", "wrong_password", "Wrong device password");
    }
    s_throttle.succeeded(ip);
    const char* e = change_password(pw);
    std::memset(pw, 0, sizeof(pw));
    if (e) return fail(req, e);
    ESP_LOGW(TAG, "Admin password reset via the setup Wi-Fi");
    login_session(req, now);
    events::note("password", events::By::Browser);
    return ok(req);
}

esp_err_t get_keys(httpd_req_t* req) {
    Json j;
    {
        Guard g;
        if (session_of(req, now_s()) < 0) return http::send_error(req, "401 Unauthorized", "unauthorized", "Login required");
        j.obj().kv("max", kMaxKeys).arr("keys");
        for (int i = 0; i < kMaxKeys; ++i) {
            const ApiKey& k = s_keys.keys[i];
            if (!k.name[0]) continue;
            j.obj().kv("slot", i + 1).kv("name", k.name).kv("created", k.created).end();
        }
        j.end().end();
    }
    return http::send_json(req, j.c_str());
}

esp_err_t post_keys(httpd_req_t* req) {
    if (!http::same_origin(req)) return ESP_OK;
    {
        Guard g;
        if (session_of(req, now_s()) < 0) return http::send_error(req, "401 Unauthorized", "unauthorized", "Login required");
    }
    Body b;
    if (!read_body(req, &b)) return ESP_OK;
    char name[kKeyNameLen + 1];
    if (!str_member(b.doc.root(), "name", name, sizeof(name)))
        return http::send_error(req, "400 Bad Request", "invalid_param", "name (1..23 characters)");
    uint8_t r[kTokenRandom];
    random_bytes(r, sizeof(r));
    const auto ts = timesync::status();
    char key[kKeyChars + 1];
    Json j;
    {
        Guard g;
        KeyStore next = s_keys;
        const int slot = add_key(next, name, r, ts.valid ? ts.utc : 0, key);
        if (slot < 0)
            return key_count(s_keys) >= kMaxKeys
                       ? http::send_error(req, "409 Conflict", "too_many_keys", "All key slots are in use")
                       : http::send_error(req, "400 Bad Request", "invalid_param", "name (1..23 characters)");
        if (save("keys", next) != ESP_OK) return fail(req, "storage_error");
        s_keys = next;
        ESP_LOGI(TAG, "API key '%s' created", name);
        j.obj().kv("slot", slot + 1).kv("name", name).kv("key", key).end();
    }
    std::memset(key, 0, sizeof(key));
    events::note("key_added", events::By::Browser);
    return http::send_json(req, j.c_str());
}

esp_err_t delete_keys(httpd_req_t* req) {
    if (!http::same_origin(req)) return ESP_OK;
    int64_t slot = 0;
    if (!http::query_int(req, "slot", &slot) || slot < 1 || slot > kMaxKeys)
        return http::send_error(req, "400 Bad Request", "invalid_param", "slot=1..4");
    {
        Guard g;
        if (session_of(req, now_s()) < 0) return http::send_error(req, "401 Unauthorized", "unauthorized", "Login required");
        KeyStore next = s_keys;
        if (!remove_key(next, static_cast<int>(slot - 1)))
            return http::send_error(req, "404 Not Found", "not_found", "No such key");
        if (save("keys", next) != ESP_OK) return fail(req, "storage_error");
        s_keys = next;
        ESP_LOGI(TAG, "API key %d deleted", static_cast<int>(slot));
    }
    events::note("key_removed", events::By::Browser);
    return ok(req);
}

esp_err_t post_engineering(httpd_req_t* req) {
    if (!http::same_origin(req)) return ESP_OK;
    const int64_t now = now_s();
    {
        Guard g;
        if (session_of(req, now) < 0) return http::send_error(req, "401 Unauthorized", "unauthorized", "Login required");
    }
    Body b;
    if (!read_body(req, &b)) return ESP_OK;
    char key[40];
    if (!str_member(b.doc.root(), "key", key, sizeof(key))) return http::send_error(req, "400 Bad Request", "invalid_param", "key");
    const uint32_t ip = http::client_ipv4(req);
    Guard g;
    const int s = session_of(req, now);
    if (s < 0) return http::send_error(req, "401 Unauthorized", "unauthorized", "Login required");
    if (const int64_t w = s_throttle.wait(ip, now)) return throttled(req, w);
    const bool good = equal_str_ct(key, device::engineering_key());
    std::memset(key, 0, sizeof(key));
    if (!good) {
        s_throttle.failed(ip, now);
        return http::send_error(req, "403 Forbidden", "wrong_key", "Wrong engineering key");
    }
    s_throttle.succeeded(ip);
    s_sessions.set_engineering(s, true, now);
    ESP_LOGW(TAG, "Engineering area unlocked");
    return ok(req);
}

esp_err_t delete_engineering(httpd_req_t* req) {
    if (!http::same_origin(req)) return ESP_OK;
    Guard g;
    const int64_t now = now_s();
    const int s = session_of(req, now);
    if (s >= 0) s_sessions.set_engineering(s, false, now);
    return ok(req);
}
}  // namespace

// ---------------------------------------------------------------------------
esp_err_t init() {
    if (s_mx) return ESP_OK;
    s_mx = xSemaphoreCreateMutex();
    if (!s_mx) return ESP_ERR_NO_MEM;
    if (!load("pw", &s_pw) || !is_set(s_pw)) s_pw = PasswordRecord{};
    KeyStore k;
    if (load("keys", &k) && k.version == 1) s_keys = k;
    ESP_LOGI(TAG, "Admin password %s, %d API keys", is_set(s_pw) ? "set" : "not set (device password)",
             key_count(s_keys));
    return ESP_OK;
}

namespace {
void to_hex(const void* data, size_t n, std::string* out) {
    static const char kHex[] = "0123456789abcdef";
    const auto* p = static_cast<const uint8_t*>(data);
    out->clear();
    out->reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        out->push_back(kHex[p[i] >> 4]);
        out->push_back(kHex[p[i] & 15]);
    }
}

bool from_hex(const JsonValue& v, void* data, size_t n) {
    char buf[2 * sizeof(KeyStore) + 1];
    if (2 * n >= sizeof(buf) || !v.get(buf, sizeof(buf)) || std::strlen(buf) != 2 * n) return false;
    auto* p = static_cast<uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) {
        int b = 0;
        for (int k = 0; k < 2; ++k) {
            const char c = buf[2 * i + k];
            const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (d < 0) return false;
            b = b * 16 + d;
        }
        p[i] = static_cast<uint8_t>(b);
    }
    return true;
}
}  // namespace

void write_secrets(Json& j) {
    if (!s_mx) return;
    std::string hex;
    Guard g;
    if (is_set(s_pw)) to_hex(&s_pw, sizeof(s_pw), &hex);
    else hex.clear();
    j.kv("password", hex);
    to_hex(&s_keys, sizeof(s_keys), &hex);
    j.kv("keys", hex);
}

const char* restore_secrets(const JsonValue& v) {
    if (!s_mx) return "storage_error";
    PasswordRecord pw{};
    char empty[2];
    const bool no_pw = v["password"].get(empty, sizeof(empty)) && !empty[0];
    if (!no_pw && (!from_hex(v["password"], &pw, sizeof(pw)) || !is_set(pw) || pw.iterations < 1000 ||
                   pw.iterations > 1000000))
        return "invalid_param";
    KeyStore keys;
    if (!from_hex(v["keys"], &keys, sizeof(keys)) || keys.version != 1) return "invalid_param";
    for (auto& k : keys.keys) k.name[sizeof(k.name) - 1] = 0;
    Guard g;
    if (save("pw", no_pw ? PasswordRecord{} : pw) != ESP_OK || save("keys", keys) != ESP_OK) return "storage_error";
    s_pw = no_pw ? PasswordRecord{} : pw;
    s_keys = keys;
    s_sessions.clear();
    ESP_LOGW(TAG, "Password and %d API keys restored from a backup, all sessions signed out", key_count(s_keys));
    return nullptr;
}

bool principal(httpd_req_t* req, Principal* out) {
    if (!s_mx || !out) return false;
    *out = Principal{};
    char cookies[256], token[kTokenChars + 2];
    const int64_t now = now_s();
    Guard g;
    if (header(req, "Cookie", cookies, sizeof(cookies)) && cookie_value(cookies, kCookie, token, sizeof(token)) &&
        s_sessions.find(token, now) >= 0) {
        std::strcpy(out->session, token);
        return true;
    }
    const int k = key_of(req);
    if (k < 0) return false;
    out->key_slot = k;
    std::memcpy(out->key_hash, s_keys.keys[k].hash, sizeof(out->key_hash));
    return true;
}

bool still_valid(const Principal& p) {
    if (!s_mx) return false;
    Guard g;
    if (p.session[0]) return s_sessions.find(p.session, now_s()) >= 0;
    if (p.key_slot < 0 || p.key_slot >= kMaxKeys) return false;
    const ApiKey& k = s_keys.keys[p.key_slot];
    return k.name[0] && std::memcmp(k.hash, p.key_hash, sizeof(p.key_hash)) == 0;
}

bool password_set() {
    if (!s_mx) return false;
    Guard g;
    return is_set(s_pw);
}

bool confirm_password(httpd_req_t* req, const char* password) {
    if (!s_mx) {
        http::send_error(req, "401 Unauthorized", "unauthorized", "Login required");
        return false;
    }
    const int64_t now = now_s();
    const uint32_t ip = http::client_ipv4(req);
    Guard g;
    if (session_of(req, now) < 0) {
        http::send_error(req, "401 Unauthorized", "unauthorized", "Login required");
        return false;
    }
    if (const int64_t w = s_throttle.wait(ip, now)) {
        throttled(req, w);
        return false;
    }
    const bool good = is_set(s_pw) ? verify(s_pw, password) : equal_str_ct(password, device::setup_password());
    if (!good) {
        s_throttle.failed(ip, now);
        http::send_error(req, "403 Forbidden", "wrong_password", "Wrong password");
        return false;
    }
    s_throttle.succeeded(ip);
    return true;
}

Result check(httpd_req_t* req, bool engineering) {
    if (!s_mx) return Result::Unauthorized;
    const int64_t now = now_s();
    Guard g;
    const int s = session_of(req, now);
    if (s >= 0) {
        if (!engineering || s_sessions.engineering(s, now)) return Result::Ok;
        return Result::EngineeringLocked;
    }
    if (key_of(req) < 0) return Result::Unauthorized;
    if (!engineering) return Result::Ok;
    // API key + engineering key header (tools)
    char ek[40];
    if (!header(req, "X-Engineering-Key", ek, sizeof(ek))) return Result::EngineeringLocked;
    const uint32_t ip = http::client_ipv4(req);
    if (s_throttle.wait(ip, now)) return Result::Throttled;
    if (!equal_str_ct(ek, device::engineering_key())) {
        s_throttle.failed(ip, now);
        return Result::EngineeringLocked;
    }
    s_throttle.succeeded(ip);
    return Result::Ok;
}

esp_err_t register_handlers(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_STATE;
    http::route(server, "/login", HTTP_GET, get_page);
    http::route(server, "/api/v1/auth/status", HTTP_GET, get_status);
    http::route(server, "/api/v1/auth/login", HTTP_POST, post_login);
    http::route(server, "/api/v1/auth/logout", HTTP_POST, post_logout);
    http::route(server, "/api/v1/auth/password", HTTP_POST, post_password);
    http::route(server, "/api/v1/auth/recover", HTTP_POST, post_recover);
    http::route(server, "/api/v1/auth/keys", HTTP_GET, get_keys);
    http::route(server, "/api/v1/auth/keys", HTTP_POST, post_keys);
    http::route(server, "/api/v1/auth/keys", HTTP_DELETE, delete_keys);
    http::route(server, "/api/v1/auth/engineering", HTTP_POST, post_engineering);
    http::route(server, "/api/v1/auth/engineering", HTTP_DELETE, delete_engineering);
    return ESP_OK;
}

}  // namespace lgal::auth
