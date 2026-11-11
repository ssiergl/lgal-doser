// LGAL-Doser – entry point (M1: hardware layer)
//
// The order is safety-relevant: first the pump drivers are
// switched off, only then does anything else start.

#include <cstdio>
#include <cstring>
#include <ctime>
#include <utility>

#include "doser/alarms.h"
#include "doser/backup_api.h"
#include "doser/dosing.h"
#include "doser/dosing_api.h"
#include "doser/engineering_api.h"
#include "doser/history.h"
#include "doser/history_api.h"
#include "doser/hwconfig.h"
#include "doser/hwtest_api.h"
#include "doser/i2c_devices.h"
#include "doser/motion.h"
#include "doser/mqtt_ha.h"
#include "doser/pins.h"
#include "doser/posix_tz.h"
#include "doser/rtc_trim.h"
#include "doser/safe_init.h"
#include "doser/schedule_api.h"
#include "doser/schedule_core.h"
#include "doser/scheduler.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lgal/auth.h"
#include "lgal/button.h"
#include "lgal/captive.h"
#include "lgal/device.h"
#include "lgal/entropy.h"
#include "lgal/events.h"
#include "lgal/http.h"
#include "lgal/led.h"
#include "lgal/logbuf.h"
#include "lgal/mqtt.h"
#include "lgal/net.h"
#include "lgal/netapi.h"
#include "lgal/ota.h"
#include "lgal/ota_manifest.h"
#include "lgal/settings.h"
#include "lgal/setup.h"
#include "lgal/sysapi.h"
#include "lgal/timesync.h"
#include "lgal/ws.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "webui/webui.h"

