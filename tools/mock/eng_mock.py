# Engineering menu mock: engineering parameters, hw status/config, raw test,
# telemetry and resets (mirrors dosing_api, schedule_api, hwtest_api, engineering_api).
import random, time
import __main__ as M

START = time.time()
SCH = {"grid_min": 5, "max_delay_min": 120}
CFG = [{"run_ma": 650, "hold_ma": 0, "microsteps": 16, "full_steps": 200, "invert": False, "accel": 64000, "v_start": 1066} for _ in range(5)]
RAW = [{"running": False, "start": 0.0, "steps": 0, "sps": 1, "pos": 0, "sg_min": 0} for _ in range(5)]
RTC = {"aging": 0}
ntp_on = lambda: bool(__import__("sys_mock").TIME["ntp_server"])
FIT = {"now": [1, 2, 3, 4, 5], "next": [1, 2, 3, 4, 5], "saved": 0.0}  # channel population (hwconfig)

def fitted():
    """Pumps fitted now: a saved population takes effect with the next restart"""
    if FIT["saved"] and __import__("sys_mock").SYS["boot"] > FIT["saved"]: FIT["now"] = list(FIT["next"]); FIT["saved"] = 0.0
    return FIT["now"]

UART = [{"transfers": 18234, "timeouts": 2, "crc_errors": 0, "echo_errors": 0}, {"transfers": 4521, "timeouts": 0, "crc_errors": 0, "echo_errors": 0}]

def _err(h, code="invalid_param", msg="x", status=400):
    h.send(status, {"error": {"code": code, "message": msg}})

def _spr(i): return CFG[i]["full_steps"] * CFG[i]["microsteps"]

def _raw(i):
    r = RAW[i]
    if r["running"]:
        done = int((time.time() - r["start"]) * r["sps"])
        if done >= abs(r["steps"]): r.update(running=False, pos=abs(r["steps"]))
        else: r["pos"] = done
    return r

def _pump(i):
    c, r = CFG[i], _raw(i)
    spr = _spr(i)
    return {"pump": i + 1, "fitted": i + 1 in fitted(), "stepgen": "rmt" if i < 4 else "mcpwm", "present": i != 3, "configured": i != 3, "version": 0x21,
            "running": r["running"], "reverse": r["steps"] < 0, "position": r["pos"], "target": abs(r["steps"]),
            "velocity": r["sps"] if r["running"] else 0, "rpm": round(r["sps"] * 60 / spr, 1) if r["running"] else 0.0,
            "sg_result": random.randint(180, 260) if r["running"] else 0, "sg_min": r["sg_min"], "tstep": 0, "pwm_scale_sum": 40,
            "drv_status": "0x80000000", "gstat": 0, "run_ma": c["run_ma"] if i != 3 else 0, "hold_ma": c["hold_ma"] if i != 3 else 0,
            "fault": "driver_lost" if i == 3 else None,
            "flags": {"standstill": not r["running"], "stealth": True, "cs_actual": 20, "overtemp_warn": False, "overtemp": False,
                      "short": False, "open_load_a": False, "open_load_b": False, "temp_over_120": False},
            "config": {**c, "steps_per_rev": spr, "max_sps": spr * 10}}

def _status():
    return {"board": {"id": "d5v1", "model": "LGAL Doser 5", "pumps": 5},
            "drivers_enabled": any(r["running"] for r in RAW), "locked": False, "fitted": fitted(), "fitted_next": FIT["next"], "fitted_factory": [1, 2, 3, 4, 5], "pumps": [_pump(i) for i in range(5)],
            "rtc": {"present": True, "utc": int(time.time()), "time_lost": False, "temp_c": 24.75, "aging": RTC["aging"],
                    "battery_warning": False, "trim": {"automatic": ntp_on(), "measuring": ntp_on(), **({"since": int(time.time()) - 15 * 3600,
                    "start_offset_ms": 4, "offset_ms": 9, "measured_at": int(time.time()) - 600} if ntp_on() else {}),
                    "last": {"at": int(time.time()) - 39 * 3600, "ppm": 1.84, "from": 0, "to": 18}}},
            "eeprom": {"present": True}, "time": {"valid": True, "utc": int(time.time()), "source": "ntp", "timezone": "CET-1CEST,M3.5.0,M10.5.0/3"},
            "uart": [{"bus": "A (GPIO38, pumps 1-4)" if b == 0 else "B (GPIO39, pump 5)", **u} for b, u in enumerate(UART)]}

