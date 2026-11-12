#include "doser/backup_api.h"

#include <cstring>
#include <memory>
#include <new>
#include <string>

#include "doser/backup_json.h"
#include "doser/dosing.h"
#include "doser/event_source.h"
#include "doser/history.h"
#include "doser/hwconfig.h"
#include "doser/motion.h"
#include "doser/schedule_json.h"
#include "doser/scheduler.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "lgal/auth.h"
#include "lgal/device.h"
#include "lgal/http.h"
#include "lgal/json.h"
#include "lgal/mqtt.h"
#include "lgal/net.h"
#include "lgal/settings.h"
#include "lgal/timesync.h"

namespace doser::backup_api {

namespace {
constexpr const char* TAG = "backup";
constexpr size_t kMaxBody = 48 * 1024;
constexpr int kMaxTokens = 3072;

using lgal::Json;
namespace http = lgal::http;
namespace bk = backup;

void collect(bk::Data* d) {
    std::snprintf(d->language, sizeof(d->language), "%s", lgal::settings::language());
    std::snprintf(d->timezone, sizeof(d->timezone), "%s", lgal::timesync::timezone());
    std::snprintf(d->ntp_server, sizeof(d->ntp_server), "%s", lgal::timesync::ntp_server());
    d->led = lgal::settings::led_night();
    d->names = true;
    std::snprintf(d->name, sizeof(d->name), "%s", lgal::device::name().c_str());
    std::snprintf(d->own_hostname, sizeof(d->own_hostname), "%s", lgal::device::own_hostname().c_str());
    d->net = lgal::net::config();
    for (int i = 0; i < kMaxPumps; ++i) {
        d->pumps[i] = dosing::settings(i);
        d->schedules[i] = scheduler::schedule(i);
        d->hw[i] = motion::config(i);
    }
    d->locks = scheduler::locks();
    d->dosing = dosing::engineering();
    d->scheduler = scheduler::config();
    d->history_keep_days = history::keep_days();
    d->fitted = hwconfig::stored_fitted_mask();
    d->mqtt = lgal::mqtt::config();
    d->mqtt_ca = lgal::mqtt::ca_pem();
}

esp_err_t get_backup(httpd_req_t* req) {
    if (!http::authorize_engineering(req)) return ESP_OK;
    int64_t secrets = 0;
    http::query_int(req, "secrets", &secrets);
    auto d = std::unique_ptr<bk::Data>(new (std::nothrow) bk::Data);
    if (!d) return http::send_error(req, "500 Internal Server Error", "internal_error", "Out of memory");
    collect(d.get());
    d->parts = bk::kSystem | bk::kNetwork | bk::kPumps | bk::kEngineering | bk::kMqtt | (secrets ? bk::kAuth : 0);
    d->wlan_password = secrets != 0;
    d->mqtt_password = secrets != 0;
    const auto ts = lgal::timesync::status();
    const std::string host = lgal::device::hostname();
    Json j;
    bk::write(j, *d, {esp_app_get_description()->version, host.c_str(), ts.valid ? ts.utc : 0},
              [](Json& jj) { lgal::auth::write_secrets(jj); });
    char disp[96];
    char date[11] = "";
    if (ts.valid) sched_json::fmt_date(scheduler::today(), date);
    std::snprintf(disp, sizeof(disp), "attachment; filename=\"%s%s%s%s.json\"", host.c_str(), date[0] ? "-" : "", date,
                  secrets ? "-secrets" : "");
    httpd_resp_set_hdr(req, "Content-Disposition", disp);
    ESP_LOGI(TAG, "Backup exported (%s secrets)", secrets ? "with" : "without");
    return http::send_json(req, j.c_str());
}

void write_applied(Json& j, uint8_t applied) {
    j.arr("applied");
    for (int b = 0; b < 8; ++b)
        if ((applied & (1 << b)) && bk::part_str(static_cast<bk::Part>(1 << b))[0]) j.v(bk::part_str(static_cast<bk::Part>(1 << b)));
    j.end();
}

esp_err_t apply_failed(httpd_req_t* req, bk::Part part, const char* code, uint8_t applied) {
    ESP_LOGE(TAG, "Restore of %s failed: %s", bk::part_str(part), code);
    Json j;
    j.obj().obj("error").kv("code", code).kv("message", "Restore failed while applying").kv("part", bk::part_str(part));
    write_applied(j, applied);
    j.end().end();
    return http::send_json(req, j.c_str(), std::strcmp(code, "conflict") == 0 ? "409 Conflict" : "500 Internal Server Error");
}

esp_err_t post_backup(httpd_req_t* req) {
    if (!http::authorize_engineering(req)) return ESP_OK;
    uint8_t want = bk::kAllParts;
    char parts[96];
    if (http::query_str(req, "parts", parts, sizeof(parts)) && !bk::parse_parts(parts, &want))
        return http::send_error(req, "400 Bad Request", "invalid_param", "parts");
    if (dosing::busy()) return http::send_error(req, "409 Conflict", "pump_busy", "A pump is running");

    std::string text;
    lgal::JsonReader doc;
    std::unique_ptr<lgal::JsonToken[]> tokens(new (std::nothrow) lgal::JsonToken[kMaxTokens]);
    auto base = std::unique_ptr<bk::Data>(new (std::nothrow) bk::Data);
    auto d = std::unique_ptr<bk::Data>(new (std::nothrow) bk::Data);
    if (!tokens || !base || !d) return http::send_error(req, "500 Internal Server Error", "internal_error", "Out of memory");
    if (!http::read_json(req, kMaxBody, &text, &doc, tokens.get(), kMaxTokens)) return ESP_OK;

    collect(base.get());
    bk::Error err;
    if (!bk::parse(doc.root(), *base, scheduler::today(), d.get(), &err)) {
        Json j;
        j.obj().obj("error").kv("code", err.code).kv("message", "Backup document rejected").kv("field", err.field).end().end();
        return http::send_json(req, j.c_str(), "400 Bad Request");
    }
    const uint8_t todo = d->parts & want;
    if (!todo) return http::send_error(req, "400 Bad Request", "nothing_to_restore", "No selected part in the document");
    // values only the device can check
    if (todo & bk::kSystem) {
        const char* field = !lgal::settings::language_supported(d->language)       ? "system.language"
                            : !lgal::timesync::timezone_valid(d->timezone)         ? "system.timezone"
                            : d->ntp_server[0] && !lgal::timesync::ntp_server_valid(d->ntp_server) ? "system.ntp_server"
                                                                                   : nullptr;
        if (field) {
            Json j;
            j.obj().obj("error").kv("code", "invalid_param").kv("message", "Backup document rejected").kv("field", field).end().end();
            return http::send_json(req, j.c_str(), "400 Bad Request");
        }
    }

    uint8_t applied = 0;
    if (todo & bk::kEngineering) {
        if (const char* e = dosing::set_engineering(d->dosing)) return apply_failed(req, bk::kEngineering, e, applied);
        if (const char* e = scheduler::set_config(d->scheduler)) return apply_failed(req, bk::kEngineering, e, applied);
        for (int i = 0; i < kMaxPumps; ++i) {
            const esp_err_t e = motion::set_config(i, d->hw[i]);
            if (e == ESP_ERR_INVALID_ARG || e == ESP_ERR_INVALID_STATE)
                return apply_failed(req, bk::kEngineering, e == ESP_ERR_INVALID_STATE ? "pump_busy" : "invalid_param", applied);
            hwconfig::save_pump(i, d->hw[i]);  // also when the driver did not answer
        }
        // channel population: in effect after the next restart
        if (d->fitted && hwconfig::save_fitted(d->fitted) != ESP_OK)
            return apply_failed(req, bk::kEngineering, "storage_error", applied);
        if (d->history_keep_days && history::set_keep_days(d->history_keep_days) != ESP_OK)
            return apply_failed(req, bk::kEngineering, "storage_error", applied);
        applied |= bk::kEngineering;
    }
    if (todo & bk::kSystem) {
        if (lgal::settings::set_language(d->language) != ESP_OK || lgal::timesync::set_timezone(d->timezone) != ESP_OK ||
            lgal::timesync::set_ntp_server(d->ntp_server) != ESP_OK || lgal::settings::set_led_night(d->led) != ESP_OK ||
            (d->names && lgal::device::set_names(d->name, d->own_hostname) != ESP_OK))
            return apply_failed(req, bk::kSystem, "storage_error", applied);
        if (d->names) lgal::net::apply_hostname();
        applied |= bk::kSystem;
    }
    if (todo & bk::kPumps) {
        for (int i = 0; i < kMaxPumps; ++i)
            if (const char* e = dosing::set_settings(i, d->pumps[i])) return apply_failed(req, bk::kPumps, e, applied);
        // schedules and lock rules are checked together; on a conflict they stay as they were
        if (const char* e = scheduler::set_all(d->schedules, d->locks)) return apply_failed(req, bk::kPumps, e, applied);
        applied |= bk::kPumps;
    }
    bool net_changed = false;
    if (todo & bk::kNetwork) {
        const auto cur = lgal::net::config();
        Json a, b;
        lgal::netcfg::write(a.obj(), cur);
        lgal::netcfg::write(b.obj(), d->net);
        net_changed = a.str() != b.str() || std::strcmp(cur.password, d->net.password) != 0;
        if (net_changed && lgal::net::set_config(d->net, 2000) != ESP_OK)
            return apply_failed(req, bk::kNetwork, "invalid_param", applied);
        applied |= bk::kNetwork;
    }
    if (todo & bk::kMqtt) {
        // a new certificate first, so that the settings may use it; an old one last
        const std::string cur_ca = lgal::mqtt::ca_pem();
        if (!d->mqtt_ca.empty() && d->mqtt_ca != cur_ca)
            if (const char* e = lgal::mqtt::set_ca(d->mqtt_ca.data(), d->mqtt_ca.size()))
                return apply_failed(req, bk::kMqtt, e, applied);
        if (lgal::mqtt::set_config(d->mqtt) != ESP_OK) return apply_failed(req, bk::kMqtt, "storage_failed", applied);
        if (d->mqtt_ca.empty() && !cur_ca.empty())
            if (const char* e = lgal::mqtt::clear_ca()) return apply_failed(req, bk::kMqtt, e, applied);
        applied |= bk::kMqtt;
    }
    bool signed_out = false;
    if (todo & bk::kAuth) {
        if (const char* e = lgal::auth::restore_secrets(doc.root()["auth"])) return apply_failed(req, bk::kAuth, e, applied);
        applied |= bk::kAuth;
        signed_out = true;
    }
    ESP_LOGW(TAG, "Backup restored (parts 0x%02x)", applied);
    note_system(req, "restored", applied);
    Json j;
    j.obj().kv("status", "ok");
    write_applied(j, applied);
    j.kv("network_changed", net_changed).kv("signed_out", signed_out).end();
    return http::send_json(req, j.c_str());
}
}  // namespace

esp_err_t register_handlers(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_STATE;
    http::route(server, "/api/v1/backup", HTTP_GET, get_backup);
    http::route(server, "/api/v1/backup", HTTP_POST, post_backup);
    return ESP_OK;
}

}  // namespace doser::backup_api
