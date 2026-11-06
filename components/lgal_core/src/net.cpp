#include "lgal/net.h"

#include <cstring>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_eth.h"
#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lgal/device.h"
#include "mdns.h"
#include "sdkconfig.h"

namespace lgal::net {

namespace {
constexpr const char* TAG = "net";

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
Status s_status;

esp_eth_handle_t s_eth = nullptr;
esp_netif_t* s_eth_netif = nullptr;
esp_netif_t* s_ap_netif = nullptr;
bool s_wifi_inited = false;
bool s_ap_temporary = false;
int64_t s_ap_deadline_us = 0;

template <typename F>
void update(F&& f) {
    taskENTER_CRITICAL(&s_mux);
    f(s_status);
    taskEXIT_CRITICAL(&s_mux);
}

void on_eth_event(void*, esp_event_base_t, int32_t id, void*) {
    switch (id) {
        case ETHERNET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Ethernet: link up");
            update([](Status& s) { s.eth_link = true; });
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Ethernet: link lost");
            update([](Status& s) {
                s.eth_link = false;
                s.eth_has_ip = false;
                s.eth_ip = 0;
            });
            break;
        default:
            break;
    }
}

void on_ip_event(void*, esp_event_base_t, int32_t id, void* data) {
    if (id == IP_EVENT_ETH_GOT_IP) {
        auto* ev = static_cast<ip_event_got_ip_t*>(data);
        ESP_LOGI(TAG, "Ethernet: IP " IPSTR, IP2STR(&ev->ip_info.ip));
        uint32_t ip = ev->ip_info.ip.addr;
        update([ip](Status& s) {
            s.eth_has_ip = true;
            s.eth_ip = ip;
        });
    } else if (id == IP_EVENT_ETH_LOST_IP) {
        ESP_LOGW(TAG, "Ethernet: IP lost");
        update([](Status& s) {
            s.eth_has_ip = false;
            s.eth_ip = 0;
        });
    }
}

void on_wifi_event(void*, esp_event_base_t, int32_t id, void*) {
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        update([](Status& s) { s.ap_clients++; });
        ESP_LOGI(TAG, "Setup AP: client connected");
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        update([](Status& s) {
            if (s.ap_clients > 0) s.ap_clients--;
        });
        ESP_LOGI(TAG, "Setup AP: client disconnected");
    }
}

esp_err_t init_ethernet(const EthConfig& c) {
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    spi_bus_config_t bus{};
    bus.mosi_io_num = c.mosi;
    bus.miso_io_num = c.miso;
    bus.sclk_io_num = c.sclk;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    err = spi_bus_initialize(c.spi_host, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) return err;

    spi_device_interface_config_t dev{};
    dev.mode = 0;
    dev.clock_speed_hz = c.clock_mhz * 1000 * 1000;
    dev.spics_io_num = c.cs;
    dev.queue_size = 20;

    eth_w5500_config_t w5500 = ETH_W5500_DEFAULT_CONFIG(c.spi_host, &dev);
    w5500.base.int_gpio_num = c.int_gpio;

    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    mac_cfg.rx_task_stack_size = 4096;
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.reset_gpio_num = -1;  // no RST GPIO, W5500 software reset

    esp_eth_mac_t* mac = esp_eth_mac_new_w5500(&w5500, &mac_cfg);
    esp_eth_phy_t* phy = esp_eth_phy_new_w5500(&phy_cfg);
    if (!mac || !phy) return ESP_FAIL;

    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    err = esp_eth_driver_install(&eth_cfg, &s_eth);
    if (err != ESP_OK) {
        // typically: W5500 does not answer (module missing / SPI error)
        mac->del(mac);
        phy->del(phy);
        return err;
    }

    uint8_t eth_mac[6];
    esp_read_mac(eth_mac, ESP_MAC_ETH);
    esp_eth_ioctl(s_eth, ETH_CMD_S_MAC_ADDR, eth_mac);

    esp_netif_config_t ncfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&ncfg);
    esp_netif_set_hostname(s_eth_netif, device::hostname().c_str());
    esp_netif_attach(s_eth_netif, esp_eth_new_netif_glue(s_eth));

    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth_event, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_ip_event, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, on_ip_event, nullptr);

    return esp_eth_start(s_eth);
}

