#include "lgal/mqtt.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_tls_errors.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lgal/device.h"
#include "lgal/http.h"
#include "lgal/json.h"
#include "lgal/json_reader.h"
#include "lgal/ota.h"
#include "mbedtls/x509_crt.h"
#include "mqtt_client.h"
#include "nvs.h"
#include "sdkconfig.h"

namespace lgal::mqtt {

namespace {
constexpr const char* TAG = "mqtt";
constexpr const char* kNs = "mqtt";
constexpr const char* kKeyCfg = "cfg";
constexpr const char* kKeyCa = "ca";
constexpr uint32_t kMagic = 0x4d515454;  // "MQTT"
constexpr uint16_t kVersion = 1;

struct Stored {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    mqttcfg::Config cfg;
};

// Locking: s_op serialises restarts (settings, CA). s_mx guards the data
// below for short moments; esp-mqtt calls under s_mx are made only outside
// the MQTT task, whose callbacks never take s_mx (no lock-order deadlock).
SemaphoreHandle_t s_op = nullptr;
SemaphoreHandle_t s_mx = nullptr;
mqttcfg::Config s_cfg;
char* s_ca = nullptr;  // PEM incl. terminator; referenced by a running client
esp_mqtt_client_handle_t s_client = nullptr;
std::string s_base;  // base topic
// Snapshot of the running connection: written only while no client runs,
// read lock-free by the MQTT task (Session, last will)
std::string s_lwt;   // "<base>/status"
std::string s_run_base;
mqttcfg::Config s_run_cfg;  // password wiped
ConnectHandler s_on_connect = nullptr;
DataHandler s_on_data = nullptr;

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;  // status
Status s_status;

struct Guard {
    explicit Guard(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(m_); }
    SemaphoreHandle_t m_;
};

int64_t now_ms() { return esp_timer_get_time() / 1000; }

// error: nullptr keeps the last error (except when connected or disabled)
void set_state(State st, const char* error, bool connect = false) {
    const int64_t now = now_ms();
    portENTER_CRITICAL(&s_mux);
    if (s_status.state != st) {
        s_status.state = st;
        s_status.since_ms = now;
    }
    if (st == State::Connected || st == State::Disabled) s_status.error = nullptr;
    else if (error) s_status.error = error;
    if (connect) ++s_status.connects;
    portEXIT_CRITICAL(&s_mux);
}

const char* error_code(const esp_mqtt_error_codes_t* e) {
    if (!e) return "tcp";
    if (e->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        switch (e->connect_return_code) {
            case MQTT_CONNECTION_REFUSE_BAD_USERNAME:
            case MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED: return "auth";
            default: return "refused";
        }
    }
    if (e->error_type != MQTT_ERROR_TYPE_TCP_TRANSPORT) return nullptr;
    switch (e->esp_tls_last_esp_err) {
        case ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME: return "dns";
        case ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT: return "timeout";
        case ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED: return "tls";
        default: break;
    }
    return e->esp_tls_cert_verify_flags ? "tls" : "tcp";
}

bool is_ota_topic(const char* topic, int len) {
    static constexpr char kSuffix[] = "/ota/set";
    const size_t n = s_run_base.size();
    return size_t(len) == n + sizeof(kSuffix) - 1 && std::memcmp(topic, s_run_base.data(), n) == 0 &&
           std::memcmp(topic + n, kSuffix, sizeof(kSuffix) - 1) == 0;
}

// "<base>/ota/set" = firmware URL. MQTT task: start_url only starts the
// download task, which reports on "<base>/ota/state"; a refusal is answered here.
void ota_command(esp_mqtt_client_handle_t client, const char* data, int len) {
    std::string code;
    if (!ota::settings().mqtt_update) {
        code = "mqtt_update_disabled";
    } else if (len <= 0 || size_t(len) > ota::kMaxUrl) {
        code = "invalid_url";
    } else {
        std::string reason;
        const std::string url(data, size_t(len));
        if (const char* e = ota::start_url(url.c_str(), "mqtt", &reason)) code = std::strcmp(e, "guard") ? e : reason;
    }
    if (code.empty()) return;
    ESP_LOGW(TAG, "OTA over MQTT refused: %s", code.c_str());
    char msg[96];
    const int n = std::snprintf(msg, sizeof(msg), "{\"state\":\"refused\",\"error\":\"%s\"}", code.c_str());
    esp_mqtt_client_publish(client, (s_run_base + "/ota/state").c_str(), msg, n, 1, 0);
}

void on_event(void*, esp_event_base_t, int32_t id, void* data) {
    auto* ev = static_cast<esp_mqtt_event_handle_t>(data);
    switch (static_cast<esp_mqtt_event_id_t>(id)) {
        case MQTT_EVENT_BEFORE_CONNECT:
            set_state(State::Connecting, nullptr);
            break;
        case MQTT_EVENT_CONNECTED: {
            set_state(State::Connected, nullptr, true);
            ESP_LOGI(TAG, "Connected");
            esp_mqtt_client_publish(ev->client, s_lwt.c_str(), "online", 6, 1, 1);
            Session s(ev->client);
            s.subscribe(s_run_base + "/ota/set", 1);  // checked against the setting on arrival
            if (s_on_connect) s_on_connect(s);
            break;
        }
        case MQTT_EVENT_DISCONNECTED:
            set_state(State::Connecting, nullptr);
            ESP_LOGW(TAG, "Disconnected");
            break;
        case MQTT_EVENT_ERROR:
            if (const char* c = error_code(ev->error_handle)) {
                set_state(State::Connecting, c);
                ESP_LOGW(TAG, "Connection error: %s", c);
            }
            break;
        case MQTT_EVENT_DATA:
            // commands are short: fragmented messages are ignored
            if (!ev->topic || ev->current_data_offset != 0 || ev->data_len != ev->total_data_len) break;
            if (is_ota_topic(ev->topic, ev->topic_len)) {
                ota_command(ev->client, ev->data, ev->data_len);
                break;
            }
            if (s_on_data) s_on_data(ev->topic, ev->topic_len, ev->data, ev->data_len);
            break;
        default: break;
    }
}

// --- storage -----------------------------------------------------------------

void load() {
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return;
    Stored st{};
    size_t len = sizeof(st);
    if (nvs_get_blob(h, kKeyCfg, &st, &len) == ESP_OK) {
        if (len == sizeof(st) && st.magic == kMagic && st.version == kVersion && st.size == sizeof(st.cfg))
            s_cfg = st.cfg;
        else
            ESP_LOGW(TAG, "Stored settings invalid, using defaults");
    }
    size_t ca_len = 0;
    if (nvs_get_blob(h, kKeyCa, nullptr, &ca_len) == ESP_OK && ca_len > 1 && ca_len <= mqttcfg::kCaMax) {
        char* buf = static_cast<char*>(std::malloc(ca_len));
        if (buf && nvs_get_blob(h, kKeyCa, buf, &ca_len) == ESP_OK && buf[ca_len - 1] == '\0') s_ca = buf;
        else std::free(buf);
    }
    nvs_close(h);
    mqttcfg::Error e;
    if (!mqttcfg::validate(s_cfg, s_ca != nullptr, &e)) {
        ESP_LOGW(TAG, "Stored settings rejected (%s), MQTT off", e.field ? e.field : "?");
        s_cfg.enabled = false;
    }
}

esp_err_t save_cfg(const mqttcfg::Config& c) {
    Stored st{};
    st.magic = kMagic;
    st.version = kVersion;
    st.size = sizeof(st.cfg);
    st.cfg = c;
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNs, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, kKeyCfg, &st, sizeof(st));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    std::memset(&st, 0, sizeof(st));
    return err;
}

// pem == nullptr: erase
esp_err_t save_ca(const char* pem, size_t len_with_nul) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNs, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = pem ? nvs_set_blob(h, kKeyCa, pem, len_with_nul) : nvs_erase_key(h, kKeyCa);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

// --- client ------------------------------------------------------------------

// with s_op
void stop() {
    esp_mqtt_client_handle_t old;
    {
        Guard g(s_mx);
        old = s_client;
        s_client = nullptr;  // publish() returns false from here on
    }
    if (!old) return;
    if (connected()) esp_mqtt_client_publish(old, s_lwt.c_str(), "offline", 7, 1, 1);
    esp_mqtt_client_destroy(old);  // waits for the MQTT task
    set_state(State::Disabled, nullptr);
}

// with s_op, client stopped
void start() {
    mqttcfg::Config c;
    {
        Guard g(s_mx);
        c = s_cfg;
        s_base = c.base_topic[0] ? std::string(c.base_topic)
                                 : std::string(mqttcfg::kDefaultPrefix) + CONFIG_LGAL_DEVICE_TYPE + "/" + device::hostname();
        s_lwt = s_base + "/status";
        s_run_base = s_base;
        s_run_cfg = c;
        std::memset(s_run_cfg.password, 0, sizeof(s_run_cfg.password));
    }
    if (!c.enabled) {
        set_state(State::Disabled, nullptr);
        return;
    }
    esp_mqtt_client_config_t mc = {};
    mc.broker.address.hostname = c.host;
    mc.broker.address.port = mqttcfg::port(c);
    mc.broker.address.transport = c.tls == mqttcfg::Tls::Off ? MQTT_TRANSPORT_OVER_TCP : MQTT_TRANSPORT_OVER_SSL;
    if (c.tls == mqttcfg::Tls::Bundle) mc.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    if (c.tls == mqttcfg::Tls::Custom) {
        if (!s_ca) {
            set_state(State::Connecting, "tls");
            return;
        }
        mc.broker.verification.certificate = s_ca;  // kept by reference
    }
    mc.credentials.client_id = device::hostname().c_str();
    if (c.username[0]) {
        mc.credentials.username = c.username;
        if (c.password[0]) mc.credentials.authentication.password = c.password;
    }
    mc.session.last_will.topic = s_lwt.c_str();
    mc.session.last_will.msg = "offline";
    mc.session.last_will.msg_len = 7;
    mc.session.last_will.qos = 1;
    mc.session.last_will.retain = 1;
    mc.session.keepalive = 30;
    mc.network.reconnect_timeout_ms = 10000;
    mc.network.timeout_ms = 10000;
    mc.buffer.size = 2048;
    mc.task.stack_size = 6144;
    esp_mqtt_client_handle_t h = esp_mqtt_client_init(&mc);
    std::memset(&c, 0, sizeof(c));  // copies of the strings are inside the client
    if (!h) {
        ESP_LOGE(TAG, "Client init failed");
        set_state(State::Connecting, "tcp");
        return;
    }
    esp_mqtt_client_register_event(h, MQTT_EVENT_ANY, on_event, nullptr);
    set_state(State::Connecting, nullptr);
    if (esp_mqtt_client_start(h) != ESP_OK) {
        ESP_LOGE(TAG, "Client start failed");
        esp_mqtt_client_destroy(h);
        return;
    }
    Guard g(s_mx);
    s_client = h;
    ESP_LOGI(TAG, "Broker %s:%u (%s), base topic %s", s_cfg.host, mqttcfg::port(s_cfg), mqttcfg::tls_name(s_cfg.tls),
             s_base.c_str());
}

// --- CA ----------------------------------------------------------------------

// Parses a NUL-terminated PEM list; false if any certificate is broken
bool parse_ca(const char* pem, size_t len_with_nul, CaInfo* info) {
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    const int rc = mbedtls_x509_crt_parse(&crt, reinterpret_cast<const unsigned char*>(pem), len_with_nul);
    bool ok = rc == 0 && crt.raw.len > 0;
    if (ok && info) {
        info->present = true;
        info->count = 0;
        for (const mbedtls_x509_crt* c = &crt; c && c->raw.len; c = c->next) ++info->count;
        char dn[160];
        if (mbedtls_x509_dn_gets(dn, sizeof(dn), &crt.subject) > 0) {
            const char* cn = std::strstr(dn, "CN=");
            const char* s = cn ? cn + 3 : dn;
            size_t n = cn ? std::strcspn(s, ",") : std::strlen(s);
            if (n >= sizeof(info->subject)) n = sizeof(info->subject) - 1;
            std::memcpy(info->subject, s, n);
            info->subject[n] = '\0';
        }
        std::snprintf(info->valid_to, sizeof(info->valid_to), "%04u-%02u-%02u", unsigned(crt.valid_to.year) % 10000u,
                      unsigned(crt.valid_to.mon) % 100u, unsigned(crt.valid_to.day) % 100u);
    }
    mbedtls_x509_crt_free(&crt);
    return ok;
}

// --- REST --------------------------------------------------------------------

esp_err_t send_doc(httpd_req_t* req) {
    const mqttcfg::Config c = config();
    const Status st = status();
    const CaInfo ca = ca_info();
    Json j;
    j.obj().obj("settings");
    mqttcfg::write(j, c);
    j.end();
    j.kv("base_topic", base_topic()).kv("port", static_cast<int>(mqttcfg::port(c)));
    j.obj("status").kv("state", state_name(st.state));
    if (st.error) j.kv("error", st.error);
    else j.null("error");
    j.kv("since_s", (now_ms() - st.since_ms) / 1000).kv("connects", static_cast<int64_t>(st.connects)).end();
    j.obj("ca").kv("present", ca.present);
    if (ca.present) j.kv("count", ca.count).kv("subject", ca.subject).kv("valid_to", ca.valid_to);
    j.end().end();
    return http::send_json(req, j.c_str());
}

esp_err_t get_mqtt(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    return send_doc(req);
}

esp_err_t put_mqtt(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    std::string text;
    JsonReader doc;
    JsonToken tok[32];
    if (!http::read_json(req, 1024, &text, &doc, tok, 32)) return ESP_OK;
    mqttcfg::Config c = config();
    mqttcfg::Error e;
    const bool ok = mqttcfg::parse(doc.root(), ca_present(), &c, &e);
    std::memset(text.data(), 0, text.size());
    if (!ok) {
        std::memset(&c, 0, sizeof(c));
        Json j;
        const char* code = e.code ? e.code : "invalid_param";
        j.obj().obj("error").kv("code", code).kv("message", e.field ? e.field : code);
        if (e.field) j.kv("field", e.field);
        j.end().end();
        return http::send_json(req, j.c_str(), "400 Bad Request");
    }
    const esp_err_t err = set_config(c);
    std::memset(&c, 0, sizeof(c));
    if (err != ESP_OK) return http::send_error(req, "500 Internal Server Error", "storage_failed", esp_err_to_name(err));
    return send_doc(req);
}

esp_err_t put_ca(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    std::string pem;
    if (!http::read_body(req, mqttcfg::kCaMax - 1, &pem)) return ESP_OK;
    if (const char* e = set_ca(pem.data(), pem.size())) {
        const bool storage = std::strcmp(e, "storage_failed") == 0;
        return http::send_error(req, storage ? "500 Internal Server Error" : "400 Bad Request", e,
                                storage ? "Could not save the certificate" : "No valid PEM certificate");
    }
    return send_doc(req);
}

esp_err_t delete_ca(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    if (const char* e = clear_ca()) {
        const bool in_use = std::strcmp(e, "ca_in_use") == 0;
        return http::send_error(req, in_use ? "409 Conflict" : "500 Internal Server Error", e,
                                in_use ? "The certificate is used by the active connection" : "Storage error");
    }
    return send_doc(req);
}

}  // namespace

