#include "lgal/net.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

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
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lgal/device.h"
#include "lgal/entropy.h"
#include "mdns.h"
#include "nvs.h"
#include "sdkconfig.h"

namespace lgal::net {

namespace {
constexpr const char* TAG = "net";
constexpr const char* kNs = "net";
constexpr const char* kKeyCfg = "cfg";
constexpr uint32_t kMagic = 0x434E474Cu;  // "LGNC"
constexpr uint16_t kVersion = 1;
constexpr int kBackoffMinMs = 2000;
constexpr int kBackoffMaxMs = 60000;
constexpr int kConnectTimeoutMs = 30000;
constexpr int kScanStartTimeoutMs = 15000;

struct Stored {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    netcfg::Config cfg;
};

// --- shared with the event handlers (s_mux) ---
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
Status s_status;
struct Events {
    bool sta_failed = false;  // disconnected, not by ourselves
    uint8_t reason = 0;
    bool sta_got_ip = false;
    bool scan_done = false;
} s_ev;

template <typename F>
void update(F&& f) {
    taskENTER_CRITICAL(&s_mux);
    f(s_status);
    taskEXIT_CRITICAL(&s_mux);
}

Events take_events() {
    taskENTER_CRITICAL(&s_mux);
    Events e = s_ev;
    s_ev = Events{};
    taskEXIT_CRITICAL(&s_mux);
    return e;
}

// --- everything below is guarded by s_lock ---
SemaphoreHandle_t s_lock = nullptr;
TaskHandle_t s_task = nullptr;
netcfg::Config s_cfg;     // stored (returned by config())
netcfg::Config s_active;  // in use; takes over s_cfg when the delayed apply is due
bool s_apply_pending = false;
int64_t s_apply_at_ms = 0;
netcfg::Policy s_policy;

esp_eth_handle_t s_eth = nullptr;
esp_netif_t* s_eth_netif = nullptr;
esp_netif_t* s_ap_netif = nullptr;
esp_netif_t* s_sta_netif = nullptr;
bool s_eth_running = false;

bool s_wifi_inited = false;
bool s_wifi_started = false;
wifi_mode_t s_mode = WIFI_MODE_NULL;
bool s_ap_want = false;
bool s_ap_temporary = false;
int64_t s_ap_deadline_us = 0;
int64_t s_ap_off_at_us = 0;  // forced switch-off, 0 = none

struct StaTarget {
    char ssid[netcfg::kSsidMax + 1];
    char password[netcfg::kPasswordMax + 1];
    netcfg::IpConfig ip;
};
StaTarget s_sta_applied{};
bool s_sta_has_applied = false;
bool s_sta_connecting = false;  // esp_wifi_connect called, no failure seen yet
int64_t s_retry_at_ms = 0;
int s_backoff_ms = kBackoffMinMs;
int64_t s_connect_started_ms = 0;

ScanState s_scan_state = ScanState::Idle;
bool s_scan_want = false;
int64_t s_scan_requested_ms = 0;
ScanResult s_scan[kScanMax];
int s_scan_count = 0;

bool s_test_active = false;
StaTarget s_test{};
int64_t s_test_deadline_ms = 0;
TestResult s_test_result;

struct Lock {
    Lock() { xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
    ~Lock() { xSemaphoreGiveRecursive(s_lock); }
};

int64_t now_ms() { return esp_timer_get_time() / 1000; }

void wake() {
    if (s_task) xTaskNotifyGive(s_task);
}

bool same_ip(const netcfg::IpConfig& a, const netcfg::IpConfig& b) {
    if (a.dhcp != b.dhcp) return false;
    if (a.dhcp) return true;
    return a.ip == b.ip && a.netmask == b.netmask && a.gateway == b.gateway && a.dns1 == b.dns1 && a.dns2 == b.dns2;
}

bool same_target(const StaTarget& a, const StaTarget& b) {
    return std::strcmp(a.ssid, b.ssid) == 0 && std::strcmp(a.password, b.password) == 0 && same_ip(a.ip, b.ip);
}

StaTarget target_from_config() {
    StaTarget t{};
    std::strcpy(t.ssid, s_active.ssid);
    std::strcpy(t.password, s_active.password);
    t.ip = s_active.wlan;
    return t;
}

// ---------------------------------------------------------------- events

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
    wake();
}

void on_ip_event(void*, esp_event_base_t, int32_t id, void* data) {
    if (id == IP_EVENT_ETH_GOT_IP) {
        auto* ev = static_cast<ip_event_got_ip_t*>(data);
        ESP_LOGI(TAG, "Ethernet: IP " IPSTR, IP2STR(&ev->ip_info.ip));
        const uint32_t ip = ev->ip_info.ip.addr;
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
    } else if (id == IP_EVENT_STA_GOT_IP) {
        auto* ev = static_cast<ip_event_got_ip_t*>(data);
        ESP_LOGI(TAG, "Wi-Fi: IP " IPSTR, IP2STR(&ev->ip_info.ip));
        const uint32_t ip = ev->ip_info.ip.addr;
        taskENTER_CRITICAL(&s_mux);
        s_status.wlan = WlanState::Connected;
        s_status.wlan_ip = ip;
        s_ev.sta_got_ip = true;
        taskEXIT_CRITICAL(&s_mux);
    } else if (id == IP_EVENT_STA_LOST_IP) {
        ESP_LOGW(TAG, "Wi-Fi: IP lost");
        update([](Status& s) {
            s.wlan_ip = 0;
            if (s.wlan == WlanState::Connected) s.wlan = WlanState::Connecting;
        });
    }
    wake();
}

void on_wifi_event(void*, esp_event_base_t, int32_t id, void* data) {
    switch (id) {
        case WIFI_EVENT_AP_STACONNECTED:
            update([](Status& s) { s.ap_clients++; });
            ESP_LOGI(TAG, "Setup AP: client connected");
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            update([](Status& s) {
                if (s.ap_clients > 0) s.ap_clients--;
            });
            ESP_LOGI(TAG, "Setup AP: client disconnected");
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "Wi-Fi: associated");
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            const auto* ev = static_cast<wifi_event_sta_disconnected_t*>(data);
            const uint8_t reason = ev->reason;
            // our own disconnect (reconfiguration, switching off) is no failure
            const bool own = reason == WIFI_REASON_ASSOC_LEAVE;
            if (!own) ESP_LOGW(TAG, "Wi-Fi: disconnected (reason %u)", reason);
            taskENTER_CRITICAL(&s_mux);
            if (s_status.wlan != WlanState::Off) s_status.wlan = WlanState::Disconnected;
            s_status.wlan_ip = 0;
            if (!own) {
                s_status.wlan_reason = reason;
                s_ev.sta_failed = true;
                s_ev.reason = reason;
            }
            taskEXIT_CRITICAL(&s_mux);
            break;
        }
        case WIFI_EVENT_SCAN_DONE:
            taskENTER_CRITICAL(&s_mux);
            s_ev.scan_done = true;
            taskEXIT_CRITICAL(&s_mux);
            break;
        default:
            break;
    }
    wake();
}

