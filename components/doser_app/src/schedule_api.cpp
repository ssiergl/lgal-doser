#include "doser/schedule_api.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>

#include "doser/schedule_json.h"
#include "doser/scheduler.h"
#include "lgal/http.h"
#include "lgal/json.h"
#include "lgal/json_reader.h"
#include "lgal/timesync.h"


namespace doser::schedule_api {

namespace {
using lgal::Json;
namespace http = lgal::http;
namespace sj = sched_json;

constexpr size_t kMaxBody = 8192;
constexpr int kMaxTokens = 256;  // a plan with 24 times needs ~110

struct ErrInfo {
    const char* code;
    const char* status;
    const char* message;
};

constexpr ErrInfo kErrors[] = {
    {"invalid_param", "400 Bad Request", "Invalid parameter"},
    {"invalid_time", "400 Bad Request", "Invalid time"},
    {"off_grid", "400 Bad Request", "Time is not on the scheduling grid"},
    {"invalid_range", "400 Bad Request", "End time before start time"},
    {"invalid_count", "400 Bad Request", "Invalid number of doses"},
    {"slots_too_dense", "400 Bad Request", "Doses closer together than the grid"},
    {"duplicate_time", "400 Bad Request", "The same time appears twice"},
    {"invalid_kind", "400 Bad Request", "Invalid plan kind"},
    {"dose_too_small", "400 Bad Request", "Single dose below the minimum"},
    {"dose_too_large", "400 Bad Request", "Single dose too large"},
    {"invalid_days", "400 Bad Request", "Invalid day selection"},
    {"invalid_catch_up", "400 Bad Request", "Catch-up window out of range (1..1440 min)"},
    {"invalid_lock", "400 Bad Request", "Invalid lock rule"},
    {"too_many_slots", "400 Bad Request", "Too many doses per day"},
    {"exceeds_daily_limit", "409 Conflict", "Planned amount exceeds the daily limit"},
    {"conflict", "409 Conflict", "Slots overlap or violate a lock time"},
    {"time_invalid", "409 Conflict", "No valid time"},
    {"no_slots", "409 Conflict", "No scheduled doses in the boost period"},
    {"storage_error", "500 Internal Server Error", "Could not save"},
};

const ErrInfo& info(const char* code) {
    for (const auto& e : kErrors)
        if (std::strcmp(e.code, code) == 0) return e;
    static constexpr ErrInfo kInternal{"internal_error", "500 Internal Server Error", "Internal error"};
    return kInternal;
}

esp_err_t fail(httpd_req_t* req, const char* code, const char* detail = nullptr) {
    const ErrInfo& e = info(code);
    return http::send_error(req, e.status, code, detail ? detail : e.message);
}

// Input error with position: {"error":{"code","message","entry","rule","field"}}
esp_err_t fail_input(httpd_req_t* req, const sj::Error& err) {
    const ErrInfo& e = info(err.code ? err.code : "invalid_param");
    Json j;
    j.obj().obj("error").kv("code", err.code ? err.code : "invalid_param").kv("message", e.message);
    if (err.entry >= 0) j.kv("entry", err.entry + 1);
    if (err.rule >= 0) j.kv("rule", err.rule + 1);
    if (err.field) j.kv("field", err.field);
    j.end().end();
    return http::send_json(req, j.c_str(), e.status);
}

// Rejected by the validation of all schedules
esp_err_t fail_validation(httpd_req_t* req, const char* code, const scheduler::SaveResult& r) {
    const ErrInfo& e = info(code);
    Json j;
    j.obj().obj("error").kv("code", code).kv("message", e.message);
    const sched::Validation& v = r.validation;
    if (v.error && std::strcmp(code, "conflict") != 0) {
        if (v.pump >= 0) j.kv("pump", v.pump + 1);
        if (v.entry >= 0) j.kv("entry", v.entry + 1);
        if (v.rule >= 0) j.kv("rule", v.rule + 1);
    }
    if (v.conflicts > 0) {
        j.arr("conflicts");
        for (int i = 0; i < v.conflicts && i < scheduler::kMaxConflicts; ++i) sj::write_conflict(j, r.conflicts[i]);
        j.end();
    }
    j.end().end();
    return http::send_json(req, j.c_str(), e.status);
}

int pump_arg(httpd_req_t* req) {
    int64_t p = 0;
    if (!http::query_int(req, "pump", &p) || p < 1 || p > sched::kPumps) return -1;
    return static_cast<int>(p - 1);
}

double ml(int64_t ul) { return ul / 1000.0; }

// Instant as UTC seconds plus device-local "YYYY-MM-DD HH:MM" (the browser
// may be in another time zone): key + key_local, or null
void write_instant(Json& j, const char* key, const char* local_key, int64_t utc) {
    if (utc <= 0 || utc == INT64_MAX) {
        j.null(key).null(local_key);
        return;
    }
    char loc[17];
    sj::fmt_local(scheduler::to_local(utc), loc);
    j.kv(key, utc).kv(local_key, loc);
}

// Parsed request body; tokens on the heap (8 KB, too large for the httpd stack)
struct Body {
    std::string text;
    lgal::JsonReader doc;
    std::unique_ptr<lgal::JsonToken[]> tokens{new (std::nothrow) lgal::JsonToken[kMaxTokens]};
};

bool read_body(httpd_req_t* req, Body* b) {
    if (!b->tokens) {
        fail(req, "internal_error", "Out of memory");
        return false;
    }
    return http::read_json(req, kMaxBody, &b->text, &b->doc, b->tokens.get(), kMaxTokens);
}

// ---------------------------------------------------------------------------
esp_err_t send_schedule(httpd_req_t* req, int p) {
    const sched::PumpSchedule s = scheduler::schedule(p);
    const int32_t day = scheduler::today();
    Json j;
    j.obj().kv("pump", p + 1).kv("grid_min", scheduler::config().grid_min);  // times must be on this grid
    sj::write_schedule(j, s);
    if (day >= 0) {
        char d[11], t[6];
        sj::fmt_date(day, d);
        sched::Slot slots[sched::kMaxSlotsPerDay];
        const int n = sched::day_slots(s, day, scheduler::config().grid_min, slots, sched::kMaxSlotsPerDay);
        j.obj("today").kv("date", d).kv("ml", ml(sched::day_total(s, day)), 3).arr("slots");
        for (int i = 0; i < n; ++i) {
            sj::fmt_hhmm(slots[i].minute, t);
            j.obj().kv("time", t).kv("ml", ml(slots[i].ul), 3).end();
        }
        j.end().end();
    } else {
        j.null("today");
    }
    j.end();
    return http::send_json(req, j.c_str());
}

esp_err_t get_schedule(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    const int p = pump_arg(req);
    if (p < 0) return fail(req, "invalid_param", "pump=1..5 required");
    return send_schedule(req, p);
}

esp_err_t put_schedule(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    const int p = pump_arg(req);
    if (p < 0) return fail(req, "invalid_param", "pump=1..5 required");
    Body b;
    if (!read_body(req, &b)) return ESP_OK;
    sched::PumpSchedule s;
    sj::Error err;
    if (!sj::parse_schedule(b.doc.root(), scheduler::today(), &s, &err)) return fail_input(req, err);
    scheduler::SaveResult r;
    if (const char* e = scheduler::set_schedule(p, s, &r)) return fail_validation(req, e, r);
    return send_schedule(req, p);
}

// ---------------------------------------------------------------------------
esp_err_t send_locks(httpd_req_t* req) {
    Json j;
    j.obj();
    sj::write_locks(j, scheduler::locks());
    j.end();
    return http::send_json(req, j.c_str());
}

esp_err_t get_locks(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    return send_locks(req);
}

esp_err_t put_locks(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    Body b;
    if (!read_body(req, &b)) return ESP_OK;
    sched::Locks l;
    sj::Error err;
    if (!sj::parse_locks(b.doc.root(), &l, &err)) return fail_input(req, err);
    scheduler::SaveResult r;
    if (const char* e = scheduler::set_locks(l, &r)) return fail_validation(req, e, r);
    return send_locks(req);
}

// ---------------------------------------------------------------------------
esp_err_t post_boost(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    const int p = pump_arg(req);
    if (p < 0) return fail(req, "invalid_param", "pump=1..5 required");
    char action[8] = "";
    http::query_str(req, "action", action, sizeof(action));
    if (!std::strcmp(action, "stop")) {
        scheduler::boost_stop(p);
        return http::send_json(req, "{\"status\":\"ok\"}");
    }
    if (action[0]) return fail(req, "invalid_param", "action=stop or ml= with days= or doses=");
    int64_t ul = 0, days = 0, doses = 0;
    if (!http::query_decimal(req, "ml", 1000, &ul) || ul <= 0 || ul > UINT32_MAX)
        return fail(req, "invalid_param", "ml required");
    const bool by_days = http::query_int(req, "days", &days), by_doses = http::query_int(req, "doses", &doses);
    if (by_days == by_doses) return fail(req, "invalid_param", "days= or doses= required");
    if (by_days && (days < 1 || days > sched::kMaxInterval)) return fail(req, "invalid_param", "days=1..30 required");
    if (by_doses && (doses < 1 || doses > sched::kMaxBoostDoses))
        return fail(req, "invalid_param", "doses=1..100 required");
    if (const char* e = scheduler::boost_start(p, static_cast<uint32_t>(ul), static_cast<uint16_t>(days),
                                               static_cast<uint16_t>(doses)))
        return fail(req, e);
    return http::send_json(req, "{\"status\":\"ok\"}");
}

// ---------------------------------------------------------------------------
void write_status(Json& j, int p) {
    const scheduler::Status s = scheduler::status(p);
    j.obj().kv("pump", p + 1);
    if (s.has_next) {
        j.obj("next");
        write_instant(j, "at", "local", s.next_utc);
        // ml includes the expected boost share, boost_ml names it
        j.kv("ml", ml(s.next_ul + s.next_boost_ul), 3);
        if (s.next_boost_ul) j.kv("boost_ml", ml(s.next_boost_ul), 3);
        j.end();
    } else {
        j.null("next");
    }
    if (s.delayed) {
        j.obj("delayed").kv("reason", s.delay_reason);
        write_instant(j, "since", "since_local", s.delayed_since);
        write_instant(j, "retry_at", "retry_local", s.retry_at);  // null: other pump still running
        j.end();
    } else {
        j.null("delayed");
    }
    if (s.queued) {
        j.obj("queued").kv("ml", ml(s.queued_ul), 3);
        write_instant(j, "since", "since_local", s.queued_since);
        j.end();
    } else {
        j.null("queued");
    }
    j.kv("skipped", s.skipped);
    if (s.last_skip_reason) {
        j.obj("last_skip").kv("reason", s.last_skip_reason);
        write_instant(j, "at", "local", s.last_skip_at);
        j.end();
    } else {
        j.null("last_skip");
    }
    if (s.last_run_at > 0) {
        j.obj("last_run");
        write_instant(j, "at", "local", s.last_run_at);
        j.end();
    } else {
        j.null("last_run");
    }
    j.kv("planned_today_ml", ml(s.planned_today_ul), 3);
    if (s.remaining_days >= 0.0f) j.kv("remaining_days", static_cast<double>(s.remaining_days), 1);
    else j.null("remaining_days");
    if (s.boost.remaining_ul > 0) {
        char d[11];
        sj::fmt_date(sched::day_of(scheduler::to_local(s.boost.start_utc)), d);
        j.obj("boost")
            .kv("active", s.boost_active)
            .kv("total_ml", ml(s.boost.total_ul), 3)
            .kv("remaining_ml", ml(s.boost.remaining_ul), 3)
            .kv("days", s.boost.days)
            .kv("doses", s.boost.doses)
            .kv("start", d)
            .kv("doses_left", s.boost.slots_left)
            .kv("per_dose_ml", ml(sched::boost_share(s.boost)), 3)
            .kv("today_ml", ml(s.boost_today_ul), 3);
        write_instant(j, "start_utc", "start_local", s.boost.start_utc);
        if (s.boost_until > 0) write_instant(j, "end_utc", "end_local", s.boost_until);
        else j.null("end_utc").null("end_local");
        j.end();
    } else {
        j.null("boost");
    }
    j.end();
}

esp_err_t get_status(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    const auto ts = lgal::timesync::status();
    Json j;
    j.obj().kv("clock_valid", ts.valid);
    if (ts.valid) {
        char loc[17];
        sj::fmt_local(scheduler::to_local(ts.utc), loc);
        j.kv("now", ts.utc).kv("now_local", loc);
    }
    j.arr("pumps");
    for (int p = 0; p < sched::kPumps; ++p) write_status(j, p);
    j.end().end();
    return http::send_json(req, j.c_str());
}

// ---------------------------------------------------------------------------
esp_err_t get_engineering(httpd_req_t* req) {
    if (!http::authorize_engineering(req)) return ESP_OK;
    const sched::Config c = scheduler::config();
    Json j;
    j.obj().kv("grid_min", c.grid_min).kv("max_delay_min", c.max_delay_min).end();
    return http::send_json(req, j.c_str());
}

esp_err_t post_engineering(httpd_req_t* req) {
    if (!http::authorize_engineering(req)) return ESP_OK;
    sched::Config c = scheduler::config();
    int64_t v = 0;
    if (http::query_int(req, "grid_min", &v)) {
        if (v < 1 || v > 60) return fail(req, "invalid_param", "grid_min must divide 1440 (1..60)");
        c.grid_min = static_cast<uint16_t>(v);
    }
    if (http::query_int(req, "max_delay_min", &v)) {
        if (v < 1 || v > 720) return fail(req, "invalid_param", "max_delay_min=1..720");
        c.max_delay_min = static_cast<uint16_t>(v);
    }
    if (const char* f = sched::validate(c)) return fail(req, "invalid_param", f);
    if (const char* e = scheduler::set_config(c)) {
        if (!std::strcmp(e, "storage_error") || !std::strcmp(e, "invalid_param")) return fail(req, e);
        // existing schedules do not fit the new grid
        return http::send_error(req, "409 Conflict", e, "Existing schedules do not fit the new parameters");
    }
    return get_engineering(req);
}
}  // namespace

esp_err_t register_handlers(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_STATE;
    http::route(server, "/api/v1/pumps/schedule", HTTP_GET, get_schedule);
    http::route(server, "/api/v1/pumps/schedule", HTTP_PUT, put_schedule);
    http::route(server, "/api/v1/pumps/boost", HTTP_POST, post_boost);
    http::route(server, "/api/v1/schedule/status", HTTP_GET, get_status);
    http::route(server, "/api/v1/schedule/locks", HTTP_GET, get_locks);
    http::route(server, "/api/v1/schedule/locks", HTTP_PUT, put_locks);
    http::route(server, "/api/v1/engineering/scheduler", HTTP_GET, get_engineering);
    http::route(server, "/api/v1/engineering/scheduler", HTTP_POST, post_engineering);
    return ESP_OK;
}

}  // namespace doser::schedule_api
