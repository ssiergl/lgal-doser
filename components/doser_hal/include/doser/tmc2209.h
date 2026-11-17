// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
// TMC2209 over UART, two-pin standard circuit per bus.
//
// Circuit: TX drives PDN_UART of all drivers on the bus through 1 kΩ, RX sits
// directly on the bus. TX is a normal push-pull UART output; a replying
// driver overrides it through the 1 kΩ. RX hears every request as well: the
// echo is checked and discarded before the reply is read.

#include <cstdint>

#include "doser/tmc2209_proto.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace doser {

class TmcBus {
public:
    struct Stats {
        uint32_t transfers;
        uint32_t timeouts;    // no reply (driver missing / no 24 V)
        uint32_t crc_errors;  // reply incomplete or corrupted
        uint32_t echo_errors; // wrong echo -> line shorted or similar
    };

    esp_err_t init(uart_port_t port, gpio_num_t tx, gpio_num_t rx, uint32_t baud);
    esp_err_t write(uint8_t addr, uint8_t reg, uint32_t value);
    esp_err_t read(uint8_t addr, uint8_t reg, uint32_t* value);
    Stats stats() const { return stats_; }

private:
    // rx_len == 0: write datagram without reply
    esp_err_t transfer(const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len);
    void log_bad_reply(uint8_t addr, uint8_t reg, const uint8_t* rx, size_t len);
    void log_bad_echo(const uint8_t* tx, size_t tx_len, const uint8_t* rx, int got);

    uart_port_t port_ = UART_NUM_MAX;
    gpio_num_t tx_pin_ = GPIO_NUM_NC;
    uint32_t byte_us_ = 87;
    SemaphoreHandle_t mutex_ = nullptr;
    Stats stats_{};
    uint8_t logged_silent_ = 0;   // log budget: replies without any byte
    uint8_t logged_garbage_ = 0;  // log budget: incomplete or corrupted replies
    uint8_t logged_echo_ = 0;     // log budget: wrong echo
};

class Tmc2209 {
public:
    void bind(TmcBus* bus, uint8_t addr) {
        bus_ = bus;
        addr_ = addr;
    }

    // Sets SENDDELAY and reads IOIN. ESP_OK only if VERSION = 0x21.
    esp_err_t probe(uint8_t* version = nullptr);
    // Writes all registers and checks via IFCNT that every write arrived.
    esp_err_t apply(const tmc::DriverRegs& regs);

    esp_err_t write(uint8_t reg, uint32_t value);
    esp_err_t read(uint8_t reg, uint32_t* value);

    uint8_t address() const { return addr_; }

private:
    TmcBus* bus_ = nullptr;
    uint8_t addr_ = 0;
};

}  // namespace doser