// ---------------------------------------------------------------- IP config

void apply_ip(esp_netif_t* netif, const netcfg::IpConfig& c) {
    if (!netif) return;
    esp_netif_dhcpc_stop(netif);  // ignore "already stopped"
    esp_netif_ip_info_t info{};
    if (c.dhcp) {
        esp_netif_set_ip_info(netif, &info);
        esp_err_t err = esp_netif_dhcpc_start(netif);
        if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED)
            ESP_LOGE(TAG, "DHCP start failed: %s", esp_err_to_name(err));
        return;
    }
    info.ip.addr = esp_netif_htonl(c.ip);
    info.netmask.addr = esp_netif_htonl(c.netmask);
    info.gw.addr = esp_netif_htonl(c.gateway);
    esp_err_t err = esp_netif_set_ip_info(netif, &info);
    if (err != ESP_OK) ESP_LOGE(TAG, "Static IP failed: %s", esp_err_to_name(err));
    const struct {
        esp_netif_dns_type_t type;
        uint32_t ip;
    } dns[] = {{ESP_NETIF_DNS_MAIN, c.dns1}, {ESP_NETIF_DNS_BACKUP, c.dns2}};
    for (const auto& d : dns) {
        esp_netif_dns_info_t di{};
        di.ip.type = ESP_IPADDR_TYPE_V4;
        di.ip.u_addr.ip4.addr = esp_netif_htonl(d.ip);
        esp_netif_set_dns_info(netif, d.type, &di);
    }
}

