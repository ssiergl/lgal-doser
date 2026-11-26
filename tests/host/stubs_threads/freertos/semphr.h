// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "freertos/FreeRTOS.h"
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new HostSemaphore(1); }
inline SemaphoreHandle_t xSemaphoreCreateBinary() { return new HostSemaphore(0); }
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks) {
    std::unique_lock<std::mutex> l(s->m);
    const auto available = [s] { return s->count > 0; };
    if (ticks == portMAX_DELAY) s->cv.wait(l, available);
    else if (!s->cv.wait_for(l, std::chrono::milliseconds(ticks), available)) return pdFALSE;
    --s->count;
    return pdTRUE;
}
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t s) {
    {
        std::lock_guard<std::mutex> l(s->m);
        s->count = 1;
    }
    s->cv.notify_one();
    return pdTRUE;
}
