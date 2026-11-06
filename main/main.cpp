// LGAL-Doser – entry point (milestone M0: OTA-capable skeleton)
//
// The order is safety-relevant: first the pump drivers are
// switched off, only then does anything else start.

#include <cstdio>

#include "doser/pins.h"
#include "doser/safe_init.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lgal/button.h"
#include "lgal/device.h"
#include "lgal/http.h"
#include "lgal/led.h"
#include "lgal/logbuf.h"
#include "lgal/net.h"
#include "lgal/ota.h"
#include "lgal/settings.h"
#include "lgal/sysapi.h"
#include "sdkconfig.h"

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
    printf(" Setup-WLAN  : %s\n", lgal::device::setup_ssid().c_str());
    printf(" Passwort    : %s   (WLAN und Web-Login 'admin')\n", lgal::device::setup_password().c_str());
    printf("========================================================\n\n");
}

void update_status_led() {
    using lgal::led::State;
    const auto ns = lgal::net::status();
    lgal::led::set(State::SetupAp, ns.ap_active);
    lgal::led::set(State::IdleLan, ns.eth_has_ip);
    // network disconnected: neither LAN IP nor setup AP
    lgal::led::set(State::Warning, !ns.eth_has_ip && !ns.ap_active);
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
    print_banner();

    // 4. Network
    lgal::net::EthConfig eth{};
    eth.spi_host = doser::pins::kEthSpiHost;
    eth.mosi = doser::pins::kEthMosi;
    eth.miso = doser::pins::kEthMiso;
    eth.sclk = doser::pins::kEthSclk;
    eth.cs = doser::pins::kEthCs;
    eth.int_gpio = doser::pins::kEthInt;
    eth.clock_mhz = CONFIG_LGAL_ETH_SPI_CLOCK_MHZ;
    ESP_ERROR_CHECK(lgal::net::init(eth));

    if (!lgal::settings::setup_completed()) {
        // Factory state: setup AP permanently open until setup (M4)
        // has been completed.
        lgal::net::start_setup_ap(false);
    }

    // 5. Web server, system API, OTA
    ESP_ERROR_CHECK(lgal::http::start());
    lgal::sysapi::register_handlers(lgal::http::handle());

    lgal::ota::set_guard([](std::string& reason) {
        // M2: this is where it is checked that no dosing is running.
        (void)reason;
        return true;
    });
    lgal::ota::set_lock_hook([](bool locked) {
        if (locked) doser::set_drivers_enabled(false);
    });
    lgal::ota::register_handlers(lgal::http::handle());

    // A new image only counts as valid once the device is reachable.
    lgal::ota::start_rollback_guard([] {
        const auto ns = lgal::net::status();
        return lgal::http::handle() != nullptr && (ns.eth_has_ip || ns.ap_active);
    });

    // 6. Button: 3 s = setup AP, 10 s = factory reset
    lgal::button::Config btn{};
    btn.gpio = doser::pins::kButton;
    btn.on_setup_ap = [] { lgal::net::start_setup_ap(true); };
    btn.on_factory_reset = [] { lgal::settings::factory_reset(); };
    lgal::button::init(btn);

    vTaskDelay(pdMS_TO_TICKS(1500));  // let the boot fade-in finish
    lgal::led::set(lgal::led::State::Boot, false);
    ESP_LOGI(TAG, "System bereit");

    while (true) {
        update_status_led();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
