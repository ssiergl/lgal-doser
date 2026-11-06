// Host tests for the hardware-independent logic (run on the Mac/PC).
//   tests/host/run.sh

#include <cmath>
#include <cstdio>
#include <cstring>

#include "doser/civil_time.h"
#include "doser/dosing_core.h"
#include "doser/posix_tz.h"
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

static int64_t utc_of(int y, int mo, int d, int h, int mi) { return civil::to_unix({y, mo, d, h, mi, 0, 0}); }

static void test_tz() {
    tz::Zone z;
    CHECK(tz::parse("CET-1CEST,M3.5.0,M10.5.0/3", &z));
    CHECK(z.std_offset == 3600 && z.dst_offset == 7200 && z.has_dst);
    // 2026: DST from 29 March 01:00 UTC to 25 October 01:00 UTC
    CHECK(tz::offset_at(z, utc_of(2026, 3, 29, 1, 0) - 1) == 3600);
    CHECK(tz::offset_at(z, utc_of(2026, 3, 29, 1, 0)) == 7200);
    CHECK(tz::offset_at(z, utc_of(2026, 10, 25, 1, 0) - 1) == 7200);
    CHECK(tz::offset_at(z, utc_of(2026, 10, 25, 1, 0)) == 3600);
    CHECK(tz::offset_at(z, utc_of(2026, 1, 15, 12, 0)) == 3600);
    tz::Mapping how;
    CHECK(tz::to_utc(z, utc_of(2026, 7, 1, 12, 0), &how) == utc_of(2026, 7, 1, 10, 0));
    CHECK(how == tz::Mapping::Unique);
    CHECK(tz::to_utc(z, utc_of(2026, 12, 1, 12, 0), &how) == utc_of(2026, 12, 1, 11, 0));
    // spring forward: 02:30 does not exist -> 03:30 CEST
    int64_t u = tz::to_utc(z, utc_of(2026, 3, 29, 2, 30), &how);
    CHECK(how == tz::Mapping::Gap && u == utc_of(2026, 3, 29, 1, 30));
    CHECK(tz::to_local(z, u) == utc_of(2026, 3, 29, 3, 30));
    CHECK(tz::to_utc(z, utc_of(2026, 3, 29, 3, 0), &how) == utc_of(2026, 3, 29, 1, 0) && how == tz::Mapping::Unique);
    // fall back: 02:30 occurs twice -> first occurrence (CEST)
    CHECK(tz::to_utc(z, utc_of(2026, 10, 25, 2, 30), &how) == utc_of(2026, 10, 25, 0, 30));
    CHECK(how == tz::Mapping::Ambiguous);
    CHECK(tz::to_utc(z, utc_of(2026, 10, 25, 3, 0), &how) == utc_of(2026, 10, 25, 2, 0) && how == tz::Mapping::Unique);

    // USA, default rules when none are given
    CHECK(tz::parse("EST5EDT", &z) && z.std_offset == -18000 && z.dst_offset == -14400);
    CHECK(tz::offset_at(z, utc_of(2026, 3, 8, 7, 0) - 1) == -18000);  // 2nd Sunday of March, 02:00 EST
    CHECK(tz::offset_at(z, utc_of(2026, 3, 8, 7, 0)) == -14400);
    CHECK(tz::offset_at(z, utc_of(2026, 11, 1, 6, 0)) == -18000);    // 1st Sunday of November, 02:00 EDT

    // southern hemisphere: DST across the turn of the year
    CHECK(tz::parse("AEST-10AEDT,M10.1.0,M4.1.0/3", &z));
    CHECK(tz::offset_at(z, utc_of(2026, 1, 15, 0, 0)) == 39600);
    CHECK(tz::offset_at(z, utc_of(2026, 7, 15, 0, 0)) == 36000);
    CHECK(tz::offset_at(z, utc_of(2026, 4, 4, 16, 0) - 1) == 39600);  // 5 April 03:00 AEDT
    CHECK(tz::offset_at(z, utc_of(2026, 4, 4, 16, 0)) == 36000);
    CHECK(tz::offset_at(z, utc_of(2026, 10, 3, 16, 0)) == 39600);     // 4 October 02:00 AEST

    // no DST, quoted names, minutes, Julian days
    CHECK(tz::parse("UTC0", &z) && !z.has_dst && tz::offset_at(z, 0) == 0);
    CHECK(tz::parse("<+0330>-3:30", &z) && tz::offset_at(z, utc_of(2026, 6, 1, 0, 0)) == 12600);
    CHECK(tz::parse("AAA0BBB,J60,J300", &z));  // J60 = 1 March, also in leap years
    CHECK(tz::offset_at(z, utc_of(2028, 3, 1, 2, 0) - 1) == 0 && tz::offset_at(z, utc_of(2028, 3, 1, 2, 0)) == 3600);
    CHECK(tz::parse("AAA0BBB,M3.5.0/-1,M10.5.0/25", &z) && z.start.time == -3600 && z.end.time == 90000);
    CHECK(!tz::parse("", &z) && !tz::parse("X", &z) && !tz::parse("CET-1CEST,M13.1.0,M10.5.0", &z));
    CHECK(!tz::parse("CET-1CEST,M3.5.0", &z) && !tz::parse("CET", &z) && !tz::parse("CET-1CEST,M3.5.0,M10.5.0x", &z));
}