// -----------------------------------------------------------------------------

Session::Session(void* client) : client_(client) {}

bool Session::publish(const std::string& topic, const char* payload, int len, bool retain, int qos) {
    return esp_mqtt_client_publish(static_cast<esp_mqtt_client_handle_t>(client_), topic.c_str(), payload, len, qos,
                                   retain ? 1 : 0) >= 0;
}

bool Session::subscribe(const std::string& topic, int qos) {
    return esp_mqtt_client_subscribe_single(static_cast<esp_mqtt_client_handle_t>(client_), topic.c_str(), qos) >= 0;
}

const std::string& Session::base() const { return s_run_base; }
const mqttcfg::Config& Session::settings() const { return s_run_cfg; }

void set_handlers(ConnectHandler on_connect, DataHandler on_data) {
    s_on_connect = on_connect;
    s_on_data = on_data;
}

esp_err_t init() {
    if (s_mx) return ESP_OK;
    s_op = xSemaphoreCreateMutex();
    s_mx = xSemaphoreCreateMutex();
    if (!s_op || !s_mx) return ESP_ERR_NO_MEM;
    load();
    Guard op(s_op);
    start();
    return ESP_OK;
}

mqttcfg::Config config() {
    if (!s_mx) return {};
    Guard g(s_mx);
    return s_cfg;
}

