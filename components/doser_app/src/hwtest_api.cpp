#include "doser/hwtest_api.h"

#include <cstdio>
#include <cstring>

#include "doser/event_source.h"
#include "doser/hwconfig.h"
#include "doser/i2c_devices.h"
#include "doser/motion.h"
#include "doser/pins.h"
#include "doser/rtc_trim.h"
#include "doser/tmc2209_proto.h"
#include "esp_log.h"
#include "lgal/http.h"
#include "lgal/json.h"
#include "lgal/timesync.h"


namespace doser::hwtest {

namespace {
constexpr const char* TAG = "hwtest";
using lgal::Json;
namespace http = lgal::http;

// Invalid/missing parameter: code "invalid_param", message names the parameter
esp_err_t bad(httpd_req_t* req, const char* msg) { return http::send_error(req, "400 Bad Request", "invalid_param", msg); }

// Pumps of the board (doser/pins.h); the API covers only those
constexpr int kN = pins::kPumpCount;

// Read pump number 1..kN from ?key= -> index 0..kN-1, otherwise -1
int pump_arg(httpd_req_t* req, const char* key = "pump") {
    int64_t p = 0;
    if (!http::query_int(req, key, &p) || p < 1 || p > kN) return -1;
    return static_cast<int>(p - 1);
}

// "A (GPIO38, pumps 1-4)", "B (GPIO39, pump 5)": from the board definition
void bus_label(int bus, char* out, size_t len) {
    int first = 0, last = 0, count = 0;
    for (int i = 0; i < kN; ++i) {
        if (pins::kTmc[i].bus != bus) continue;
        if (!count++) first = i + 1;
        last = i + 1;
    }
    const int gpio = static_cast<int>(pins::kUartBus[bus]);
    const char letter = static_cast<char>('A' + bus);
    if (count == 1) std::snprintf(out, len, "%c (GPIO%d, pump %d)", letter, gpio, first);
    else if (count == last - first + 1) std::snprintf(out, len, "%c (GPIO%d, pumps %d-%d)", letter, gpio, first, last);
    else std::snprintf(out, len, "%c (GPIO%d, %d pumps)", letter, gpio, count);
}

void write_pump(Json& j, int i) {
    const auto s = motion::status(i);
    const auto c = motion::config(i);
    const auto d = tmc::decode_drv_status(s.drv_status);
    char hex[12];
    snprintf(hex, sizeof(hex), "0x%08" PRIx32, s.drv_status);
    j.obj()
        .kv("pump", i + 1)
        .kv("fitted", hwconfig::fitted(i))
        .kv("stepgen", s.stepgen ? s.stepgen : "?")
        .kv("present", s.present)
        .kv("configured", s.configured)
        .kv("version", static_cast<int>(s.version))
        .kv("running", s.running)
        .kv("reverse", s.reverse)
        .kv("position", static_cast<int64_t>(s.position))
        .kv("target", static_cast<int64_t>(s.target))
        .kv("velocity", static_cast<int64_t>(s.velocity))
        .kv("rpm", c.steps_per_rev() ? s.velocity * 60.0 / c.steps_per_rev() : 0.0, 1)
        .kv("sg_result", static_cast<int>(s.sg_result))
        .kv("sg_min", static_cast<int>(s.sg_min))
        .kv("tstep", static_cast<int64_t>(s.tstep))
        .kv("pwm_scale_sum", static_cast<int>(s.pwm_scale & 0xFF))
        .kv("drv_status", hex)
        .kv("gstat", static_cast<int>(s.gstat))
        .kv("run_ma", static_cast<int>(s.run_ma))
        .kv("hold_ma", static_cast<int>(s.hold_ma));
    if (s.fault) j.kv("fault", s.fault);
    else j.null("fault");
    j.obj("flags")
        .kv("standstill", d.stst)
        .kv("stealth", d.stealth)
        .kv("cs_actual", static_cast<int>(d.cs_actual))
        .kv("overtemp_warn", d.otpw)
        .kv("overtemp", d.ot)
        .kv("short", d.short_circuit())
        .kv("open_load_a", d.ola)
        .kv("open_load_b", d.olb)
        .kv("temp_over_120", d.t120)
        .end();
    j.obj("config")
        .kv("run_ma", static_cast<int>(c.driver.run_ma))
        .kv("hold_ma", static_cast<int>(c.driver.hold_ma))
        .kv("microsteps", static_cast<int>(c.driver.microsteps))
        .kv("full_steps", static_cast<int>(c.full_steps))
        .kv("invert", c.driver.invert)
        .kv("accel", static_cast<int64_t>(c.accel_or_default()))
        .kv("v_start", static_cast<int64_t>(c.v_start_or_default()))
        .kv("steps_per_rev", static_cast<int64_t>(c.steps_per_rev()))
        .kv("max_sps", static_cast<int64_t>(c.max_sps()))
        .end();
    j.end();
}

// Channel population ?pumps=1,2,4,5 (at least one); takes effect after a restart
esp_err_t post_channels(httpd_req_t* req) {
    if (!http::authorize_engineering(req)) return ESP_OK;
    char list[24] = "";
    if (!http::query_str(req, "pumps", list, sizeof(list))) return bad(req, "pumps=1,2,... required");
    uint8_t mask = 0;
    for (const char* c = list; *c; ++c) {
        if (*c >= '1' && *c < '1' + kN) mask |= uint8_t(1u << (*c - '1'));
        else if (*c != ',') return bad(req, kN == 1 ? "pumps: 1" : "pumps: numbers 1..N of the board, separated by commas");
    }
    if (!mask) return bad(req, "pumps: at least one pump");
    const esp_err_t err = hwconfig::save_fitted(mask);
    if (err != ESP_OK) return http::send_error(req, "500 Internal Server Error", "storage_error", esp_err_to_name(err));
    ESP_LOGI(TAG, "Channel population 0x%02x saved, in effect after a restart", mask);
    note_system(req, "channels", mask);
    return http::send_json(req, "{\"status\":\"ok\",\"restart\":true}");
}

esp_err_t get_status(httpd_req_t* req) {
    if (!http::authorize_engineering(req)) return ESP_OK;
    Json j;
    j.obj();
    j.obj("board").kv("id", LGAL_BOARD_ID).kv("model", LGAL_BOARD_MODEL).kv("pumps", kN).end();
    j.kv("drivers_enabled", motion::drivers_enabled()).kv("locked", motion::locked());
    // channel population: in effect now and after the next restart
    j.arr("fitted");
    for (int i = 0; i < kN; ++i)
        if (hwconfig::fitted_mask() >> i & 1) j.v(i + 1);
    j.end().arr("fitted_next");
    for (int i = 0; i < kN; ++i)
        if (hwconfig::stored_fitted_mask() >> i & 1) j.v(i + 1);
    j.end().arr("fitted_factory");
    for (int i = 0; i < kN; ++i)
        if (hwconfig::factory_fitted_mask() >> i & 1) j.v(i + 1);
    j.end();
    j.arr("pumps");
    for (int i = 0; i < kN; ++i) write_pump(j, i);
    j.end();

    j.obj("rtc").kv("present", rtc::present());
    if (rtc::present()) {
        int64_t utc = 0;
        bool lost = true;
        float temp = 0;
        int8_t aging = 0;
        if (rtc::read(&utc, &lost) == ESP_OK) j.kv("utc", utc).kv("time_lost", lost);
        if (rtc::temperature(&temp) == ESP_OK) j.kv("temp_c", static_cast<double>(temp), 2);
        if (rtc::aging(&aging) == ESP_OK) j.kv("aging", static_cast<int>(aging));
        j.kv("battery_warning", rtc_trim::battery_warning());
        const auto t = rtc_trim::status();
        // with a time server the aging is trimmed automatically, without one it is set by hand
        j.obj("trim").kv("automatic", lgal::timesync::ntp_server()[0] != 0).kv("measuring", t.state.anchored != 0);
        if (t.state.anchored) j.kv("since", t.state.anchor_utc).kv("start_offset_ms", t.state.anchor_ms);
        if (t.measured_at) j.kv("offset_ms", t.offset_ms).kv("measured_at", t.measured_at);
        if (t.state.trim_utc)
            j.obj("last").kv("at", t.state.trim_utc).kv("ppm", static_cast<double>(t.state.trim_ppm), 2)
                .kv("from", t.state.trim_from).kv("to", t.state.trim_to).end();
        j.end();
    }
    j.end();
    j.obj("eeprom").kv("present", eeprom::present()).end();

    const auto ts = lgal::timesync::status();
    j.obj("time")
        .kv("valid", ts.valid)
        .kv("utc", ts.utc)
        .kv("source", lgal::timesync::source_str(ts.source))
        .kv("timezone", lgal::timesync::timezone())
        .end();

    j.arr("uart");
    for (int b = 0; b < motion::bus_count(); ++b) {
        const auto st = motion::bus_stats(b);
        char label[48];
        bus_label(b, label, sizeof(label));
        j.obj()
            .kv("bus", label)
            .kv("transfers", static_cast<int64_t>(st.transfers))
            .kv("timeouts", static_cast<int64_t>(st.timeouts))
            .kv("crc_errors", static_cast<int64_t>(st.crc_errors))
            .kv("echo_errors", static_cast<int64_t>(st.echo_errors))
            .end();
    }
    j.end();
    j.end();
    return http::send_json(req, j.c_str());
}

esp_err_t post_run(httpd_req_t* req) {
    if (!http::authorize_engineering(req)) return ESP_OK;
    const int p = pump_arg(req);
    if (p < 0) return bad(req, "pump=1..5 required");
    int64_t steps = 0, sps = 0, accel = 0, vstart = 0, force = 0;
    if (!http::query_int(req, "steps", &steps) || steps == 0 || steps > 20000000 || steps < -20000000)
        return bad(req, "steps=±1..20000000 required");
    if (!http::query_int(req, "sps", &sps) || sps <= 0) return bad(req, "sps (steps/s) required");
    http::query_int(req, "accel", &accel);
    http::query_int(req, "vstart", &vstart);
    http::query_int(req, "force", &force);
    if (accel < 0 || vstart < 0) return bad(req, "accel/vstart must not be negative");

    motion::Move m{};
    m.steps = static_cast<int32_t>(steps);
    m.sps = static_cast<uint32_t>(sps);
    m.accel = static_cast<uint32_t>(accel);
    m.v_start = static_cast<uint32_t>(vstart);
    m.force = force != 0;
    const esp_err_t err = motion::run(p, m);
    switch (err) {
        case ESP_OK: return http::send_json(req, "{\"status\":\"running\"}");
        case ESP_ERR_NOT_FOUND:
            return http::send_error(req, "409 Conflict", "driver_unavailable",
                                   "Driver not reachable or not configured (force=1 steps anyway)");
        case ESP_ERR_INVALID_ARG: return http::send_error(req, "400 Bad Request", "speed_out_of_range", "Speed outside 1..max_sps");
        case ESP_ERR_INVALID_STATE: return http::send_error(req, "409 Conflict", "pump_busy", "Pump already running or OTA lock active");
        default: return http::send_error(req, "500 Internal Server Error", "internal_error", esp_err_to_name(err));
    }
}

esp_err_t post_stop(httpd_req_t* req) {
    if (!http::authorize_engineering(req)) return ESP_OK;
    int64_t p = 0;
    http::query_int(req, "pump", &p);
    char mode[8] = "soft";
    http::query_str(req, "mode", mode, sizeof(mode));
    const bool hard = std::strcmp(mode, "hard") == 0;
    if (p == 0) motion::stop_all(hard);
    else if (p >= 1 && p <= kN) motion::stop(static_cast<int>(p - 1), hard);
    else return bad(req, "pump=0..N of the board");
    return http::send_json(req, hard ? "{\"status\":\"stopped\"}" : "{\"status\":\"stopping\"}");
}

esp_err_t post_pump_config(httpd_req_t* req) {
    if (!http::authorize_engineering(req)) return ESP_OK;
    const int p = pump_arg(req);
    if (p < 0) return bad(req, "pump=1..5 required");
    auto c = motion::config(p);
    int64_t v = 0;
    if (http::query_int(req, "run_ma", &v)) c.driver.run_ma = static_cast<uint16_t>(v);
    if (http::query_int(req, "hold_ma", &v)) c.driver.hold_ma = static_cast<uint16_t>(v);
    if (http::query_int(req, "microsteps", &v)) c.driver.microsteps = static_cast<uint16_t>(v);
    if (http::query_int(req, "full_steps", &v)) c.full_steps = static_cast<uint16_t>(v);
    if (http::query_int(req, "invert", &v)) c.driver.invert = v != 0;
    if (http::query_int(req, "accel", &v) && v >= 0) c.accel = static_cast<uint32_t>(v);
    if (http::query_int(req, "vstart", &v) && v >= 0) c.v_start = static_cast<uint32_t>(v);
    const esp_err_t err = motion::set_config(p, c);
    if (err == ESP_ERR_INVALID_ARG) return bad(req, "Invalid (current 50..1500 mA, hold <= run, microsteps 1..256)");
    if (err == ESP_ERR_INVALID_STATE) return http::send_error(req, "409 Conflict", "pump_busy", "Pump is running");
    hwconfig::save_pump(p, c);
    note_system(req, "engineering");
    if (err != ESP_OK) return http::send_error(req, "502 Bad Gateway", "driver_config_failed", "Saved, but driver could not be configured");
    return http::send_json(req, "{\"status\":\"ok\"}");
}

esp_err_t get_tmc(httpd_req_t* req) {
    if (!http::authorize_engineering(req)) return ESP_OK;
    const int p = pump_arg(req);
    if (p < 0) return bad(req, "pump=1..5 required");
    static const struct {
        uint8_t reg;
        const char* name;
    } regs[] = {
        {tmc::reg::GCONF, "GCONF"},         {tmc::reg::GSTAT, "GSTAT"},       {tmc::reg::IFCNT, "IFCNT"},
        {tmc::reg::IOIN, "IOIN"},           {tmc::reg::TSTEP, "TSTEP"},       {tmc::reg::SG_RESULT, "SG_RESULT"},
        {tmc::reg::MSCNT, "MSCNT"},         {tmc::reg::CHOPCONF, "CHOPCONF"}, {tmc::reg::DRV_STATUS, "DRV_STATUS"},
        {tmc::reg::PWMCONF, "PWMCONF"},     {tmc::reg::PWM_SCALE, "PWM_SCALE"}, {tmc::reg::PWM_AUTO, "PWM_AUTO"},
    };
    Json j;
    j.obj().kv("pump", p + 1).obj("registers");
    for (const auto& r : regs) {
        uint32_t v = 0;
        if (motion::read_register(p, r.reg, &v) == ESP_OK) {
            char hex[12];
            snprintf(hex, sizeof(hex), "0x%08" PRIx32, v);
            j.kv(r.name, hex);
        } else {
            j.null(r.name);
        }
    }
    j.end().end();
    return http::send_json(req, j.c_str());
}

esp_err_t post_time(httpd_req_t* req) {
    if (!http::authorize_engineering(req)) return ESP_OK;
    int64_t utc = 0;
    if (!http::query_int(req, "utc", &utc) || !lgal::timesync::set_manual(utc)) return bad(req, "utc invalid");
    note_system(req, "time_set");
    return http::send_json(req, "{\"status\":\"ok\"}");
}

esp_err_t post_aging(httpd_req_t* req) {
    if (!http::authorize_engineering(req)) return ESP_OK;
    int64_t v = 0;
    if (!http::query_int(req, "offset", &v) || v < -128 || v > 127) return bad(req, "offset=-128..127");
    // with a time server the trim owns the register
    if (lgal::timesync::ntp_server()[0])
        return http::send_error(req, "409 Conflict", "ntp_active", "Trimmed automatically from the time server");
    if (rtc::set_aging(static_cast<int8_t>(v)) != ESP_OK)
        return http::send_error(req, "503 Service Unavailable", "rtc_unavailable", "RTC not reachable");
    note_system(req, "engineering");
    return http::send_json(req, "{\"status\":\"ok\"}");
}

esp_err_t post_eeprom_test(httpd_req_t* req) {
    if (!http::authorize_engineering(req)) return ESP_OK;
    const esp_err_t err = eeprom::self_test();
    ESP_LOGI(TAG, "EEPROM test: %s", esp_err_to_name(err));
    if (err != ESP_OK) return http::send_error(req, "500 Internal Server Error", "eeprom_test_failed", esp_err_to_name(err));
    return http::send_json(req, "{\"status\":\"ok\"}");
}
}  // namespace

esp_err_t register_handlers(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_STATE;
    http::route(server, "/api/v1/hw/status", HTTP_GET, get_status);
    http::route(server, "/api/v1/hw/channels", HTTP_POST, post_channels);
    http::route(server, "/api/v1/hw/pump/run", HTTP_POST, post_run);
    http::route(server, "/api/v1/hw/pump/stop", HTTP_POST, post_stop);
    http::route(server, "/api/v1/hw/pump/config", HTTP_POST, post_pump_config);
    http::route(server, "/api/v1/hw/tmc", HTTP_GET, get_tmc);
    http::route(server, "/api/v1/hw/time", HTTP_POST, post_time);
    http::route(server, "/api/v1/hw/rtc/aging", HTTP_POST, post_aging);
    http::route(server, "/api/v1/hw/eeprom/test", HTTP_POST, post_eeprom_test);
    return ESP_OK;
}

}  // namespace doser::hwtest
