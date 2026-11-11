#include "doser/i2c_devices.h"

#include <sys/time.h>

#include <algorithm>
#include <climits>
#include <cstring>

#include "doser/civil_time.h"
#include "doser/pins.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace doser {

namespace {
constexpr const char* TAG = "i2c";
constexpr int kTimeoutMs = 50;
constexpr uint32_t kSpeedHz = 100000;  // conservative because of the J16 extension cable

i2c_master_bus_handle_t s_bus = nullptr;
i2c_master_dev_handle_t s_rtc = nullptr;
i2c_master_dev_handle_t s_eeprom = nullptr;
bool s_lost_at_boot = false;

esp_err_t add_device(uint8_t addr, i2c_master_dev_handle_t* dev) {
    i2c_device_config_t dc{};
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address = addr;
    dc.scl_speed_hz = kSpeedHz;
    return i2c_master_bus_add_device(s_bus, &dc, dev);
}

uint8_t bcd2bin(uint8_t v) { return static_cast<uint8_t>((v >> 4) * 10 + (v & 0x0F)); }
uint8_t bin2bcd(int v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); }

esp_err_t rtc_read_regs(uint8_t reg, uint8_t* buf, size_t len) {
    if (!s_rtc) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(s_rtc, &reg, 1, buf, len, kTimeoutMs);
}

esp_err_t rtc_write_regs(uint8_t reg, const uint8_t* buf, size_t len) {
    if (!s_rtc) return ESP_ERR_INVALID_STATE;
    uint8_t tmp[16];
    if (len + 1 > sizeof(tmp)) return ESP_ERR_INVALID_SIZE;
    tmp[0] = reg;
    std::memcpy(tmp + 1, buf, len);
    return i2c_master_transmit(s_rtc, tmp, len + 1, kTimeoutMs);
}
}  // namespace

// ---------------------------------------------------------------------------
namespace i2c {
esp_err_t init() {
    if (s_bus) return ESP_OK;
    i2c_master_bus_config_t bc{};
    bc.i2c_port = I2C_NUM_0;
    bc.sda_io_num = pins::kI2cSda;
    bc.scl_io_num = pins::kI2cScl;
    bc.clk_source = I2C_CLK_SRC_DEFAULT;
    bc.glitch_ignore_cnt = 7;
    bc.flags.enable_internal_pullup = 0;  // 4.7 kΩ external
    return i2c_new_master_bus(&bc, &s_bus);
}

bool probe(uint8_t addr) { return s_bus && i2c_master_probe(s_bus, addr, kTimeoutMs) == ESP_OK; }
}  // namespace i2c

