// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "freertos/FreeRTOS.h"
typedef void (*TaskFunction_t)(void*);
// The test remembers the task function; vTaskDelay ends one pass
// via an exception, so the endless loop runs exactly once per call.
struct TaskYield {};
BaseType_t xTaskCreate(TaskFunction_t fn, const char*, uint32_t, void*, int, void*);
inline void vTaskDelay(TickType_t) { throw TaskYield{}; }
