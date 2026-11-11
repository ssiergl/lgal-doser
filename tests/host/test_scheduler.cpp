// Host simulation of the scheduler (components/doser_app/src/scheduler.cpp)
// together with the real dosing engine. Motor, clock and storage are
// simulated; days pass in one-second steps (20 ms while a pump moves).

#include <cstdio>
#include <cstring>
#include <ctime>  // tzset

static int g_fail = 0;
static int g_checks = 0;
bool g_log_verbose = false;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)
#define CHECK_STR(a, b) CHECK((a) && std::strcmp((a), (b)) == 0)

#include "doser/civil_time.h"
#include "doser/schedule_store.h"
#include "doser/scheduler.h"
#include "sim.h"

// --- Store in memory --------------------------------------------------------
namespace store {
sched::Config cfg;
sched::Locks locks;
sched::PumpSchedule sch[5];
sched::RunState run[5];
bool have_run[5];
int run_saves[5];
}  // namespace store

namespace doser::schedule_store {
void load_config(sched::Config* c) { *c = store::cfg; }
esp_err_t save_config(const sched::Config& c) {
    store::cfg = c;
    return ESP_OK;
}
void load_locks(sched::Locks* l) { *l = store::locks; }
esp_err_t save_locks(const sched::Locks& l) {
    store::locks = l;
    return ESP_OK;
}
void load_schedule(int pump, sched::PumpSchedule* s) { *s = store::sch[pump]; }
esp_err_t save_schedule(int pump, const sched::PumpSchedule& s) {
    store::sch[pump] = s;
    return ESP_OK;
}
bool load_run(int pump, sched::RunState* s) {
    if (store::have_run[pump]) *s = store::run[pump];
    return store::have_run[pump];
}
esp_err_t save_run(int pump, const sched::RunState& s) {
    store::run[pump] = s;
    store::have_run[pump] = true;
    ++store::run_saves[pump];
    return ESP_OK;
}
}  // namespace doser::schedule_store

// ---------------------------------------------------------------------------
static tz::Zone g_zone;

// Local wall-clock time (CET/CEST) -> UTC
static int64_t local(int mo, int d, int h, int mi, int s = 0) {
    return tz::to_utc(g_zone, civil::to_unix({2026, mo, d, h, mi, s, 0}));
}

static sched::TimeDose single(uint16_t minute, uint32_t ul) { return sched::TimeDose{minute, 0, ul}; }

// "times" plan, daily
static sched::PumpSchedule plan(std::initializer_list<sched::TimeDose> times) {
    sched::PumpSchedule s;
    s.kind = sched::Kind::Times;
    for (const auto& t : times) s.times[s.time_count++] = t;
    return s;
}

// "spread" plan, daily
static sched::PumpSchedule spread(uint16_t from, uint16_t to, uint16_t n, uint32_t ul) {
    sched::PumpSchedule s;
    s.kind = sched::Kind::Spread;
    s.start_min = from;
    s.end_min = to;
    s.count = n;
    s.amount_ul = ul;
    return s;
}

static uint64_t total(int p) { return dosing::status(p).state.total_ul; }

static void test_basic() {
    // Friday 6 November, 13:30 local
    CHECK_STR(scheduler::set_schedule(0, plan({single(1440, 1000)})), "invalid_time");
    CHECK(scheduler::set_schedule(0, plan({single(480, 1000)})) == nullptr);  // daily 08:00, 1 ml
    sim::run_for(2);
    auto st = scheduler::status(0);
    CHECK(st.clock_valid && st.has_next && st.next_utc == local(11, 7, 8, 0) && st.next_ul == 1000);
    const uint64_t before = total(0);
    sim::run_until(local(11, 7, 7, 59, 59));
    CHECK(total(0) == before);
    sim::run_until(local(11, 7, 8, 0, 30));
    CHECK(total(0) == before + 1000);
    auto ds = dosing::status(0);
    CHECK_STR(ds.last_result, "success");
    CHECK(ds.last_at - local(11, 7, 8, 0) < 5);
    st = scheduler::status(0);
    CHECK(st.last_run_at == local(11, 7, 8, 0) && st.skipped == 0);
    CHECK(st.next_utc == local(11, 8, 8, 0));
    CHECK(store::run[0].cursor.utc == local(11, 7, 8, 0));  // persisted
}

