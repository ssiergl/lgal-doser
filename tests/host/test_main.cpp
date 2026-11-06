// Host tests for the hardware-independent logic (run on the Mac/PC).
//   tests/host/run.sh

#include <cmath>
#include <cstdio>
#include <cstring>

#include "doser/civil_time.h"
#include "doser/ramp.h"
#include "doser/tmc2209_proto.h"

static int g_fail = 0;
static int g_checks = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

using namespace doser;

static void test_crc_and_datagrams() {
    // Example from the datasheet context / TMCStepper: read access IFCNT, address 0
    uint8_t rd[4];
    tmc::build_read(rd, 0, tmc::reg::IFCNT);
    CHECK(rd[0] == 0x05 && rd[1] == 0x00 && rd[2] == 0x02);
    CHECK(rd[3] == tmc::crc8(rd, 3));
    // Known value: read GCONF address 0 -> CRC 0x48
    tmc::build_read(rd, 0, tmc::reg::GCONF);
    CHECK(rd[3] == 0x48);

    uint8_t wr[8];
    tmc::build_write(wr, 3, tmc::reg::GCONF, 0x000001C0);
    CHECK(wr[1] == 3 && wr[2] == 0x80 && wr[3] == 0 && wr[4] == 0 && wr[5] == 0x01 && wr[6] == 0xC0);
    CHECK(wr[7] == tmc::crc8(wr, 7));

    // build a reply and parse it again
    uint8_t reply[8] = {0x05, 0xFF, tmc::reg::IOIN, 0x21, 0x00, 0x01, 0x40, 0};
    reply[7] = tmc::crc8(reply, 7);
    uint32_t v = 0;
    CHECK(tmc::parse_reply(reply, tmc::reg::IOIN, &v));
    CHECK(v == 0x21000140);
    CHECK((v >> 24) == tmc::kVersion);
    reply[5] ^= 1;  // bit error
    CHECK(!tmc::parse_reply(reply, tmc::reg::IOIN, &v));
    reply[5] ^= 1;
    CHECK(!tmc::parse_reply(reply, tmc::reg::GCONF, &v));  // wrong register
}

static void test_current() {
    // Rsense 0.11 Ω (R110 on the module)
    auto c = tmc::current_scale(600, 0, 110);
    CHECK(c.vsense);
    CHECK(c.run_ma <= 600 && c.run_ma >= 560);
    CHECK(c.freewheel && c.ihold == 0 && c.hold_ma == 0);
    std::printf("  600 mA -> vsense=%d IRUN=%u => %u mA\n", c.vsense, c.irun, c.run_ma);

    auto c2 = tmc::current_scale(1000, 300, 110);
    CHECK(!c2.vsense);  // above ~979 mA only with vsense = 0
    CHECK(c2.run_ma <= 1000 && c2.run_ma > 900);
    CHECK(!c2.freewheel && c2.hold_ma <= 300 && c2.hold_ma > 200);
    std::printf("  1000/300 mA -> vsense=%d IRUN=%u IHOLD=%u => %u/%u mA\n", c2.vsense, c2.irun, c2.ihold,
                c2.run_ma, c2.hold_ma);

    auto c3 = tmc::current_scale(5000, 0, 110);
    CHECK(c3.irun == 31);

    CHECK(tmc::mres_for(16) == 4 && tmc::mres_for(32) == 3 && tmc::mres_for(256) == 0 && tmc::mres_for(1) == 8);
    CHECK(tmc::mres_for(12) == 0xFF);

    tmc::DriverSettings s;
    s.microsteps = 32;
    auto r = tmc::make_regs(s, 110, false);
    CHECK(((r.chopconf >> 24) & 0xF) == 3);
    CHECK(r.chopconf & (1u << 28));  // Interpolation
    CHECK((r.chopconf & 0xF) == 3);  // TOFF
    CHECK(!(r.gconf & tmc::GCONF_SHAFT));
    CHECK(r.gconf & tmc::GCONF_PDN_DISABLE);
    CHECK(r.gconf & tmc::GCONF_MSTEP_REG_SELECT);
    CHECK(!(r.gconf & tmc::GCONF_EN_SPREADCYCLE));
    CHECK(((r.pwmconf >> 20) & 3) == 1);  // freewheel
    CHECK(tmc::make_regs(s, 110, true).gconf & tmc::GCONF_SHAFT);
    s.invert = true;
    CHECK(!(tmc::make_regs(s, 110, true).gconf & tmc::GCONF_SHAFT));

    auto d = tmc::decode_drv_status((1u << 31) | (17u << 16) | (1u << 3));
    CHECK(d.stst && d.cs_actual == 17 && d.s2gb && d.short_circuit() && !d.ot);
}

struct SimResult {
    uint32_t steps;
    double seconds;
    uint32_t vmax_seen;
    double max_accel;
};

static SimResult simulate_steps(const RampParams& p) {
    Ramp r;
    r.start(p);
    SimResult s{0, 0, 0, 0};
    // Average the speed over windows of 64 steps: single
    // intervals jump by ±1 µs due to the 1 µs resolution.
    constexpr uint32_t kWin = 64;
    double win_t = 0, last_v = 0, last_t = 0;
    while (true) {
        uint32_t iv = r.next_interval_us();
        if (iv == 0) break;
        ++s.steps;
        s.seconds += iv / 1e6;
        win_t += iv / 1e6;
        if (s.steps % kWin == 0) {
            double v = kWin / win_t;
            double center = s.seconds - win_t / 2;  // middle of window
            if (v > s.vmax_seen) s.vmax_seen = static_cast<uint32_t>(v);
            if (last_v > 0) {
                double a = std::fabs(v - last_v) / (center - last_t);
                if (a > s.max_accel) s.max_accel = a;
            }
            last_v = v;
            last_t = center;
            win_t = 0;
        }
        if (s.steps > 50000000) break;
    }
    return s;
}