// ---------------------------------------------------------------- Wi-Fi

void set_ap_config() {
    wifi_config_t wc{};
    const auto& ssid = device::setup_ssid();
    const auto& pass = device::setup_password();
    std::memcpy(wc.ap.ssid, ssid.c_str(), ssid.size());
    wc.ap.ssid_len = ssid.size();
    std::memcpy(wc.ap.password, pass.c_str(), pass.size());
    wc.ap.channel = 1;  // follows the router's channel while the client is connected
    wc.ap.max_connection = 4;
    wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wc.ap.pmf_cfg.required = false;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &wc);
    if (err != ESP_OK) ESP_LOGE(TAG, "Setup AP config failed: %s", esp_err_to_name(err));
}

void apply_mode(wifi_mode_t m) {
    if (!s_wifi_inited) return;
    if (m == s_mode && (m == WIFI_MODE_NULL || s_wifi_started)) return;
    const bool had_ap = s_mode == WIFI_MODE_AP || s_mode == WIFI_MODE_APSTA;
    const bool has_ap = m == WIFI_MODE_AP || m == WIFI_MODE_APSTA;
    const bool has_sta = m == WIFI_MODE_STA || m == WIFI_MODE_APSTA;
    if (m == WIFI_MODE_NULL) {
        esp_wifi_stop();
        s_wifi_started = false;
        entropy::radio(false);
        ESP_LOGI(TAG, "Wi-Fi radio off");
    } else {
        esp_err_t err = esp_wifi_set_mode(m);
        if (err == ESP_OK && has_ap && (!had_ap || !s_wifi_started)) set_ap_config();
        if (err == ESP_OK && !s_wifi_started) {
            entropy::radio(true);
            err = esp_wifi_start();
            if (err == ESP_OK) s_wifi_started = true;
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Wi-Fi mode %d failed: %s", int(m), esp_err_to_name(err));
            if (!s_wifi_started) entropy::radio(false);
            return;
        }
    }
    s_mode = m;
    if (!has_sta) {
        s_sta_connecting = false;
        s_sta_has_applied = false;
    }
    if (had_ap != has_ap) {
        update([has_ap](Status& s) {
            s.ap_active = has_ap;
            if (!has_ap) s.ap_clients = 0;
        });
    }
}

void apply_sta(const StaTarget& t) {
    wifi_config_t wc{};
    std::memcpy(wc.sta.ssid, t.ssid, strnlen(t.ssid, sizeof(wc.sta.ssid)));
    std::memcpy(wc.sta.password, t.password, strnlen(t.password, sizeof(wc.sta.password)));
    wc.sta.threshold.authmode = t.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (err != ESP_OK) ESP_LOGE(TAG, "Wi-Fi client config failed: %s", esp_err_to_name(err));
    apply_ip(s_sta_netif, t.ip);
}