static void test_conflicts_on_save() {
    sched::Locks l;
    l.count = 1;
    l.rules[0] = sched::LockRule{0, 1, 5};  // pump 1 <-> pump 2: 5 min
    CHECK(scheduler::set_locks(l) == nullptr);
    scheduler::SaveResult r;
    CHECK_STR(scheduler::set_schedule(1, plan({single(480, 2000)}), &r), "conflict");
    CHECK(r.validation.conflicts == 1);
    CHECK_STR(r.conflicts[0].code, "collision_lock");
    CHECK(r.conflicts[0].gap_min == 5 && r.conflicts[0].first.minute == 480);
    CHECK(scheduler::schedule(1).kind == sched::Kind::None);  // not saved
    CHECK(scheduler::set_schedule(1, plan({single(490, 2000)})) == nullptr);
    // a lock rule that contradicts existing schedules is rejected as well
    l.rules[0].gap_min = 15;
    CHECK_STR(scheduler::set_locks(l, &r), "conflict");
    CHECK(scheduler::locks().rules[0].gap_min == 5);
    // grid: 08:10 does not fit a 15 min grid
    sched::Config c;
    c.grid_min = 15;
    CHECK_STR(scheduler::set_config(c), "off_grid");
    c.grid_min = 7;
    CHECK_STR(scheduler::set_config(c), "invalid_param");
}

static void test_runtime_guard() {
    // Saturday 07:59:50: manual dose on pump 2 just before the 08:00 slot of pump 1
    sim::run_until(local(11, 8, 7, 59, 50));
    int64_t retry = 0;
    bool queued = false;
    CHECK(scheduler::manual_dose(1, 2000, dosing::Trigger::Manual, -1, &retry, &queued) == nullptr && !queued);
    const uint64_t before = total(0);
    sim::run_until(local(11, 8, 8, 1));
    const int64_t end1 = dosing::status(1).state.last_end;
    auto st = scheduler::status(0);
    CHECK(st.delayed && std::strcmp(st.delay_reason, "collision_lock") == 0);
    CHECK(st.retry_at == end1 + 300 && st.next_utc == local(11, 8, 8, 0));
    CHECK(total(0) == before);
    sim::run_until(end1 + 302);
    CHECK(total(0) == before + 1000);
    st = scheduler::status(0);
    CHECK(!st.delayed && st.last_run_at == end1 + 300 && st.skipped == 0);
    // pump 2's own slot at 08:10 is 5 min after the delayed dose: on time
    const uint64_t before1 = total(1);
    sim::run_until(local(11, 8, 8, 10, 30));
    CHECK(total(1) == before1 + 2000);
    CHECK(scheduler::status(1).last_run_at == local(11, 8, 8, 10));
}

static void test_delay_timeout() {
    // OTA lock from 07:59 on Monday: both slots wait and are dropped after 30 min
    auto c = scheduler::config();
    CHECK(c.max_delay_min == 120);  // factory default
    c.max_delay_min = 30;
    CHECK(scheduler::set_config(c) == nullptr);
    sim::run_until(local(11, 9, 7, 59));
    sim::locked = true;
    const uint64_t b0 = total(0), b1 = total(1);
    sim::run_until(local(11, 9, 8, 20));
    CHECK(scheduler::status(0).delayed && std::strcmp(scheduler::status(0).delay_reason, "locked") == 0);
    sim::run_until(local(11, 9, 8, 45));
    auto s0 = scheduler::status(0), s1 = scheduler::status(1);
    CHECK(!s0.delayed && s0.skipped == 1 && std::strcmp(s0.last_skip_reason, "delay_timeout") == 0);
    CHECK(s0.last_skip_at == local(11, 9, 8, 30, 1));
    CHECK(!s1.delayed && s1.skipped == 1 && std::strcmp(s1.last_skip_reason, "delay_timeout") == 0);
    CHECK(total(0) == b0 && total(1) == b1);
    auto* e = sim::last_event(history::Kind::Skip, 1);
    CHECK(e && e->source == history::Source::Schedule && e->value == 1 && e->target_ul > 0);
    CHECK(e && e->utc == uint32_t(local(11, 9, 8, 30, 1)) && std::strcmp(history::code_str(e->code), "delay_timeout") == 0);
    sim::locked = false;
    c.max_delay_min = 120;
    CHECK(scheduler::set_config(c) == nullptr);
}

