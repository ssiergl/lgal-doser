// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

#include "doser/hwconfig.h"

#include <cstdio>

#include "doser/nvs_blob.h"
#include "doser/pins.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace doser::hwconfig {

namespace {
constexpr const char* TAG = "hwconfig";
constexpr const char* kNs = "hw";
constexpr uint16_t kVersion = 1;
uint8_t s_factory = pins::kBoardMask;        // factory population
uint8_t s_fitted = pins::kBoardMask;         // in effect since startup
uint8_t s_fitted_saved = pins::kBoardMask;   // in effect after the next restart

// u8 value from the factory partition (tools/provision.py), namespace "lgal";
// 0 when missing
uint8_t read_factory_u8(const char* key) {
    constexpr const char* kFactory = "fctry";  // also lgal::device (device.cpp)
    uint8_t v = 0;
    nvs_handle_t h;
    // already initialised by an earlier call returns ESP_OK; formatting is left to lgal::device
    if (nvs_flash_init_partition(kFactory) == ESP_OK &&
        nvs_open_from_partition(kFactory, "lgal", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u8(h, key, &v) != ESP_OK) v = 0;
        nvs_close(h);
    }
    return v;
}

// Factory population (provision.py --pumps): u8 mask "pumps", limited to the
// board. Missing or no pump of the board: all pumps of the board.
uint8_t load_factory_mask() {
    const uint8_t m = read_factory_u8("pumps") & pins::kBoardMask;
    return m ? m : pins::kBoardMask;
}

bool valid_mask(uint8_t m) { return m != 0 && (m & ~pins::kBoardMask) == 0; }
}  // namespace

void load(motion::PumpConfig (&pumps)[kMaxPumps]) {
    // until the population is saved (and again after a factory reset): factory population
    s_factory = s_fitted = s_fitted_saved = load_factory_mask();
    if (s_factory != pins::kBoardMask) ESP_LOGI(TAG, "Factory population 0x%02x", s_factory);
    char key[12];
    int stored = 0;
    for (int i = 0; i < kMaxPumps; ++i) {
        snprintf(key, sizeof(key), "pump%d", i);
        if (nvs_blob::load(kNs, key, kVersion, &pumps[i])) ++stored;
    }
    if (!stored) ESP_LOGI(TAG, "No stored hardware parameters, using defaults");
    uint8_t m = 0;
    if (nvs_blob::load(kNs, "fitted", kVersion, &m) && valid_mask(m)) s_fitted = s_fitted_saved = m;
    for (int i = 0; i < pins::kPumpCount; ++i)
        if (!fitted(i)) ESP_LOGI(TAG, "Pump %d: not fitted", i + 1);
}

uint8_t board_rev() {
    // provision.py --board-rev: u8 "board_rev" = revision times ten
    static const uint8_t rev = [] {
        const uint8_t r = read_factory_u8("board_rev");
        return r < 10 ? uint8_t(10) : r;
    }();
    return rev;
}

int board_pumps() { return pins::kPumpCount; }
uint8_t board_mask() { return pins::kBoardMask; }

uint8_t fitted_mask() { return s_fitted; }
bool fitted(int pump) { return pump >= 0 && pump < kMaxPumps && (s_fitted >> pump & 1); }
uint8_t stored_fitted_mask() { return s_fitted_saved; }
uint8_t factory_fitted_mask() { return s_factory; }

esp_err_t save_fitted(uint8_t mask) {
    if (!valid_mask(mask)) return ESP_ERR_INVALID_ARG;
    const esp_err_t err = nvs_blob::save(kNs, "fitted", kVersion, mask);
    if (err == ESP_OK) s_fitted_saved = mask;
    return err;
}

esp_err_t save_pump(int pump, const motion::PumpConfig& cfg) {
    char key[12];
    snprintf(key, sizeof(key), "pump%d", pump);
    return nvs_blob::save(kNs, key, kVersion, cfg);
}

}  // namespace doser::hwconfig