static void test_dose_conversion() {
    using namespace dose;
    const uint32_t spr = 3200;  // 200 full steps × 16
    CHECK(ul_to_steps(370, kNominalUlPerRev, spr) == 3200);
    CHECK(ul_to_steps(20, kNominalUlPerRev, spr) == 173);  // 0.02 ml
    CHECK(steps_to_ul(3200, kNominalUlPerRev, spr) == 370);
    CHECK(ul_to_steps(1000000, kNominalUlPerRev, spr) == 8648649);
    CHECK(mrev_to_steps(250, spr) == 800);  // ¼ rev
    CHECK(mrev_to_steps(250, 6400) == 1600);
    CHECK(std::fabs(flow_ml_min(300, kNominalUlPerRev) - 111.0f) < 0.01f);
    // Calibration: 20 ml target with nominal value, collected 18.5 ml
    const uint32_t steps = ul_to_steps(20000, kNominalUlPerRev, spr);
    auto c = calibrate(steps, spr, 18500);
    CHECK(c.ok && std::fabs(c.ul_per_rev - 342.25f) < 0.1f);
    CHECK(ul_to_steps(18500, c.ul_per_rev, spr) == steps);
    // Change microsteps: calibration stays valid
    CHECK(ul_to_steps(18500, c.ul_per_rev, 6400) == 2 * steps);
    CHECK(!calibrate(steps, spr, 1000).ok);      // implausibly small
    CHECK(!calibrate(steps, spr, 400000).ok);    // implausibly large
    CHECK(!calibrate(0, spr, 1000).ok);
    // Retraction: global or per pump
    PumpSettings p;
    Engineering e;
    CHECK(retract_mrev(p, e) == 250);
    p.retract_mrev = 0;
    CHECK(retract_mrev(p, e) == 0);
    p.retract_mrev = 500;
    CHECK(retract_mrev(p, e) == 500);
    CHECK(validate(e, 600) == nullptr);
    e.tier_rpm[2] = 700;
    CHECK(validate(e, 600) != nullptr);
    e.tier_rpm[2] = 150;  // not ascending
    CHECK(validate(e, 600) != nullptr);
}

static void test_ledger() {
    using namespace dose;
    const int64_t t0 = 1793968215;  // 2026-11-06 12:30:15 UTC
    Ledger l{};
    l.add(t0, 1000);
    l.add(t0 + 600, 500);  // same hour
    CHECK(l.sum_from(window_start_hour(LimitMode::Rolling24h, t0 + 600, 0)) == 1500);
    // 23 h later: still in the rolling window, 25 h later no longer
    CHECK(l.sum_from(window_start_hour(LimitMode::Rolling24h, t0 + 23 * 3600, 0)) == 1500);
    CHECK(l.sum_from(window_start_hour(LimitMode::Rolling24h, t0 + 24 * 3600, 0)) == 1500);  // started hour: conservative
    l.add(t0 + 25 * 3600 + 3599, 0);
    CHECK(l.sum_from(window_start_hour(LimitMode::Rolling24h, t0 + 25 * 3600 + 3599, 0)) == 0);
    // Calendar day (CET): local midnight = 23:00 UTC of the previous day
    Ledger d{};
    const int64_t midnight = 1793919600;  // 2026-11-06 00:00 CET
    d.add(midnight - 60, 700);            // 23:59 on the previous day
    d.add(midnight + 3600, 300);
    d.add(t0, 200);
    CHECK(d.sum_from(window_start_hour(LimitMode::CalendarDay, t0, midnight)) == 500);
    // credit on abort
    d.subtract(t0, 150);
    CHECK(d.sum_from(window_start_hour(LimitMode::CalendarDay, t0, midnight)) == 350);
    // Long pause: old buckets are cleared on the next entry
    d.add(t0 + 30 * 24 * 3600, 10);
    CHECK(d.sum_from(0) == 10);
    // Clock far back: nothing is lost (conservative)
    d.add(t0, 5);
    CHECK(d.sum_from(window_start_hour(LimitMode::Rolling24h, t0, 0)) == 15);
    // check before start
    PumpSettings p;
    Engineering e;
    PumpState s{};
    p.daily_limit_ul = 10000;
    CHECK(check_dose(5000, p, e, s, 5000) == nullptr);
    CHECK(std::strcmp(check_dose(5001, p, e, s, 5000), "daily_limit") == 0);
    CHECK(std::strcmp(check_dose(19, p, e, s, 0), "dose_too_small") == 0);
    CHECK(check_dose(100, p, e, s, 0) == nullptr);
    p.mode = Mode::Disabled;
    CHECK(std::strcmp(check_dose(100, p, e, s, 0), "pump_disabled") == 0);
}

static void test_records() {
    using namespace dose;
    CHECK(crc32("123456789", 9) == 0xCBF43926u);  // standard check value
    PumpState s{};
    s.remaining_ul = 123456;
    s.tube_ms = 3600000;
    Record a, b;
    seal(a, 7, s);
    s.remaining_ul = 120000;
    seal(b, 8, s);
    CHECK(valid(a) && valid(b));
    CHECK(newest(a, b) == 1);
    b.state.remaining_ul ^= 1;  // bit error / half written
    CHECK(!valid(b) && newest(a, b) == 0);
    Record z{};
    CHECK(newest(z, z) == -1);
    seal(a, 0xFFFFFFFFu, s);  // sequence number wraps around
    seal(b, 0, s);
    CHECK(newest(a, b) == 1);
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
    std::printf("Time zone rules\n");
    test_tz();
    std::printf("Dosing: conversion, calibration\n");
    test_dose_conversion();
    std::printf("Daily limit\n");
    test_ledger();
    std::printf("EEPROM records\n");
    test_records();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
