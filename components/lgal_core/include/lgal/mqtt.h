#pragma once
// MQTT client (M6): one broker connection, optionally over TLS (public CA
// bundle or an uploaded CA certificate). Last will "<base>/status" =
// "offline" (retained), "online" after each connect. Reconnects on its own.
// The application publishes its data and handles commands through the
// callbacks (doser: doser_app/mqtt_ha).
//
// REST (admin):
//   GET    /api/v1/mqtt      settings (without password), status, CA info
//   PUT    /api/v1/mqtt      settings, partial (see mqtt_config.h)
//   PUT    /api/v1/mqtt/ca   CA certificate(s) as PEM text (body)
//   DELETE /api/v1/mqtt/ca   remove the CA certificate

#include <cstddef>
#include <cstdint>
#include <string>

#include "esp_err.h"
#include "esp_http_server.h"
#include "lgal/mqtt_config.h"

namespace lgal::mqtt {

// Connection handed to the connect callback (MQTT task): subscribe and
// publish there through this object, never through mqtt::publish(), and
// read the settings of the running connection from it (no locks).
class Session {
public:
    explicit Session(void* client);
    bool publish(const std::string& topic, const char* payload, int len, bool retain, int qos = 0);
    bool subscribe(const std::string& topic, int qos = 0);
    const std::string& base() const;                // base topic
    const mqttcfg::Config& settings() const;        // without password

private:
    void* client_;
};

// Callbacks run in the MQTT task; keep them short and do not call
// mqtt::publish() or the settings functions from them.
using ConnectHandler = void (*)(Session& s);  // after each connect
using DataHandler = void (*)(const char* topic, int topic_len, const char* data, int data_len);
void set_handlers(ConnectHandler on_connect, DataHandler on_data);  // before init

esp_err_t init();  // after settings::init, device::init and net::init

// Contains the password: never send it out
mqttcfg::Config config();
// Saves and restarts the client (validated by the caller: mqttcfg::parse)
esp_err_t set_config(const mqttcfg::Config& c);
// Base topic in use ("" in the settings = "lg-aquatic-life/<device type>/<factory
// host name>", so it stays when the device gets a name). After a change of the
// base topic the client deletes the retained messages below the old one.
std::string base_topic();

// CA certificate for tls "custom" (PEM, stored in NVS)
struct CaInfo {
    bool present = false;
    int count = 0;           // certificates in the chain
    char subject[96] = "";   // of the first certificate
    char valid_to[11] = "";  // YYYY-MM-DD
};
CaInfo ca_info();
bool ca_present();
// nullptr or "invalid_certificate", "storage_failed"
const char* set_ca(const char* pem, size_t len);
// nullptr or "ca_in_use" (enabled with tls "custom"), "storage_failed"
const char* clear_ca();
// PEM text ("" without certificate), for the backup
std::string ca_pem();

enum class State : uint8_t { Disabled, Connecting, Connected };
const char* state_name(State s);
struct Status {
    State state = State::Disabled;
    // last connection error: "dns", "tcp", "timeout", "tls", "auth", "refused", nullptr
    const char* error = nullptr;
    int64_t since_ms = 0;  // state since (esp_timer ms)
    uint32_t connects = 0;
};
Status status();
bool connected();

// Topic below the base topic: "pump/1/state" -> "<base>/pump/1/state"
std::string topic(const char* suffix);
// From application tasks (blocks while sending); false when not connected
bool publish(const std::string& topic, const char* payload, int len, bool retain, int qos = 0);

esp_err_t register_handlers(httpd_handle_t server);

}  // namespace lgal::mqtt