esp_err_t set_config(const mqttcfg::Config& c) {
    if (!s_mx) return ESP_ERR_INVALID_STATE;
    Guard op(s_op);
    const esp_err_t err = save_cfg(c);
    if (err != ESP_OK) return err;
    stop();
    {
        Guard g(s_mx);
        s_cfg = c;
    }
    start();
    return ESP_OK;
}

std::string base_topic() {
    if (!s_mx) return {};
    Guard g(s_mx);
    return s_base;
}

std::string topic(const char* suffix) {
    std::string t = base_topic();
    t += '/';
    t += suffix;
    return t;
}

bool ca_present() {
    if (!s_mx) return false;
    Guard g(s_mx);
    return s_ca != nullptr;
}

CaInfo ca_info() {
    CaInfo info;
    if (!s_mx) return info;
    Guard g(s_mx);
    if (s_ca && !parse_ca(s_ca, std::strlen(s_ca) + 1, &info)) info.present = true;  // stored, but unreadable
    return info;
}

std::string ca_pem() {
    if (!s_mx) return {};
    Guard g(s_mx);
    return s_ca ? std::string(s_ca) : std::string();
}

const char* set_ca(const char* pem, size_t len) {
    if (!s_mx) return "storage_failed";
    if (!mqttcfg::looks_like_pem(pem, static_cast<unsigned>(len))) return "invalid_certificate";
    char* buf = static_cast<char*>(std::malloc(len + 1));
    if (!buf) return "storage_failed";
    std::memcpy(buf, pem, len);
    buf[len] = '\0';
    if (!parse_ca(buf, len + 1, nullptr)) {
        std::free(buf);
        return "invalid_certificate";
    }
    Guard op(s_op);
    if (save_ca(buf, len + 1) != ESP_OK) {
        std::free(buf);
        return "storage_failed";
    }
    stop();  // the running client may reference the old certificate
    {
        Guard g(s_mx);
        std::free(s_ca);
        s_ca = buf;
    }
    start();
    ESP_LOGI(TAG, "CA certificate saved (%u bytes)", unsigned(len));
    return nullptr;
}

