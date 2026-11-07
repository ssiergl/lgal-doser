#pragma once
// Network: Ethernet (W5500, main connection), Wi-Fi client (fallback or
// fixed), setup SoftAP, mDNS (spec 2).
//
// Settings (lgal/net_config.h) are stored in NVS ("net"). Which interface
// runs is decided by netcfg::Policy: in "auto" the Wi-Fi client starts after
// the LAN has been down for 30 s and stops as soon as the LAN is back.
// Each interface uses DHCP or a static configuration; the DNS servers follow
// the active interface.

#include <cstdint>

#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "esp_err.h"
#include "lgal/net_config.h"

namespace lgal::net {

struct EthConfig {
    spi_host_device_t spi_host;
    gpio_num_t mosi, miso, sclk, cs, int_gpio;
    int clock_mhz;
};

// Initializes netif, event loop, Ethernet, Wi-Fi and mDNS and applies the
// stored settings. A missing or defective W5500 is not fatal (Wi-Fi or the
// setup AP keep the device reachable).
esp_err_t init(const EthConfig& eth);

// Start the setup AP. temporary=true: switches off after
// CONFIG_LGAL_SETUP_AP_TIMEOUT_MIN as soon as no client is connected.
esp_err_t start_setup_ap(bool temporary);
// Switch the setup AP off after the given time, even with clients connected
// (end of the first setup). start_setup_ap cancels it.
void stop_setup_ap_in(int seconds);

enum class Iface : uint8_t { None, Lan, Wlan };
enum class WlanState : uint8_t { Off, Connecting, Connected, Disconnected };

// Stored settings (may not be applied yet, see set_config)
netcfg::Config config();
// Validates and stores; applies after apply_delay_ms so that an HTTP
// response still reaches the client before addresses change.
// ESP_ERR_INVALID_ARG if invalid.
esp_err_t set_config(const netcfg::Config& c, int apply_delay_ms = 0);
// Addresses currently in use on an interface (host byte order, dhcp = how
// they were obtained). false if the interface has no IP address.
bool ip_info(Iface i, netcfg::IpConfig* out);

const char* iface_name(Iface i);
const char* wlan_state_name(WlanState s);

struct Status {
    bool eth_present = false;  // W5500 answered at boot
    bool eth_running = false;  // driver started (off in mode "wlan")
    bool eth_link = false;
    bool eth_has_ip = false;
    uint32_t eth_ip = 0;  // network byte order
    WlanState wlan = WlanState::Off;
    uint32_t wlan_ip = 0;      // network byte order, 0 without IP
    uint8_t wlan_reason = 0;   // last disconnect reason (wifi_err_reason_t)
    Iface active = Iface::None;
    int64_t lan_down_ms = 0;   // auto mode: how long the LAN has been down
    bool ap_active = false;
    int ap_clients = 0;
};
Status status();
// Is this client (IPv4, network byte order) connected through the open setup AP?
bool is_setup_ap_client(uint32_t ip);
// Address of the device in the setup AP (network byte order)
uint32_t setup_ap_ip();
// RSSI of the connected access point (dBm), 0 if not connected
int wlan_rssi();

// Wi-Fi scan (asynchronous; the radio is switched on for it if needed)
struct ScanResult {
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    bool secure;
};
enum class ScanState : uint8_t { Idle, Running, Done, Failed };
inline constexpr int kScanMax = 20;
esp_err_t start_scan();  // ESP_ERR_INVALID_STATE while a scan is running
// Copies up to max results (strongest first, one per SSID, hidden networks
// left out) and returns the state.
ScanState scan_results(ScanResult* out, int max, int* count);

// Connection test with credentials that are not stored yet (setup wizard).
// While it runs, the Wi-Fi client uses these credentials; afterwards the
// stored settings apply again.
enum class TestState : uint8_t { Idle, Running, Ok, Failed };
struct TestResult {
    TestState state = TestState::Idle;
    uint32_t ip = 0;       // network byte order (Ok)
    uint8_t reason = 0;    // wifi_err_reason_t (Failed), 0 = timeout
};
inline constexpr int kTestTimeoutMs = 25000;
esp_err_t start_wlan_test(const char* ssid, const char* password, const netcfg::IpConfig& ip);
TestResult wlan_test_result();

}  // namespace lgal::net