namespace {
constexpr const char* TAG = "main";

void print_banner() {
    const esp_app_desc_t* app = esp_app_get_description();
    // Deliberately printf instead of ESP_LOG: ends up only on the serial console
    // (first USB flash), not in the RAM log available over the network.
    printf("\n========================================================\n");
    printf(" LG Aquatic Life · Doser · Firmware %s\n", app->version);
    printf(" MAC         : %s\n", lgal::device::mac_str().c_str());
    printf(" Hostname    : %s.local\n", lgal::device::hostname().c_str());
    printf(" Setup Wi-Fi : %s\n", lgal::device::setup_ssid().c_str());
    printf(" Password    : %s   (setup Wi-Fi and first web login)\n", lgal::device::setup_password().c_str());
    printf("========================================================\n\n");
}

// Alarm input from the pump, schedule and clock state (doser/alarms.h)
doser::alarms::Input alarm_input(const lgal::timesync::Status& ts) {
    doser::alarms::Input in;
    in.rtc_ok = ts.rtc_ok;
    in.clock_valid = ts.valid;
    const int32_t today = doser::scheduler::today();
    const auto eng = doser::dosing::engineering();
    for (int i = 0; i < 5; ++i) {
        const auto d = doser::dosing::status(i);
        const auto s = doser::scheduler::status(i);
        auto& a = in.pumps[i];
        a.active = doser::hwconfig::fitted(i) && d.settings.mode != doser::dose::Mode::Disabled;
        a.fault = d.last_result && std::strcmp(d.last_result, "fault") == 0;
        a.limit_hit_today = today >= 0 && s.last_skip_reason && std::strcmp(s.last_skip_reason, "daily_limit") == 0 &&
                            doser::sched::day_of(doser::scheduler::to_local(s.last_skip_at)) == today;
        a.tracked = d.settings.capacity_ul > 0;
        a.low_days = doser::dose::low_days(d.settings, eng);
        a.remaining_days = s.remaining_days;
        if (eng.tube_life_h) a.tube_percent = static_cast<float>(d.state.tube_ms) / 36000.0f / eng.tube_life_h;
    }
    return in;
}

// History entries when an alarm condition starts or ends. Conditions already
// present at boot were logged before the restart; without a valid clock
// record() drops.
void log_alarm_changes(uint16_t now) {
    using doser::alarms::Cond;
    namespace history = doser::history;
    static bool first = true;
    static uint16_t prev = 0;
    if (first) {
        first = false;
        prev = now;
        return;
    }
    const uint16_t started = now & ~prev, ended = prev & ~now;
    prev = now;
    for (const auto& [bits, kind] : {std::pair{started, history::Kind::Alarm}, std::pair{ended, history::Kind::AlarmEnd}}) {
        if (!bits) continue;
        if (bits & (1u << doser::alarms::kRtcBit)) history::note(kind, -1, history::Source::None, "rtc_fault");
        for (int i = 0; i < 5; ++i)
            for (int c = 0; c < doser::alarms::kConds; ++c)
                if (bits & doser::alarms::cond_bit(i, static_cast<Cond>(c)))
                    history::note(kind, i, history::Source::None, doser::alarms::cond_code(static_cast<Cond>(c)));
    }
}

// "1.2.3" -> 10203 for the history (0 = not a version)
int32_t version_value(const char* s) {
    lgal::ota::Version v;
    if (!lgal::ota::parse_version(s, &v) || v.major > 9999 || v.minor > 99 || v.patch > 99) return 0;
    return static_cast<int32_t>(v.major * 10000 + v.minor * 100 + v.patch);
}

// Start of the device and a changed firmware for the history, as soon as the
// clock is valid (from the RTC right away, else after the first time sync)
void log_boot(const lgal::timesync::Status& ts) {
    namespace history = doser::history;
    static bool done = false;
    if (done || !ts.valid || !history::ready()) return;
    done = true;
    history::Event e;
    e.utc = static_cast<uint32_t>(ts.utc - esp_timer_get_time() / 1000000);
    e.kind = history::Kind::System;
    e.code = history::code_of("boot");
    e.value = static_cast<int32_t>(esp_reset_reason());
    history::record(e);

    const char* version = esp_app_get_description()->version;
    nvs_handle_t h;
    if (nvs_open("doser", NVS_READWRITE, &h) != ESP_OK) return;
    char last[32] = "";
    size_t len = sizeof(last);
    const bool known = nvs_get_str(h, "fw", last, &len) == ESP_OK;
    if (!known || std::strcmp(last, version) != 0) {
        if (known) {  // not on the very first start
            e.utc = 0;
            e.code = history::code_of("updated");
            e.value = version_value(version);
            e.done_ul = static_cast<uint32_t>(version_value(last));
            history::record(e);
            ESP_LOGI(TAG, "Firmware changed: %s -> %s", last, version);
        }
        nvs_set_str(h, "fw", version);
        nvs_commit(h);
    }
    nvs_close(h);
}

// Changes in the core (network, MQTT, device settings, password, keys)
void log_core_event(const char* code, lgal::events::By by) {
    namespace history = doser::history;
    const history::Source src = by == lgal::events::By::ApiKey    ? history::Source::Api
                                : by == lgal::events::By::Browser ? history::Source::Manual
                                                                  : history::Source::None;
    history::note(history::Kind::System, -1, src, code);
}

void update_status_led() {
    using lgal::led::State;
    const auto ns = lgal::net::status();
    const auto ts = lgal::timesync::status();
    // alarms every 2 s (the schedule status computes the remaining days)
    static doser::alarms::Output al;
    static int tick = 0;
    if (tick++ % 4 == 0) {
        const auto in = alarm_input(ts);
        al = doser::alarms::evaluate(in);
        const uint16_t conditions = doser::alarms::conditions(in);
        log_alarm_changes(conditions);
        log_boot(ts);
        doser::mqtt_ha::set_alarms(al.critical, al.warning, conditions);
    }
    lgal::led::set(State::Critical, al.critical);
    lgal::led::set(State::SetupAp, ns.ap_active);
    lgal::led::set(State::IdleLan, ns.active == lgal::net::Iface::Lan);
    lgal::led::set(State::IdleWlan, ns.active == lgal::net::Iface::Wlan);
    // network disconnected: neither LAN/WLAN IP nor setup AP
    lgal::led::set(State::Warning, al.warning || (ns.active == lgal::net::Iface::None && !ns.ap_active));
    lgal::led::set(State::Dosing, doser::motion::any_running());
    lgal::led::set(State::Service, doser::dosing::service());

    // night mode dims only the idle indication
    int minute = -1;
    if (ts.valid) {
        const time_t t = static_cast<time_t>(ts.utc);
        tm lt{};
        localtime_r(&t, &lt);
        minute = lt.tm_hour * 60 + lt.tm_min;
    }
    lgal::led::set_idle_brightness(lgal::led::idle_brightness(lgal::settings::led_night(), minute));
}

void init_hardware() {
    // stored parameters (current, microsteps) or defaults
    doser::motion::PumpConfig pumps[5];
    doser::hwconfig::load(pumps);

    // I²C: RTC + EEPROM (absence does not abort startup)
    doser::rtc::init();
    doser::eeprom::init();

    const esp_err_t err = doser::motion::init(pumps, doser::hwconfig::fitted_mask());
    if (err != ESP_OK) ESP_LOGE(TAG, "Pump hardware not ready: %s", esp_err_to_name(err));

    // Dosing engine: load settings (NVS) and run state (EEPROM)
    if (doser::dosing::init() != ESP_OK) ESP_LOGE(TAG, "Dosing engine not ready");
}

void init_time() {
    lgal::timesync::RtcHooks hooks;
    hooks.read = [](int64_t* utc) {
        bool lost = true;
        return doser::rtc::present() && doser::rtc::read(utc, &lost) == ESP_OK && !lost;
    };
    // writes go through the trim module (in phase, keeps its drift measurement)
    hooks.write = [](int64_t, bool ntp) {
        if (!doser::rtc::present()) return false;
        if (ntp) doser::rtc_trim::on_ntp();
        else doser::rtc_trim::on_manual();
        return true;
    };
    hooks.lost = [] { return doser::rtc_trim::battery_warning(); };
    doser::rtc_trim::init();
    lgal::timesync::init(hooks, CONFIG_LGAL_TIMEZONE, CONFIG_LGAL_NTP_SERVER, [](const char* tz) {
        doser::tz::Zone z;
        return doser::tz::parse(tz, &z);
    });
}
}  // namespace

