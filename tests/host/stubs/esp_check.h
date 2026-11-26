// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "esp_err.h"
#include "esp_log.h"
#define ESP_RETURN_ON_ERROR(x, tag, fmt, ...)                       \
    do {                                                            \
        const esp_err_t err_rc_ = (x);                              \
        if (err_rc_ != ESP_OK) {                                    \
            ESP_LOGE(tag, "%s: " fmt, __func__, ##__VA_ARGS__);     \
            return err_rc_;                                         \
        }                                                           \
    } while (0)