const char* clear_ca() {
    if (!s_mx) return "storage_failed";
    Guard op(s_op);
    {
        Guard g(s_mx);
        if (s_cfg.enabled && s_cfg.tls == mqttcfg::Tls::Custom) return "ca_in_use";
    }
    if (save_ca(nullptr, 0) != ESP_OK) return "storage_failed";
    stop();
    {
        Guard g(s_mx);
        std::free(s_ca);
        s_ca = nullptr;
    }
    start();
    return nullptr;
}

const char* state_name(State s) {
    switch (s) {
        case State::Disabled: return "disabled";
        case State::Connecting: return "connecting";
        case State::Connected: return "connected";
    }
    return "disabled";
}

Status status() {
    portENTER_CRITICAL(&s_mux);
    const Status s = s_status;
    portEXIT_CRITICAL(&s_mux);
    return s;
}

bool connected() { return status().state == State::Connected; }

bool publish(const std::string& topic, const char* payload, int len, bool retain, int qos) {
    if (!s_mx) return false;
    Guard g(s_mx);
    if (!s_client || !connected()) return false;
    return esp_mqtt_client_publish(s_client, topic.c_str(), payload, len, qos, retain ? 1 : 0) >= 0;
}

esp_err_t register_handlers(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_STATE;
    http::route(server, "/api/v1/mqtt", HTTP_GET, get_mqtt);
    http::route(server, "/api/v1/mqtt", HTTP_PUT, put_mqtt);
    http::route(server, "/api/v1/mqtt/ca", HTTP_PUT, put_ca);
    http::route(server, "/api/v1/mqtt/ca", HTTP_DELETE, delete_ca);
    return ESP_OK;
}

}  // namespace lgal::mqtt
