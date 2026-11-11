#include "doser/scheduler.h"

#include <cinttypes>
#include <cstring>

#include "doser/history.h"
#include "doser/schedule_store.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lgal/timesync.h"

namespace doser::scheduler {

namespace {
constexpr const char* TAG = "sched";
constexpr int kN = sched::kPumps;
constexpr int kTickMs = 1000;
// Every plan has a slot within this time if it has one at all ("every 30 days")
constexpr int64_t kLookAhead = int64_t(sched::kMaxInterval + 1) * 86400;

struct PumpRt {
    sched::PumpSchedule sch;
    sched::RunState run;
    bool run_loaded = false;
    // slot that is due but could not start yet
    bool pending = false;
    sched::Due due{};
    uint32_t share = 0;  // boost share taken for it
    int64_t pending_since = 0;
    int64_t retry_at = 0;
    const char* delay_reason = nullptr;
    // queued manual dose (lock policy "queue")
    bool queued = false;
    uint32_t q_ul = 0;
    dosing::Trigger q_trigger = dosing::Trigger::Manual;
    int q_tier = -1;
    int64_t q_since = 0;
    // statistics (RAM)
    const char* last_skip_reason = nullptr;
    int64_t last_skip_at = 0;
    int64_t last_run_at = 0;
};

SemaphoreHandle_t s_mx = nullptr;
PumpRt s_rt[kN];
sched::Config s_cfg;
sched::Locks s_locks;
tz::Zone s_zone;
char s_tz[64] = "";
bool s_ready = false;  // cursors initialized with a valid clock
// scratch for validation (protected by s_mx, too large for small stacks)
sched::PumpSchedule s_all[kN];

struct Guard {
    Guard() { xSemaphoreTake(s_mx, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(s_mx); }
};

bool valid_pump(int i) { return i >= 0 && i < kN && s_mx; }

bool is(const char* a, const char* b) { return a && std::strcmp(a, b) == 0; }

// Reasons to wait instead of skipping
bool is_wait(const char* e) { return is(e, "collision_lock") || is(e, "pump_busy") || is(e, "locked"); }

void refresh_zone() {
    const char* tz = lgal::timesync::timezone();
    if (!tz || !*tz || std::strncmp(tz, s_tz, sizeof(s_tz)) == 0) return;
    std::strncpy(s_tz, tz, sizeof(s_tz) - 1);
    if (!tz::parse(tz, &s_zone)) {
        s_zone = tz::Zone{};
        ESP_LOGW(TAG, "Time zone '%s' not understood, schedules run on UTC", tz);
    }
}

int32_t local_day(int64_t utc) { return sched::day_of(tz::to_local(s_zone, utc)); }

void save(int p) { schedule_store::save_run(p, s_rt[p].run); }

void log_skip(int p, const char* reason, uint32_t ul, int count, int64_t now) {
    history::Event e;
    e.utc = static_cast<uint32_t>(now);
    e.kind = history::Kind::Skip;
    e.pump = static_cast<uint8_t>(p + 1);
    e.source = history::Source::Schedule;
    e.code = history::code_of(reason);
    e.target_ul = ul;
    e.value = count;
    history::record(e);
}

void skip(int p, const char* reason, const sched::Due& d, int64_t now) {
    PumpRt& r = s_rt[p];
    r.run.cursor = sched::cursor_of(d);
    sched::boost_pass(r.run.boost, d.utc, 1);
    ++r.run.skipped;
    r.last_skip_reason = reason;
    r.last_skip_at = now;
    save(p);
    log_skip(p, reason, d.ul, 1, now);
    ESP_LOGW(TAG, "Pump %d: slot %02u:%02u (%" PRIu32 " ul) skipped: %s", p + 1, d.minute / 60, d.minute % 60, d.ul,
             reason);
}

// New schedule or first valid clock: slots before now never existed
void start_from_now(PumpRt& r, int64_t now) {
    const sched::Cursor c{now, UINT32_MAX};
    if (r.run.cursor.utc < c.utc) r.run.cursor = c;
}

// First slot not processed yet: the pending one, otherwise from now on
sched::Cursor open_from(const PumpRt& r, int64_t now) {
    sched::Cursor from = r.pending ? r.run.cursor : sched::Cursor{now, UINT32_MAX};
    if (from.utc < r.run.cursor.utc || (from.utc == r.run.cursor.utc && from.tag < r.run.cursor.tag))
        from = r.run.cursor;
    return from;
}

// Slots of the plan from `from` up to the end of `until`
int slots_until(const PumpRt& r, const sched::Cursor& from, int64_t until) {
    return sched::count_due(r.sch, s_zone, s_cfg.grid_min, from, until);
}

bool has_slot(const PumpRt& r, const sched::Cursor& from) {
    sched::Due d;
    return sched::next_due(r.sch, s_zone, s_cfg.grid_min, from, from.utc + kLookAhead, &d);
}

// Boost window after a schedule change or a cursor reset: the slots still to
// come; a window in doses keeps its count while the plan has slots at all
void recount_boost(PumpRt& r) {
    sched::Boost& b = r.run.boost;
    if (!b.remaining_ul) return;
    sched::boost_recount(b, b.days ? slots_until(r, r.run.cursor, sched::boost_end(b)) : has_slot(r, r.run.cursor));
}

// End of the boost window: for a window in doses the time of its last slot
// still open (0 = none found)
int64_t boost_until(const PumpRt& r, int64_t now) {
    const sched::Boost& b = r.run.boost;
    if (b.days) return sched::boost_end(b);
    sched::Cursor c = open_from(r, now);
    sched::Due d;
    int64_t at = 0;
    for (int i = 0; i < b.slots_left; ++i) {
        if (!sched::next_due(r.sch, s_zone, s_cfg.grid_min, c, c.utc + kLookAhead, &d)) break;
        at = d.utc;
        c = sched::cursor_of(d);
    }
    return at;
}

// Boost of today's plan: the even share for each slot of today inside the
// boost window (those already dosed got about the same)
uint32_t boost_today(const PumpRt& r, int32_t day, int64_t until) {
    const sched::Boost& b = r.run.boost;
    const uint32_t share = sched::boost_share(b);
    if (!share || until <= 0) return 0;
    static sched::Slot sl[sched::kMaxSlotsPerDay];  // with s_mx held
    const int n = sched::day_slots(r.sch, day, s_cfg.grid_min, sl, sched::kMaxSlotsPerDay);
    const int64_t from = tz::to_local(s_zone, b.start_utc), to = tz::to_local(s_zone, until);
    uint32_t k = 0;
    for (int i = 0; i < n; ++i) {
        const int64_t at = int64_t(day) * 86400 + sl[i].minute * 60;
        if (at > from && at <= to) ++k;
    }
    return share * k;
}

void init_cursors(int64_t now) {
    for (int p = 0; p < kN; ++p) {
        PumpRt& r = s_rt[p];
        if (!r.run_loaded || r.run.cursor.utc == 0) {
            start_from_now(r, now);
        } else if (r.run.cursor.utc > now + 86400) {
            // cursor far in the future: the clock was wrong when it was written
            ESP_LOGW(TAG, "Pump %d: schedule cursor in the future, restarting from now", p + 1);
            r.run.cursor = sched::Cursor{now, UINT32_MAX};
            recount_boost(r);
        }
        save(p);
    }
    s_ready = true;
}

void try_start(int p, int64_t now) {
    PumpRt& r = s_rt[p];
    if (r.retry_at > now && r.retry_at != INT64_MAX && is(r.delay_reason, "collision_lock")) return;
    int64_t retry = 0;
    const uint32_t ul = r.due.ul + r.share;
    const char* e = dosing::dose(p, ul, r.share ? dosing::Trigger::Boost : dosing::Trigger::Schedule, -1, &retry);
    if (!e) {
        r.run.cursor = sched::cursor_of(r.due);
        sched::boost_pass(r.run.boost, r.due.utc, 1);
        r.pending = false;
        r.delay_reason = nullptr;
        r.last_run_at = now;
        save(p);
        if (now - r.pending_since > 1)
            ESP_LOGI(TAG, "Pump %d: delayed slot started after %" PRId64 " s", p + 1, now - r.pending_since);
        return;
    }
    if (is_wait(e)) {
        if (now - r.pending_since > int64_t(s_cfg.max_delay_min) * 60) {
            sched::boost_refund(r.run.boost, r.share);
            r.pending = false;
            r.delay_reason = nullptr;
            skip(p, "delay_timeout", r.due, now);
            return;
        }
        if (!is(r.delay_reason, e)) ESP_LOGI(TAG, "Pump %d: slot delayed (%s)", p + 1, e);
        r.delay_reason = e;
        r.retry_at = is(e, "collision_lock") ? retry : 0;
        return;
    }
    sched::boost_refund(r.run.boost, r.share);
    r.pending = false;
    r.delay_reason = nullptr;
    if (is(e, "pump_manual_only") || is(e, "pump_disabled")) {
        // deliberate mode, not a fault: no statistics, no warning
        r.run.cursor = sched::cursor_of(r.due);
        sched::boost_pass(r.run.boost, r.due.utc, 1);
        save(p);
        return;
    }
    skip(p, e, r.due, now);
}

void process(int p, int64_t now) {
    PumpRt& r = s_rt[p];
    if (r.queued) {
        if (now - r.q_since > int64_t(s_cfg.max_delay_min) * 60) {
            r.queued = false;
            ESP_LOGW(TAG, "Pump %d: queued dose dropped (lock time did not end)", p + 1);
        } else {
            const char* e = dosing::dose(p, r.q_ul, r.q_trigger, r.q_tier, nullptr);
            if (is_wait(e)) return;  // the schedule waits behind the queued dose
            r.queued = false;
            if (e) ESP_LOGW(TAG, "Pump %d: queued dose failed: %s", p + 1, e);
            else ESP_LOGI(TAG, "Pump %d: queued dose started", p + 1);
            return;
        }
    }
    if (r.pending) {
        try_start(p, now);
        return;
    }
    // After a long outage everything older than the policy window is skipped at once
    // The cursor only moves to the last skipped slot: without slots to skip
    // nothing changes and nothing is written (the window moves every second).
    const sched::Cursor rp = sched::resume_point(r.sch, r.run.cursor, now);
    sched::Due d;
    if (rp.utc != r.run.cursor.utc || rp.tag != r.run.cursor.tag) {
        int n = 0;
        sched::Cursor c = r.run.cursor;
        while (sched::next_due(r.sch, s_zone, s_cfg.grid_min, c, rp.utc, &d)) {
            ++n;
            c = sched::cursor_of(d);
        }
        if (n) {
            r.run.cursor = c;
            sched::boost_pass(r.run.boost, c.utc, n);
            r.run.skipped += n;
            r.last_skip_reason = "missed";
            r.last_skip_at = now;
            save(p);
            log_skip(p, "missed", 0, n, now);
            ESP_LOGW(TAG, "Pump %d: %d missed slots skipped", p + 1, n);
        }
    }
    if (!sched::next_due(r.sch, s_zone, s_cfg.grid_min, r.run.cursor, now, &d)) return;
    if (sched::on_due(r.sch, d, now) == sched::Action::SkipMissed) {
        skip(p, "missed", d, now);
        return;
    }
    r.due = d;
    r.share = sched::boost_take(r.run.boost, d.utc);
    r.pending = true;
    r.pending_since = now;
    r.delay_reason = nullptr;
    r.retry_at = 0;
    try_start(p, now);
}

void tick() {
    const auto ts = lgal::timesync::status();
    if (!ts.valid) return;
    Guard g;
    refresh_zone();
    if (!s_ready) init_cursors(ts.utc);
    for (int p = 0; p < kN; ++p) process(p, ts.utc);
}

void task(void*) {
    while (true) {
        tick();
        vTaskDelay(pdMS_TO_TICKS(kTickMs));
    }
}

// Validation of all schedules with one of them or the locks replaced.
// Expects s_all to be filled; call with s_mx held.
const char* check_all(const sched::Locks& locks, const sched::Config& cfg, const sched::Context& base,
                      SaveResult* out) {
    sched::Context ctx = base;
    ctx.grid_min = cfg.grid_min;
    SaveResult local;
    SaveResult& res = out ? *out : local;
    res.validation = sched::validate(s_all, locks, ctx, res.conflicts, kMaxConflicts);
    return res.validation.error;
}

// Takes over a saved schedule (with s_mx): pending slot dropped, cursor from now
void adopt(int pump, const sched::PumpSchedule& s, const lgal::timesync::Status& ts) {
    PumpRt& rt = s_rt[pump];
    rt.sch = s;
    if (rt.pending) {
        sched::boost_refund(rt.run.boost, rt.share);
        rt.pending = false;
    }
    if (ts.valid) {
        start_from_now(rt, ts.utc);
        recount_boost(rt);
        save(pump);
    }
    ESP_LOGI(TAG, "Pump %d: schedule saved (%s)", pump + 1, sched::kind_str(s.kind));
}

// Context from the dosing engine (call without s_mx: takes the dosing mutex)
sched::Context make_context() {
    sched::Context ctx;
    for (int q = 0; q < kN; ++q) {
        ctx.run[q] = dosing::run_params(q);
        ctx.daily_limit_ul[q] = dosing::settings(q).daily_limit_ul;
    }
    ctx.min_dose_ul = dosing::engineering().min_dose_ul;
    return ctx;
}
}  // namespace

// ---------------------------------------------------------------------------
esp_err_t init() {
    if (s_mx) return ESP_OK;
    s_mx = xSemaphoreCreateMutex();
    if (!s_mx) return ESP_ERR_NO_MEM;
    schedule_store::load_config(&s_cfg);
    schedule_store::load_locks(&s_locks);
    int plans = 0;
    for (int p = 0; p < kN; ++p) {
        schedule_store::load_schedule(p, &s_rt[p].sch);
        s_rt[p].run_loaded = schedule_store::load_run(p, &s_rt[p].run);
        if (s_rt[p].sch.kind != sched::Kind::None) ++plans;
    }
    dosing::set_locks(s_locks);
    refresh_zone();
    ESP_LOGI(TAG, "Ready: %d plans, %u lock rules, grid %u min", plans, s_locks.count, s_cfg.grid_min);
    if (xTaskCreate(task, "sched", 6144, nullptr, 5, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

const char* set_schedule(int pump, const sched::PumpSchedule& s, SaveResult* r) {
    if (!valid_pump(pump)) return "invalid_param";
    sched::Context ctx = make_context();
    const auto ts = lgal::timesync::status();
    Guard g;
    ctx.today = ts.valid ? local_day(ts.utc) : 0;
    for (int q = 0; q < kN; ++q) s_all[q] = s_rt[q].sch;
    s_all[pump] = s;
    if (const char* e = check_all(s_locks, s_cfg, ctx, r)) return e;
    if (schedule_store::save_schedule(pump, s) != ESP_OK) return "storage_error";
    adopt(pump, s, ts);
    return nullptr;
}

sched::PumpSchedule schedule(int pump) {
    if (!valid_pump(pump)) return {};
    Guard g;
    return s_rt[pump].sch;
}

const char* set_locks(const sched::Locks& l, SaveResult* r) {
    if (!s_mx) return "invalid_param";
    if (l.manual > sched::ManualDuringLock::Queue) return "invalid_param";
    sched::Context ctx = make_context();
    const auto ts = lgal::timesync::status();
    Guard g;
    ctx.today = ts.valid ? local_day(ts.utc) : 0;
    for (int q = 0; q < kN; ++q) s_all[q] = s_rt[q].sch;
    if (const char* e = check_all(l, s_cfg, ctx, r)) return e;
    if (schedule_store::save_locks(l) != ESP_OK) return "storage_error";
    s_locks = l;
    dosing::set_locks(l);
    ESP_LOGI(TAG, "Lock rules saved (%u), manual doses during a lock: %s", l.count,
             sched::manual_during_lock_str(l.manual));
    return nullptr;
}

sched::Locks locks() {
    if (!s_mx) return {};
    Guard g;
    return s_locks;
}

const char* set_all(const sched::PumpSchedule (&all)[kN], const sched::Locks& l, SaveResult* r) {
    if (!s_mx) return "invalid_param";
    if (l.manual > sched::ManualDuringLock::Queue) return "invalid_param";
    sched::Context ctx = make_context();
    const auto ts = lgal::timesync::status();
    Guard g;
    ctx.today = ts.valid ? local_day(ts.utc) : 0;
    for (int q = 0; q < kN; ++q) s_all[q] = all[q];
    if (const char* e = check_all(l, s_cfg, ctx, r)) return e;
    for (int q = 0; q < kN; ++q)
        if (schedule_store::save_schedule(q, all[q]) != ESP_OK) return "storage_error";
    if (schedule_store::save_locks(l) != ESP_OK) return "storage_error";
    for (int q = 0; q < kN; ++q) adopt(q, all[q], ts);
    s_locks = l;
    dosing::set_locks(l);
    ESP_LOGI(TAG, "All schedules and %u lock rules replaced", l.count);
    return nullptr;
}

const char* set_config(const sched::Config& c) {
    if (!s_mx) return "invalid_param";
    if (sched::validate(c)) return "invalid_param";
    sched::Context ctx = make_context();
    const auto ts = lgal::timesync::status();
    Guard g;
    ctx.today = ts.valid ? local_day(ts.utc) : 0;
    // existing plans must still fit the new grid
    for (int q = 0; q < kN; ++q) s_all[q] = s_rt[q].sch;
    if (const char* e = check_all(s_locks, c, ctx, nullptr)) return e;
    if (schedule_store::save_config(c) != ESP_OK) return "storage_error";
    s_cfg = c;
    return nullptr;
}

sched::Config config() {
    if (!s_mx) return {};
    Guard g;
    return s_cfg;
}

const char* boost_start(int pump, uint32_t total_ul, uint16_t days, uint16_t doses) {
    if (!valid_pump(pump) || (days > 0) == (doses > 0) || days > sched::kMaxInterval || doses > sched::kMaxBoostDoses)
        return "invalid_param";
    const auto eng = dosing::engineering();
    if (total_ul < eng.min_dose_ul || total_ul > 10 * dose::kMaxDoseUl) return "invalid_param";
    const auto ts = lgal::timesync::status();
    if (!ts.valid) return "time_invalid";
    Guard g;
    PumpRt& r = s_rt[pump];
    refresh_zone();
    // manual-only and disabled pumps never run the plan
    const bool aut = dosing::settings(pump).mode == dose::Mode::Auto;
    const sched::Cursor from = open_from(r, ts.utc);
    const int n = !aut ? 0 : days ? slots_until(r, from, ts.utc + int64_t(days) * 86400) : has_slot(r, from) ? doses : 0;
    if (n <= 0) return "no_slots";
    sched::boost_start(r.run.boost, total_ul, days, ts.utc, n);
    save(pump);
    if (days) ESP_LOGI(TAG, "Pump %d: boost %" PRIu32 " ul over %u days", pump + 1, total_ul, days);
    else ESP_LOGI(TAG, "Pump %d: boost %" PRIu32 " ul over %u doses", pump + 1, total_ul, doses);
    return nullptr;
}

void boost_stop(int pump) {
    if (!valid_pump(pump)) return;
    Guard g;
    s_rt[pump].run.boost = sched::Boost{};
    save(pump);
}

const char* manual_dose(int pump, uint32_t ul, dosing::Trigger trigger, int tier, int64_t* retry_at, bool* queued) {
    if (queued) *queued = false;
    if (!valid_pump(pump)) return "invalid_param";
    const char* e = dosing::dose(pump, ul, trigger, tier, retry_at);
    if (!is(e, "collision_lock")) return e;
    Guard g;
    if (s_locks.manual != sched::ManualDuringLock::Queue) return e;
    PumpRt& r = s_rt[pump];
    if (r.queued) return "queue_full";
    r.queued = true;
    r.q_ul = ul;
    r.q_trigger = trigger;
    r.q_tier = tier;
    r.q_since = lgal::timesync::status().utc;
    if (queued) *queued = true;
    ESP_LOGI(TAG, "Pump %d: dose of %" PRIu32 " ul queued until the lock time ends", pump + 1, ul);
    return nullptr;
}

void cancel_queued(int pump) {
    if (!valid_pump(pump)) return;
    Guard g;
    s_rt[pump].queued = false;
}

int32_t today() {
    const auto ts = lgal::timesync::status();
    if (!ts.valid || !s_mx) return -1;
    Guard g;
    refresh_zone();
    return local_day(ts.utc);
}

int64_t to_local(int64_t utc) {
    if (!s_mx) return utc;
    Guard g;
    refresh_zone();
    return tz::to_local(s_zone, utc);
}

Status status(int pump) {
    Status s{};
    if (!valid_pump(pump)) return s;
    const auto set = dosing::settings(pump);
    const int32_t remaining_ul = dosing::status(pump).state.remaining_ul;
    const auto ts = lgal::timesync::status();
    Guard g;
    const PumpRt& r = s_rt[pump];
    s.clock_valid = ts.valid;
    s.delayed = r.pending && r.delay_reason;
    s.delayed_since = r.pending ? r.pending_since : 0;
    s.retry_at = r.retry_at;
    s.delay_reason = r.delay_reason;
    s.queued = r.queued;
    s.queued_ul = r.q_ul;
    s.queued_since = r.q_since;
    s.skipped = r.run.skipped;
    s.last_skip_reason = r.last_skip_reason;
    s.last_skip_at = r.last_skip_at;
    s.last_run_at = r.last_run_at;
    s.boost = r.run.boost;
    s.remaining_days = -1.0f;
    if (!ts.valid) return s;
    refresh_zone();
    const int32_t day = local_day(ts.utc);
    s.boost_active = sched::boost_active(r.run.boost, ts.utc);
    sched::Due d;
    const sched::Cursor from = open_from(r, ts.utc);
    // manual-only and disabled pumps never run the plan: no next dose
    if (set.mode == dose::Mode::Auto && sched::next_due(r.sch, s_zone, s_cfg.grid_min, from, ts.utc + 8 * 86400, &d)) {
        s.has_next = true;
        s.next_utc = d.utc;
        s.next_ul = d.ul;
        s.next_boost_ul = d.utc <= sched::boost_end(r.run.boost) ? sched::boost_share(r.run.boost) : 0;
    }
    if (r.run.boost.remaining_ul) s.boost_until = boost_until(r, ts.utc);
    if (set.mode == dose::Mode::Auto) {
        s.planned_today_ul = sched::day_total(r.sch, day);
        s.boost_today_ul = boost_today(r, day, s.boost_until);
        if (set.capacity_ul)
            s.remaining_days = sched::remaining_days(r.sch, r.run.boost, s.boost_until, day, ts.utc, remaining_ul);
    }
    return s;
}

}  // namespace doser::scheduler