void collect_scan() {
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    uint16_t max = std::min<uint16_t>(n, 48);
    auto* recs = static_cast<wifi_ap_record_t*>(std::calloc(max ? max : 1, sizeof(wifi_ap_record_t)));
    if (!recs) {
        esp_wifi_clear_ap_list();
        s_scan_state = ScanState::Failed;
        return;
    }
    if (esp_wifi_scan_get_ap_records(&max, recs) != ESP_OK) max = 0;
    if (n > max) esp_wifi_clear_ap_list();
    s_scan_count = 0;
    for (int i = 0; i < max; ++i) {
        const auto& r = recs[i];
        char ssid[33];
        std::memcpy(ssid, r.ssid, 32);
        ssid[32] = 0;
        if (!ssid[0]) continue;  // hidden network
        int found = -1;
        for (int k = 0; k < s_scan_count; ++k)
            if (std::strcmp(s_scan[k].ssid, ssid) == 0) found = k;
        if (found >= 0) {
            if (r.rssi > s_scan[found].rssi) {
                s_scan[found].rssi = r.rssi;
                s_scan[found].channel = r.primary;
            }
            continue;
        }
        if (s_scan_count >= kScanMax) {
            // replace the weakest if this one is stronger
            int weakest = 0;
            for (int k = 1; k < s_scan_count; ++k)
                if (s_scan[k].rssi < s_scan[weakest].rssi) weakest = k;
            if (r.rssi <= s_scan[weakest].rssi) continue;
            found = weakest;
        } else {
            found = s_scan_count++;
        }
        auto& o = s_scan[found];
        std::strcpy(o.ssid, ssid);
        o.rssi = r.rssi;
        o.channel = r.primary;
        o.secure = r.authmode != WIFI_AUTH_OPEN && r.authmode != WIFI_AUTH_OWE;
    }
    std::free(recs);
    std::sort(s_scan, s_scan + s_scan_count, [](const ScanResult& a, const ScanResult& b) { return a.rssi > b.rssi; });
    s_scan_state = ScanState::Done;
    ESP_LOGI(TAG, "Wi-Fi scan: %d networks", s_scan_count);
}

// ---------------------------------------------------------------- control

void set_eth_running(bool on) {
    if (!s_eth || on == s_eth_running) return;
    if (on) {
        esp_err_t err = esp_eth_start(s_eth);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Ethernet start failed: %s", esp_err_to_name(err));
            return;
        }
        ESP_LOGI(TAG, "Ethernet on");
    } else {
        esp_eth_stop(s_eth);
        ESP_LOGI(TAG, "Ethernet off");
    }
    s_eth_running = on;
    update([on](Status& s) {
        s.eth_running = on;
        if (!on) {
            s.eth_link = false;
            s.eth_has_ip = false;
            s.eth_ip = 0;
        }
    });
}

// Brings drivers and connections in line with settings, policy, setup AP,
// scan and connection test. Idempotent; called by the net task and after
// every change.
void apply_settings() {
    s_apply_pending = false;
    const bool lan_changed = !same_ip(s_cfg.lan, s_active.lan);
    s_active = s_cfg;
    if (lan_changed && s_eth_netif) {
        apply_ip(s_eth_netif, s_active.lan);
        if (s_active.lan.dhcp) {
            update([](Status& s) {
                s.eth_has_ip = false;
                s.eth_ip = 0;
            });
        }
    }
    ESP_LOGI(TAG, "Settings applied: mode %s", netcfg::mode_name(s_active.mode));
}

