#include "doser/dosing_api.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "doser/dosing.h"
#include "doser/hwconfig.h"
#include "doser/motion.h"
#include "doser/schedule_json.h"
#include "doser/scheduler.h"
#include "esp_log.h"
#include "lgal/http.h"
#include "lgal/json.h"
#include "lgal/ws.h"

namespace doser::dosing_api {

namespace {
using lgal::Json;
using lgal::JsonValue;
namespace http = lgal::http;

struct ErrInfo {
    const char* code;
    const char* status;
    const char* message;
};

constexpr ErrInfo kErrors[] = {
    {"invalid_param", "400 Bad Request", "Invalid parameter"},
    {"dose_too_small", "400 Bad Request", "Dose below the minimum"},
    {"dose_too_large", "400 Bad Request", "Dose too large"},
    {"measured_out_of_range", "400 Bad Request", "Measured volume is implausible (0.25x..4x nominal)"},
    {"no_steps", "400 Bad Request", "Calibration run has no steps"},
    {"pump_busy", "409 Conflict", "Pump is busy"},
    {"pump_disabled", "409 Conflict", "Pump is disabled"},
    {"pump_manual_only", "409 Conflict", "Pump is set to manual only"},
    {"locked", "409 Conflict", "Pumps are locked (update in progress)"},
    {"daily_limit", "409 Conflict", "Daily limit would be exceeded"},
    {"time_invalid", "409 Conflict", "No valid time - daily limit cannot be checked"},
    {"driver_unavailable", "409 Conflict", "Driver not reachable or not configured"},
    {"speed_out_of_range", "400 Bad Request", "Speed out of range"},
    {"not_priming", "409 Conflict", "Pump is not priming"},
    {"no_calibration_run", "409 Conflict", "No finished calibration run"},
    {"collision_lock", "409 Conflict", "Lock time to another pump is running"},
    {"queue_full", "409 Conflict", "A dose is already waiting for the lock time to end"},
    {"storage_error", "500 Internal Server Error", "Could not save"},
    {"internal_error", "500 Internal Server Error", "Internal error"},
};

esp_err_t fail(httpd_req_t* req, const char* code, const char* detail = nullptr) {
    for (const auto& e : kErrors) {
        if (std::strcmp(e.code, code) == 0)
            return http::send_error(req, e.status, code, detail ? detail : e.message);
    }
    return http::send_error(req, "500 Internal Server Error", code, detail ? detail : code);
}

esp_err_t ok(httpd_req_t* req) { return http::send_json(req, "{\"status\":\"ok\"}"); }

int pump_arg(httpd_req_t* req) {
    int64_t p = 0;
    if (!http::query_int(req, "pump", &p) || p < 1 || p > 5) return -1;
    return static_cast<int>(p - 1);
}

int tier_arg(httpd_req_t* req) {
    int64_t t = -1;
    if (!http::query_int(req, "tier", &t) || t < 0 || t >= dose::kTiers) return -1;
    return static_cast<int>(t);
}

double ml(int64_t ul) { return ul / 1000.0; }

void write_pump(Json& j, int i) {
    const auto s = dosing::status(i);
    const auto& set = s.settings;
    const auto& st = s.state;
    const auto eng = dosing::engineering();
    const float upr = dose::ul_per_rev(set);
    const uint16_t rmrev = dose::retract_mrev(set, eng);

    j.obj()
        .kv("pump", i + 1)
        .kv("name", set.name)
        .kv("mode", dose::mode_str(set.mode))
        .kv("direction", motion::config(i).driver.invert ? "ccw" : "cw")
        .kv("activity", dosing::activity_str(s.activity))
        .kv("phase", dosing::phase_str(s.phase));
    if (s.activity != dosing::Activity::Idle) {
        j.kv("trigger", dosing::trigger_str(s.trigger))
            .kv("target_ml", ml(s.target_ul), 3)
            .kv("done_ml", ml(s.done_ul), 3)
            .kv("rpm", static_cast<double>(s.rpm), 1);
        if (s.activity == dosing::Activity::Priming) j.kv("prime_s", s.prime_ms / 1000.0, 1);
    }
    j.obj("last");
    if (s.last_result) {
        j.kv("result", s.last_result)
            .kv("activity", dosing::activity_str(s.last_activity))
            .kv("target_ml", ml(s.last_target_ul), 3)
            .kv("done_ml", ml(s.last_done_ul), 3)
            .kv("at", s.last_at);
    }
    j.end();

    j.obj("calibration")
        .kv("calibrated", set.ul_per_rev > 0.0f)
        .kv("ml_per_rev", upr / 1000.0, 4)
        .kv("steps_per_ml", s.steps_per_rev ? s.steps_per_rev * 1000.0 / upr : 0.0, 1)
        .kv("calibrated_at", set.calibrated_at)
        .kv("pending", s.calib_pending);
    if (s.calib_pending) j.kv("pending_steps", s.calib_steps).kv("pending_target_ml", ml(s.calib_target_ul), 3);
    j.end();

    j.obj("speed").kv("tier", static_cast<int>(set.speed_tier)).arr("tiers");
    for (int t = 0; t < dose::kTiers; ++t)
        j.obj().kv("rpm", eng.tier_rpm[t]).kv("ml_min", static_cast<double>(dose::flow_ml_min(eng.tier_rpm[t], upr)), 1).end();
    j.end().end();

    // setting: off (factory default) | global (engineering value) | own
    j.obj("retract")
        .kv("setting", set.retract_mrev < 0 ? "global" : set.retract_mrev == 0 ? "off" : "own")
        .kv("global_rev", eng.retract_mrev / 1000.0, 3)
        .kv("rev", rmrev / 1000.0, 3)
        .kv("ml", rmrev * upr / 1.0e6, 3)
        .kv("steps", dose::mrev_to_steps(rmrev, s.steps_per_rev))
        .kv("pending_rev", st.retracted_mrev / 1000.0, 3)
        .end();

    j.obj("daily")
        .kv("limit_ml", ml(set.daily_limit_ul), 3)
        .kv("used_ml", ml(s.used_today_ul), 3)
        .kv("mode", dose::limit_mode_str(eng.limit_mode))
        .kv("time_valid", s.time_valid)
        .end();

    const int32_t rem = st.remaining_ul > 0 ? st.remaining_ul : 0;
    j.obj("container").kv("tracked", set.capacity_ul > 0).kv("capacity_ml", ml(set.capacity_ul), 1).kv("remaining_ml", ml(rem), 2);
    if (set.capacity_ul) j.kv("percent", 100.0 * rem / set.capacity_ul, 1);
    j.end();

    const double tube_h = st.tube_ms / 3.6e6;
    j.obj("tube")
        .kv("hours", tube_h, 2)
        .kv("limit_h", eng.tube_life_h)
        .kv("percent", 100.0 * tube_h / eng.tube_life_h, 1)
        .end();

    j.kv("total_ml", ml(static_cast<int64_t>(st.total_ul)), 3);
    j.end();
}

esp_err_t get_pumps(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    Json j;
    j.obj().arr("pumps");
    for (int i = 0; i < 5; ++i) write_pump(j, i);
    j.end().end();
    return http::send_json(req, j.c_str());
}

esp_err_t post_dose(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    const int p = pump_arg(req);
    int64_t ul = 0;
    if (p < 0) return fail(req, "invalid_param", "pump=1..5 required");
    if (!http::query_decimal(req, "ml", 1000, &ul) || ul <= 0 || ul > dose::kMaxDoseUl)
        return fail(req, "invalid_param", "ml required");
    // Through the scheduler: lock-time policy (reject or queue) applies
    int64_t retry_at = 0;
    bool queued = false;
    const char* e =
        scheduler::manual_dose(p, static_cast<uint32_t>(ul), dosing::Trigger::Api, tier_arg(req), &retry_at, &queued);
    if (e && !std::strcmp(e, "collision_lock")) {
        Json j;
        j.obj().obj("error").kv("code", e).kv("message", "Lock time to another pump is running");
        if (retry_at == INT64_MAX || retry_at <= 0) {
            j.null("retry_at").null("retry_local");  // other pump still running
        } else {
            char loc[17];
            sched_json::fmt_local(scheduler::to_local(retry_at), loc);
            j.kv("retry_at", retry_at).kv("retry_local", loc);
        }
        j.end().end();
        return http::send_json(req, j.c_str(), "409 Conflict");
    }
    if (e) return fail(req, e);
    if (queued) return http::send_json(req, "{\"status\":\"queued\"}", "202 Accepted");
    return http::send_json(req, "{\"status\":\"dosing\"}");
}

esp_err_t post_stop(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    int64_t p = 0;
    http::query_int(req, "pump", &p);
    if (p == 0) {
        for (int i = 0; i < 5; ++i) scheduler::cancel_queued(i);
        dosing::stop_all();
    } else if (p >= 1 && p <= 5) {
        scheduler::cancel_queued(static_cast<int>(p - 1));
        dosing::stop(static_cast<int>(p - 1));
    } else {
        return fail(req, "invalid_param", "pump=0..5");
    }
    return http::send_json(req, "{\"status\":\"stopping\"}");
}

esp_err_t post_prime(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    const int p = pump_arg(req);
    char action[8] = "";
    if (p < 0) return fail(req, "invalid_param", "pump=1..5 required");
    http::query_str(req, "action", action, sizeof(action));
    const char* e = nullptr;
    if (!std::strcmp(action, "start")) e = dosing::prime_start(p, tier_arg(req));
    else if (!std::strcmp(action, "beat")) e = dosing::prime_beat(p);
    else if (!std::strcmp(action, "stop")) dosing::stop(p);
    else return fail(req, "invalid_param", "action=start|beat|stop");
    return e ? fail(req, e) : ok(req);
}

esp_err_t post_calibrate(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    const int p = pump_arg(req);
    char action[8] = "";
    if (p < 0) return fail(req, "invalid_param", "pump=1..5 required");
    http::query_str(req, "action", action, sizeof(action));
    const char* e = nullptr;
    int64_t ul = 0;
    if (!std::strcmp(action, "start")) {
        if (http::query_decimal(req, "ml", 1000, &ul) && (ul < 1000 || ul > 200000))
            return fail(req, "invalid_param", "ml=1..200");
        e = dosing::calibrate_start(p, static_cast<uint32_t>(ul), tier_arg(req));
    } else if (!std::strcmp(action, "apply")) {
        if (!http::query_decimal(req, "measured_ml", 1000, &ul) || ul <= 0 || ul > 1000000)
            return fail(req, "invalid_param", "measured_ml required");
        e = dosing::calibrate_apply(p, static_cast<uint32_t>(ul));
    } else if (!std::strcmp(action, "cancel")) {
        dosing::calibrate_cancel(p);
    } else {
        return fail(req, "invalid_param", "action=start|apply|cancel");
    }
    return e ? fail(req, e) : ok(req);
}

esp_err_t post_settings(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    const int p = pump_arg(req);
    if (p < 0) return fail(req, "invalid_param", "pump=1..5 required");
    auto s = dosing::settings(p);
    char buf[64];
    int64_t v = 0;
    if (http::query_str(req, "name", buf, sizeof(buf))) {
        if (std::strlen(buf) >= sizeof(s.name)) return fail(req, "invalid_param", "name: max. 23 bytes");
        std::strcpy(s.name, buf);
    }
    if (http::query_str(req, "mode", buf, sizeof(buf)) && !dose::parse_mode(buf, &s.mode))
        return fail(req, "invalid_param", "mode=auto|manual_only|disabled");
    if (http::query_int(req, "tier", &v)) {
        if (v < 0 || v >= dose::kTiers) return fail(req, "invalid_param", "tier=0..2");
        s.speed_tier = static_cast<uint8_t>(v);
    }
    if (http::query_decimal(req, "daily_limit_ml", 1000, &v)) {
        if (v < 0 || v > 10000000) return fail(req, "invalid_param", "daily_limit_ml=0..10000");
        s.daily_limit_ul = static_cast<uint32_t>(v);
    }
    if (http::query_str(req, "retract_rev", buf, sizeof(buf))) {
        if (!std::strcmp(buf, "global")) {
            s.retract_mrev = -1;
        } else if (!std::strcmp(buf, "off")) {
            s.retract_mrev = 0;
        } else if (http::query_decimal(req, "retract_rev", 1000, &v) && v >= 0 && v <= 2000) {
            s.retract_mrev = static_cast<int16_t>(v);
        } else {
            return fail(req, "invalid_param", "retract_rev=0..2|off|global");
        }
    }
    // Direction is the driver's GCONF.shaft (DIR is wired to GND); it can
    // only change while the pump stands still.
    auto mc = motion::config(p);
    bool turn = false;
    if (http::query_str(req, "direction", buf, sizeof(buf))) {
        const bool ccw = std::strcmp(buf, "ccw") == 0;
        if (!ccw && std::strcmp(buf, "cw") != 0) return fail(req, "invalid_param", "direction=cw|ccw");
        turn = ccw != mc.driver.invert;
        mc.driver.invert = ccw;
        if (turn && motion::status(p).running) return fail(req, "pump_busy");
    }
    if (const char* e = dosing::set_settings(p, s)) return fail(req, e);
    if (turn) {
        const esp_err_t err = motion::set_config(p, mc);
        if (err == ESP_ERR_INVALID_STATE) return fail(req, "pump_busy");
        // Saved even if the driver does not answer: it gets the setting
        // when it is configured again.
        if (err != ESP_ERR_INVALID_ARG) hwconfig::save_pump(p, mc);
    }
    return ok(req);
}

esp_err_t post_container(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    const int p = pump_arg(req);
    if (p < 0) return fail(req, "invalid_param", "pump=1..5 required");
    int64_t rem = -1, cap = -1;
    if (http::query_decimal(req, "remaining_ml", 1000, &rem) && rem < 0) return fail(req, "invalid_param", "remaining_ml");
    if (http::query_decimal(req, "capacity_ml", 1000, &cap) && cap < 0) return fail(req, "invalid_param", "capacity_ml");
    if (rem < 0 && cap < 0) return fail(req, "invalid_param", "remaining_ml or capacity_ml required");
    if (const char* e = dosing::set_container(p, rem, cap)) return fail(req, e);
    return ok(req);
}

esp_err_t post_tube(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    const int p = pump_arg(req);
    int64_t h = 0;
    if (p < 0) return fail(req, "invalid_param", "pump=1..5 required");
    if (!http::query_int(req, "hours", &h) || h < 0) return fail(req, "invalid_param", "hours required");
    if (const char* e = dosing::set_tube_hours(p, static_cast<uint32_t>(h))) return fail(req, e);
    return ok(req);
}

void write_engineering(Json& j) {
    const auto e = dosing::engineering();
    j.obj()
        .kv("retract_rev", e.retract_mrev / 1000.0, 3)
        .kv("calib_ml", ml(e.calib_target_ul), 1)
        .arr("tier_rpm");
    for (int t = 0; t < dose::kTiers; ++t) j.v(static_cast<int>(e.tier_rpm[t]));
    j.end()
        .kv("prime_max_s", e.prime_max_s)
        .kv("prime_heartbeat_ms", e.prime_heartbeat_ms)
        .kv("min_dose_ml", ml(e.min_dose_ul), 3)
        .kv("tube_life_h", e.tube_life_h)
        .kv("limit_mode", dose::limit_mode_str(e.limit_mode))
        .kv("nominal_ml_per_rev", dose::kNominalUlPerRev / 1000.0, 3)
        .kv("max_rpm", 600)
        .end();
}

esp_err_t get_engineering(httpd_req_t* req) {
    if (!http::authorize_engineering(req)) return ESP_OK;
    Json j;
    write_engineering(j);
    return http::send_json(req, j.c_str());
}

esp_err_t post_engineering(httpd_req_t* req) {
    if (!http::authorize_engineering(req)) return ESP_OK;
    auto e = dosing::engineering();
    int64_t v = 0;
    char buf[16];
    if (http::query_decimal(req, "retract_rev", 1000, &v)) {
        if (v < 0 || v > 2000) return fail(req, "invalid_param", "retract_rev=0..2");
        e.retract_mrev = static_cast<uint16_t>(v);
    }
    if (http::query_decimal(req, "calib_ml", 1000, &v)) {
        if (v < 1000 || v > 200000) return fail(req, "invalid_param", "calib_ml=1..200");
        e.calib_target_ul = static_cast<uint32_t>(v);
    }
    const char* tier_keys[] = {"tier1_rpm", "tier2_rpm", "tier3_rpm"};
    for (int t = 0; t < dose::kTiers; ++t) {
        if (http::query_int(req, tier_keys[t], &v)) {
            if (v < 10 || v > 600) return fail(req, "invalid_param", "tier rpm=10..600");
            e.tier_rpm[t] = static_cast<uint16_t>(v);
        }
    }
    if (http::query_int(req, "prime_max_s", &v)) {
        if (v < 5 || v > 600) return fail(req, "invalid_param", "prime_max_s=5..600");
        e.prime_max_s = static_cast<uint16_t>(v);
    }
    if (http::query_int(req, "prime_heartbeat_ms", &v)) {
        if (v < 200 || v > 5000) return fail(req, "invalid_param", "prime_heartbeat_ms=200..5000");
        e.prime_heartbeat_ms = static_cast<uint16_t>(v);
    }
    if (http::query_decimal(req, "min_dose_ml", 1000, &v)) {
        if (v < 1 || v > 10000) return fail(req, "invalid_param", "min_dose_ml=0.001..10");
        e.min_dose_ul = static_cast<uint32_t>(v);
    }
    if (http::query_int(req, "tube_life_h", &v)) {
        if (v < 10 || v > 65535) return fail(req, "invalid_param", "tube_life_h=10..65535");
        e.tube_life_h = static_cast<uint16_t>(v);
    }
    if (http::query_str(req, "limit_mode", buf, sizeof(buf)) && !dose::parse_limit_mode(buf, &e.limit_mode))
        return fail(req, "invalid_param", "limit_mode=calendar_day|rolling_24h");
    if (dose::validate(e, 600)) return fail(req, "invalid_param", "tier rpm must be ascending");
    if (const char* err = dosing::set_engineering(e)) return fail(req, err);
    return get_engineering(req);
}

// ---------------------------------------------------------------------------
// WebSocket (lgal/ws.h)

// Live part of every pump for the status push: only what changes during
// operation, the full picture comes from GET /api/v1/pumps
void write_live(Json& j) {
    j.arr("pumps");
    for (int i = 0; i < 5; ++i) {
        const auto s = dosing::status(i);
        const auto& set = s.settings;
        j.obj()
            .kv("pump", i + 1)
            .kv("activity", dosing::activity_str(s.activity))
            .kv("phase", dosing::phase_str(s.phase));
        if (s.activity != dosing::Activity::Idle) {
            j.kv("trigger", dosing::trigger_str(s.trigger))
                .kv("target_ml", ml(s.target_ul), 3)
                .kv("done_ml", ml(s.done_ul), 3)
                .kv("rpm", static_cast<double>(s.rpm), 1);
            if (s.activity == dosing::Activity::Priming) j.kv("prime_s", s.prime_ms / 1000.0, 1);
        }
        const int32_t rem = s.state.remaining_ul > 0 ? s.state.remaining_ul : 0;
        j.kv("remaining_ml", ml(rem), 2);
        if (set.capacity_ul) j.kv("percent", 100.0 * rem / set.capacity_ul, 1);
        j.kv("used_today_ml", ml(s.used_today_ul), 3);
        if (s.last_result) {
            j.obj("last")
                .kv("result", s.last_result)
                .kv("activity", dosing::activity_str(s.last_activity))
                .kv("done_ml", ml(s.last_done_ul), 3)
                .kv("at", s.last_at)
                .end();
        }
        j.end();
    }
    j.end();
}

// Client that started priming per pump (-1 = none / REST). Only touched in
// the HTTP server task (messages and close hook), no lock needed.
int s_prime_owner[5] = {-1, -1, -1, -1, -1};

// {"type":"prime","pump":1..5,"action":"start|beat|stop","tier"?:0..2}
// Hold-to-run: start once, beat every ~150 ms while the button is held. A
// missing beat (prime_heartbeat_ms) or a closed connection stops the pump.
const char* ws_prime(int client, const JsonValue& msg, Json& reply) {
    int64_t pump = 0, tier = -1;
    char action[8] = "";
    if (!msg["pump"].get(&pump) || pump < 1 || pump > 5) return "invalid_param";
    msg["action"].get(action, sizeof(action));
    if (msg["tier"].valid() && (!msg["tier"].get(&tier) || tier < 0 || tier >= dose::kTiers)) return "invalid_param";
    const int p = static_cast<int>(pump - 1);
    reply.kv("pump", pump).kv("action", action);
    const char* e = nullptr;
    if (!std::strcmp(action, "start")) {
        e = dosing::prime_start(p, static_cast<int>(tier));
        if (!e) s_prime_owner[p] = client;
    } else if (!std::strcmp(action, "beat")) {
        e = dosing::prime_beat(p);
    } else if (!std::strcmp(action, "stop")) {
        e = dosing::prime_stop(p);
        s_prime_owner[p] = -1;
    } else {
        return "invalid_param";
    }
    lgal::ws::notify();
    return e;
}

void ws_closed(int client) {
    for (int p = 0; p < 5; ++p) {
        if (s_prime_owner[p] != client) continue;
        s_prime_owner[p] = -1;
        if (!dosing::prime_stop(p)) ESP_LOGW("dosing_api", "Pump %d: priming stopped (connection closed)", p + 1);
    }
}
}  // namespace

esp_err_t register_handlers(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_STATE;
    http::route(server, "/api/v1/pumps", HTTP_GET, get_pumps);
    http::route(server, "/api/v1/pumps/dose", HTTP_POST, post_dose);
    http::route(server, "/api/v1/pumps/stop", HTTP_POST, post_stop);
    http::route(server, "/api/v1/pumps/prime", HTTP_POST, post_prime);
    http::route(server, "/api/v1/pumps/calibrate", HTTP_POST, post_calibrate);
    http::route(server, "/api/v1/pumps/settings", HTTP_POST, post_settings);
    http::route(server, "/api/v1/pumps/container", HTTP_POST, post_container);
    http::route(server, "/api/v1/pumps/tube", HTTP_POST, post_tube);
    http::route(server, "/api/v1/engineering/dosing", HTTP_GET, get_engineering);
    http::route(server, "/api/v1/engineering/dosing", HTTP_POST, post_engineering);
    lgal::ws::set_status_writer(write_live);
    lgal::ws::on_message("prime", ws_prime);
    lgal::ws::on_close(ws_closed);
    return ESP_OK;
}

}  // namespace doser::dosing_api
