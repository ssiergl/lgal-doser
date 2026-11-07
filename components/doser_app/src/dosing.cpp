#include "doser/dosing.h"

#include <cinttypes>
#include <cstring>
#include <ctime>

#include "doser/dosing_store.h"
#include "doser/history.h"
#include "doser/motion.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lgal/timesync.h"

namespace doser::dosing {

namespace {
constexpr const char* TAG = "dosing";
constexpr int kN = 5;
constexpr int kTickMs = 20;
constexpr uint16_t kMaxRpm = 600;

struct Job {
    Activity act = Activity::Idle;
    Phase phase = Phase::None;
    Trigger trigger = Trigger::Manual;
    uint32_t sps = 0;
    uint32_t spr = 0;               // steps/rev at start
    uint32_t target_ul = 0;
    uint32_t main_steps = 0;
    uint32_t unretract_steps = 0;
    uint16_t unretract_mrev = 0;    // retraction being compensated
    uint32_t retract_steps = 0;
    uint16_t retract_mrev = 0;      // to be stored after the retraction
    uint32_t done_ul = 0;
    // booking (dosing only)
    int64_t booked_at = 0;
    uint32_t booked_ul = 0;
    bool booked_container = false;
    bool booked_ledger = false;     // only with valid time
    // sequence
    int64_t phase_start_us = 0;
    int64_t prime_start_us = 0;
    int64_t last_beat_us = 0;
    bool stop_req = false;
    bool stopping = false;          // stop handed to motion
    bool aborted = false;
    const char* reason = nullptr;   // priming: reason for the end
};

struct Pump {
    Job job;
    dose::PumpSettings set;
    dose::PumpState st{};
    const char* last_result = nullptr;
    Activity last_act = Activity::Idle;
    uint32_t last_target = 0, last_done = 0;
    int64_t last_at = 0;
    bool calib_pending = false;
    uint32_t calib_steps = 0, calib_spr = 0, calib_target = 0;
};

SemaphoreHandle_t s_mx = nullptr;
Pump s_p[kN];
dose::Engineering s_eng;
sched::Locks s_locks;

struct Guard {
    Guard() { xSemaphoreTake(s_mx, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(s_mx); }
};

bool valid_pump(int i) { return i >= 0 && i < kN && s_mx; }

void log_event(history::Kind kind, int i, Trigger trigger, const char* code, uint32_t target_ul, uint32_t done_ul,
               int32_t value = 0) {
    history::Event e;
    e.kind = kind;
    e.pump = static_cast<uint8_t>(i + 1);
    e.source = static_cast<history::Source>(static_cast<uint8_t>(trigger) + 1);
    e.code = history::code_of(code);
    e.target_ul = target_ul;
    e.done_ul = done_ul;
    e.value = value;
    history::record(e);
}

void log_setting(history::Kind kind, int i, const char* code, uint32_t target_ul, uint32_t done_ul, int32_t value) {
    log_event(kind, i, Trigger::Manual, code, target_ul, done_ul, value);
}

bool time_valid() { return lgal::timesync::status().valid; }
int64_t now_utc() { return lgal::timesync::status().utc; }

int64_t local_midnight(int64_t utc) {
    time_t t = static_cast<time_t>(utc);
    tm lt{};
    localtime_r(&t, &lt);
    lt.tm_hour = lt.tm_min = lt.tm_sec = 0;
    lt.tm_isdst = -1;
    return static_cast<int64_t>(mktime(&lt));
}

uint32_t used_today(const Pump& p) {
    const int64_t now = now_utc();
    return p.st.ledger.sum_from(dose::window_start_hour(s_eng.limit_mode, now, local_midnight(now)));
}

int tier_or_default(const Pump& p, int tier) {
    return (tier >= 0 && tier < dose::kTiers) ? tier : p.set.speed_tier;
}

const char* map_motion_err(esp_err_t e) {
    switch (e) {
        case ESP_OK: return nullptr;
        case ESP_ERR_NOT_FOUND: return "driver_unavailable";
        case ESP_ERR_INVALID_ARG: return "speed_out_of_range";
        case ESP_ERR_INVALID_STATE: return motion::locked() ? "locked" : "pump_busy";
        default: return "internal_error";
    }
}

esp_err_t start_move(int i, int32_t steps) {
    Job& j = s_p[i].job;
    motion::Move m{};
    m.steps = steps;
    m.sps = j.sps;
    j.phase_start_us = esp_timer_get_time();
    j.stopping = false;
    return motion::run(i, m);
}

// Common check before every job
const char* precheck(int i) {
    const Pump& p = s_p[i];
    if (p.set.mode == dose::Mode::Disabled) return "pump_disabled";
    if (p.job.act != Activity::Idle || motion::status(i).running) return "pump_busy";
    if (motion::locked()) return "locked";
    return nullptr;
}

// Prepare job and start the first move (compensate retraction or main move)
const char* begin(int i, Activity act, uint32_t main_steps, int tier, uint16_t unretract_mrev) {
    Pump& p = s_p[i];
    Job& j = p.job;
    const auto cfg = motion::config(i);
    j = Job{};
    j.act = act;
    j.spr = cfg.steps_per_rev();
    j.sps = cfg.rpm_to_sps(s_eng.tier_rpm[tier_or_default(p, tier)]);
    j.main_steps = main_steps;
    j.unretract_mrev = unretract_mrev;
    j.unretract_steps = dose::mrev_to_steps(j.unretract_mrev, j.spr);
    j.retract_mrev = dose::retract_mrev(p.set, s_eng);
    j.retract_steps = dose::mrev_to_steps(j.retract_mrev, j.spr);
    j.phase = j.unretract_steps ? Phase::Unretract : Phase::Main;
    const esp_err_t err = start_move(i, j.phase == Phase::Unretract ? static_cast<int32_t>(j.unretract_steps)
                                                                     : static_cast<int32_t>(main_steps));
    if (err != ESP_OK) {
        j = Job{};
        return map_motion_err(err);
    }
    return nullptr;
}

void refund(Pump& p, uint32_t ul) {
    Job& j = p.job;
    if (!ul || !j.booked_ul) return;
    if (ul > j.booked_ul) ul = j.booked_ul;
    if (j.booked_ledger) p.st.ledger.subtract(j.booked_at, ul);
    if (j.booked_container) p.st.remaining_ul += static_cast<int32_t>(ul);
    p.st.total_ul -= ul;
    j.booked_ul -= ul;
}

void finish(int i, bool fault) {
    Pump& p = s_p[i];
    Job& j = p.job;
    const char* result = fault ? (motion::locked() ? "locked" : "fault")
                         : j.aborted ? "aborted"
                         : (j.act == Activity::Priming && j.reason) ? j.reason
                                                                    : "success";
    if (time_valid()) p.st.last_end = static_cast<uint32_t>(now_utc());
    dosing_store::save_state(i, p.st);
    p.last_result = result;
    p.last_act = j.act;
    p.last_target = j.target_ul;
    p.last_done = j.done_ul;
    p.last_at = now_utc();
    ESP_LOGI(TAG, "Pump %d: %s %s, %" PRIu32 " of %" PRIu32 " ul", i + 1, activity_str(j.act), result, j.done_ul,
             j.target_ul);
    const auto kind = j.act == Activity::Priming       ? history::Kind::Prime
                      : j.act == Activity::Calibrating ? history::Kind::CalibRun
                                                       : history::Kind::Dose;
    log_event(kind, i, j.trigger, result, j.target_ul, j.done_ul);
    j = Job{};
}

void tick_pump(int i) {
    Pump& p = s_p[i];
    Job& j = p.job;
    if (j.act == Activity::Idle) return;
    const auto ms = motion::status(i);
    const int64_t now_us = esp_timer_get_time();

    if (ms.running) {
        if (j.stopping) return;
        if (j.act == Activity::Priming && j.phase == Phase::Main) {
            if (j.stop_req) j.reason = "success";
            else if (now_us - j.last_beat_us > int64_t(s_eng.prime_heartbeat_ms) * 1000) j.reason = "heartbeat_lost";
            else if (now_us - j.prime_start_us > int64_t(s_eng.prime_max_s) * 1000000) j.reason = "max_duration";
            if (j.reason) {
                if (std::strcmp(j.reason, "success") != 0) ESP_LOGW(TAG, "Pump %d: priming stopped (%s)", i + 1, j.reason);
                motion::stop(i, false);
                j.stopping = true;
            }
        } else if (j.stop_req && j.phase != Phase::Retract) {
            // Retraction always runs to the end (short), otherwise the compensation move is wrong
            motion::stop(i, false);
            j.stopping = true;
            j.aborted = true;
        }
        return;
    }

    // Move finished
    p.st.tube_ms += static_cast<uint64_t>((now_us - j.phase_start_us) / 1000);
    const bool fault = ms.fault != nullptr || motion::locked();
    const uint32_t moved = ms.position;

    switch (j.phase) {
        case Phase::Unretract:
            if (fault) {
                p.st.retracted_mrev = 0;  // partially compensated: rather a little too little
                refund(p, j.booked_ul);
                finish(i, true);
            } else if (j.aborted) {
                refund(p, j.booked_ul);
                // Back to the retracted position
                j.retract_steps = moved;
                j.retract_mrev = j.unretract_mrev;
                if (moved && start_move(i, -static_cast<int32_t>(moved)) == ESP_OK) {
                    j.phase = Phase::Retract;
                } else {
                    p.st.retracted_mrev = moved ? 0 : j.unretract_mrev;
                    finish(i, false);
                }
            } else {
                j.phase = Phase::Main;
                if (start_move(i, static_cast<int32_t>(j.main_steps)) != ESP_OK) {
                    refund(p, j.booked_ul);
                    finish(i, true);
                }
            }
            return;

        case Phase::Main: {
            const bool complete = moved >= j.main_steps && j.act != Activity::Priming;
            const uint32_t ul = complete && j.act == Activity::Dosing
                                    ? j.target_ul
                                    : dose::steps_to_ul(moved, dose::ul_per_rev(p.set), j.spr);
            j.done_ul = ul;
            if (j.act == Activity::Dosing) refund(p, j.booked_ul > ul ? j.booked_ul - ul : 0);
            if (j.act == Activity::Calibrating) {
                p.calib_pending = complete && !fault;
                p.calib_steps = moved;
                p.calib_spr = j.spr;
                p.calib_target = j.target_ul;
                if (!complete) j.aborted = true;
            }
            if (fault) {
                p.st.retracted_mrev = 0;
                finish(i, true);
            } else if (j.retract_steps) {
                j.phase = Phase::Retract;
                if (start_move(i, -static_cast<int32_t>(j.retract_steps)) != ESP_OK) {
                    p.st.retracted_mrev = 0;
                    finish(i, false);
                }
            } else {
                p.st.retracted_mrev = 0;
                finish(i, false);
            }
            return;
        }

        case Phase::Retract:
            if (fault) {
                p.st.retracted_mrev = j.retract_steps
                                          ? static_cast<uint16_t>(uint64_t(j.retract_mrev) * moved / j.retract_steps)
                                          : 0;
            } else {
                p.st.retracted_mrev = j.retract_mrev;
            }
            finish(i, fault);
            return;

        default:
            j = Job{};
            return;
    }
}

void task(void*) {
    while (true) {
        {
            Guard g;
            for (int i = 0; i < kN; ++i) tick_pump(i);
        }
        vTaskDelay(pdMS_TO_TICKS(kTickMs));
    }
}
}  // namespace

// ---------------------------------------------------------------------------
const char* activity_str(Activity a) {
    switch (a) {
        case Activity::Dosing: return "dosing";
        case Activity::Priming: return "priming";
        case Activity::Calibrating: return "calibrating";
        default: return "idle";
    }
}

const char* phase_str(Phase p) {
    switch (p) {
        case Phase::Unretract: return "unretract";
        case Phase::Main: return "main";
        case Phase::Retract: return "retract";
        default: return "none";
    }
}

const char* trigger_str(Trigger t) {
    switch (t) {
        case Trigger::Api: return "api";
        case Trigger::Schedule: return "schedule";
        case Trigger::Boost: return "boost";
        case Trigger::Mqtt: return "mqtt";
        default: return "manual";
    }
}

esp_err_t init() {
    if (s_mx) return ESP_OK;
    s_mx = xSemaphoreCreateMutex();
    if (!s_mx) return ESP_ERR_NO_MEM;
    dosing_store::load_engineering(&s_eng);
    for (int i = 0; i < kN; ++i) {
        dosing_store::load_settings(i, &s_p[i].set);
        auto& set = s_p[i].set;
        if (set.speed_tier >= dose::kTiers) set.speed_tier = 1;
        if (set.mode > dose::Mode::Disabled) set.mode = dose::Mode::Disabled;
        if (!(set.ul_per_rev >= 0.0f) || set.ul_per_rev > dose::kNominalUlPerRev * 4) set.ul_per_rev = 0.0f;
        if (!dosing_store::load_state(i, &s_p[i].st)) s_p[i].st = dose::PumpState{};
    }
    ESP_LOGI(TAG, "Ready: tiers %u/%u/%u rpm, retraction %u/1000 rev, calibration %" PRIu32 " ul, state in %s",
             s_eng.tier_rpm[0], s_eng.tier_rpm[1], s_eng.tier_rpm[2], s_eng.retract_mrev, s_eng.calib_target_ul,
             dosing_store::state_backend());
    if (xTaskCreate(task, "dosing", 4096, nullptr, 6, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

const char* dose(int pump, uint32_t ul, Trigger trigger, int tier, int64_t* retry_at) {
    if (!valid_pump(pump)) return "invalid_param";
    Guard g;
    Pump& p = s_p[pump];
    if (const char* e = precheck(pump)) return e;
    if ((trigger == Trigger::Schedule || trigger == Trigger::Boost) && p.set.mode != dose::Mode::Auto)
        return "pump_manual_only";
    // Lock times: without a valid clock only running pumps count
    const bool clock = time_valid();
    int64_t last_end[kN];
    bool running[kN];
    for (int q = 0; q < kN; ++q) {
        last_end[q] = clock ? s_p[q].st.last_end : 0;
        running[q] = s_p[q].job.act != Activity::Idle;
    }
    const int64_t start = clock ? now_utc() : 0;
    const int64_t until = sched::lock_until(s_locks, pump, last_end, running, start);
    if (until > start) {
        if (retry_at) *retry_at = until;
        return "collision_lock";
    }
    if (p.set.daily_limit_ul && !time_valid()) return "time_invalid";  // limit cannot be checked
    if (const char* e = dose::check_dose(ul, p.set, s_eng, p.st, used_today(p))) return e;
    const uint32_t spr = motion::config(pump).steps_per_rev();
    const uint32_t steps = dose::ul_to_steps(ul, dose::ul_per_rev(p.set), spr);
    if (!steps) return "dose_too_small";

    // Book in full and save before the motor turns
    const dose::PumpState before = p.st;
    const int64_t now = now_utc();
    const bool ledger = time_valid();  // without a time no assignment to a day is possible
    if (ledger) p.st.ledger.add(now, ul);
    if (p.set.capacity_ul) p.st.remaining_ul -= static_cast<int32_t>(ul);
    p.st.total_ul += ul;
    p.st.retracted_mrev = 0;  // will be compensated right away
    dosing_store::save_state(pump, p.st);

    const char* err = begin(pump, Activity::Dosing, steps, tier, before.retracted_mrev);
    if (err) {
        p.st = before;
        dosing_store::save_state(pump, p.st);
        return err;
    }
    Job& j = p.job;
    j.trigger = trigger;
    j.target_ul = ul;
    j.booked_at = now;
    j.booked_ul = ul;
    j.booked_container = p.set.capacity_ul != 0;
    j.booked_ledger = ledger;
    ESP_LOGI(TAG, "Pump %d: dose %" PRIu32 " ul (%s) = %" PRIu32 " steps at %" PRIu32 " steps/s", pump + 1, ul,
             trigger_str(trigger), steps, j.sps);
    return nullptr;
}

const char* prime_start(int pump, int tier) {
    if (!valid_pump(pump)) return "invalid_param";
    Guard g;
    Pump& p = s_p[pump];
    if (p.job.act == Activity::Priming) {  // repeated start = heartbeat
        p.job.last_beat_us = esp_timer_get_time();
        return nullptr;
    }
    if (const char* e = precheck(pump)) return e;
    const auto cfg = motion::config(pump);
    const uint32_t sps = cfg.rpm_to_sps(s_eng.tier_rpm[tier_or_default(p, tier)]);
    // Enough steps for the maximum duration; stopped via heartbeat/timeout
    const uint64_t steps = uint64_t(sps) * (s_eng.prime_max_s + 5);
    // The tube gets filled anyway: no retraction compensation needed
    const char* err = begin(pump, Activity::Priming, steps > 0x7FFFFFFF ? 0x7FFFFFFF : uint32_t(steps), tier, 0);
    if (err) return err;
    p.st.retracted_mrev = 0;
    const int64_t now = esp_timer_get_time();
    p.job.prime_start_us = now;
    p.job.last_beat_us = now;
    ESP_LOGI(TAG, "Pump %d: priming started", pump + 1);
    return nullptr;
}

const char* prime_beat(int pump) {
    if (!valid_pump(pump)) return "invalid_param";
    Guard g;
    Job& j = s_p[pump].job;
    if (j.act != Activity::Priming || j.stopping) return "not_priming";
    j.last_beat_us = esp_timer_get_time();
    return nullptr;
}

const char* prime_stop(int pump) {
    if (!valid_pump(pump)) return "invalid_param";
    Guard g;
    Job& j = s_p[pump].job;
    if (j.act != Activity::Priming) return "not_priming";
    j.stop_req = true;
    return nullptr;
}

const char* calibrate_start(int pump, uint32_t target_ul, int tier) {
    if (!valid_pump(pump)) return "invalid_param";
    Guard g;
    Pump& p = s_p[pump];
    if (const char* e = precheck(pump)) return e;
    if (!target_ul) target_ul = s_eng.calib_target_ul;
    if (target_ul < 1000 || target_ul > 200000) return "invalid_param";
    const uint32_t spr = motion::config(pump).steps_per_rev();
    const uint32_t steps = dose::ul_to_steps(target_ul, dose::ul_per_rev(p.set), spr);
    p.calib_pending = false;
    const char* err = begin(pump, Activity::Calibrating, steps, tier, p.st.retracted_mrev);
    if (err) return err;
    p.st.retracted_mrev = 0;
    p.job.target_ul = target_ul;
    ESP_LOGI(TAG, "Pump %d: calibration run %" PRIu32 " ul = %" PRIu32 " steps", pump + 1, target_ul, steps);
    return nullptr;
}

const char* calibrate_apply(int pump, uint32_t measured_ul) {
    if (!valid_pump(pump)) return "invalid_param";
    Guard g;
    Pump& p = s_p[pump];
    if (!p.calib_pending) return "no_calibration_run";
    const auto r = dose::calibrate(p.calib_steps, p.calib_spr, measured_ul);
    if (!r.ok) return r.error;
    p.set.ul_per_rev = r.ul_per_rev;
    p.set.calibrated_at = time_valid() ? now_utc() : 0;
    p.calib_pending = false;
    if (dosing_store::save_settings(pump, p.set) != ESP_OK) return "storage_error";
    // The collected amount is missing from the container
    if (p.set.capacity_ul) {
        p.st.remaining_ul -= static_cast<int32_t>(measured_ul);
        dosing_store::save_state(pump, p.st);
    }
    log_setting(history::Kind::Calibration, pump, "set", p.calib_target, measured_ul,
                static_cast<int32_t>(r.ul_per_rev * 1000.0f + 0.5f));
    ESP_LOGI(TAG, "Pump %d: calibrated %.1f ul/rev (%.2f ml/min at %u rpm)", pump + 1, r.ul_per_rev,
             dose::flow_ml_min(s_eng.tier_rpm[p.set.speed_tier], r.ul_per_rev), s_eng.tier_rpm[p.set.speed_tier]);
    return nullptr;
}

void calibrate_cancel(int pump) {
    if (!valid_pump(pump)) return;
    Guard g;
    s_p[pump].calib_pending = false;
}

void stop(int pump) {
    if (!valid_pump(pump)) return;
    Guard g;
    s_p[pump].job.stop_req = true;
}

void stop_all() {
    for (int i = 0; i < kN; ++i) stop(i);
}

const char* set_settings(int pump, const dose::PumpSettings& s) {
    if (!valid_pump(pump)) return "invalid_param";
    if (s.speed_tier >= dose::kTiers || s.retract_mrev < -1 || s.retract_mrev > 2000 ||
        s.daily_limit_ul > 10000000 || s.capacity_ul > 100000000 || s.ul_per_rev < 0.0f ||
        s.mode > dose::Mode::Disabled)
        return "invalid_param";
    Guard g;
    Pump& p = s_p[pump];
    p.set = s;
    p.set.name[sizeof(p.set.name) - 1] = 0;
    return dosing_store::save_settings(pump, p.set) == ESP_OK ? nullptr : "storage_error";
}

void set_locks(const sched::Locks& l) {
    if (!s_mx) return;
    Guard g;
    s_locks = l;
}

sched::RunParams run_params(int pump) {
    sched::RunParams r;
    if (!valid_pump(pump)) return r;
    Guard g;
    const Pump& p = s_p[pump];
    const auto cfg = motion::config(pump);
    const uint32_t spr = cfg.steps_per_rev();
    r.ul_per_rev = dose::ul_per_rev(p.set);
    r.rpm = s_eng.tier_rpm[p.set.speed_tier < dose::kTiers ? p.set.speed_tier : 1];
    r.retract_mrev = dose::retract_mrev(p.set, s_eng);
    const uint32_t accel = spr ? cfg.accel_or_default() * 60 / spr : 600;
    r.accel_rpm_s = static_cast<uint16_t>(accel < 1 ? 1 : accel > 60000 ? 60000 : accel);
    return r;
}

const char* set_engineering(const dose::Engineering& e) {
    if (dose::validate(e, kMaxRpm)) return "invalid_param";
    Guard g;
    s_eng = e;
    return dosing_store::save_engineering(e) == ESP_OK ? nullptr : "storage_error";
}

const char* set_container(int pump, int64_t remaining_ul, int64_t capacity_ul) {
    if (!valid_pump(pump)) return "invalid_param";
    Guard g;
    Pump& p = s_p[pump];
    if (p.job.act != Activity::Idle) return "pump_busy";
    const int64_t cap = capacity_ul >= 0 ? capacity_ul : p.set.capacity_ul;
    if (cap < 0 || cap > 100000000) return "invalid_param";
    if (remaining_ul >= 0 && cap && remaining_ul > cap) return "invalid_param";
    if (capacity_ul >= 0 && static_cast<uint32_t>(capacity_ul) != p.set.capacity_ul) {
        p.set.capacity_ul = static_cast<uint32_t>(capacity_ul);
        if (dosing_store::save_settings(pump, p.set) != ESP_OK) return "storage_error";
    }
    if (remaining_ul >= 0) p.st.remaining_ul = static_cast<int32_t>(remaining_ul);
    else if (capacity_ul >= 0) p.st.remaining_ul = static_cast<int32_t>(cap);  // new container: full
    dosing_store::save_state(pump, p.st);
    log_setting(history::Kind::Container, pump, remaining_ul < 0 ? "refilled" : "set", p.set.capacity_ul, 0,
                p.st.remaining_ul);
    ESP_LOGI(TAG, "Pump %d: container %" PRId32 " of %" PRIu32 " ul", pump + 1, p.st.remaining_ul, p.set.capacity_ul);
    return nullptr;
}

const char* set_tube_hours(int pump, uint32_t hours) {
    if (!valid_pump(pump) || hours > 100000) return "invalid_param";
    Guard g;
    s_p[pump].st.tube_ms = uint64_t(hours) * 3600000ULL;
    dosing_store::save_state(pump, s_p[pump].st);
    log_setting(history::Kind::Tube, pump, "set", 0, 0, static_cast<int32_t>(hours));
    return nullptr;
}

dose::Engineering engineering() {
    Guard g;
    return s_eng;
}

dose::PumpSettings settings(int pump) {
    if (!valid_pump(pump)) return {};
    Guard g;
    return s_p[pump].set;
}

Status status(int pump) {
    Status s{};
    if (!valid_pump(pump)) return s;
    Guard g;
    const Pump& p = s_p[pump];
    const Job& j = p.job;
    s.activity = j.act;
    s.phase = j.phase;
    s.trigger = j.trigger;
    s.target_ul = j.target_ul;
    s.steps_per_rev = motion::config(pump).steps_per_rev();
    if (j.act != Activity::Idle) {
        s.rpm = j.spr ? j.sps * 60.0f / j.spr : 0.0f;
        if (j.phase == Phase::Main)
            s.done_ul = dose::steps_to_ul(motion::status(pump).position, dose::ul_per_rev(p.set), j.spr);
        else if (j.phase == Phase::Retract)
            s.done_ul = j.done_ul;
        if (j.act == Activity::Priming) s.prime_ms = uint32_t((esp_timer_get_time() - j.prime_start_us) / 1000);
    }
    s.last_result = p.last_result;
    s.last_activity = p.last_act;
    s.last_target_ul = p.last_target;
    s.last_done_ul = p.last_done;
    s.last_at = p.last_at;
    s.calib_pending = p.calib_pending;
    s.calib_steps = p.calib_steps;
    s.calib_target_ul = p.calib_target;
    s.used_today_ul = used_today(p);
    s.time_valid = time_valid();
    s.state = p.st;
    s.settings = p.set;
    return s;
}

bool busy() {
    if (!s_mx) return false;
    Guard g;
    for (const auto& p : s_p)
        if (p.job.act != Activity::Idle) return true;
    return false;
}

bool service() {
    if (!s_mx) return false;
    Guard g;
    for (const auto& p : s_p)
        if (p.job.act == Activity::Priming || p.job.act == Activity::Calibrating) return true;
    return false;
}

}  // namespace doser::dosing