static void test_power_loss() {
    // Device off from Monday 9:00 to Tuesday 10:00: both 08:00/08:10 slots missed
    sim::set_utc(local(11, 10, 10, 0));
    const uint64_t b0 = total(0);
    sim::run_for(3);
    auto s0 = scheduler::status(0);
    CHECK(s0.skipped == 2 && std::strcmp(s0.last_skip_reason, "missed") == 0 && total(0) == b0);
    auto* e = sim::last_event(history::Kind::Skip, 1);
    CHECK(e && std::strcmp(history::code_str(e->code), "missed") == 0);
    CHECK(scheduler::status(1).skipped == 2);
    CHECK(s0.next_utc == local(11, 11, 8, 0));
    // pump 1 catches up slots up to 3 h old
    auto p0 = scheduler::schedule(0);
    p0.power_loss = sched::PowerLoss::CatchUpWindow;
    p0.catch_up_min = 180;
    CHECK(scheduler::set_schedule(0, p0) == nullptr);
    sim::set_utc(local(11, 11, 9, 0));  // off again until Wednesday 9:00
    sim::run_for(10);
    CHECK(total(0) == b0 + 1000);                // 08:00 caught up at 09:00
    CHECK(scheduler::status(0).skipped == 2);
    CHECK(scheduler::status(1).skipped == 3);    // pump 2: skip_missed
    // long outage: 5 days, skipped at once
    const int saves = store::run_saves[1];
    sim::set_utc(local(11, 16, 12, 0));
    sim::run_for(3);
    CHECK(scheduler::status(1).skipped == 3 + 5);
    CHECK(store::run_saves[1] - saves == 1);  // one write for all skipped slots
    // idle hours without due slots write nothing (the policy window moves along)
    const int idle = store::run_saves[1];
    sim::run_for(3 * 3600);
    CHECK(store::run_saves[1] == idle);
    CHECK(scheduler::status(0).skipped == 2 + 5);  // 3 h window: nothing to catch up
}

static void test_schedule_change() {
    // Monday 16 Nov 12:00: a new slot at 09:00 has already passed today -> not run, not "missed"
    const uint64_t b0 = total(0);
    const uint32_t skipped = scheduler::status(0).skipped;
    auto p0 = scheduler::schedule(0);
    p0.times[p0.time_count++] = single(540, 1000);
    CHECK(scheduler::set_schedule(0, p0) == nullptr);
    sim::run_for(60);
    CHECK(total(0) == b0 && scheduler::status(0).skipped == skipped);
    sim::run_until(local(11, 17, 9, 0, 30));
    CHECK(total(0) == b0 + 2000);  // tomorrow 08:00 and 09:00
}

static void test_limit_and_mode() {
    // pump 3: 2 x 1 ml, then a daily limit of 1.5 ml
    CHECK(scheduler::set_schedule(2, spread(600, 720, 2, 2000)) == nullptr);  // 10:00, 12:00
    auto set = dosing::settings(2);
    set.daily_limit_ul = 1500;
    CHECK(dosing::set_settings(2, set) == nullptr);
    const uint64_t b = total(2);
    sim::run_until(local(11, 17, 12, 1));
    CHECK(total(2) == b + 1000);
    auto st = scheduler::status(2);
    CHECK(st.skipped == 1 && std::strcmp(st.last_skip_reason, "daily_limit") == 0);
    // validation now knows the limit
    CHECK_STR(scheduler::set_schedule(2, spread(600, 720, 2, 2000)), "exceeds_daily_limit");
    // manual only: slots pass silently
    set.daily_limit_ul = 0;
    set.mode = dose::Mode::ManualOnly;
    CHECK(dosing::set_settings(2, set) == nullptr);
    sim::run_until(local(11, 18, 12, 1));
    st = scheduler::status(2);
    CHECK(total(2) == b + 1000 && st.skipped == 1);
    CHECK(st.planned_today_ul == 0 && st.remaining_days < 0);
    set.mode = dose::Mode::Auto;
    CHECK(dosing::set_settings(2, set) == nullptr);
}

