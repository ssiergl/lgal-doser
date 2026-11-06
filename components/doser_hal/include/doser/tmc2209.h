#pragma once
// TMC2209 via single-wire UART (half duplex).
//
// Circuit: ONE GPIO per bus, via 1 kΩ to PDN_UART of all drivers on the bus.
// The UART sends and receives on the same pin. Sent bytes come back as
// echo (checked and discarded). For the driver's reply the
// output is switched off (otherwise the ESP would hold the line via the 1 kΩ
// against the driver) and switched on again afterwards.

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
        uint32_t crc_errors;  // reply corrupted
        uint32_t echo_errors; // wrong echo -> line shorted or similar
    };

    esp_err_t init(uart_port_t port, gpio_num_t pin, uint32_t baud);
    esp_err_t write(uint8_t addr, uint8_t reg, uint32_t value);
    esp_err_t read(uint8_t addr, uint8_t reg, uint32_t* value);
    Stats stats() const { return stats_; }

private:
    esp_err_t transfer(const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len);
    void drive();    // pin drives (UART TX connected)
    void release();  // pin high-impedance (pull-up), driver may reply

    uart_port_t port_ = UART_NUM_MAX;
    gpio_num_t pin_ = GPIO_NUM_NC;
    uint32_t byte_us_ = 87;
    SemaphoreHandle_t mutex_ = nullptr;
    Stats stats_{};
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