// ---------------------------------------------------------------------------
namespace rtc {
esp_err_t init() {
    ESP_RETURN_ON_ERROR(i2c::init(), TAG, "bus");
    if (!i2c::probe(kAddr)) {
        ESP_LOGW(TAG, "DS3231 (0x68) not found");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_RETURN_ON_ERROR(add_device(kAddr, &s_rtc), TAG, "rtc dev");
    // Control: oscillator on, no square wave, alarms off (INTCN=1)
    const uint8_t ctrl = 0x1C;
    rtc_write_regs(0x0E, &ctrl, 1);
    uint8_t status = 0;
    s_lost_at_boot = rtc_read_regs(0x0F, &status, 1) == ESP_OK && (status & 0x80);
    ESP_LOGI(TAG, "DS3231 found%s", s_lost_at_boot ? ", oscillator had stopped (time lost)" : "");
    return ESP_OK;
}

bool present() { return s_rtc != nullptr; }

esp_err_t read(int64_t* utc, bool* time_lost) {
    uint8_t r[7];
    ESP_RETURN_ON_ERROR(rtc_read_regs(0x00, r, sizeof(r)), TAG, "rtc read");
    uint8_t status = 0;
    ESP_RETURN_ON_ERROR(rtc_read_regs(0x0F, &status, 1), TAG, "rtc status");
    civil::DateTime t{};
    t.second = bcd2bin(r[0] & 0x7F);
    t.minute = bcd2bin(r[1] & 0x7F);
    if (r[2] & 0x40) {  // 12 h mode (never set by us, but read robustly)
        int h = bcd2bin(r[2] & 0x1F) % 12;
        if (r[2] & 0x20) h += 12;
        t.hour = h;
    } else {
        t.hour = bcd2bin(r[2] & 0x3F);
    }
    t.day = bcd2bin(r[4] & 0x3F);
    t.month = bcd2bin(r[5] & 0x1F);
    t.year = 2000 + bcd2bin(r[6]) + ((r[5] & 0x80) ? 100 : 0);
    const bool sane = t.month >= 1 && t.month <= 12 && t.day >= 1 && t.day <= 31 && t.hour < 24 && t.minute < 60 &&
                      t.second < 60;
    *utc = civil::to_unix(t);
    *time_lost = (status & 0x80) != 0 || !sane;
    return ESP_OK;
}

esp_err_t write(int64_t utc) {
    const auto t = civil::from_unix(utc);
    if (t.year < 2000 || t.year > 2199) return ESP_ERR_INVALID_ARG;
    uint8_t r[7];
    r[0] = bin2bcd(t.second);
    r[1] = bin2bcd(t.minute);
    r[2] = bin2bcd(t.hour);  // 24 h mode
    r[3] = static_cast<uint8_t>(t.weekday);
    r[4] = bin2bcd(t.day);
    r[5] = static_cast<uint8_t>(bin2bcd(t.month) | (t.year >= 2100 ? 0x80 : 0));
    r[6] = bin2bcd(t.year % 100);
    ESP_RETURN_ON_ERROR(rtc_write_regs(0x00, r, sizeof(r)), TAG, "rtc write");
    // clear OSF, 32 kHz output off (saves battery)
    uint8_t status = 0;
    ESP_RETURN_ON_ERROR(rtc_read_regs(0x0F, &status, 1), TAG, "rtc status");
    status &= static_cast<uint8_t>(~(0x80 | 0x08));
    return rtc_write_regs(0x0F, &status, 1);
}

esp_err_t temperature(float* celsius) {
    uint8_t r[2];
    ESP_RETURN_ON_ERROR(rtc_read_regs(0x11, r, 2), TAG, "rtc temp");
    *celsius = static_cast<int8_t>(r[0]) + (r[1] >> 6) * 0.25f;
    return ESP_OK;
}

bool lost_at_boot() { return s_lost_at_boot; }

esp_err_t edge_offset(int32_t* offset_ms) {
    uint8_t first = 0, sec = 0;
    ESP_RETURN_ON_ERROR(rtc_read_regs(0x00, &first, 1), TAG, "rtc sec");
    const int64_t until = esp_timer_get_time() + 1500000;
    do {
        vTaskDelay(1);
        ESP_RETURN_ON_ERROR(rtc_read_regs(0x00, &sec, 1), TAG, "rtc sec");
        if (sec != first) {
            timeval tv{};
            gettimeofday(&tv, nullptr);
            int64_t utc = 0;
            bool lost = true;
            ESP_RETURN_ON_ERROR(read(&utc, &lost), TAG, "rtc read");
            const int64_t diff = utc * 1000 - (static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000);
            *offset_ms = static_cast<int32_t>(std::clamp<int64_t>(diff, INT32_MIN, INT32_MAX));
            return ESP_OK;
        }
    } while (esp_timer_get_time() < until);
    return ESP_ERR_TIMEOUT;  // oscillator not running
}

esp_err_t write_aligned() {
    timeval tv{};
    gettimeofday(&tv, nullptr);
    const time_t start = tv.tv_sec;
    vTaskDelay(pdMS_TO_TICKS(std::max<int>(0, 998 - static_cast<int>(tv.tv_usec / 1000))));
    do {
        gettimeofday(&tv, nullptr);
    } while (tv.tv_sec == start);  // at most ~2 ms of polling
    return write(tv.tv_sec);
}

esp_err_t aging(int8_t* offset) {
    uint8_t v = 0;
    ESP_RETURN_ON_ERROR(rtc_read_regs(0x10, &v, 1), TAG, "rtc aging");
    *offset = static_cast<int8_t>(v);
    return ESP_OK;
}

esp_err_t set_aging(int8_t offset) {
    const uint8_t v = static_cast<uint8_t>(offset);
    ESP_RETURN_ON_ERROR(rtc_write_regs(0x10, &v, 1), TAG, "rtc aging");
    // New calibration effective at once: trigger temperature conversion (CONV)
    uint8_t ctrl = 0;
    ESP_RETURN_ON_ERROR(rtc_read_regs(0x0E, &ctrl, 1), TAG, "rtc ctrl");
    ctrl |= 0x20;
    return rtc_write_regs(0x0E, &ctrl, 1);
}
}  // namespace rtc

// ---------------------------------------------------------------------------
namespace eeprom {
namespace {
esp_err_t wait_ready() {
    // Write cycle max. 10 ms; the chip only answers again afterwards (ACK polling)
    for (int i = 0; i < 20; ++i) {
        if (i2c_master_probe(s_bus, kAddr, kTimeoutMs) == ESP_OK) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_ERR_TIMEOUT;
}
}  // namespace

esp_err_t init() {
    ESP_RETURN_ON_ERROR(i2c::init(), TAG, "bus");
    if (!i2c::probe(kAddr)) {
        ESP_LOGW(TAG, "AT24C32 (0x57) not found");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_RETURN_ON_ERROR(add_device(kAddr, &s_eeprom), TAG, "eeprom dev");
    ESP_LOGI(TAG, "AT24C32 found");
    return ESP_OK;
}

bool present() { return s_eeprom != nullptr; }

esp_err_t read(uint16_t addr, void* data, size_t len) {
    if (!s_eeprom) return ESP_ERR_INVALID_STATE;
    if (addr + len > kSize) return ESP_ERR_INVALID_SIZE;
    const uint8_t a[2] = {static_cast<uint8_t>(addr >> 8), static_cast<uint8_t>(addr)};
    return i2c_master_transmit_receive(s_eeprom, a, 2, static_cast<uint8_t*>(data), len, kTimeoutMs);
}

esp_err_t write(uint16_t addr, const void* data, size_t len) {
    if (!s_eeprom) return ESP_ERR_INVALID_STATE;
    if (addr + len > kSize) return ESP_ERR_INVALID_SIZE;
    const auto* src = static_cast<const uint8_t*>(data);
    while (len) {
        // Page writes must not cross a 32-byte boundary
        const size_t chunk = std::min(len, kPage - (addr % kPage));
        uint8_t buf[2 + kPage];
        buf[0] = static_cast<uint8_t>(addr >> 8);
        buf[1] = static_cast<uint8_t>(addr);
        std::memcpy(buf + 2, src, chunk);
        ESP_RETURN_ON_ERROR(i2c_master_transmit(s_eeprom, buf, chunk + 2, kTimeoutMs), TAG, "eeprom write");
        ESP_RETURN_ON_ERROR(wait_ready(), TAG, "eeprom busy");
        addr = static_cast<uint16_t>(addr + chunk);
        src += chunk;
        len -= chunk;
    }
    return ESP_OK;
}

esp_err_t erase() {
    uint8_t ff[kPage];
    std::memset(ff, 0xFF, sizeof(ff));
    for (size_t a = 0; a < kSize; a += kPage) ESP_RETURN_ON_ERROR(write(static_cast<uint16_t>(a), ff, kPage), TAG, "erase");
    return ESP_OK;
}

esp_err_t self_test() {
    constexpr uint16_t kTestAddr = kSize - kPage;
    uint8_t saved[kPage], pattern[kPage], check[kPage];
    ESP_RETURN_ON_ERROR(read(kTestAddr, saved, kPage), TAG, "save");
    for (size_t i = 0; i < kPage; ++i) pattern[i] = static_cast<uint8_t>(0xA5 ^ (i * 37));
    esp_err_t err = write(kTestAddr, pattern, kPage);
    if (err == ESP_OK) err = read(kTestAddr, check, kPage);
    if (err == ESP_OK && std::memcmp(pattern, check, kPage) != 0) err = ESP_ERR_INVALID_CRC;
    const esp_err_t restore = write(kTestAddr, saved, kPage);
    return err != ESP_OK ? err : restore;
}
}  // namespace eeprom

}  // namespace doser