static void test_ramp_steps() {
    // 300 rpm at 3200 µsteps/rev = 16000 steps/s, ramp 32000 steps/s²
    RampParams p{100000, 16000, 1066, 32000};
    auto s = simulate_steps(p);
    CHECK(s.steps == 100000);
    CHECK(s.vmax_seen >= 15900 && s.vmax_seen <= 16100);
    // Analytic: t = s/v + (v - v0)/a  ≈ 6.25 + 0.467 s
    double expect = 100000.0 / 16000 + (16000.0 - 1066) / 32000;
    std::printf("  trapezoid: %u steps in %.3f s (expected ~%.3f s), vmax %u, amax %.0f\n", s.steps, s.seconds,
                expect, s.vmax_seen, s.max_accel);
    CHECK(std::fabs(s.seconds - expect) < 0.05);
    CHECK(s.max_accel < 32000 * 1.3);

    // Triangle: too short for full speed
    auto t = simulate_steps({500, 16000, 1066, 32000});
    CHECK(t.steps == 500);
    CHECK(t.vmax_seen < 16000);

    // minimum dose 0.02 ml ≈ 175 steps
    CHECK(simulate_steps({175, 16000, 1066, 32000}).steps == 175);
    CHECK(simulate_steps({1, 16000, 1066, 32000}).steps == 1);
    CHECK(simulate_steps({0, 16000, 1066, 32000}).steps == 0);

    // Without ramp: constant frequency, exact mean via remainder carry
    auto c = simulate_steps({30000, 3000, 0, 0});
    CHECK(c.steps == 30000);
    CHECK(std::fabs(c.seconds - 10.0) < 0.001);

    // 600 rpm at 32 µsteps = 64000 steps/s
    auto f = simulate_steps({640000, 64000, 2000, 64000});
    CHECK(f.steps == 640000);
    CHECK(f.vmax_seen >= 63000);

    // Very slow (long pauses > 32767 µs are split in the RMT encoder)
    auto sl = simulate_steps({20, 20, 16, 0});
    CHECK(sl.steps == 20);
    CHECK(std::fabs(sl.seconds - 1.0) < 0.001);
}

static void test_ramp_soft_stop() {
    Ramp r;
    r.start({1000000, 16000, 1066, 32000});
    uint32_t n = 0;
    while (n < 50000) {
        if (r.next_interval_us() == 0) break;
        ++n;
    }
    CHECK(r.velocity() >= 15900);
    r.request_stop();
    uint32_t extra = 0;
    while (r.next_interval_us() != 0) ++extra;
    // braking distance (v² - v0²)/2a ≈ 3982 steps
    std::printf("  soft stop after %u steps, braking distance %u\n", n, extra);
    CHECK(extra > 3500 && extra < 4500);
    CHECK(r.done());
    CHECK(r.position() == n + extra);
}

static void test_ramp_velocity_mode() {
    // Simulates the MCPWM channel: 1 ms control tick, position = integral of frequency
    Ramp r;
    const uint32_t total = 50000;
    r.start({total, 16000, 1066, 32000});
    double pos = 0;
    uint32_t v = 1066;
    double t = 0;
    uint32_t v_at_end = 0;
    while (pos < total && t < 30) {
        pos += v * 0.001;
        t += 0.001;
        if (pos >= total) {
            v_at_end = v;
            break;
        }
        v = r.advance(1000, static_cast<uint32_t>(pos));
        CHECK(v > 0);
    }
    std::printf("  tempo mode: target reached after %.3f s, final frequency %u steps/s\n", t, v_at_end);
    CHECK(v_at_end < 2500);  // braking ramp completed before the target
    CHECK(std::fabs(t - (total / 16000.0 + (16000.0 - 1066) / 32000)) < 0.1);
}

static void test_civil() {
    using civil::DateTime;
    CHECK(civil::to_unix({1970, 1, 1, 0, 0, 0, 0}) == 0);
    CHECK(civil::to_unix({2026, 11, 6, 12, 30, 15, 0}) == 1793968215);
    auto d = civil::from_unix(1793968215);
    CHECK(d.year == 2026 && d.month == 11 && d.day == 6 && d.hour == 12 && d.minute == 30 && d.second == 15);
    CHECK(d.weekday == 5);  // Friday
    auto leap = civil::from_unix(civil::to_unix({2028, 2, 29, 23, 59, 59, 0}));
    CHECK(leap.year == 2028 && leap.month == 2 && leap.day == 29);
    auto y2100 = civil::from_unix(civil::to_unix({2100, 3, 1, 0, 0, 0, 0}) - 1);
    CHECK(y2100.month == 2 && y2100.day == 28);
}

int main() {
    std::printf("TMC2209 protocol\n");
    test_crc_and_datagrams();
    std::printf("Current calculation\n");
    test_current();
    std::printf("Ramp (step mode)\n");
    test_ramp_steps();
    test_ramp_soft_stop();
    std::printf("Ramp (tempo mode)\n");
    test_ramp_velocity_mode();
    std::printf("Calendar\n");
    test_civil();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
