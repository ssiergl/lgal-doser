#pragma once
// TMC2209 – registers, UART datagrams and current calculation.
//
// Pure logic without ESP-IDF dependency (also tested in tests/host).
// Source: TMC2209 datasheet rev. 1.08.

#include <cstddef>
#include <cstdint>

namespace doser::tmc {

namespace reg {
constexpr uint8_t GCONF = 0x00;
constexpr uint8_t GSTAT = 0x01;
constexpr uint8_t IFCNT = 0x02;
constexpr uint8_t NODECONF = 0x03;
constexpr uint8_t IOIN = 0x06;
constexpr uint8_t IHOLD_IRUN = 0x10;
constexpr uint8_t TPOWERDOWN = 0x11;
constexpr uint8_t TSTEP = 0x12;
constexpr uint8_t TPWMTHRS = 0x13;
constexpr uint8_t TCOOLTHRS = 0x14;
constexpr uint8_t VACTUAL = 0x22;
constexpr uint8_t SGTHRS = 0x40;
constexpr uint8_t SG_RESULT = 0x41;
constexpr uint8_t COOLCONF = 0x42;
constexpr uint8_t MSCNT = 0x6A;
constexpr uint8_t CHOPCONF = 0x6C;
constexpr uint8_t DRV_STATUS = 0x6F;
constexpr uint8_t PWMCONF = 0x70;
constexpr uint8_t PWM_SCALE = 0x71;
constexpr uint8_t PWM_AUTO = 0x72;
}  // namespace reg

// GCONF
constexpr uint32_t GCONF_I_SCALE_ANALOG = 1u << 0;
constexpr uint32_t GCONF_INTERNAL_RSENSE = 1u << 1;
constexpr uint32_t GCONF_EN_SPREADCYCLE = 1u << 2;
constexpr uint32_t GCONF_SHAFT = 1u << 3;
constexpr uint32_t GCONF_PDN_DISABLE = 1u << 6;
constexpr uint32_t GCONF_MSTEP_REG_SELECT = 1u << 7;
constexpr uint32_t GCONF_MULTISTEP_FILT = 1u << 8;

// GSTAT
constexpr uint32_t GSTAT_RESET = 1u << 0;
constexpr uint32_t GSTAT_DRV_ERR = 1u << 1;
constexpr uint32_t GSTAT_UV_CP = 1u << 2;

constexpr uint8_t kSync = 0x05;
constexpr uint8_t kMasterAddr = 0xFF;
constexpr uint8_t kVersion = 0x21;  // IOIN[31:24] on the TMC2209

// NODECONF.SENDDELAY: 2 -> 3*8 bit times reply delay. Gives the firmware
// ~200 µs at 115200 Bd to switch the single-wire pin from sending to receiving.
constexpr uint8_t kSendDelay = 2;

// CRC8 (polynomial x^8+x^2+x+1, LSB first) per datasheet section 4.2
uint8_t crc8(const uint8_t* data, size_t len);

void build_write(uint8_t out[8], uint8_t addr, uint8_t reg, uint32_t value);
void build_read(uint8_t out[4], uint8_t addr, uint8_t reg);
// Checks sync, master address, register and CRC of an 8-byte reply.
bool parse_reply(const uint8_t in[8], uint8_t reg, uint32_t* value);

// MRES field for microsteps (1..256, power of two). 0xFF = invalid.
uint8_t mres_for(uint16_t microsteps);

// Current: I_rms = (CS+1)/32 · V_fs/(R_sense+20 mΩ) · 1/√2
struct CurrentScale {
    bool vsense;          // true: V_fs = 0.18 V (finer resolution up to ~1 A)
    uint8_t irun;         // 0..31
    uint8_t ihold;        // 0..31
    bool freewheel;       // hold current 0 -> freewheel at standstill
    uint16_t run_ma;      // actually set run current (RMS)
    uint16_t hold_ma;     // actually set hold current (RMS)
};
// Rounds down: the set current is never larger than requested.
CurrentScale current_scale(uint16_t run_ma, uint16_t hold_ma, uint16_t rsense_mohm);

struct DriverSettings {
    uint16_t run_ma = 600;
    uint16_t hold_ma = 0;
    uint16_t microsteps = 16;
    bool invert = false;  // Invert base direction (GCONF.shaft)
};

struct DriverRegs {
    uint32_t gconf;
    uint32_t chopconf;
    uint32_t ihold_irun;
    uint32_t tpowerdown;
    uint32_t tpwmthrs;
    uint32_t tcoolthrs;
    uint32_t pwmconf;
    CurrentScale current;
};
// Complete register configuration: UART mode, StealthChop, interpolation.
// reverse = this move backwards (DIR is tied to GND -> GCONF.shaft).
DriverRegs make_regs(const DriverSettings& s, uint16_t rsense_mohm, bool reverse);
uint32_t gconf_for(const DriverSettings& s, bool reverse);
uint32_t ihold_irun(uint8_t ihold, uint8_t irun, uint8_t iholddelay = 6);

struct DrvStatus {
    bool otpw, ot, s2ga, s2gb, s2vsa, s2vsb, ola, olb;
    bool t120, t143, t150, t157;
    uint8_t cs_actual;
    bool stealth, stst;
    bool short_circuit() const { return s2ga || s2gb || s2vsa || s2vsb; }
};
DrvStatus decode_drv_status(uint32_t v);

}  // namespace doser::tmc