void init_mdns() {
    if (mdns_init() != ESP_OK) {
        ESP_LOGE(TAG, "mDNS start failed");
        return;
    }
    mdns_hostname_set(device::hostname().c_str());
    mdns_instance_name_set("LG Aquatic Life Doser");
    mdns_txt_item_t txt[] = {
        {"vendor", "LG Aquatic Life"},
        {"type", CONFIG_LGAL_DEVICE_TYPE},
        {"id", device::short_id().c_str()},
    };
    mdns_service_add(nullptr, "_http", "_tcp", 80, txt, sizeof(txt) / sizeof(txt[0]));
    ESP_LOGI(TAG, "mDNS: %s.local", device::hostname().c_str());
}

void housekeeping_task(void*) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        Status st = status();
        if (st.ap_active && s_ap_temporary && esp_timer_get_time() > s_ap_deadline_us && st.ap_clients == 0) {
            ESP_LOGI(TAG, "Temporary setup AP expired");
            stop_setup_ap();
        }
    }
}
}  // namespace

esp_err_t init(const EthConfig& eth) {
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    err = init_ethernet(eth);
    if (err == ESP_OK) {
        update([](Status& s) { s.eth_present = true; });
        ESP_LOGI(TAG, "Ethernet (W5500) started");
    } else {
        ESP_LOGE(TAG, "Ethernet unavailable: %s", esp_err_to_name(err));
    }

    init_mdns();
    xTaskCreate(housekeeping_task, "net_hk", 3072, nullptr, 2, nullptr);
    return ESP_OK;
}

esp_err_t start_setup_ap(bool temporary) {
    if (!s_wifi_inited) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        esp_netif_set_hostname(s_ap_netif, device::hostname().c_str());
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t err = esp_wifi_init(&cfg);
        if (err != ESP_OK) return err;
        esp_wifi_set_storage(WIFI_STORAGE_RAM);
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, nullptr);
        s_wifi_inited = true;
    }

    wifi_config_t wc{};
    const auto& ssid = device::setup_ssid();
    const auto& pass = device::setup_password();
    memcpy(wc.ap.ssid, ssid.c_str(), ssid.size());
    wc.ap.ssid_len = ssid.size();
    memcpy(wc.ap.password, pass.c_str(), pass.size());
    wc.ap.channel = 1;
    wc.ap.max_connection = 4;
    wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wc.ap.pmf_cfg.required = false;

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &wc);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Setup AP start failed: %s", esp_err_to_name(err));
        return err;
    }

    s_ap_temporary = temporary;
    s_ap_deadline_us = esp_timer_get_time() + int64_t(CONFIG_LGAL_SETUP_AP_TIMEOUT_MIN) * 60 * 1000000;
    update([](Status& s) { s.ap_active = true; });
    ESP_LOGI(TAG, "Setup AP active: SSID %s, http://192.168.4.1/%s", ssid.c_str(),
             temporary ? " (temporary)" : "");
    return ESP_OK;
}

void stop_setup_ap() {
    if (!s_wifi_inited) return;
    esp_wifi_stop();
    update([](Status& s) {
        s.ap_active = false;
        s.ap_clients = 0;
    });
    ESP_LOGI(TAG, "Setup AP stopped, Wi-Fi radio off");
}

bool is_setup_ap_client(uint32_t ip) {
    if (!ip || !s_ap_netif || !status().ap_active) return false;
    esp_netif_ip_info_t info{};
    if (esp_netif_get_ip_info(s_ap_netif, &info) != ESP_OK) return false;
    return (ip & info.netmask.addr) == (info.ip.addr & info.netmask.addr);
}

Status status() {
    taskENTER_CRITICAL(&s_mux);
    Status copy = s_status;
    taskEXIT_CRITICAL(&s_mux);
    return copy;
}

}  // namespace lgal::net