static void test_boost_and_forecast() {
    // pump 4: 4 ml per day in 4 doses, container 100 ml
    CHECK(scheduler::set_schedule(3, spread(480, 1200, 4, 4000)) == nullptr);  // 08, 12, 16, 20 h
    sim::run_until(local(11, 19, 7, 0));
    CHECK(dosing::set_container(3, 100000, 100000) == nullptr);
    auto st = scheduler::status(3);
    CHECK(st.planned_today_ul == 4000 && st.remaining_days > 24.9f && st.remaining_days < 25.1f);
    CHECK_STR(scheduler::boost_start(4, 3000, 3), "no_slots");
    CHECK(scheduler::boost_start(3, 3000, 3) == nullptr);  // 12 doses until 22 Nov 07:00: +0.25 ml each
    st = scheduler::status(3);
    CHECK(st.boost_active && st.remaining_days < 24.0f);
    CHECK(st.boost.slots_left == 12 && st.next_boost_ul == 250 && st.boost_today_ul == 1000);
    const uint64_t b = total(3);
    sim::run_until(local(11, 19, 8, 0, 30));
    CHECK(total(3) == b + 1250);
    CHECK(dosing::status(3).last_target_ul == 1250);
    sim::run_until(local(11, 22, 7, 0));
    CHECK(total(3) == b + 3 * 4000 + 3000);
    st = scheduler::status(3);
    CHECK(!st.boost_active && st.boost.remaining_ul == 0);
    CHECK(dosing::status(3).state.remaining_ul == 100000 - 15000);

    // started in the evening: the share is the same for every dose (not the
    // day's part on the last dose of the day)
    sim::run_until(local(11, 22, 18, 0));
    CHECK(scheduler::boost_start(3, 3000, 3) == nullptr);  // 20 h + 4 + 4 + 08, 12, 16 h = 12 doses
    st = scheduler::status(3);
    CHECK(st.boost.slots_left == 12 && st.next_boost_ul == 250 && st.boost_today_ul == 250);
    const uint64_t c = total(3);
    sim::run_until(local(11, 22, 20, 0, 30));
    CHECK(total(3) == c + 1250);
    sim::run_until(local(11, 25, 19, 0));
    CHECK(total(3) == c + 12 * 1000 + 3000);
    CHECK(scheduler::status(3).boost.remaining_ul == 0);

    // over the next 6 doses (pro): from 19:00 that is 20 h, then 08 .. 20 h, 08 h
    CHECK_STR(scheduler::boost_start(3, 3000, 0, 0), "invalid_param");
    CHECK_STR(scheduler::boost_start(3, 3000, 3, 6), "invalid_param");
    CHECK_STR(scheduler::boost_start(3, 3000, 0, sched::kMaxBoostDoses + 1), "invalid_param");
    CHECK_STR(scheduler::boost_start(4, 3000, 0, 6), "no_slots");
    CHECK(scheduler::boost_start(3, 3000, 0, 6) == nullptr);
    st = scheduler::status(3);
    CHECK(st.boost_active && st.boost.doses == 6 && st.boost.slots_left == 6 && st.next_boost_ul == 500);
    CHECK(st.boost_until == local(11, 27, 8, 0) && st.boost_today_ul == 500);
    const uint64_t e = total(3);
    sim::run_until(local(11, 27, 9, 0));
    CHECK(total(3) == e + 6 * 1000 + 3000);
    CHECK(scheduler::status(3).boost.remaining_ul == 0);
}

static void test_queue_and_clock_jump() {
    sched::Locks l = scheduler::locks();
    l.manual = sched::ManualDuringLock::Queue;
    CHECK(scheduler::set_locks(l) == nullptr);
    // Sunday 22 Nov 13:00: manual dose on pump 1, then on pump 2 -> queued
    sim::run_until(local(11, 22, 13, 0));
    int64_t retry = 0;
    bool queued = false;
    CHECK(scheduler::manual_dose(0, 1000, dosing::Trigger::Api, -1, &retry, &queued) == nullptr && !queued);
    sim::run_for(5);
    const uint64_t b1 = total(1);
    CHECK(scheduler::manual_dose(1, 500, dosing::Trigger::Api, -1, &retry, &queued) == nullptr && queued);
    CHECK_STR(scheduler::manual_dose(1, 500, dosing::Trigger::Api, -1, &retry, &queued), "queue_full");
    CHECK(scheduler::status(1).queued && scheduler::status(1).queued_ul == 500);
    sim::run_for(200);
    CHECK(total(1) == b1);
    sim::run_for(120);
    CHECK(total(1) == b1 + 500 && !scheduler::status(1).queued);
    l.manual = sched::ManualDuringLock::Reject;
    CHECK(scheduler::set_locks(l) == nullptr);

    // clock set back by one hour right after the 08:00 slot: it does not run twice
    sim::run_until(local(11, 23, 8, 0, 30));
    const uint64_t b0 = total(0);
    sim::utc_offset -= 3600;
    sim::run_for(1.5 * 3600);  // 07:00:30 .. 08:30:30 again
    CHECK(total(0) == b0);
    sim::utc_offset += 3600;
}

