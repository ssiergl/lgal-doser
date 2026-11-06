#pragma once
// Network: Ethernet (W5500, main connection), setup SoftAP, mDNS.
// M0: Ethernet DHCP only. Static IP and Wi-Fi client follow in M4.

#include <cstdint>
#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "esp_err.h"

namespace lgal::net {

struct EthConfig {
    spi_host_device_t spi_host;
    gpio_num_t mosi, miso, sclk, cs, int_gpio;
    int clock_mhz;
};

// Initializes netif, event loop, Ethernet and mDNS. A missing or
// defective W5500 is not fatal (the device stays reachable via AP).
esp_err_t init(const EthConfig& eth);

// Start the setup AP. temporary=true: switches off after
// CONFIG_LGAL_SETUP_AP_TIMEOUT_MIN as soon as no client is connected.
esp_err_t start_setup_ap(bool temporary);
void stop_setup_ap();

struct Status {
    bool eth_present = false;
    bool eth_link = false;
    bool eth_has_ip = false;
    uint32_t eth_ip = 0;  // network byte order
    bool ap_active = false;
    int ap_clients = 0;
};
Status status();
// Is this client (IPv4, network byte order) connected through the open setup AP?
bool is_setup_ap_client(uint32_t ip);

}  // namespace lgal::net