void reconcile() {
    const int64_t now = now_ms();
    if (s_apply_pending && now >= s_apply_at_ms) apply_settings();
    const Status st = status();
    const Events ev = take_events();

    // setup AP timeout
    if (s_ap_want && s_ap_temporary && esp_timer_get_time() > s_ap_deadline_us && st.ap_clients == 0) {
        ESP_LOGI(TAG, "Temporary setup AP expired");
        s_ap_want = false;
    }
    if (s_ap_off_at_us && esp_timer_get_time() >= s_ap_off_at_us) {
        s_ap_off_at_us = 0;
        if (s_ap_want) ESP_LOGI(TAG, "Setup AP switched off");
        s_ap_want = false;
    }

    // LAN / WLAN selection
    netcfg::Policy::Input in;
    in.eth_present = st.eth_present;
    in.lan_up = s_eth_running && st.eth_has_ip;
    in.wlan_configured = netcfg::wlan_configured(s_active);
    const auto out = s_policy.update(s_active.mode, in, now);
    set_eth_running(out.lan);

    // scan results
    if (ev.scan_done && s_scan_state == ScanState::Running) collect_scan();

    // connection test result
    if (s_test_active) {
        TestResult r;
        if (st.wlan == WlanState::Connected && st.wlan_ip && s_sta_connecting && s_sta_has_applied &&
            same_target(s_sta_applied, s_test)) {
            r.state = TestState::Ok;
            r.ip = st.wlan_ip;
        } else if (ev.sta_failed && s_sta_connecting) {
            r.state = TestState::Failed;
            r.reason = ev.reason;
        } else if (now > s_test_deadline_ms) {
            r.state = TestState::Failed;
        }
        if (r.state != TestState::Idle) {
            s_test_active = false;
            s_test_result = r;
            ESP_LOGI(TAG, "Wi-Fi test: %s (reason %u)", r.state == TestState::Ok ? "ok" : "failed", r.reason);
        }
    }

    const bool connect = s_test_active || out.wlan;
    const bool sta_iface = connect || s_scan_want || s_scan_state == ScanState::Running;
    wifi_mode_t m = WIFI_MODE_NULL;
    if (s_ap_want) m = sta_iface ? WIFI_MODE_APSTA : WIFI_MODE_AP;
    else if (sta_iface) m = WIFI_MODE_STA;
    apply_mode(m);
    const bool sta_up = s_wifi_started && (s_mode == WIFI_MODE_STA || s_mode == WIFI_MODE_APSTA);

    // Wi-Fi client connection
    if (connect && sta_up) {
        const StaTarget t = s_test_active ? s_test : target_from_config();
        if (!s_sta_has_applied || !same_target(t, s_sta_applied)) {
            if (s_sta_connecting) esp_wifi_disconnect();
            s_sta_connecting = false;
            apply_sta(t);
            s_sta_applied = t;
            s_sta_has_applied = true;
            s_retry_at_ms = now;
            s_backoff_ms = kBackoffMinMs;
            update([](Status& s) {
                s.wlan = WlanState::Disconnected;
                s.wlan_ip = 0;
            });
        }
        if (ev.sta_got_ip) s_backoff_ms = kBackoffMinMs;
        if (ev.sta_failed && s_sta_connecting) {
            s_sta_connecting = false;
            s_retry_at_ms = now + s_backoff_ms;
            s_backoff_ms = std::min(s_backoff_ms * 2, kBackoffMaxMs);
        }
        if (s_sta_connecting && st.wlan != WlanState::Connected && now - s_connect_started_ms > kConnectTimeoutMs) {
            ESP_LOGW(TAG, "Wi-Fi: connect timeout");
            esp_wifi_disconnect();
            s_sta_connecting = false;
            s_retry_at_ms = now + s_backoff_ms;
            s_backoff_ms = std::min(s_backoff_ms * 2, kBackoffMaxMs);
        }
        if (!s_sta_connecting && now >= s_retry_at_ms && s_scan_state != ScanState::Running) {
            esp_err_t err = esp_wifi_connect();
            if (err == ESP_OK) {
                s_sta_connecting = true;
                s_connect_started_ms = now;
                ESP_LOGI(TAG, "Wi-Fi: connecting to \"%s\"", t.ssid);
                update([](Status& s) { s.wlan = WlanState::Connecting; });
            } else {
                s_retry_at_ms = now + kBackoffMinMs;
            }
        }
    } else {
        if (s_sta_connecting && sta_up) esp_wifi_disconnect();
        s_sta_connecting = false;
        update([](Status& s) {
            s.wlan = WlanState::Off;
            s.wlan_ip = 0;
        });
    }

    // scan start; a pending connection attempt is cut short (it is retried after the scan)
    if (s_scan_want && sta_up) {
        if (s_sta_connecting && st.wlan != WlanState::Connected) {
            esp_wifi_disconnect();
            s_sta_connecting = false;
            s_retry_at_ms = now;
        }
        {
            esp_err_t err = esp_wifi_scan_start(nullptr, false);
            if (err == ESP_OK) {
                s_scan_want = false;
                s_scan_state = ScanState::Running;
            } else if (err != ESP_ERR_WIFI_STATE) {
                ESP_LOGE(TAG, "Wi-Fi scan failed: %s", esp_err_to_name(err));
                s_scan_want = false;
                s_scan_state = ScanState::Failed;
            }
        }
        if (s_scan_want && now - s_scan_requested_ms > kScanStartTimeoutMs) {
            s_scan_want = false;
            s_scan_state = ScanState::Failed;
        }
    }

    update([&](Status& s) { s.lan_down_ms = s_active.mode == netcfg::Mode::Wlan ? 0 : s_policy.lan_down_ms(now); });
}

