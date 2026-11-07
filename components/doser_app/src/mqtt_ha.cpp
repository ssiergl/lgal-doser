#include "doser/mqtt_ha.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

#include "doser/dosing.h"
#include "doser/ha_core.h"
#include "doser/scheduler.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lgal/device.h"
#include "lgal/mqtt.h"
#include "lgal/settings.h"

namespace doser::mqtt_ha {

namespace {
constexpr const char* TAG = "mqtt_ha";
constexpr int kPollMs = 2000;
constexpr int64_t kRefreshMs = 300000;   // states are re-sent at least every 5 min
constexpr int64_t kDiscoveryMs = 30000;  // discovery checked for changes

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
            sched::PumpSchedule s = scheduler::schedule(c.pump);
            if (s.kind == sched::Kind::None) {
                ESP_LOGW(TAG, "Pump %d: no schedule to %s", c.pump + 1, c.on ? "resume" : "pause");
                break;
            }
            s.enabled = c.on ? 1 : 0;
            if (const char* e = scheduler::set_schedule(c.pump, s))
                ESP_LOGW(TAG, "Pump %d: schedule not changed (%s)", c.pump + 1, e);
            break;
        }
        case ha::Cmd::Stop:
            ESP_LOGW(TAG, "Stop all pumps (MQTT)");
            dosing::stop_all();
            break;
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
    d.name = "LGAL Doser " + sid;
    d.firmware = esp_app_get_description()->version;
    d.url = "http://" + lgal::device::hostname() + ".local/";
    d.base = lgal::mqtt::base_topic();
    d.german = std::strcmp(lgal::settings::language(), "de") == 0;
    d.control = c.control;
    for (int p = 0; p < ha::kPumps; ++p) {
        d.pump_names[p] = dosing::settings(p).name;
    }
    return d;
}

ha::PumpState pump_state(int p) {
    const auto d = dosing::status(p);
    const auto s = scheduler::status(p);
    const auto plan = scheduler::schedule(p);
    ha::PumpState st;
    st.activity = dosing::activity_str(d.activity);
    st.total_ul = d.state.total_ul;
    st.planned_ul = s.planned_today_ul;
    st.tracked = d.settings.capacity_ul > 0;
    st.remaining_ul = d.state.remaining_ul;
    st.days = s.remaining_days;
    st.has_next = s.clock_valid && s.has_next;
    st.next_utc = s.next_utc;
    st.has_schedule = plan.kind != sched::Kind::None;
    st.schedule_on = plan.enabled != 0;
    st.amount_ul = amount(p);
    st.tube_h = static_cast<float>(d.state.tube_ms) / 3600000.0f;
    st.last = d.last_result;
    return st;
}

struct Published {
    std::string discovery_topic;
    std::string discovery;
    std::string pump[ha::kPumps];
    std::string device;
    int64_t at_ms = 0;
};

void send(const std::string& topic, const std::string& payload, std::string* last, bool force) {
    if (!force && payload == *last) return;
    if (lgal::mqtt::publish(topic, payload.data(), int(payload.size()), true)) *last = payload;
}

void task(void*) {
    Published pub;
    int64_t discovery_at = -kDiscoveryMs;
    while (true) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kPollMs));
        if (!lgal::mqtt::connected()) continue;
        portENTER_CRITICAL(&s_mux);
        const bool resend = s_resend;
        s_resend = false;
        s_dirty = false;
        const ha::DeviceState alarms = s_alarms;
        portEXIT_CRITICAL(&s_mux);
        const int64_t now = esp_timer_get_time() / 1000;
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
            lgal::Json j;
            ha::write_pump_state(j, pump_state(p));
            char sfx[16];
            std::snprintf(sfx, sizeof(sfx), "pump/%d/state", p + 1);
            send(lgal::mqtt::topic(sfx), j.c_str(), &pub.pump[p], refresh);
        }
        lgal::Json j;
        ha::write_device_state(j, alarms);
        send(lgal::mqtt::topic("state"), j.c_str(), &pub.device, refresh);
    }
}

}  // namespace

void init() {
    if (s_task) return;
    lgal::mqtt::set_handlers(on_connect, on_data);
    if (xTaskCreate(task, "mqtt_ha", 6144, nullptr, 3, &s_task) != pdPASS) ESP_LOGE(TAG, "Task not started");
}

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