static void test_set_all() {
    sched::PumpSchedule before[5], all[5];
    for (int q = 0; q < 5; ++q) before[q] = all[q] = scheduler::schedule(q);
    const sched::Locks old_locks = scheduler::locks();
    // pump 5 at 08:00 and a lock pump 4 <-> pump 5 that pump 4 at 08:05 violates
    all[3] = plan({single(485, 1000)});
    all[4] = plan({single(480, 1000)});
    sched::Locks l;
    l.count = 1;
    l.rules[0] = sched::LockRule{3, 4, 10};
    scheduler::SaveResult r;
    CHECK_STR(scheduler::set_all(all, l, &r), "conflict");
    CHECK(r.validation.conflicts >= 1);
    for (int q = 0; q < 5; ++q) CHECK(std::memcmp(&store::sch[q], &before[q], sizeof(before[q])) == 0);
    CHECK(scheduler::schedule(4).kind == before[4].kind);
    CHECK(scheduler::locks().count == old_locks.count);  // nothing changed
    // valid set: replaced together
    all[3] = plan({single(500, 1000)});
    CHECK(scheduler::set_all(all, l) == nullptr);
    CHECK(scheduler::schedule(4).kind == sched::Kind::Times && scheduler::schedule(4).times[0].minute == 480);
    CHECK(scheduler::schedule(3).times[0].minute == 500);
    CHECK(scheduler::locks().count == 1 && store::locks.rules[0].gap_min == 10);
    CHECK(store::sch[4].kind == sched::Kind::Times);
    CHECK(scheduler::status(4).has_next);
    // back to the previous state
    CHECK(scheduler::set_all(before, old_locks) == nullptr);
    CHECK(scheduler::schedule(4).kind == before[4].kind);
}

static void test_not_fitted() {
    // pump 5 with a plan, then not fitted: no next dose, nothing dosed, no boost
    CHECK(scheduler::set_schedule(4, spread(480, 1200, 4, 4000)) == nullptr);
    CHECK(scheduler::status(4).has_next);
    hwconfig::g_fitted = 0x0F;
    const uint64_t b = total(4);
    const auto st = scheduler::status(4);
    CHECK(!st.has_next && st.planned_today_ul == 0);
    CHECK_STR(scheduler::boost_start(4, 3000, 3), "no_slots");
    CHECK_STR(dosing::dose(4, 1000, dosing::Trigger::Manual), "pump_not_fitted");
    sim::run_until(sim::utc() + 2 * 86400);
    CHECK(total(4) == b && scheduler::status(4).skipped == st.skipped);
    hwconfig::g_fitted = 0x1F;
    CHECK(scheduler::status(4).has_next);
}

static size_t count_events(history::Kind k) {
    size_t n = 0;
    for (const auto& e : sim::events) n += e.kind == k;
    return n;
}

static bool is_code(const history::Event* e, const char* code) { return e && e->code == history::code_of(code); }