void net_task(void*) {
    while (true) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
        Lock l;
        reconcile();
    }
}

// ---------------------------------------------------------------- init

void load_config() {
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return;
    Stored st{};
    size_t len = sizeof(st);
    const esp_err_t err = nvs_get_blob(h, kKeyCfg, &st, &len);
    nvs_close(h);
    if (err != ESP_OK) return;
    netcfg::Error e;
    if (len != sizeof(st) || st.magic != kMagic || st.version != kVersion || st.size != sizeof(netcfg::Config) ||
        !netcfg::validate(st.cfg, &e)) {
        ESP_LOGW(TAG, "Stored network settings invalid, using defaults");
        return;
    }
    s_cfg = st.cfg;
    s_active = st.cfg;
}

esp_err_t save_config(const netcfg::Config& c) {
    Stored st{};
    st.magic = kMagic;
    st.version = kVersion;
    st.size = sizeof(netcfg::Config);
    st.cfg = c;
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNs, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, kKeyCfg, &st, sizeof(st));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
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
        s_eth = nullptr;
        return err;
    }

    uint8_t eth_mac[6];
    esp_read_mac(eth_mac, ESP_MAC_ETH);
    esp_eth_ioctl(s_eth, ETH_CMD_S_MAC_ADDR, eth_mac);

    esp_netif_config_t ncfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&ncfg);
    esp_netif_set_hostname(s_eth_netif, device::hostname().c_str());  // copied
    esp_netif_attach(s_eth_netif, esp_eth_new_netif_glue(s_eth));
    apply_ip(s_eth_netif, s_cfg.lan);

    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth_event, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_ip_event, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, on_ip_event, nullptr);
    return ESP_OK;
}

esp_err_t init_wifi() {
    s_ap_netif = esp_netif_create_default_wifi_ap();
    s_sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(s_ap_netif, device::hostname().c_str());
    esp_netif_set_hostname(s_sta_netif, device::hostname().c_str());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, on_ip_event, nullptr);
    s_wifi_inited = true;
    return ESP_OK;
}

// mDNS service name: the name of the device, else the product
std::string instance_name() {
    const std::string n = device::name();
    return n.empty() ? std::string("LG Aquatic Life Doser") : n;
}

void init_mdns() {
    if (mdns_init() != ESP_OK) {
        ESP_LOGE(TAG, "mDNS start failed");
        return;
    }
    mdns_hostname_set(device::hostname().c_str());
    mdns_instance_name_set(instance_name().c_str());
    mdns_txt_item_t txt[] = {
        {"vendor", "LG Aquatic Life"},
        {"type", CONFIG_LGAL_DEVICE_TYPE},
        {"id", device::short_id().c_str()},
    };
    mdns_service_add(nullptr, "_http", "_tcp", 80, txt, sizeof(txt) / sizeof(txt[0]));
    ESP_LOGI(TAG, "mDNS: %s.local", device::hostname().c_str());
}
}  // namespace

const char* iface_name(Iface i) {
    switch (i) {
        case Iface::Lan: return "lan";
        case Iface::Wlan: return "wlan";
        default: return "none";
    }
}

const char* wlan_state_name(WlanState s) {
    switch (s) {
        case WlanState::Connecting: return "connecting";
        case WlanState::Connected: return "connected";
        case WlanState::Disconnected: return "disconnected";
        default: return "off";
    }
}

