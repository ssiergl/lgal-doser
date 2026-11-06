#pragma once
// Host stub: mutex as no-op, the task is executed step by step by the test
#include <cstdint>
typedef void* SemaphoreHandle_t;
typedef int BaseType_t;
typedef uint32_t TickType_t;
#define pdPASS 1
#define pdTRUE 1
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(ms) (ms)
