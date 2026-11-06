#include "lgal/sysapi.h"

#include <cinttypes>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "esp_app_desc.h"
#include "esp_core_dump.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_netif_ip_addr.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lgal/device.h"
#include "lgal/http.h"
#include "lgal/logbuf.h"
#include "lgal/net.h"
#include "lgal/settings.h"
#include "lgal/json.h"
#include "lgal/timesync.h"

extern const char recovery_html_start[] asm("_binary_recovery_html_start");
extern const char recovery_html_end[] asm("_binary_recovery_html_end");

namespace lgal::sysapi {

namespace {
constexpr const char* TAG = "sysapi";
std::vector<std::pair<std::string, std::string>> s_links;

const char* reset_reason_str(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_EXT: return "EXT";
        case ESP_RST_SW: return "SW";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT: return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO: return "SDIO";
        default: return "UNKNOWN";
    }
}

const char* ota_state_str(esp_ota_img_states_t s) {
    switch (s) {
        case ESP_OTA_IMG_NEW: return "NEW";
        case ESP_OTA_IMG_PENDING_VERIFY: return "PENDING_VERIFY";
        case ESP_OTA_IMG_VALID: return "VALID";
        case ESP_OTA_IMG_INVALID: return "INVALID";
        case ESP_OTA_IMG_ABORTED: return "ABORTED";
        default: return "UNDEFINED";
    }
}

esp_err_t get_root(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, recovery_html_start, recovery_html_end - recovery_html_start);
}

esp_err_t get_info(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;

    const esp_app_desc_t* app = esp_app_get_description();
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state = ESP_OTA_IMG_UNDEFINED;
    esp_ota_get_state_partition(running, &ota_state);
    const esp_partition_t* last_invalid = esp_ota_get_last_invalid_partition();

    net::Status ns = net::status();
    esp_ip4_addr_t ip{ns.eth_ip};
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip));

    bool coredump = esp_core_dump_image_check() == ESP_OK;

    char buf[1400];
    snprintf(buf, sizeof(buf),
             "{\"device\":{\"vendor\":\"LG Aquatic Life\",\"model\":\"LGAL-Doser\",\"hostname\":\"%s\","
             "\"mac\":\"%s\",\"setup_completed\":%s},"
             "\"firmware\":{\"project\":\"%s\",\"version\":\"%s\",\"idf\":\"%s\",\"built\":\"%s %s\","
             "\"partition\":\"%s\",\"ota_state\":\"%s\",\"last_invalid_partition\":%s%s%s},"
             "\"system\":{\"uptime_s\":%" PRId64 ",\"reset_reason\":\"%s\",\"heap_free\":%u,"
             "\"heap_min_free\":%u,\"heap_largest_block\":%u,\"psram_free\":%u,\"coredump\":%s},"
             "\"network\":{\"eth_present\":%s,\"eth_link\":%s,\"eth_ip\":%s%s%s,"
             "\"ap_active\":%s,\"ap_clients\":%d}}",
             device::hostname().c_str(), device::mac_str().c_str(),
             settings::setup_completed() ? "true" : "false", app->project_name, app->version,
             esp_get_idf_version(), app->date, app->time, running ? running->label : "?",
             ota_state_str(ota_state), last_invalid ? "\"" : "", last_invalid ? last_invalid->label : "null",
             last_invalid ? "\"" : "", esp_timer_get_time() / 1000000,
             reset_reason_str(esp_reset_reason()),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), coredump ? "true" : "false",
             ns.eth_present ? "true" : "false", ns.eth_link ? "true" : "false",
             ns.eth_has_ip ? "\"" : "", ns.eth_has_ip ? ip_str : "null", ns.eth_has_ip ? "\"" : "",
             ns.ap_active ? "true" : "false", ns.ap_clients);

    // Append time and device-specific pages
    const auto ts = timesync::status();
    char local[40] = "";
    if (ts.valid) {
        time_t t = static_cast<time_t>(ts.utc);
        tm lt{};
        localtime_r(&t, &lt);
        strftime(local, sizeof(local), "%Y-%m-%d %H:%M:%S %Z", &lt);
    }
    Json extra;
    extra.obj("time")
        .kv("valid", ts.valid)
        .kv("utc", ts.utc)
        .kv("local", local)
        .kv("source", timesync::source_str(ts.source))
        .kv("last_ntp_sync", ts.last_ntp_sync)
        .kv("rtc_ok", ts.rtc_ok)
        .kv("timezone", timesync::timezone())
        .end();
    extra.arr("links");
    for (const auto& l : s_links) extra.obj().kv("name", l.first).kv("path", l.second).end();
    extra.end();
    std::string out(buf);
    out.pop_back();  // closing bracket
    out += ',';
    out += extra.str();
    out += '}';
    return http::send_json(req, out.c_str());
}

esp_err_t get_log(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    std::string log = logbuf::snapshot();
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, log.data(), log.size());
}

void restart_cb(void*) { esp_restart(); }

esp_err_t post_reboot(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    ESP_LOGW(TAG, "Reboot requested via API");
    http::send_json(req, "{\"status\":\"rebooting\"}");
    esp_timer_create_args_t args{};
    args.callback = restart_cb;
    args.name = "reboot";
    esp_timer_handle_t t;
    if (esp_timer_create(&args, &t) == ESP_OK) esp_timer_start_once(t, 1000 * 1000);
    return ESP_OK;
}

esp_err_t get_coredump(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    size_t addr = 0, size = 0;
    if (esp_core_dump_image_check() != ESP_OK || esp_core_dump_image_get(&addr, &size) != ESP_OK) {
        return http::send_error(req, "404 Not Found", "no coredump");
    }
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"coredump.elf\"");
    constexpr size_t kChunk = 2048;
    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[kChunk]);
    if (!buf) return http::send_error(req, "500 Internal Server Error", "no memory");
    for (size_t off = 0; off < size; off += kChunk) {
        size_t n = (size - off) < kChunk ? (size - off) : kChunk;
        if (esp_flash_read(nullptr, buf.get(), addr + off, n) != ESP_OK) break;
        if (httpd_resp_send_chunk(req, reinterpret_cast<const char*>(buf.get()), n) != ESP_OK) return ESP_FAIL;
    }
    return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t delete_coredump(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    esp_core_dump_image_erase();
    return http::send_json(req, "{\"status\":\"deleted\"}");
}

void reg(httpd_handle_t s, const char* uri, httpd_method_t m, esp_err_t (*h)(httpd_req_t*)) {
    httpd_uri_t u{};
    u.uri = uri;
    u.method = m;
    u.handler = h;
    httpd_register_uri_handler(s, &u);
}
}  // namespace

void add_link(const char* name, const char* path) { s_links.emplace_back(name, path); }

esp_err_t register_handlers(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_STATE;
    reg(server, "/", HTTP_GET, get_root);
    reg(server, "/recovery", HTTP_GET, get_root);
    reg(server, "/api/v1/system/info", HTTP_GET, get_info);
    reg(server, "/api/v1/system/log", HTTP_GET, get_log);
    reg(server, "/api/v1/system/reboot", HTTP_POST, post_reboot);
    reg(server, "/api/v1/system/coredump", HTTP_GET, get_coredump);
    reg(server, "/api/v1/system/coredump", HTTP_DELETE, delete_coredump);
    return ESP_OK;
}

}  // namespace lgal::sysapi
