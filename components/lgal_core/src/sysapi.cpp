#include "lgal/sysapi.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
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
#include "lgal/device_name.h"
#include "lgal/events.h"
#include "lgal/http.h"
#include "lgal/led.h"
#include "lgal/logbuf.h"
#include "lgal/net.h"
#include "lgal/settings.h"
#include "lgal/json.h"
#include "lgal/json_reader.h"
#include "lgal/timesync.h"

extern const char recovery_html_start[] asm("_binary_recovery_html_start");
extern const char recovery_html_end[] asm("_binary_recovery_html_end");
extern const char i18n_js_start[] asm("_binary_i18n_js_start");
extern const char i18n_js_end[] asm("_binary_i18n_js_end");

namespace lgal::sysapi {

namespace {
constexpr const char* TAG = "sysapi";
struct Link {
    std::string id, name, path;
};
std::vector<Link> s_links;

}  // namespace

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

namespace {

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

// Members of the time object (system/info and system/time)
void write_time(Json& j) {
    const auto ts = timesync::status();
    char local[40] = "";
    if (ts.valid) {
        time_t t = static_cast<time_t>(ts.utc);
        tm lt{};
        localtime_r(&t, &lt);
        strftime(local, sizeof(local), "%Y-%m-%d %H:%M:%S %Z", &lt);
    }
    j.kv("valid", ts.valid)
        .kv("utc", ts.utc)
        .kv("local", local)
        .kv("source", timesync::source_str(ts.source))
        .kv("last_ntp_sync", ts.last_ntp_sync)
        .kv("rtc_ok", ts.rtc_ok)
        .kv("rtc_lost", ts.rtc_lost)
        .kv("timezone", timesync::timezone())
        .kv("ntp_server", timesync::ntp_server());
}

void write_language(Json& j) {
    j.kv("language", settings::language()).arr("languages");
    for (const char* l : settings::kLanguages) j.v(l);
    j.end();
}

esp_err_t (*s_home)(httpd_req_t*) = nullptr;  // web UI, maintenance page if unset

esp_err_t get_root(httpd_req_t* req) {
    // factory state: the start page is the setup wizard (/recovery stays reachable)
    if (!settings::setup_completed() && std::strcmp(req->uri, "/") == 0) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/setup");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        return httpd_resp_send(req, nullptr, 0);
    }
    if (!http::authorize(req)) return ESP_OK;
    if (s_home && std::strcmp(req->uri, "/") == 0) return s_home(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    // EMBED_TXTFILES appends a terminating 0 – do not send it
    return httpd_resp_send(req, recovery_html_start, recovery_html_end - recovery_html_start - 1);
}

// Shared translation base; contains nothing confidential -> no login
esp_err_t get_i18n(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/javascript; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, i18n_js_start, i18n_js_end - i18n_js_start - 1);
}

esp_err_t get_info(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;

    const esp_app_desc_t* app = esp_app_get_description();
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state = ESP_OTA_IMG_UNDEFINED;
    esp_ota_get_state_partition(running, &ota_state);
    const esp_partition_t* last_invalid = esp_ota_get_last_invalid_partition();

    net::Status ns = net::status();
    esp_ip4_addr_t ip{ns.eth_ip}, wip{ns.wlan_ip};
    char ip_str[16], wip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip));
    snprintf(wip_str, sizeof(wip_str), IPSTR, IP2STR(&wip));

    bool coredump = esp_core_dump_image_check() == ESP_OK;

    char buf[1400];
    snprintf(buf, sizeof(buf),
             "{\"device\":{\"vendor\":\"LG Aquatic Life\",\"model\":\"LGAL-Doser\",\"hostname\":\"%s\","
             "\"mac\":\"%s\",\"setup_completed\":%s},"
             "\"firmware\":{\"project\":\"%s\",\"version\":\"%s\",\"idf\":\"%s\",\"built\":\"%s %s\","
             "\"partition\":\"%s\",\"ota_state\":\"%s\",\"last_invalid_partition\":%s%s%s},"
             "\"system\":{\"uptime_s\":%" PRId64 ",\"reset_reason\":\"%s\",\"heap_free\":%u,"
             "\"heap_min_free\":%u,\"heap_largest_block\":%u,\"psram_free\":%u,\"coredump\":%s},"
             "\"network\":{\"active\":\"%s\",\"eth_present\":%s,\"eth_link\":%s,\"eth_ip\":%s%s%s,"
             "\"wlan\":\"%s\",\"wlan_ip\":%s%s%s,\"ap_active\":%s,\"ap_clients\":%d}}",
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
             net::iface_name(ns.active), ns.eth_present ? "true" : "false", ns.eth_link ? "true" : "false",
             ns.eth_has_ip ? "\"" : "", ns.eth_has_ip ? ip_str : "null", ns.eth_has_ip ? "\"" : "",
             net::wlan_state_name(ns.wlan), ns.wlan_ip ? "\"" : "", ns.wlan_ip ? wip_str : "null",
             ns.wlan_ip ? "\"" : "", ns.ap_active ? "true" : "false", ns.ap_clients);

    // Append time and device-specific pages
    Json extra;
    extra.obj("time");
    write_time(extra);
    extra.end();
    write_language(extra);
    extra.kv("device_name", device::name());
    extra.arr("links");
    for (const auto& l : s_links) extra.obj().kv("id", l.id).kv("name", l.name).kv("path", l.path).end();
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
        return http::send_error(req, "404 Not Found", "no_coredump", "No coredump stored");
    }
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"coredump.elf\"");
    constexpr size_t kChunk = 2048;
    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[kChunk]);
    if (!buf) return http::send_error(req, "500 Internal Server Error", "no_memory", "Out of memory");
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

