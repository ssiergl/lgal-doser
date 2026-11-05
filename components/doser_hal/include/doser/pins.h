#pragma once
// Pin assignment LGAL-Doser, board v1.0 (11/2026).
// Source: CLAUDE.md / schematic "Dosingpump 5x Kamoer". Do not change.
//
// Schematic note: the net names STEP1..STEP5 run opposite to the
// pump number (net STEP5 = GPIO40 = U4 = pump 1). Only the pump number
// as printed on housing/board applies here.

#include <array>
#include "driver/gpio.h"
#include "driver/spi_common.h"

namespace doser::pins {

constexpr int kPumpCount = 5;

// STEP inputs of the drivers, index 0 = pump 1
constexpr std::array<gpio_num_t, kPumpCount> kStep = {
    GPIO_NUM_40,  // pump 1, U4
    GPIO_NUM_41,  // pump 2, U5
    GPIO_NUM_42,  // pump 3, U6
    GPIO_NUM_2,   // pump 4, U7
    GPIO_NUM_1,   // pump 5, U8
};

// EN of all drivers, active low, external pull-up 10k -> OFF after reset
constexpr gpio_num_t kDriverEnable = GPIO_NUM_21;

// TMC2209 single-wire UART (TX+RX on the same pin, 1k in series)
constexpr gpio_num_t kUartBusA = GPIO_NUM_38;  // pumps 1-4, addresses 0-3
constexpr gpio_num_t kUartBusB = GPIO_NUM_39;  // pump 5, address 0

struct TmcAddress {
    gpio_num_t bus;
    uint8_t addr;
};
constexpr std::array<TmcAddress, kPumpCount> kTmc = {{
    {kUartBusA, 0},
    {kUartBusA, 1},
    {kUartBusA, 2},
    {kUartBusA, 3},
    {kUartBusB, 0},
}};

// Ethernet W5500 (USR-ES1), SPI2 via IOMUX pins
constexpr spi_host_device_t kEthSpiHost = SPI2_HOST;
constexpr gpio_num_t kEthMosi = GPIO_NUM_11;
constexpr gpio_num_t kEthSclk = GPIO_NUM_12;
constexpr gpio_num_t kEthMiso = GPIO_NUM_13;
constexpr gpio_num_t kEthCs = GPIO_NUM_10;
constexpr gpio_num_t kEthInt = GPIO_NUM_9;
// ETH RST: no GPIO, the W5500 is reset via software

// I2C (DS3231 0x68, AT24C32 0x57, extension J16)
constexpr gpio_num_t kI2cSda = GPIO_NUM_8;
constexpr gpio_num_t kI2cScl = GPIO_NUM_14;

// RGB light, common anode, NPN low side: HIGH = color on
constexpr gpio_num_t kLedRed = GPIO_NUM_48;
constexpr gpio_num_t kLedGreen = GPIO_NUM_47;
constexpr gpio_num_t kLedBlue = GPIO_NUM_45;  // strapping pin, only drive after boot

// Button reset/setup, active low (strapping pin GPIO0)
constexpr gpio_num_t kButton = GPIO_NUM_0;

}  // namespace doser::pins