esp_err_t init(const EthConfig& eth) {
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    s_lock = xSemaphoreCreateRecursiveMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    load_config();
    ESP_LOGI(TAG, "Mode %s, LAN %s, Wi-Fi %s", netcfg::mode_name(s_cfg.mode), s_cfg.lan.dhcp ? "DHCP" : "static",
             netcfg::wlan_configured(s_cfg) ? (s_cfg.wlan.dhcp ? "DHCP" : "static") : "not configured");

    err = init_ethernet(eth);
    if (err == ESP_OK) {
        update([](Status& s) { s.eth_present = true; });
        ESP_LOGI(TAG, "Ethernet (W5500) ready");
    } else {
        ESP_LOGE(TAG, "Ethernet unavailable: %s", esp_err_to_name(err));
    }

    entropy::radio(true);  // the noise source must be off while Wi-Fi sets up the radio
    err = init_wifi();
    if (err != ESP_OK) ESP_LOGE(TAG, "Wi-Fi unavailable: %s", esp_err_to_name(err));
    entropy::radio(false);  // until reconcile() starts the radio

    init_mdns();
    {
        Lock l;
        reconcile();
    }
    xTaskCreate(net_task, "net", 4096, nullptr, 3, &s_task);
    return ESP_OK;
}

esp_err_t start_setup_ap(bool temporary) {
    if (!s_wifi_inited) return ESP_ERR_INVALID_STATE;
    Lock l;
    // a permanent AP (setup not completed) stays permanent
    if (!s_ap_want || s_ap_temporary) {
        s_ap_temporary = temporary;
        s_ap_deadline_us = esp_timer_get_time() + int64_t(CONFIG_LGAL_SETUP_AP_TIMEOUT_MIN) * 60 * 1000000;
    }
    s_ap_want = true;
    s_ap_off_at_us = 0;
    reconcile();
    if (!status().ap_active) return ESP_FAIL;
    ESP_LOGI(TAG, "Setup AP active: SSID %s, http://192.168.4.1/%s", device::setup_ssid().c_str(),
             s_ap_temporary ? " (temporary)" : "");
    return ESP_OK;
}

void stop_setup_ap_in(int seconds) {
    Lock l;
    s_ap_off_at_us = esp_timer_get_time() + int64_t(seconds) * 1000000;
}

netcfg::Config config() {
    Lock l;
    return s_cfg;
}

void apply_hostname() {
    const std::string h = device::hostname();
    for (esp_netif_t* n : {s_eth_netif, s_sta_netif, s_ap_netif})
        if (n) esp_netif_set_hostname(n, h.c_str());
    mdns_hostname_set(h.c_str());
    mdns_instance_name_set(instance_name().c_str());
    ESP_LOGI(TAG, "Host name: %s.local", h.c_str());
}

esp_err_t set_config(const netcfg::Config& c, int apply_delay_ms) {
    netcfg::Error e;
    if (!netcfg::validate(c, &e)) return ESP_ERR_INVALID_ARG;
    Lock l;
    esp_err_t err = save_config(c);
    if (err != ESP_OK) return err;
    s_cfg = c;
    s_apply_pending = true;
    s_apply_at_ms = now_ms() + apply_delay_ms;
    ESP_LOGI(TAG, "Settings saved, applying in %d ms", apply_delay_ms);
    reconcile();
    return ESP_OK;
}

bool is_setup_ap_client(uint32_t ip) {
    if (!ip || !s_ap_netif || !status().ap_active) return false;
    esp_netif_ip_info_t info{};
    if (esp_netif_get_ip_info(s_ap_netif, &info) != ESP_OK) return false;
    return (ip & info.netmask.addr) == (info.ip.addr & info.netmask.addr);
}

uint32_t setup_ap_ip() {
    esp_netif_ip_info_t info{};
    if (!s_ap_netif || esp_netif_get_ip_info(s_ap_netif, &info) != ESP_OK) return 0;
    return info.ip.addr;
}