// Public: the login page needs the device language before signing in
esp_err_t get_language(httpd_req_t* req) {
    Json j;
    j.obj();
    write_language(j);
    j.end();
    return http::send_json(req, j.c_str());
}

esp_err_t post_language(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    char lang[8] = "";
    if (!http::query_str(req, "lang", lang, sizeof(lang)) || !settings::language_supported(lang))
        return http::send_error(req, "400 Bad Request", "invalid_param", "lang: unsupported language");
    if (settings::set_language(lang) != ESP_OK)
        return http::send_error(req, "500 Internal Server Error", "storage_error", "Could not save setting");
    events::note("settings", req);
    return get_language(req);
}

esp_err_t get_time(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    Json j;
    j.obj();
    write_time(j);
    j.end();
    return http::send_json(req, j.c_str());
}

esp_err_t param_error(httpd_req_t* req, const char* field) {
    Json j;
    j.obj().obj("error").kv("code", "invalid_param").kv("message", field).kv("field", field).end().end();
    return http::send_json(req, j.c_str(), "400 Bad Request");
}

// "YYYY-MM-DDTHH:MM[:SS]" (also with a space instead of T) -> broken-down
// time with checked ranges; tm_isdst = -1 lets mktime pick DST.
bool parse_local(const char* s, tm* out) {
    int y, mo, d, h, mi, sec = 0, n = 0;
    char sep;
    if (std::sscanf(s, "%4d-%2d-%2d%c%2d:%2d%n", &y, &mo, &d, &sep, &h, &mi, &n) != 6 || (sep != 'T' && sep != ' '))
        return false;
    if (s[n] == ':') {
        int m = 0;
        if (std::sscanf(s + n, ":%2d%n", &sec, &m) != 1) return false;
        n += m;
    }
    if (s[n] != '\0') return false;
    static constexpr int kDays[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    if (y < 2025 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > kDays[mo - 1] || (mo == 2 && !leap && d > 28) ||
        h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 || sec > 59)
        return false;
    *out = tm{};
    out->tm_year = y - 1900;
    out->tm_mon = mo - 1;
    out->tm_mday = d;
    out->tm_hour = h;
    out->tm_min = mi;
    out->tm_sec = sec;
    out->tm_isdst = -1;
    return true;
}

// {"timezone"?, "ntp_server"?, "utc"? | "local"?}: all members are checked
// before any is applied. "local" is wall time in the device's time zone (the
// new one if the request also sets it).
esp_err_t put_time(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    std::string body;
    JsonReader doc;
    JsonToken tok[16];
    if (!http::read_json(req, 512, &body, &doc, tok, 16)) return ESP_OK;
    const JsonValue root = doc.root();
    char tz[timesync::kMaxTz + 2] = "", ntp[timesync::kMaxNtp + 2] = "";
    int64_t utc = 0;
    char local_s[24] = "";
    tm local{};
    const JsonValue vtz = root["timezone"], vntp = root["ntp_server"], vutc = root["utc"], vlocal = root["local"];
    if (vtz.valid() && (!vtz.get(tz, sizeof(tz)) || !timesync::timezone_valid(tz))) return param_error(req, "timezone");
    if (vntp.valid() && (!vntp.get(ntp, sizeof(ntp)) || !timesync::ntp_server_valid(ntp)))
        return param_error(req, "ntp_server");
    if (vutc.valid() && (!vutc.get(&utc) || utc < 1735689600)) return param_error(req, "utc");
    if (vlocal.valid() && (vutc.valid() || !vlocal.get(local_s, sizeof(local_s)) || !parse_local(local_s, &local)))
        return param_error(req, "local");
    esp_err_t err = ESP_OK;
    if (vtz.valid()) err = timesync::set_timezone(tz);
    if (err == ESP_OK && vntp.valid()) err = timesync::set_ntp_server(ntp);
    if (err != ESP_OK) return http::send_error(req, "500 Internal Server Error", "storage_failed", esp_err_to_name(err));
    if (vtz.valid() || vntp.valid()) events::note("settings", req);
    if (vlocal.valid()) {
        const time_t t = mktime(&local);  // after set_timezone: uses the zone just saved
        if (t == static_cast<time_t>(-1) || !timesync::set_manual(static_cast<int64_t>(t)))
            return param_error(req, "local");
        events::note("time_set", req);
    } else if (vutc.valid()) {
        if (timesync::set_manual(utc)) events::note("time_set", req);
    }
    return get_time(req);
}

// Local minute of the day, -1 without valid time
int local_minute() {
    const auto ts = timesync::status();
    if (!ts.valid) return -1;
    time_t t = static_cast<time_t>(ts.utc);
    tm lt{};
    localtime_r(&t, &lt);
    return lt.tm_hour * 60 + lt.tm_min;
}

void fmt_hm(uint16_t minute, char (&out)[6]) {
    const unsigned h = (minute / 60) % 24, m = minute % 60;
    out[0] = char('0' + h / 10);
    out[1] = char('0' + h % 10);
    out[2] = ':';
    out[3] = char('0' + m / 10);
    out[4] = char('0' + m % 10);
    out[5] = 0;
}

// "HH:MM" -> minute of the day
bool parse_hm(const char* s, uint16_t* out) {
    unsigned h = 0, m = 0;
    char rest = 0;
    if (std::strlen(s) != 5 || std::sscanf(s, "%2u:%2u%c", &h, &m, &rest) != 2 || h > 23 || m > 59) return false;
    *out = static_cast<uint16_t>(h * 60 + m);
    return true;
}

esp_err_t send_led(httpd_req_t* req) {
    const led::NightMode n = settings::led_night();
    char start[6], end[6];
    fmt_hm(n.start_min, start);
    fmt_hm(n.end_min, end);
    Json j;
    j.obj().kv("shown", led::shown_state()).kv("idle_brightness", static_cast<int64_t>(led::idle_brightness()));
    j.obj("night")
        .kv("enabled", n.enabled)
        .kv("start", start)
        .kv("end", end)
        .kv("brightness", static_cast<int64_t>(n.brightness))
        .kv("active", n.enabled && led::in_window(n.start_min, n.end_min, local_minute()))
        .end();
    j.end();
    return http::send_json(req, j.c_str());
}

esp_err_t get_led(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    return send_led(req);
}

// {"night":{"enabled"?,"start"?,"end"?,"brightness"?}}: missing members stay
esp_err_t put_led(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    std::string body;
    JsonReader doc;
    JsonToken tok[16];
    if (!http::read_json(req, 256, &body, &doc, tok, 16)) return ESP_OK;
    const JsonValue night = doc.root()["night"];
    if (!night.valid()) return param_error(req, "night");
    led::NightMode n = settings::led_night();
    char buf[8];
    int64_t v = 0;
    if (const JsonValue x = night["enabled"]; x.valid() && !x.get(&n.enabled)) return param_error(req, "night.enabled");
    if (const JsonValue x = night["start"]; x.valid() && (!x.get(buf, sizeof(buf)) || !parse_hm(buf, &n.start_min)))
        return param_error(req, "night.start");
    if (const JsonValue x = night["end"]; x.valid() && (!x.get(buf, sizeof(buf)) || !parse_hm(buf, &n.end_min)))
        return param_error(req, "night.end");
    if (const JsonValue x = night["brightness"]; x.valid()) {
        if (!x.get(&v) || v < 0 || v > 100) return param_error(req, "night.brightness");
        n.brightness = static_cast<uint8_t>(v);
    }
    if (n.enabled && n.start_min == n.end_min) return param_error(req, "night.end");
    if (const esp_err_t err = settings::set_led_night(n); err != ESP_OK)
        return http::send_error(req, "500 Internal Server Error", "storage_error", esp_err_to_name(err));
    led::set_idle_brightness(led::idle_brightness(n, local_minute()));
    events::note("settings", req);
    return send_led(req);
}

// --- device name -------------------------------------------------------------

esp_err_t send_device(httpd_req_t* req) {
    Json j;
    j.obj().kv("name", device::name()).kv("hostname", device::hostname());
    j.kv("own_hostname", device::own_hostname()).kv("default_hostname", device::default_hostname()).end();
    return http::send_json(req, j.c_str());
}

esp_err_t get_device(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    return send_device(req);
}

std::string trimmed(const char* s) {
    std::string t(s);
    const size_t a = t.find_first_not_of(' ');
    if (a == std::string::npos) return {};
    return t.substr(a, t.find_last_not_of(' ') - a + 1);
}

// {"name"?, "own_hostname"?}: "" = no name / host name from the name
esp_err_t put_device(httpd_req_t* req) {
    if (!http::authorize(req)) return ESP_OK;
    std::string body;
    JsonReader doc;
    JsonToken tok[8];
    if (!http::read_json(req, 256, &body, &doc, tok, 8)) return ESP_OK;
    std::string name = device::name(), host = device::own_hostname();
    char buf[devname::kNameMax * 2 + 1];
    if (const JsonValue x = doc.root()["name"]; x.valid()) {
        if (!x.get(buf, sizeof(buf))) return param_error(req, "name");
        name = trimmed(buf);
        if (!devname::valid_name(name.c_str())) return param_error(req, "name");
    }
    if (const JsonValue x = doc.root()["own_hostname"]; x.valid()) {
        if (!x.get(buf, sizeof(buf))) return param_error(req, "own_hostname");
        host = trimmed(buf);
        for (auto& c : host)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        if (!host.empty() && !devname::valid_host(host.c_str())) return param_error(req, "own_hostname");
    }
    if (const esp_err_t err = device::set_names(name, host); err != ESP_OK)
        return http::send_error(req, "500 Internal Server Error", "storage_error", esp_err_to_name(err));
    net::apply_hostname();
    events::note("settings", req);
    return send_device(req);
}

void reg(httpd_handle_t s, const char* uri, httpd_method_t m, esp_err_t (*h)(httpd_req_t*)) {
    httpd_uri_t u{};
    u.uri = uri;
    u.method = m;
    u.handler = h;
    httpd_register_uri_handler(s, &u);
}
}  // namespace

