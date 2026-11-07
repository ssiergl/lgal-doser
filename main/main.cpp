// LGAL-Doser – entry point (M1: hardware layer)
//
// The order is safety-relevant: first the pump drivers are
// switched off, only then does anything else start.

#include <cstdio>
#include <cstring>
#include <ctime>

#include "doser/alarms.h"
#include "doser/dosing.h"
#include "doser/dosing_api.h"
#include "doser/hwconfig.h"
#include "doser/hwtest_api.h"
#include "doser/i2c_devices.h"
#include "doser/motion.h"
#include "doser/pins.h"
#include "doser/posix_tz.h"
#include "doser/safe_init.h"
#include "doser/schedule_api.h"
#include "doser/schedule_core.h"
#include "doser/scheduler.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lgal/auth.h"
#include "lgal/button.h"
#include "lgal/captive.h"
#include "lgal/device.h"
#include "lgal/http.h"
#include "lgal/led.h"
#include "lgal/logbuf.h"
#include "lgal/net.h"
#include "lgal/netapi.h"
#include "lgal/ota.h"
#include "lgal/settings.h"
#include "lgal/setup.h"
#include "lgal/sysapi.h"
#include "lgal/timesync.h"
#include "lgal/ws.h"
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
    const uint16_t tube_life_h = doser::dosing::engineering().tube_life_h;
    for (int i = 0; i < 5; ++i) {
        const auto d = doser::dosing::status(i);
        const auto s = doser::scheduler::status(i);
        auto& a = in.pumps[i];
        a.active = d.settings.mode != doser::dose::Mode::Disabled;
        a.fault = d.last_result && std::strcmp(d.last_result, "fault") == 0;
        a.limit_hit_today = today >= 0 && s.last_skip_reason && std::strcmp(s.last_skip_reason, "daily_limit") == 0 &&
                            doser::sched::day_of(doser::scheduler::to_local(s.last_skip_at)) == today;
        a.tracked = d.settings.capacity_ul > 0;
        if (a.tracked) a.percent = 100.0f * static_cast<float>(d.state.remaining_ul) / static_cast<float>(d.settings.capacity_ul);
        a.remaining_days = s.remaining_days;
        if (tube_life_h) a.tube_percent = static_cast<float>(d.state.tube_ms) / 36000.0f / tube_life_h;
    }
    return in;
}

void update_status_led() {
    using lgal::led::State;
    const auto ns = lgal::net::status();
    const auto ts = lgal::timesync::status();
    // alarms every 2 s (the schedule status computes the remaining days)
    static doser::alarms::Output al;
    static int tick = 0;
    if (tick++ % 4 == 0) al = doser::alarms::evaluate(alarm_input(ts));
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

    const esp_err_t err = doser::motion::init(pumps);
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
    hooks.write = [](int64_t utc) { return doser::rtc::present() && doser::rtc::write(utc) == ESP_OK; };
    lgal::timesync::init(hooks, CONFIG_LGAL_TIMEZONE, CONFIG_LGAL_NTP_SERVER, [](const char* tz) {
        doser::tz::Zone z;
        return doser::tz::parse(tz, &z);
    });
}
}  // namespace

extern "C" void app_main(void) {
    // 1. Safe hardware state: drivers OFF, STEP LOW
    doser::safe_init();

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
    print_banner();

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
    lgal::captive::register_handlers(lgal::http::handle());
    lgal::setup::register_handlers(lgal::http::handle());
    lgal::ws::register_handlers(lgal::http::handle());
    webui::register_handlers(lgal::http::handle());
    doser::hwtest::register_handlers(lgal::http::handle());
    doser::dosing_api::register_handlers(lgal::http::handle());
    doser::schedule_api::register_handlers(lgal::http::handle());
    lgal::sysapi::add_link("app", "Web interface", "/");
    lgal::sysapi::add_link("dosing", "Dosing test", "/dosing");
    lgal::sysapi::add_link("schedule", "Schedule test", "/schedule");
    lgal::sysapi::add_link("hwtest", "Hardware test", "/hwtest");
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
    btn.on_factory_reset = [] { lgal::settings::factory_reset(); };
    lgal::button::init(btn);

    vTaskDelay(pdMS_TO_TICKS(1500));  // let the boot fade-in finish
    lgal::led::set(lgal::led::State::Boot, false);
    ESP_LOGI(TAG, "System ready");

    while (true) {
        update_status_led();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
