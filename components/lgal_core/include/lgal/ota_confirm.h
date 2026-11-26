// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
// Confirmation of a freshly flashed image (pure logic, host-testable:
// tests/host/test_ota_manifest.cpp).

namespace lgal::ota {

// A new image proves itself: after delay_s of stable running healthy() must pass. While it
// fails, it is asked again every retry_s until wait_s more have passed: the network may still
// be coming up (router restarting, Wi-Fi still connecting). true = mark the image valid,
// false = roll back. sleep_ms() waits (vTaskDelay on the device, a simulated clock in the test).
template <class Healthy, class Sleep>
bool await_healthy(Healthy&& healthy, Sleep&& sleep_ms, int delay_s, int wait_s, int retry_s) {
    sleep_ms(delay_s * 1000);
    for (int waited = 0;; waited += retry_s) {
        if (healthy()) return true;
        if (waited >= wait_s) return false;
        sleep_ms(retry_s * 1000);
    }
}

}  // namespace lgal::ota