def _num(q, k):
    try: return float(q[k].replace(",", "."))
    except (KeyError, ValueError): return None

def route(h, method, path, q, eng_ok):
    if not (path.startswith("/api/v1/engineering/") or path.startswith("/api/v1/hw/")): return False
    if not eng_ok(): _err(h, "engineering_locked", "Engineering area is locked", 403); return True
    E = M.ENG
    if path == "/api/v1/engineering/dosing":
        if method == "POST":
            e = dict(E, tier_rpm=list(E["tier_rpm"]))
            lim = {"retract_rev": (0, 2), "calib_ml": (1, 200), "prime_max_s": (5, 600), "prime_heartbeat_ms": (200, 5000),
                   "min_dose_ml": (0.001, 10), "tube_life_h": (10, 65535)}
            for k, (lo, hi) in lim.items():
                if k in q:
                    v = _num(q, k)
                    if v is None or not lo <= v <= hi: _err(h, "invalid_param", "%s=%g..%g" % (k, lo, hi)); return True
                    e[k] = v
            for t in range(3):
                k = "tier%d_rpm" % (t + 1)
                if k in q:
                    v = _num(q, k)
                    if v is None or not 10 <= v <= 600: _err(h, "invalid_param", "tier rpm=10..600"); return True
                    e["tier_rpm"][t] = int(v)
            for k in ("warn_days", "low_days"):
                if k in q:
                    v = _num(q, k)
                    if v is None or not 0 <= v <= 60: _err(h, "invalid_param", k); return True
                    e[k] = int(v)
            if e["low_days"] > e["warn_days"]: _err(h, "invalid_param", "low_days"); return True
            if "limit_mode" in q:
                if q["limit_mode"] not in ("calendar_day", "rolling_24h"): _err(h, "invalid_param", "limit_mode"); return True
                e["limit_mode"] = q["limit_mode"]
            r = e["tier_rpm"]
            if r[1] < r[0] or r[2] < r[1]: _err(h, "invalid_param", "tier rpm must be ascending"); return True
            E.clear(); E.update(e)
        h.send(200, E); return True
    if path == "/api/v1/engineering/scheduler":
        if method == "POST":
            if "grid_min" in q:
                g = int(_num(q, "grid_min") or 0)
                if not 1 <= g <= 60 or 1440 % g: _err(h, "invalid_param", "grid_min must divide 1440 (1..60)"); return True
                SCH["grid_min"] = g; M.GRID = g
            if "max_delay_min" in q:
                v = _num(q, "max_delay_min")
                if v is None or not 1 <= v <= 720: _err(h, "invalid_param", "max_delay_min=1..720"); return True
                SCH["max_delay_min"] = int(v)
        h.send(200, SCH); return True
    if path == "/api/v1/engineering/telemetry" and method == "GET":
        ht = 337 * 1024; free = random.randint(150, 160) * 1024
        h.send(200, {"uptime_s": int(time.time() - START) + 3 * 86400 + 4000, "reset_reason": "SW", "tasks": 27,
                     "chip_temp_c": round(41 + random.random() * 2, 1),
                     "heap": {"free": free, "min_free": 121 * 1024, "largest_block": 96 * 1024, "total": ht,
                              "fragmentation_pct": round(100 * (1 - 96 * 1024 / free), 1)},
                     "psram": {"free": 8_100_000, "total": 8_388_608},
                     "nvs": {"used_entries": 412, "free_entries": 1604, "total_entries": 2016, "namespaces": 11},
                     "storage": {"total": 6_029_312, "used": 155_648, "history_days": 23, "history_bytes": 98_304, "history_dropped": 0}})
        return True
    if path == "/api/v1/engineering/reset" and method == "POST":
        w = q.get("what")
        if w == "network": h.send(200, {"status": "ok"}); return True
        _err(h, "invalid_param", "what=network"); return True
    if path == "/api/v1/hw/status" and method == "GET": h.send(200, _status()); return True
    if path == "/api/v1/hw/channels" and method == "POST":
        ns = sorted({int(x) for x in q.get("pumps", "").split(",") if x in "12345" and x})
        if not ns: _err(h, msg="pumps: at least one pump"); return True
        fitted(); FIT.update(next=ns, saved=time.time()); h.send(200, {"status": "ok", "restart": True}); return True
    def pump():
        try: p = int(q.get("pump", "0"))
        except ValueError: return -1
        return p - 1 if 1 <= p <= 5 else -1
    if path == "/api/v1/hw/pump/config" and method == "POST":
        i = pump()
        if i < 0: _err(h, "invalid_param", "pump=1..5 required"); return True
        if _raw(i)["running"]: _err(h, "pump_busy", "Pump is running", 409); return True
        c = dict(CFG[i])
        for k, ck in (("run_ma", "run_ma"), ("hold_ma", "hold_ma"), ("microsteps", "microsteps"), ("full_steps", "full_steps"), ("accel", "accel"), ("vstart", "v_start")):
            if k in q: c[ck] = int(_num(q, k) or 0)
        if "invert" in q: c["invert"] = q["invert"] != "0"
        if c["microsteps"] not in (1, 2, 4, 8, 16, 32, 64, 128, 256) or not 50 <= c["run_ma"] <= 1500 or c["hold_ma"] > c["run_ma"]:
            _err(h, "invalid_param", "Invalid (current 50..1500 mA, hold <= run, microsteps 1..256)"); return True
        CFG[i] = c
        if i == 3: _err(h, "driver_config_failed", "Saved, but driver could not be configured", 502); return True
        h.send(200, {"status": "ok"}); return True
    if path == "/api/v1/hw/pump/run" and method == "POST":
        i = pump()
        if i < 0: _err(h, "invalid_param", "pump=1..5 required"); return True
        steps, sps = int(_num(q, "steps") or 0), int(_num(q, "sps") or 0)
        if steps == 0 or abs(steps) > 20000000: _err(h, "invalid_param", "steps=±1..20000000 required"); return True
        if not 1 <= sps <= _spr(i) * 10: _err(h, "speed_out_of_range", "Speed outside 1..max_sps"); return True
        if i == 3 and q.get("force") != "1": _err(h, "driver_unavailable", "Driver not reachable", 409); return True
        if _raw(i)["running"]: _err(h, "pump_busy", "Pump already running", 409); return True
        RAW[i].update(running=True, start=time.time(), steps=steps, sps=sps, pos=0, sg_min=random.randint(150, 200))
        UART[0 if i < 4 else 1]["transfers"] += 40
        h.send(200, {"status": "running"}); return True
    if path == "/api/v1/hw/pump/stop" and method == "POST":
        p = int(_num(q, "pump") or 0)
        for i in (range(5) if p == 0 else [p - 1]):
            r = _raw(i)
            if r["running"]: r.update(running=False, steps=r["pos"] if r["steps"] > 0 else -r["pos"])
        h.send(200, {"status": "stopped" if q.get("mode") == "hard" else "stopping"}); return True
    if path == "/api/v1/hw/rtc/aging" and method == "POST":
        v = _num(q, "offset")
        if v is None or not -128 <= v <= 127 or v != int(v): _err(h, "invalid_param", "offset=-128..127"); return True
        if ntp_on(): _err(h, "ntp_active", "Trimmed automatically from the time server", status=409); return True
        RTC["aging"] = int(v)
        h.send(200, {"status": "ok"}); return True
    if path == "/api/v1/hw/tmc" and method == "GET":
        n = _num(q, "pump")
        if n is None or n not in (1, 2, 3, 4, 5): _err(h, "invalid_param", "pump=1..5"); return True
        names = ["GCONF", "GSTAT", "IFCNT", "IOIN", "TSTEP", "SG_RESULT", "MSCNT", "CHOPCONF", "DRV_STATUS", "PWMCONF", "PWM_SCALE", "PWM_AUTO"]
        vals = [0x1C0, 0, 7, 0x21000040, 0xFFFFF, 0, 8, 0x14000053, 0x80000000, 0xC10D0024, 0x1F, 0x00240000]
        regs = {k: (None if n == 4 else "0x%08x" % v) for k, v in zip(names, vals)}
        h.send(200, {"pump": int(n), "registers": regs}); return True
    if path == "/api/v1/hw/eeprom/test" and method == "POST":
        h.send(200, {"status": "ok"}); return True
    return False
