#include "doser/mqtt_ha.h"

#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <string>

#include "doser/dosing.h"
#include "doser/event_cursor.h"
#include "doser/ha_core.h"
#include "doser/history.h"
#include "doser/history_json.h"
#include "doser/hwconfig.h"
#include "doser/scheduler.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lgal/device.h"
#include "lgal/mqtt.h"
#include "lgal/ota.h"
#include "lgal/settings.h"
#include "lgal/timesync.h"
#include "nvs.h"

namespace doser::mqtt_ha {

namespace {
constexpr const char* TAG = "mqtt_ha";
constexpr int kPollMs = 2000;
constexpr int64_t kRefreshMs = 300000;   // states are re-sent at least every 5 min
constexpr int64_t kDiscoveryMs = 30000;  // discovery checked for changes
// Event stream "<base>/event" (event_cursor.h)
constexpr size_t kEvBatch = 20;          // events per round
constexpr size_t kEvKeep = 64;           // oldest events of a window held in RAM
constexpr uint32_t kEvLagS = 2;          // younger events may still sit in the history queue
constexpr int kEvBacklogMs = 250;        // next round while a backlog is being sent
constexpr int64_t kEvSaveMs = 60000;     // cursor written to NVS at most once a minute
constexpr const char* kEvNs = "mqtt_ev";

TaskHandle_t s_task = nullptr;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
// shared with the MQTT task (s_mux)
bool s_resend = false;  // after a connect or a Home Assistant restart
bool s_dirty = false;   // command executed: send states now
uint32_t s_amount_ul[ha::kPumps] = {1000, 1000, 1000, 1000, 1000};
ha::DeviceState s_alarms;
// MQTT task only (set in on_connect); no lgal::mqtt calls that lock there
bool s_control = false;
std::string s_base;
std::string s_ha_status;  // "<prefix>/status"

void wake(bool resend, bool dirty) {
    portENTER_CRITICAL(&s_mux);
    s_resend |= resend;
    s_dirty |= dirty;
    portEXIT_CRITICAL(&s_mux);
    if (s_task) xTaskNotifyGive(s_task);
}

uint32_t amount(int p) {
    portENTER_CRITICAL(&s_mux);
    const uint32_t v = s_amount_ul[p];
    portEXIT_CRITICAL(&s_mux);
    return v;
}

// --- MQTT task ---------------------------------------------------------------

void on_connect(lgal::mqtt::Session& s) {
    const lgal::mqttcfg::Config& c = s.settings();
    s_control = c.control;
    s_base = s.base();
    s_ha_status = std::string(c.discovery_prefix) + "/status";
    if (c.discovery) s.subscribe(s_ha_status);
    if (c.control) {
        s.subscribe(s_base + "/pump/+/+/set");
        s.subscribe(s_base + "/stop/set");
        s.subscribe(s_base + "/update/set");
    }
    wake(true, false);
}

void execute(const ha::Command& c) {
    switch (c.cmd) {
        case ha::Cmd::Dose: {
            const uint32_t ul = c.press ? amount(c.pump) : c.ul;
            int64_t retry_at = 0;
            bool queued = false;
            const char* e = scheduler::manual_dose(c.pump, ul, dosing::Trigger::Mqtt, -1, &retry_at, &queued);
            if (e) ESP_LOGW(TAG, "Pump %d: dose of %lu ul rejected (%s)", c.pump + 1, (unsigned long)ul, e);
            else ESP_LOGI(TAG, "Pump %d: dose of %lu ul %s", c.pump + 1, (unsigned long)ul, queued ? "queued" : "started");
            break;
        }
        case ha::Cmd::Amount:
            portENTER_CRITICAL(&s_mux);
            s_amount_ul[c.pump] = c.ul;
            portEXIT_CRITICAL(&s_mux);
            break;
        case ha::Cmd::Schedule: {
            // The switch is the mode: ON = auto (schedule runs), OFF = manual
            // only. A disabled pump stays disabled.
            auto s = dosing::settings(c.pump);
            if (s.mode == dose::Mode::Disabled) {
                ESP_LOGW(TAG, "Pump %d: disabled, mode not changed", c.pump + 1);
                break;
            }
            s.mode = c.on ? dose::Mode::Auto : dose::Mode::ManualOnly;
            if (const char* e = dosing::set_settings(c.pump, s, history::Source::Mqtt))
                ESP_LOGW(TAG, "Pump %d: mode not changed (%s)", c.pump + 1, e);
            break;
        }
        case ha::Cmd::Stop:
            ESP_LOGW(TAG, "Stop all pumps (MQTT)");
            dosing::stop_all();
            break;
        case ha::Cmd::Install: {
            // only the newer release of the fixed update source; the task reports on "<base>/ota/state"
            std::string reason;
            const char* e = lgal::ota::start_release_install("mqtt", &reason);
            if (e) ESP_LOGW(TAG, "Firmware update rejected (%s)", std::strcmp(e, "guard") ? e : reason.c_str());
            else ESP_LOGW(TAG, "Firmware update from the update source started (MQTT)");
            break;
        }
        case ha::Cmd::None: break;
    }
}

void on_data(const char* topic, int topic_len, const char* data, int data_len) {
    if (topic_len == int(s_ha_status.size()) && std::memcmp(topic, s_ha_status.data(), s_ha_status.size()) == 0) {
        if (data_len == 6 && std::memcmp(data, "online", 6) == 0) wake(true, false);
        return;
    }
    ha::Command c;
    if (!s_control || !ha::parse_command(s_base, topic, topic_len, data, data_len, &c)) {
        ESP_LOGW(TAG, "Ignored message on %.*s", topic_len, topic);
        return;
    }
    execute(c);
    wake(false, true);
}

// --- publishing task ---------------------------------------------------------

ha::DeviceInfo device_info(const lgal::mqttcfg::Config& c) {
    ha::DeviceInfo d;
    std::string sid = lgal::device::short_id();
    d.id = "lgal_doser_" + sid;
    for (auto& ch : sid) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    d.name = lgal::device::name();
    if (d.name.empty()) d.name = "LGAL Doser " + sid;
    d.firmware = esp_app_get_description()->version;
    d.url = "http://" + lgal::device::hostname() + ".local/";
    d.base = lgal::mqtt::base_topic();
    d.german = std::strcmp(lgal::settings::language(), "de") == 0;
    d.control = c.control;
    d.update_source = !lgal::ota::settings().source.empty();
    for (int p = 0; p < ha::kPumps; ++p) {
        d.fitted[p] = hwconfig::fitted(p);
        d.pump_names[p] = dosing::settings(p).name;
    }
    return d;
}

ha::PumpState pump_state(int p) {
    const auto d = dosing::status(p);
    const auto s = scheduler::status(p);
    ha::PumpState st;
    st.activity = dosing::activity_str(d.activity);
    st.total_ul = d.state.total_ul;
    st.planned_ul = s.planned_today_ul;
    st.tracked = d.settings.capacity_ul > 0;
    st.remaining_ul = d.state.remaining_ul;
    st.days = s.remaining_days;
    st.has_next = s.clock_valid && s.has_next;
    st.next_utc = s.next_utc;
    st.automatic = d.settings.mode == dose::Mode::Auto;
    st.amount_ul = amount(p);
    st.tube_h = static_cast<float>(d.state.tube_ms) / 3600000.0f;
    st.last = d.last_result;
    return st;
}

ha::UpdateState update_state() {
    const lgal::ota::ReleaseInfo r = lgal::ota::release_info();
    const lgal::ota::Status st = lgal::ota::status();
    ha::UpdateState u;
    u.installed = lgal::ota::running_version();
    u.latest = r.latest;
    u.available = r.available;
    u.notes = r.notes;
    u.in_progress = lgal::ota::in_progress();
    if (st.state == lgal::ota::State::Running && st.total > 0)
        u.percent = static_cast<int>(uint64_t(st.received) * 100 / uint32_t(st.total));
    return u;
}

struct Published {
    std::string discovery_topic;
    std::string discovery;
    std::string pump[ha::kPumps];
    std::string device;
    std::string update;
    int64_t at_ms = 0;
};

void send(const std::string& topic, const std::string& payload, std::string* last, bool force) {
    if (!force && payload == *last) return;
    if (lgal::mqtt::publish(topic, payload.data(), int(payload.size()), true)) *last = payload;
}

// --- event stream ------------------------------------------------------------

struct Window {
    history::Event seen[kEvKeep];  // ring of the last seen = oldest events
    size_t total = 0;
    history::Event ordered[kEvKeep];
};

struct Events {
    event_cursor::Cursor cur, saved;
    bool loaded = false;
    int64_t saved_ms = 0;
    uint32_t conn_utc = 0;  // events before this connection count as replay
};

bool keep_oldest(const history::Event& e, void* ctx) {
    auto* w = static_cast<Window*>(ctx);
    w->seen[w->total++ % kEvKeep] = e;  // query runs newest first
    return true;
}

bool publish_event(const history::Event& e, void* ctx) {
    const auto* ev = static_cast<const Events*>(ctx);
    lgal::Json j;
    history_json::write_event(j, e, scheduler::to_local(e.utc), true);
    j.kv("replay", e.utc < ev->conn_utc).end();
    return lgal::mqtt::publish(lgal::mqtt::topic("event"), j.c_str(), int(j.str().size()), false, 1);
}

void save_cursor(Events* ev, int64_t now_ms) {
    nvs_handle_t h;
    if (nvs_open(kEvNs, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, "cur", &ev->cur, sizeof(ev->cur)) == ESP_OK && nvs_commit(h) == ESP_OK) {
        ev->saved = ev->cur;
        ev->saved_ms = now_ms;
    }
    nvs_close(h);
}

// One round: sends up to kEvBatch events after the cursor. true = more to send.
bool pump_events(Events* ev, int64_t now_ms) {
    static Window w;  // task only; too large for the stack
    const auto ts = lgal::timesync::status();
    if (!history::ready() || !ts.valid) return false;
    const uint32_t now = static_cast<uint32_t>(ts.utc);
    if (!ev->loaded) {
        ev->loaded = true;
        nvs_handle_t h;
        size_t len = sizeof(ev->cur);
        if (nvs_open(kEvNs, NVS_READONLY, &h) == ESP_OK) {
            if (nvs_get_blob(h, "cur", &ev->cur, &len) != ESP_OK || len != sizeof(ev->cur)) ev->cur = {};
            nvs_close(h);
        }
        if (!ev->cur.utc || ev->cur.utc > now + 60) ev->cur = {now, 0};  // first start or clock moved back
        ev->saved = ev->cur;
        ESP_LOGI(TAG, "events from %" PRIu32 " (+%u)", ev->cur.utc, unsigned(ev->cur.n));
    }
    if (!ev->conn_utc) ev->conn_utc = now;
    const uint32_t until = now - kEvLagS;
    size_t budget = kEvBatch;
    bool stalled = false;
    for (int round = 0; round < 8 && budget && !stalled && ev->cur.utc <= until; ++round) {
        const uint32_t end = event_cursor::window_end(ev->cur, until);
        history::Filter f;
        f.from = ev->cur.utc;
        f.to = end;
        w.total = 0;
        history::query(f, keep_oldest, &w);
        const size_t n = w.total < kEvKeep ? w.total : kEvKeep;
        for (size_t i = 0; i < n; ++i) w.ordered[i] = w.seen[(w.total - 1 - i) % kEvKeep];
        size_t sent = 0;
        const bool done = event_cursor::advance(&ev->cur, end, w.ordered, n, w.total <= kEvKeep, budget,
                                                publish_event, ev, &sent);
        budget -= sent;
        stalled = !done && sent == 0;  // broker gone (or nothing to do in a partial window)
    }
    if ((ev->cur.utc != ev->saved.utc || ev->cur.n != ev->saved.n) && now_ms - ev->saved_ms >= kEvSaveMs)
        save_cursor(ev, now_ms);
    return !stalled && budget == 0 && ev->cur.utc <= until;
}

void task(void*) {
    Published pub;
    Events events;
    bool backlog = false;
    int64_t discovery_at = -kDiscoveryMs;
    while (true) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(backlog ? kEvBacklogMs : kPollMs));
        if (!lgal::mqtt::connected()) {
            backlog = false;
            continue;
        }
        portENTER_CRITICAL(&s_mux);
        const bool resend = s_resend;
        s_resend = false;
        s_dirty = false;
        const ha::DeviceState alarms = s_alarms;
        portEXIT_CRITICAL(&s_mux);
        const int64_t now = esp_timer_get_time() / 1000;
        if (resend) events.conn_utc = 0;  // new connection: what is buffered now is a replay
        backlog = pump_events(&events, now);
        const bool refresh = resend || now - pub.at_ms >= kRefreshMs;
        if (refresh) pub.at_ms = now;

