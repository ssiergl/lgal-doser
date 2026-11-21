// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

#include "doser/history_api.h"

#include <cstdint>
#include <cstring>
#include <string>

#include "doser/dosing.h"
#include "doser/event_source.h"
#include "doser/history.h"
#include "doser/history_json.h"
#include "doser/schedule_core.h"
#include "doser/schedule_json.h"
#include "doser/scheduler.h"
#include "lgal/http.h"
#include "lgal/json.h"
#include "lgal/timesync.h"

namespace doser::history_api {

namespace {
using lgal::Json;
namespace http = lgal::http;
namespace hj = history_json;

constexpr int64_t kMaxLimit = 500;

// Period as local calendar days: from_date=YYYY-MM-DD&to_date=YYYY-MM-DD, both inclusive,
// at most kMaxDays days. 0: not given, 1: *first/*last set, -1: error already sent
int read_dates(httpd_req_t* req, int32_t* first, int32_t* last) {
    char a[16], b[16];
    const bool has_a = http::query_str(req, "from_date", a, sizeof(a));
    const bool has_b = http::query_str(req, "to_date", b, sizeof(b));
    if (!has_a && !has_b) return 0;
    if (!has_a || !has_b || !sched_json::parse_date(a, first) || !sched_json::parse_date(b, last) ||
        *last < *first || *last - *first >= hj::kMaxDays) {
        http::send_error(req, "400 Bad Request", "invalid_param", "from_date..to_date (YYYY-MM-DD, at most 90 days)");
        return -1;
    }
    return 1;
}

// Filter from the query; false: error already sent
bool read_filter(httpd_req_t* req, history::Filter* f) {
    int64_t v = 0;
    if (http::query_int(req, "pump", &v)) {
        if (v < 0 || v > kMaxPumps) {
            const std::string range = "pump=0.." + std::to_string(kMaxPumps);
            http::send_error(req, "400 Bad Request", "invalid_param", range.c_str());
            return false;
        }
        f->pump = static_cast<uint8_t>(v);
    }
    char kinds[96];
    if (http::query_str(req, "kind", kinds, sizeof(kinds)) && !hj::parse_kinds(kinds, &f->kinds)) {
        http::send_error(req, "400 Bad Request", "invalid_param", "kind");
        return false;
    }
    if (http::query_int(req, "days", &v)) {
        const auto ts = lgal::timesync::status();
        if (v < 1 || v > hj::kMaxDays) {
            http::send_error(req, "400 Bad Request", "invalid_param", "days=1..90");
            return false;
        }
        if (ts.valid) {
            // from local midnight (v - 1) days ago
            const int64_t local = scheduler::to_local(ts.utc);
            const int64_t since_midnight = local - int64_t(sched::day_of(local)) * 86400;
            const int64_t from = ts.utc - since_midnight - (v - 1) * 86400;
            f->from = from > 0 ? static_cast<uint32_t>(from) : 0;
        }
    }
    int32_t first = 0, last = 0;
    const int dates = read_dates(req, &first, &last);
    if (dates < 0) return false;
    if (dates > 0) {
        // local midnight of the first day up to the last second before the day after the last
        const int64_t from = scheduler::to_utc(int64_t(first) * 86400);
        const int64_t to = scheduler::to_utc(int64_t(last + 1) * 86400) - 1;
        f->from = from < 0 ? 0 : from > UINT32_MAX ? UINT32_MAX : uint32_t(from);
        f->to = to < 0 ? 0 : to > UINT32_MAX ? UINT32_MAX : uint32_t(to);
    }
    if (http::query_int(req, "from", &v)) f->from = v < 0 ? 0 : v > UINT32_MAX ? UINT32_MAX : uint32_t(v);
    if (http::query_int(req, "to", &v)) f->to = v < 0 ? 0 : v > UINT32_MAX ? UINT32_MAX : uint32_t(v);
    return true;
}

esp_err_t get_history(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    history::Filter f;
    if (!read_filter(req, &f)) return ESP_OK;
    int64_t limit = 100, offset = 0;
    if (http::query_int(req, "limit", &limit) && (limit < 1 || limit > kMaxLimit))
        return http::send_error(req, "400 Bad Request", "invalid_param", "limit=1..500");
    if (http::query_int(req, "offset", &offset) && offset < 0)
        return http::send_error(req, "400 Bad Request", "invalid_param", "offset");
    struct Ctx {
        Json j;
        int64_t skip, left;
        bool more = false;
    } c{{}, offset, limit};
    c.j.obj().arr("events");
    history::query(f, [](const history::Event& e, void* p) {
        auto* c = static_cast<Ctx*>(p);
        if (c->skip > 0) {
            --c->skip;
            return true;
        }
        if (c->left == 0) {
            c->more = true;  // one beyond the page
            return false;
        }
        --c->left;
        hj::write_event(c->j, e, scheduler::to_local(e.utc));
        return true;
    }, &c);
    c.j.end().kv("more", c.more).end();
    return http::send_json(req, c.j.c_str());
}

esp_err_t get_csv(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    history::Filter f;
    if (!read_filter(req, &f)) return ESP_OK;
    httpd_resp_set_type(req, "text/csv; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"doser-history.csv\"");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    struct Ctx {
        httpd_req_t* req;
        char buf[2048];
        size_t len = 0;
        bool failed = false;
        bool flush() {
            if (len && httpd_resp_send_chunk(req, buf, len) != ESP_OK) failed = true;
            len = 0;
            return !failed;
        }
    };
    auto* ctx = new Ctx{};  // 2 KB buffer: off the httpd task stack
    ctx->req = req;
    const char* head = hj::csv_header();
    std::memcpy(ctx->buf, head, std::strlen(head));
    ctx->len = std::strlen(head);
    history::query(f, [](const history::Event& e, void* p) {
        auto* c = static_cast<Ctx*>(p);
        if (sizeof(c->buf) - c->len < 256 && !c->flush()) return false;
        c->len += hj::csv_line(e, scheduler::to_local(e.utc), c->buf + c->len, sizeof(c->buf) - c->len);
        return true;
    }, ctx);
    ctx->flush();
    const bool failed = ctx->failed;
    delete ctx;
    if (failed) return ESP_FAIL;
    return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t get_daily(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    int64_t days = 14;
    if (http::query_int(req, "days", &days) && (days < 1 || days > hj::kMaxDays))
        return http::send_error(req, "400 Bad Request", "invalid_param", "days=1..90");
    int32_t first = 0, last = 0;
    const int dates = read_dates(req, &first, &last);
    if (dates < 0) return ESP_OK;
    const int32_t today = scheduler::today();
    Json j;
    j.obj().kv("keep_days", static_cast<int>(history::keep_days()));
    if (today < 0) {
        j.null("today").arr("days").end().end();
        return http::send_json(req, j.c_str());
    }
    auto* d = new hj::Daily;
    if (dates > 0) d->reset(last, last - first + 1);
    else d->reset(today, static_cast<int>(days));
    history::Filter f;
    // a day earlier in UTC: local days may start before the UTC day
    const int64_t from = (int64_t(d->first_day) - 1) * 86400;
    f.from = from > 0 ? static_cast<uint32_t>(from) : 0;
    if (dates > 0) f.to = static_cast<uint32_t>((int64_t(last) + 2) * 86400);  // a day later in UTC, too
    f.kinds = static_cast<uint16_t>((1u << static_cast<uint8_t>(history::Kind::Dose)) |
                                    (1u << static_cast<uint8_t>(history::Kind::Skip)));
    history::query(f, [](const history::Event& e, void* p) {
        static_cast<hj::Daily*>(p)->add(e, scheduler::to_local(e.utc));
        return true;
    }, d);
    char date[11];
    sched_json::fmt_date(today, date);
    j.kv("today", date);
    d->write(j);
    delete d;
    j.end();
    return http::send_json(req, j.c_str());
}

esp_err_t delete_history(httpd_req_t* req) {
    if (!http::authorize_engineering(req)) return ESP_OK;
    if (history::clear() != ESP_OK)
        return http::send_error(req, "500 Internal Server Error", "storage_error", "History not available");
    // "Today" on the pages comes from the dosing ledger and the skip counter: start them
    // again too, so that nothing refers to the cleared history any more
    dosing::clear_today();
    scheduler::clear_skipped();
    note_system(req, "cleared");  // the first entry of the new history
    return http::send_json(req, "{\"status\":\"ok\"}");
}
void write_settings(Json& j) {
    j.obj()
        .kv("keep_days", static_cast<int>(history::keep_days()))
        .kv("min_days", static_cast<int>(history::kKeepDaysMin))
        .kv("max_days", static_cast<int>(history::kKeepDaysMax))
        .end();
}

esp_err_t get_settings(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    Json j;
    write_settings(j);
    return http::send_json(req, j.c_str());
}

esp_err_t post_settings(httpd_req_t* req) {
    if (!http::authorize_engineering(req)) return ESP_OK;
    int64_t v = 0;
    if (!http::query_int(req, "keep_days", &v) || v < history::kKeepDaysMin || v > history::kKeepDaysMax)
        return http::send_error(req, "400 Bad Request", "invalid_param", "keep_days=30..90");
    if (history::set_keep_days(static_cast<uint16_t>(v)) != ESP_OK)
        return http::send_error(req, "500 Internal Server Error", "storage_error", "Could not save");
    note_system(req, "engineering");
    Json j;
    write_settings(j);
    return http::send_json(req, j.c_str());
}
}  // namespace

esp_err_t register_handlers(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_STATE;
    http::route(server, "/api/v1/history", HTTP_GET, get_history);
    http::route(server, "/api/v1/history.csv", HTTP_GET, get_csv);
    http::route(server, "/api/v1/history/daily", HTTP_GET, get_daily);
    http::route(server, "/api/v1/history", HTTP_DELETE, delete_history);
    http::route(server, "/api/v1/history/settings", HTTP_GET, get_settings);
    http::route(server, "/api/v1/history/settings", HTTP_POST, post_settings);
    return ESP_OK;
}

}  // namespace doser::history_api
