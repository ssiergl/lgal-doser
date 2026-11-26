// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <thread>

#include "freertos/FreeRTOS.h"
typedef void (*TaskFunction_t)(void*);
inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char*, uint32_t, void* arg, int, TaskHandle_t*, int) {
    std::thread(fn, arg).detach();
    return pdPASS;
}
inline void vTaskDelay(TickType_t ticks) { std::this_thread::sleep_for(std::chrono::milliseconds(ticks)); }
inline void vTaskDelete(TaskHandle_t) {}
