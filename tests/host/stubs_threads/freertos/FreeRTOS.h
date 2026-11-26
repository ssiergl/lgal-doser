// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
// Host stub with real threads (tests/host/test_motion.cpp): a semaphore is a mutex with a
// condition variable, a task a detached thread, a tick one millisecond
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
struct HostSemaphore {
    explicit HostSemaphore(int initial) : count(initial) {}
    std::mutex m;
    std::condition_variable cv;
    int count;
};
typedef HostSemaphore* SemaphoreHandle_t;
typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef void* TaskHandle_t;
#define pdPASS 1
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(ms) (ms)