        if (resend || now - discovery_at >= kDiscoveryMs) {
            discovery_at = now;
            lgal::mqttcfg::Config c = lgal::mqtt::config();
            const std::string topic = ha::discovery_topic(c.discovery_prefix, device_info(c).id);
            if (c.discovery) {
                lgal::Json j;
                ha::write_discovery(j, device_info(c));
                if (topic != pub.discovery_topic) pub.discovery.clear();
                pub.discovery_topic = topic;
                send(topic, j.c_str(), &pub.discovery, resend);
            } else if (resend) {
                lgal::mqtt::publish(topic, "", 0, true);  // removes the device in Home Assistant
                pub.discovery.clear();
            }
            std::memset(c.password, 0, sizeof(c.password));
        }
        for (int p = 0; p < ha::kPumps; ++p) {
            char sfx[16];
            std::snprintf(sfx, sizeof(sfx), "pump/%d/state", p + 1);
            if (!hwconfig::fitted(p)) {  // not fitted: clear a retained state from before
                send(lgal::mqtt::topic(sfx), "", &pub.pump[p], refresh);
                continue;
            }
            lgal::Json j;
            ha::write_pump_state(j, pump_state(p));
            send(lgal::mqtt::topic(sfx), j.c_str(), &pub.pump[p], refresh);
        }
        lgal::Json j;
        ha::write_device_state(j, alarms);
        send(lgal::mqtt::topic("state"), j.c_str(), &pub.device, refresh);
        lgal::Json ju;
        ha::write_update_state(ju, update_state());
        send(lgal::mqtt::topic("update"), ju.c_str(), &pub.update, refresh);
    }
}

}  // namespace

void init() {
    if (s_task) return;
    lgal::mqtt::set_handlers(on_connect, on_data);
    if (xTaskCreate(task, "mqtt_ha", 6144, nullptr, 3, &s_task) != pdPASS) ESP_LOGE(TAG, "Task not started");
}

void update_changed() { wake(false, true); }

void set_alarms(bool critical, bool warning, uint16_t conditions) {
    portENTER_CRITICAL(&s_mux);
    const bool changed =
        s_alarms.critical != critical || s_alarms.warning != warning || s_alarms.conditions != conditions;
    s_alarms.critical = critical;
    s_alarms.warning = warning;
    s_alarms.conditions = conditions;
    portEXIT_CRITICAL(&s_mux);
    if (changed && s_task) xTaskNotifyGive(s_task);
}

}  // namespace doser::mqtt_ha