void add_link(const char* id, const char* name, const char* path) { s_links.push_back({id, name, path}); }

void set_home_page(esp_err_t (*send)(httpd_req_t* req)) { s_home = send; }

esp_err_t register_handlers(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_STATE;
    reg(server, "/", HTTP_GET, get_root);
    reg(server, "/recovery", HTTP_GET, get_root);
    reg(server, "/api/v1/system/info", HTTP_GET, get_info);
    reg(server, "/api/v1/system/log", HTTP_GET, get_log);
    reg(server, "/api/v1/system/reboot", HTTP_POST, post_reboot);
    reg(server, "/api/v1/system/coredump", HTTP_GET, get_coredump);
    reg(server, "/api/v1/system/coredump", HTTP_DELETE, delete_coredump);
    reg(server, "/i18n.js", HTTP_GET, get_i18n);
    reg(server, "/api/v1/system/language", HTTP_GET, get_language);
    reg(server, "/api/v1/system/language", HTTP_POST, post_language);
    reg(server, "/api/v1/system/time", HTTP_GET, get_time);
    reg(server, "/api/v1/system/time", HTTP_PUT, put_time);
    reg(server, "/api/v1/system/led", HTTP_GET, get_led);
    reg(server, "/api/v1/system/led", HTTP_PUT, put_led);
    reg(server, "/api/v1/system/device", HTTP_GET, get_device);
    reg(server, "/api/v1/system/device", HTTP_PUT, put_device);
    return ESP_OK;
}

}  // namespace lgal::sysapi