static void test_events() {
    using history::Kind;
    using history::Source;
    // pump 4 (08, 12, 16, 20 h): a boost over 2 doses runs out by itself
    CHECK(scheduler::set_schedule(3, spread(480, 1200, 4, 4000)) == nullptr);
    CHECK(scheduler::boost_start(3, 2000, 0, 2, Source::Api) == nullptr);
    auto* e = sim::last_event(Kind::Boost, 4);
    CHECK(is_code(e, "started") && e->source == Source::Api && e->target_ul == 2000 && e->value == -2);
    sim::run_until(sim::utc() + 2 * 86400);
    e = sim::last_event(Kind::Boost, 4);
    CHECK(is_code(e, "finished") && e->source == Source::Schedule && e->done_ul == 2000 && e->value == -2);

    // cancelled before its first dose
    CHECK(scheduler::boost_start(3, 2000, 1, 0, Source::Manual) == nullptr);
    CHECK(sim::last_event(Kind::Boost, 4)->value == 1);
    scheduler::boost_stop(3, Source::Mqtt);
    e = sim::last_event(Kind::Boost, 4);
    CHECK(is_code(e, "cancelled") && e->source == Source::Mqtt && e->done_ul == 0 && e->target_ul == 2000);
    const size_t boosts = count_events(Kind::Boost);
    scheduler::boost_stop(3, Source::Mqtt);  // nothing running: no event
    CHECK(count_events(Kind::Boost) == boosts);

    // the plan removed: the boost has no slots left and ends, the plan change is recorded
    CHECK(scheduler::boost_start(3, 3000, 0, 3) == nullptr);
    CHECK(scheduler::set_schedule(3, sched::PumpSchedule{}, nullptr, Source::Manual) == nullptr);
    e = sim::last_event(Kind::Boost, 4);
    CHECK(is_code(e, "finished") && e->done_ul == 0);
    CHECK(is_code(sim::last_event(Kind::Schedule, 4), "cleared"));
    CHECK(scheduler::set_schedule(3, spread(480, 1200, 4, 4000), nullptr, Source::Api) == nullptr);
    e = sim::last_event(Kind::Schedule, 4);
    CHECK(is_code(e, "set") && e->source == Source::Api && e->target_ul == 4000 && e->value == 4);
    CHECK(scheduler::set_schedule(3, plan({single(540, 1000), single(600, 1500)}), nullptr, Source::Manual) == nullptr);
    e = sim::last_event(Kind::Schedule, 4);
    CHECK(e->target_ul == 2500 && e->value == 2);
    const size_t plans = count_events(Kind::Schedule);
    CHECK(scheduler::set_schedule(3, spread(480, 1200, 4, 4000)) == nullptr);  // without source (restore)
    CHECK(count_events(Kind::Schedule) == plans);

    // lock rules (device)
    CHECK(scheduler::set_locks(scheduler::locks(), nullptr, Source::Api) == nullptr);
    e = sim::last_event(Kind::Locks, 0);
    CHECK(is_code(e, "set") && e->value == scheduler::locks().count);

    // mode and other settings
    auto set = dosing::settings(3);
    set.mode = dose::Mode::ManualOnly;
    const size_t others = count_events(Kind::Settings);
    CHECK(dosing::set_settings(3, set, Source::Mqtt) == nullptr);
    e = sim::last_event(Kind::Mode, 4);
    CHECK(e && e->source == Source::Mqtt && e->value == static_cast<int32_t>(dose::Mode::ManualOnly));
    CHECK(count_events(Kind::Settings) == others);  // only the mode changed
    std::strcpy(set.name, "Kalium");
    CHECK(dosing::set_settings(3, set, Source::Manual) == nullptr);
    CHECK(count_events(Kind::Settings) == others + 1);
    CHECK(dosing::set_settings(3, set, Source::Manual, true) == nullptr);  // direction changed
    CHECK(count_events(Kind::Settings) == others + 2);
    const size_t modes = count_events(Kind::Mode);
    set.mode = dose::Mode::Auto;
    set.name[0] = 0;
    CHECK(dosing::set_settings(3, set) == nullptr);  // without source (restore)
    CHECK(count_events(Kind::Mode) == modes && count_events(Kind::Settings) == others + 2);
}

int main(int argc, char**) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    g_log_verbose = argc > 1;
    setenv("TZ", sim::tz, 1);
    tzset();
    CHECK(tz::parse(sim::tz, &g_zone));
    CHECK(dosing::init() == ESP_OK && sim::task);
    CHECK(scheduler::init() == ESP_OK && sim::sched_task);
    std::printf("Scheduler: basic run\n");
    test_basic();
    std::printf("Scheduler: conflicts on save\n");
    test_conflicts_on_save();
    std::printf("Scheduler: runtime guard\n");
    test_runtime_guard();
    std::printf("Scheduler: delay timeout\n");
    test_delay_timeout();
    std::printf("Scheduler: power loss\n");
    test_power_loss();
    std::printf("Scheduler: schedule change\n");
    test_schedule_change();
    std::printf("Scheduler: daily limit and mode\n");
    test_limit_and_mode();
    std::printf("Scheduler: boost and forecast\n");
    test_boost_and_forecast();
    std::printf("Scheduler: queue and clock jump\n");
    test_queue_and_clock_jump();
    std::printf("Scheduler: replace all\n");
    test_set_all();
    std::printf("Scheduler: pump not fitted\n");
    test_not_fitted();
    std::printf("Scheduler: history events\n");
    test_events();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