extern "C" void app_main(void) {
    // 1. Safe hardware state: drivers OFF, STEP LOW
    doser::safe_init();
    lgal::entropy::init();  // true random numbers also with the radio off

    // 2. Status LED (boot = fade in white). GPIO45 only now, after boot.
    lgal::led::Config led_cfg{};
    led_cfg.red = doser::pins::kLedRed;
    led_cfg.green = doser::pins::kLedGreen;
    led_cfg.blue = doser::pins::kLedBlue;
    lgal::led::init(led_cfg);

    // 3. Basic services
    lgal::logbuf::init();
    ESP_ERROR_CHECK(lgal::settings::init());
    lgal::device::init();
    lgal::auth::init();
    lgal::events::set_hook(log_core_event);
    print_banner();
    // event history on the LittleFS partition
    if (doser::history::init() != ESP_OK) ESP_LOGE(TAG, "History not available");

    // 4. Pumps, RTC (drivers stay off until a move starts)
    init_hardware();

    // 5. Network and time
    lgal::net::EthConfig eth{};
    eth.spi_host = doser::pins::kEthSpiHost;
    eth.mosi = doser::pins::kEthMosi;
    eth.miso = doser::pins::kEthMiso;
    eth.sclk = doser::pins::kEthSclk;
    eth.cs = doser::pins::kEthCs;
    eth.int_gpio = doser::pins::kEthInt;
    eth.clock_mhz = CONFIG_LGAL_ETH_SPI_CLOCK_MHZ;
    ESP_ERROR_CHECK(lgal::net::init(eth));
    lgal::captive::start();
    init_time();
    // Schedules: needs the dosing engine and the time zone
    if (doser::scheduler::init() != ESP_OK) ESP_LOGE(TAG, "Scheduler not ready");
    doser::mqtt_ha::init();
    if (lgal::mqtt::init() != ESP_OK) ESP_LOGE(TAG, "MQTT not available");

    if (!lgal::settings::setup_completed()) {
        // Factory state: setup AP permanently open until the setup
        // wizard has been completed.
        lgal::net::start_setup_ap(false);
    }

    // 6. Web server, system API, dosing and hardware test, OTA
    ESP_ERROR_CHECK(lgal::http::start());
    lgal::auth::register_handlers(lgal::http::handle());
    lgal::sysapi::register_handlers(lgal::http::handle());
    lgal::netapi::register_handlers(lgal::http::handle());
    lgal::mqtt::register_handlers(lgal::http::handle());
    lgal::captive::register_handlers(lgal::http::handle());
    lgal::setup::register_handlers(lgal::http::handle());
    lgal::ws::register_handlers(lgal::http::handle());
    webui::register_handlers(lgal::http::handle());
    doser::hwtest::register_handlers(lgal::http::handle());
    doser::dosing_api::register_handlers(lgal::http::handle());
    doser::schedule_api::register_handlers(lgal::http::handle());
    doser::history_api::register_handlers(lgal::http::handle());
    doser::backup_api::register_handlers(lgal::http::handle());
    doser::engineering::register_handlers(lgal::http::handle());
    lgal::sysapi::add_link("app", "Web interface", "/");
    lgal::sysapi::add_link("network", "Network", "/network");
    lgal::sysapi::add_link("setup", "Setup wizard", "/setup");
    lgal::sysapi::add_link("account", "Account", "/login");

    lgal::ota::set_guard([](std::string& reason) {
        if (doser::motion::any_running() || doser::dosing::busy()) {
            reason = "pump_running";
            return false;
        }
        return true;
    });
    lgal::ota::set_lock_hook([](bool locked) {
        doser::motion::lock(locked);
    });
    lgal::ota::set_change_hook(doser::mqtt_ha::update_changed);
    lgal::ota::register_handlers(lgal::http::handle());

    // A new image only counts as valid once the device is reachable.
    lgal::ota::start_rollback_guard([] {
        const auto ns = lgal::net::status();
        return lgal::http::handle() != nullptr && (ns.active != lgal::net::Iface::None || ns.ap_active);
    });

    // 7. Button: 3 s = setup AP, 10 s = factory reset
    lgal::button::Config btn{};
    btn.gpio = doser::pins::kButton;
    btn.on_setup_ap = [] { lgal::net::start_setup_ap(true); };
    btn.on_factory_reset = [] { doser::engineering::factory_reset(); };
    lgal::button::init(btn);

    vTaskDelay(pdMS_TO_TICKS(1500));  // let the boot fade-in finish
    lgal::led::set(lgal::led::State::Boot, false);
    ESP_LOGI(TAG, "System ready");

    while (true) {
        update_status_led();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
