#include "doser/tmc2209_proto.h"

#include <cmath>

namespace doser::tmc {

uint8_t crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        uint8_t b = data[i];
        for (int j = 0; j < 8; ++j) {
            if (((crc >> 7) ^ (b & 0x01)) != 0) {
                crc = static_cast<uint8_t>((crc << 1) ^ 0x07);
            } else {
                crc = static_cast<uint8_t>(crc << 1);
            }
            b >>= 1;
        }
    }
    return crc;
}

void build_write(uint8_t out[8], uint8_t addr, uint8_t reg, uint32_t value) {
    out[0] = kSync;
    out[1] = addr;
    out[2] = static_cast<uint8_t>(reg | 0x80);
    out[3] = static_cast<uint8_t>(value >> 24);
    out[4] = static_cast<uint8_t>(value >> 16);
    out[5] = static_cast<uint8_t>(value >> 8);
    out[6] = static_cast<uint8_t>(value);
    out[7] = crc8(out, 7);
}

void build_read(uint8_t out[4], uint8_t addr, uint8_t reg) {
    out[0] = kSync;
    out[1] = addr;
    out[2] = static_cast<uint8_t>(reg & 0x7F);
    out[3] = crc8(out, 3);
}

bool parse_reply(const uint8_t in[8], uint8_t reg, uint32_t* value) {
    if ((in[0] & 0x0F) != kSync || in[1] != kMasterAddr || in[2] != (reg & 0x7F)) return false;
    if (crc8(in, 7) != in[7]) return false;
    *value = (uint32_t(in[3]) << 24) | (uint32_t(in[4]) << 16) | (uint32_t(in[5]) << 8) | in[6];
    return true;
}

uint8_t mres_for(uint16_t microsteps) {
    switch (microsteps) {
        case 256: return 0;
        case 128: return 1;
        case 64: return 2;
        case 32: return 3;
        case 16: return 4;
        case 8: return 5;
        case 4: return 6;
        case 2: return 7;
        case 1: return 8;
        default: return 0xFF;
    }
}

namespace {
// RMS value at CS = 31 in mA
double full_scale_ma(bool vsense, uint16_t rsense_mohm) {
    const double vfs = vsense ? 0.180 : 0.325;
    return vfs / ((rsense_mohm + 20) / 1000.0) / std::sqrt(2.0) * 1000.0;
}

uint8_t cs_for(uint16_t ma, double fs) {
    int cs = static_cast<int>(std::floor(ma * 32.0 / fs)) - 1;
    if (cs < 0) cs = 0;
    if (cs > 31) cs = 31;
    return static_cast<uint8_t>(cs);
}

uint16_t ma_for(uint8_t cs, double fs) { return static_cast<uint16_t>(std::lround((cs + 1) * fs / 32.0)); }
}  // namespace

CurrentScale current_scale(uint16_t run_ma, uint16_t hold_ma, uint16_t rsense_mohm) {
    CurrentScale c{};
    const double fs_hi = full_scale_ma(true, rsense_mohm);
    c.vsense = run_ma <= fs_hi;
    const double fs = c.vsense ? fs_hi : full_scale_ma(false, rsense_mohm);
    c.irun = cs_for(run_ma, fs);
    c.run_ma = ma_for(c.irun, fs);
    if (hold_ma == 0) {
        c.ihold = 0;
        c.freewheel = true;
        c.hold_ma = 0;
    } else {
        c.ihold = cs_for(hold_ma, fs);
        if (c.ihold > c.irun) c.ihold = c.irun;
        c.freewheel = false;
        c.hold_ma = ma_for(c.ihold, fs);
    }
    return c;
}

uint32_t gconf_for(const DriverSettings& s, bool reverse) {
    // Current only via registers (no VREF), external Rsense, StealthChop,
    // PDN_UART as UART, microsteps from CHOPCONF.MRES.
    uint32_t g = GCONF_PDN_DISABLE | GCONF_MSTEP_REG_SELECT | GCONF_MULTISTEP_FILT;
    if (s.invert != reverse) g |= GCONF_SHAFT;
    return g;
}

uint32_t ihold_irun(uint8_t ihold, uint8_t irun, uint8_t iholddelay) {
    return (uint32_t(ihold) & 0x1F) | ((uint32_t(irun) & 0x1F) << 8) | ((uint32_t(iholddelay) & 0x0F) << 16);
}

DriverRegs make_regs(const DriverSettings& s, uint16_t rsense_mohm, bool reverse) {
    DriverRegs r{};
    r.current = current_scale(s.run_ma, s.hold_ma, rsense_mohm);
    r.gconf = gconf_for(s, reverse);

    uint8_t mres = mres_for(s.microsteps);
    if (mres == 0xFF) mres = 4;  // 16
    // Base = reset value 0x10000053 (TOFF=3, HSTRT=5, HEND=0, intpol=1), TBL=2
    uint32_t chop = 0x10000053u | (2u << 15);
    chop = (chop & ~(0x0Fu << 24)) | (uint32_t(mres) << 24);
    if (r.current.vsense) chop |= 1u << 17;
    r.chopconf = chop;

    r.ihold_irun = ihold_irun(r.current.ihold, r.current.irun);
    r.tpowerdown = 20;        // ~0.4 s until hold current reduction
    r.tpwmthrs = 0;           // StealthChop at all speeds
    r.tcoolthrs = 0xFFFFF;    // StallGuard reading (SG_RESULT) always active

    // PWMCONF reset value 0xC10D0024 (autoscale + autograd), freewheel at IHOLD = 0
    uint32_t pwm = 0xC10D0024u & ~(0x3u << 20);
    if (r.current.freewheel) pwm |= 1u << 20;
    r.pwmconf = pwm;
    return r;
}

DrvStatus decode_drv_status(uint32_t v) {
    DrvStatus d{};
    d.otpw = v & (1u << 0);
    d.ot = v & (1u << 1);
    d.s2ga = v & (1u << 2);
    d.s2gb = v & (1u << 3);
    d.s2vsa = v & (1u << 4);
    d.s2vsb = v & (1u << 5);
    d.ola = v & (1u << 6);
    d.olb = v & (1u << 7);
    d.t120 = v & (1u << 8);
    d.t143 = v & (1u << 9);
    d.t150 = v & (1u << 10);
    d.t157 = v & (1u << 11);
    d.cs_actual = static_cast<uint8_t>((v >> 16) & 0x1F);
    d.stealth = v & (1u << 30);
    d.stst = v & (1u << 31);
    return d;
}

}  // namespace doser::tmc
