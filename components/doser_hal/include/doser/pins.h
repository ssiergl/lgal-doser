#pragma once
// Pin assignment of the board the firmware is built for. The build selects
// the board (idf.py -DBOARD=<id>, boards/<id>.cmake, default d5v1); its
// header doser/boards/<id>.h defines in namespace doser::pins:
//   kPumpCount      pumps of the board, 1..kMaxPumps
//   kStep           STEP pin per pump (index 0 = pump 1)
//   kUartBus        single-wire UART buses of the TMC2209 drivers (1 or 2)
//   kTmc            bus index and address per pump
//   kTmcRsenseMohm, kTmcBaud, kDriverEnable (EN of all drivers, active low)
//   kEth*, kI2c*, kLed*, kButton

#include <cstdint>

#include "doser/limits.h"

#ifndef LGAL_BOARD_HEADER
#error "No board selected: LGAL_BOARD_HEADER is set by the top-level CMakeLists.txt"
#endif
#include LGAL_BOARD_HEADER

namespace doser::pins {

static_assert(kPumpCount >= 1 && kPumpCount <= kMaxPumps, "1..kMaxPumps pumps per board");
static_assert(kStep.size() == kPumpCount && kTmc.size() == kPumpCount);
// UART1 and UART2 (UART0 is the console)
static_assert(kUartBus.size() >= 1 && kUartBus.size() <= 2, "1 or 2 driver buses");
// RMT (4 TX channels) for pumps 1-4, MCPWM+PCNT for pump 5 (doser/stepgen.h)
static_assert(kPumpCount <= 5, "the ESP32-S3 has step generators for 5 pumps");

// Pumps of the board as bit mask (bit i = pump i+1)
inline constexpr uint8_t kBoardMask = static_cast<uint8_t>((1u << kPumpCount) - 1);

}  // namespace doser::pins
