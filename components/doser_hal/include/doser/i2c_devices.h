#pragma once
// I²C bus (SDA GPIO8, SCL GPIO14, 4.7 kΩ on the board) with
//   DS3231 (0x68)   real-time clock, stores UTC
//   AT24C32 (0x57)  4 KB EEPROM (1 M write cycles) for frequently changed
//                   state from M2 on (retraction, running dose, daily counters)
// Connector J16 brings the bus out for later extensions (display).

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace doser::i2c {
esp_err_t init();
bool probe(uint8_t addr);
}  // namespace doser::i2c

namespace doser::rtc {
constexpr uint8_t kAddr = 0x68;

esp_err_t init();
bool present();
// time_lost = oscillator stopped (battery empty/missing) -> value unusable
esp_err_t read(int64_t* utc, bool* time_lost);
esp_err_t write(int64_t utc);  // also clears the "time lost" flag
esp_err_t temperature(float* celsius);
esp_err_t aging(int8_t* offset);
esp_err_t set_aging(int8_t offset);  // drift fine adjustment, ~0.1 ppm/LSB
// Waits for the next RTC second (max. 1.5 s) and returns RTC time minus
// system time at that edge in ms (resolution about 1 ms)
esp_err_t edge_offset(int32_t* offset_ms);
// Writes the system time exactly at the start of a system second, so the
// RTC second runs in phase with it (blocks up to 1 s)
esp_err_t write_aligned();
}  // namespace doser::rtc

namespace doser::eeprom {
constexpr uint8_t kAddr = 0x57;
constexpr size_t kSize = 4096;
constexpr size_t kPage = 32;

esp_err_t init();
bool present();
esp_err_t read(uint16_t addr, void* data, size_t len);
esp_err_t write(uint16_t addr, const void* data, size_t len);
// Write/read test on the last page, content is restored
esp_err_t self_test();
// Whole EEPROM to 0xFF (factory reset)
esp_err_t erase();
}  // namespace doser::eeprom
