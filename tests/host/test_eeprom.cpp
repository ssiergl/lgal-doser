// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// Host test of the EEPROM driver (components/doser_hal/src/i2c_devices.cpp)
// against a simulated AT24C32, with real threads: the factory reset erases
// the EEPROM while another task may be in the middle of a record, and nothing
// written afterwards may bring an old state back.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#include "doser/i2c_devices.h"
#include "driver/i2c_master.h"

static int g_fail = 0;
static int g_checks = 0;
bool g_log_verbose = false;
#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(cond)) {                                                    \
            ++g_fail;                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                 \
    } while (0)

using namespace doser;

// ---------------------------------------------------------------------------
// Simulated bus with an AT24C32 at 0x57 only
struct i2c_master_bus_t {};
struct i2c_master_dev_t {
    uint16_t addr;
};

namespace {
i2c_master_bus_t g_bus;
i2c_master_dev_t g_dev;
std::mutex g_chip;  // the chip takes one transfer at a time
uint8_t g_mem[eeprom::kSize];
std::atomic<int> g_page_writes{0};

// Gate: holds a page write at kGateAddr that is not part of the erase, so that
// the writing task stops in the middle of its record
constexpr uint16_t kGateAddr = 0x020;
std::mutex g_gate_m;
std::condition_variable g_gate_cv;
bool g_gate_armed = false, g_gate_waiting = false;

bool blank() {
    for (uint8_t b : g_mem)
        if (b != 0xFF) return false;
    return true;
}

void fill(uint8_t* buf, size_t n, uint8_t seed) {
    for (size_t i = 0; i < n; ++i) buf[i] = static_cast<uint8_t>(seed + i * 7);
}
}  // namespace

int64_t esp_timer_get_time() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t*, i2c_master_bus_handle_t* bus) {
    *bus = &g_bus;
    return ESP_OK;
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t, const i2c_device_config_t* c,
                                    i2c_master_dev_handle_t* dev) {
    g_dev.addr = c->device_address;
    *dev = &g_dev;
    return ESP_OK;
}

esp_err_t i2c_master_probe(i2c_master_bus_handle_t, uint16_t address, int) {
    return address == eeprom::kAddr ? ESP_OK : ESP_ERR_NOT_FOUND;
}

// Page write: two address bytes, then the data, which wraps within its page
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t* d, size_t n, int) {
    if (dev != &g_dev || n < 3 || n > 2 + eeprom::kPage) return ESP_FAIL;
    const uint16_t a = static_cast<uint16_t>((d[0] << 8 | d[1]) % eeprom::kSize);
    bool erasing = true;
    for (size_t i = 2; i < n; ++i) erasing = erasing && d[i] == 0xFF;
    {
        std::unique_lock<std::mutex> l(g_gate_m);
        if (g_gate_armed && a == kGateAddr && !erasing) {
            g_gate_waiting = true;
            g_gate_cv.notify_all();
            g_gate_cv.wait(l, [] { return !g_gate_armed; });
        }
    }
    std::this_thread::sleep_for(std::chrono::microseconds(20));  // transfer and write cycle
    std::lock_guard<std::mutex> l(g_chip);
    const size_t page = a - a % eeprom::kPage;
    for (size_t i = 2; i < n; ++i) g_mem[page + (a % eeprom::kPage + i - 2) % eeprom::kPage] = d[i];
    ++g_page_writes;
    return ESP_OK;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev, const uint8_t* w, size_t wn, uint8_t* r, size_t rn,
                                      int) {
    if (dev != &g_dev || wn != 2) return ESP_FAIL;
    std::lock_guard<std::mutex> l(g_chip);
    const size_t a = static_cast<size_t>(w[0] << 8 | w[1]);
    for (size_t i = 0; i < rn; ++i) r[i] = g_mem[(a + i) % eeprom::kSize];
    return ESP_OK;
}

// ---------------------------------------------------------------------------
static void test_read_write() {
    std::memset(g_mem, 0xA5, sizeof(g_mem));  // content from before
    CHECK(eeprom::init() == ESP_OK && eeprom::present());
    uint8_t out[40], in[40];
    fill(out, sizeof(out), 1);
    CHECK(eeprom::write(30, out, sizeof(out)) == ESP_OK);  // across a page boundary
    CHECK(eeprom::read(30, in, sizeof(in)) == ESP_OK && std::memcmp(in, out, sizeof(out)) == 0);
    CHECK(g_mem[29] == 0xA5 && g_mem[70] == 0xA5);
    CHECK(eeprom::write(eeprom::kSize - 4, out, 8) == ESP_ERR_INVALID_SIZE);
}

// The factory reset erases while a task is in the middle of a record (the run
// state of a pump, several pages): the erase waits for the record, and every
// write after it is refused
static void test_erase() {
    uint8_t rec[64];
    fill(rec, sizeof(rec), 9);
    {
        std::lock_guard<std::mutex> l(g_gate_m);
        g_gate_armed = true;
    }
    std::atomic<esp_err_t> written{ESP_FAIL}, erased{ESP_FAIL};
    std::thread writer([&] { written = eeprom::write(0x000, rec, sizeof(rec)); });
    {
        std::unique_lock<std::mutex> l(g_gate_m);
        g_gate_cv.wait(l, [] { return g_gate_waiting; });  // first page written, the second held
    }
    const int pages = g_page_writes;
    std::thread eraser([&] { erased = eeprom::erase(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(g_page_writes == pages);  // the erase waits for the record
    {
        std::lock_guard<std::mutex> l(g_gate_m);
        g_gate_armed = false;
    }
    g_gate_cv.notify_all();
    writer.join();
    eraser.join();
    CHECK(written == ESP_OK && erased == ESP_OK);
    CHECK(blank());
    // what a task still saves until the restart is refused
    CHECK(eeprom::write(0x640, rec, sizeof(rec)) == ESP_ERR_INVALID_STATE);
    CHECK(eeprom::write(0x000, rec, 1) == ESP_ERR_INVALID_STATE);
    CHECK(blank());
    uint8_t in[4] = {};
    CHECK(eeprom::read(0x000, in, sizeof(in)) == ESP_OK && in[0] == 0xFF && in[3] == 0xFF);
}

int main(int argc, char**) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    g_log_verbose = argc > 1;
    std::printf("EEPROM read and write\n");
    test_read_write();
    std::printf("Erase during a record (factory reset)\n");
    test_erase();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