Status status() {
    taskENTER_CRITICAL(&s_mux);
    Status copy = s_status;
    taskEXIT_CRITICAL(&s_mux);
    copy.active = copy.eth_has_ip ? Iface::Lan : copy.wlan_ip ? Iface::Wlan : Iface::None;
    return copy;
}

int wlan_rssi() {
    if (status().wlan != WlanState::Connected) return 0;
    wifi_ap_record_t ap{};
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
}

bool ip_info(Iface i, netcfg::IpConfig* out) {
    const Status st = status();
    esp_netif_t* netif = nullptr;
    bool dhcp = true;
    {
        Lock l;
        if (i == Iface::Lan && st.eth_has_ip) {
            netif = s_eth_netif;
            dhcp = s_active.lan.dhcp;
        } else if (i == Iface::Wlan && st.wlan_ip) {
            netif = s_sta_netif;
            dhcp = s_sta_has_applied ? s_sta_applied.ip.dhcp : true;
        }
    }
    if (!netif) return false;
    esp_netif_ip_info_t info{};
    if (esp_netif_get_ip_info(netif, &info) != ESP_OK || !info.ip.addr) return false;
    *out = netcfg::IpConfig{};
    out->dhcp = dhcp;
    out->ip = esp_netif_htonl(info.ip.addr);
    out->netmask = esp_netif_htonl(info.netmask.addr);
    out->gateway = esp_netif_htonl(info.gw.addr);
    esp_netif_dns_info_t d{};
    if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &d) == ESP_OK && d.ip.type == ESP_IPADDR_TYPE_V4)
        out->dns1 = esp_netif_htonl(d.ip.u_addr.ip4.addr);
    if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_BACKUP, &d) == ESP_OK && d.ip.type == ESP_IPADDR_TYPE_V4)
        out->dns2 = esp_netif_htonl(d.ip.u_addr.ip4.addr);
    return true;
}

esp_err_t start_scan() {
    if (!s_wifi_inited) return ESP_ERR_INVALID_STATE;
    Lock l;
    if (s_scan_want || s_scan_state == ScanState::Running) return ESP_ERR_INVALID_STATE;
    s_scan_want = true;
    s_scan_requested_ms = now_ms();
    s_scan_count = 0;
    s_scan_state = ScanState::Running;  // reported as running until started or failed
    reconcile();
    return ESP_OK;
}

ScanState scan_results(ScanResult* out, int max, int* count) {
    Lock l;
    const int n = std::min(max, s_scan_count);
    if (s_scan_state == ScanState::Done)
        for (int i = 0; i < n; ++i) out[i] = s_scan[i];
    *count = s_scan_state == ScanState::Done ? n : 0;
    return s_scan_state;
}

esp_err_t start_wlan_test(const char* ssid, const char* password, const netcfg::IpConfig& ip) {
    if (!s_wifi_inited) return ESP_ERR_INVALID_STATE;
    netcfg::Config probe;
    netcfg::Error e;
    if (!ssid || !password || std::strlen(ssid) > size_t(netcfg::kSsidMax) ||
        std::strlen(password) > size_t(netcfg::kPasswordMax))
        return ESP_ERR_INVALID_ARG;
    std::strcpy(probe.ssid, ssid);
    std::strcpy(probe.password, password);
    probe.wlan = ip;
    if (!ssid[0] || !netcfg::validate(probe, &e)) return ESP_ERR_INVALID_ARG;
    Lock l;
    std::strcpy(s_test.ssid, ssid);
    std::strcpy(s_test.password, password);
    s_test.ip = ip;
    s_test_active = true;
    s_test_deadline_ms = now_ms() + kTestTimeoutMs;
    s_test_result = TestResult{};
    s_test_result.state = TestState::Running;
    // a connection with exactly these credentials counts only once it is (re)established
    s_sta_has_applied = false;
    ESP_LOGI(TAG, "Wi-Fi test: \"%s\"", ssid);
    reconcile();
    return ESP_OK;
}

TestResult wlan_test_result() {
    Lock l;
    return s_test_result;
}

}  // namespace lgal::net
