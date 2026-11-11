# Mock of the history endpoints: 60 days of generated events (test data only)
import random, time

KEEP = 60  # maximum retention
SET = {"keep": KEEP}
EV = []  # newest last

def _add(utc, kind, pump, source, code, target=0.0, done=0.0, **extra):
    e = {"utc": int(utc), "local": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(utc)), "kind": kind, "pump": pump, "code": code}
    if source: e["source"] = source
    if kind in ("dose", "prime", "calib_run"): e.update(target_ml=round(target, 3), done_ml=round(done, 3))
    e.update(extra)
    EV.append(e)

def _gen():
    rnd = random.Random(7)
    now = time.time()
    lt = time.localtime(now)
    midnight = now - (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec)
    plans = {1: [(8 * 60 + k * 65, 5.0) for k in range(12)], 2: [(9 * 60 + k * 144, 5.0) for k in range(6)],
             3: [(600, 5.0), (1320, 2.5)], 5: [(720, 2.0)]}
    for d in range(KEEP + 1, -1, -1):
        day0 = midnight - d * 86400
        off = 20 <= d <= 21  # device off two days
        for pump, slots in plans.items():
            if pump == 3 and time.localtime(day0 + 43200).tm_wday not in (0, 2, 4, 5): continue
            for minute, ml in slots:
                t = day0 + minute * 60 + rnd.randint(0, 20)
                if t > now: continue
                if off:
                    continue
                if pump == 2 and d == 5 and minute > 14 * 60:
                    _add(t, "skip", 2, "schedule", "daily_limit", count=1, target_ml=ml); continue
                res = "success"; done = ml
                if pump == 2 and d == 0 and minute >= 13 * 60: res = "fault"; done = round(ml * 0.3, 3)
                if pump == 1 and d == 9 and minute == 8 * 60: res = "aborted"; done = 1.2
                _add(t, "dose", pump, "schedule", res, ml, done)
        if d == 19:
            for pump in plans: _add(day0 + 5, "skip", pump, "schedule", "missed", count=len(plans[pump]))
        if d % 6 == 0 and d:
            _add(day0 + 19 * 3600, "dose", 1, "manual", "success", 10.0, 10.0)
        if d == 12:
            _add(day0 + 18 * 3600, "container", 2, None, "refilled", remaining_ml=2000.0, capacity_ml=2000.0)
            _add(day0 + 18 * 3600 + 120, "prime", 2, "manual", "success", 0, 3.4)
        if d == 30:
            _add(day0 + 17 * 3600, "calib_run", 1, "manual", "success", 20.0, 20.0)
            _add(day0 + 17 * 3600 + 90, "calibration", 1, None, "set", target_ml=20.0, measured_ml=18.5, ul_per_rev=342.25)
        if d == 25: _add(day0 + 16 * 3600, "tube", 4, None, "set", hours=0)
        if d == 3: _add(day0 + 11 * 3600, "alarm", 5, None, "container_low")
        if d == 1: _add(day0 + 14 * 3600, "alarm", 2, None, "container_low")
    _add(now - 3500, "alarm", 2, None, "container_low")
    EV.sort(key=lambda e: e["utc"])
_gen()

def _err(h, code="invalid_param", msg="x", status=400):
    h.send(status, {"error": {"code": code, "message": msg}})

def _filter(q):
    out = EV
    try:
        pump = int(q.get("pump", 0))
        days = int(q.get("days", 0))
    except ValueError:
        return None
    if not 0 <= pump <= 5 or (q.get("days") and not 1 <= days <= 60): return None
    kinds = [k for k in q.get("kind", "").split(",") if k]
    if any(k not in ("dose", "prime", "calib_run", "skip", "container", "calibration", "tube", "alarm") for k in kinds): return None
    if days:
        lt = time.localtime()
        frm = time.time() - (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec) - (days - 1) * 86400
        out = [e for e in out if e["utc"] >= frm]
    if pump: out = [e for e in out if e["pump"] == pump]
    if kinds: out = [e for e in out if e["kind"] in kinds]
    return list(reversed(out))

def _backup(secrets):
    import time as _t
    pumps = []
    for i in range(5):
        pumps.append({"pump": i + 1, "name": ["KH", "Calcium", "Magnesium", "", "Spurenelemente"][i], "mode": "auto" if i != 3 else "disabled",
                      "tier": 1, "daily_limit_ml": [80, 0, 20, 0, 5][i], "retract_mrev": -1 if i == 0 else 0,
                      "capacity_ml": [2000, 2000, 1000, 0, 500][i], "ul_per_rev": 370.0, "calibrated_at": int(_t.time()) - 86400 * 20,
                      "warn_days": 0, "low_days": 0,
                      "schedule": {"kind": "none", "power_loss": "skip_missed", "catch_up_min": 60}})
    doc = {"format": "lgal-doser-backup", "version": 1, "firmware": "0.5.0-dev", "device": "lgal-doser-e4f5", "exported_utc": int(_t.time()),
           "system": {"language": "de", "timezone": "CET-1CEST,M3.5.0,M10.5.0/3", "ntp_server": "pool.ntp.org",
                      "led_night": {"enabled": False, "start": "22:00", "end": "07:00", "brightness": 0}},
           "network": {"mode": "auto", "lan": {"dhcp": True, "ip": "", "netmask": "", "gateway": "", "dns1": "", "dns2": ""},
                       "wlan": {"ssid": "HomeNet", "password_set": True, "dhcp": True, "ip": "", "netmask": "", "gateway": "", "dns1": "", "dns2": ""}},
           "pumps": pumps, "locks": {"manual": "reject", "rules": [{"pumps": [1, 2], "gap_min": 5}]},
           "engineering": {"dosing": {"retract_mrev": 250, "calib_ml": 10.0, "tier_rpm": [100, 200, 300], "prime_max_s": 60,
                                      "prime_heartbeat_ms": 500, "min_dose_ml": 0.02, "tube_life_h": 1000, "limit_mode": "calendar_day",
                                      "warn_days": 7, "low_days": 4},
                           "scheduler": {"grid_min": 5, "max_delay_min": 30},
                           "hardware": {"pumps": [{"run_ma": 600, "hold_ma": 0, "microsteps": 16, "invert": False, "full_steps": 200, "accel": 0, "v_start": 0}] * 5},
                           "history": {"keep_days": SET["keep"]}}}
    import mqtt_mock
    m = {k: v for k, v in mqtt_mock.CFG.items() if k != "password"}
    m["password_set"] = bool(mqtt_mock.CFG["password"]); m["ca_pem"] = mqtt_mock.CA["pem"]
    doc["mqtt"] = m
    if secrets:
        if mqtt_mock.CFG["password"]: doc["mqtt"]["password"] = mqtt_mock.CFG["password"]
        doc["network"]["wlan_password"] = "test-wlan-pw"
        doc["auth"] = {"password": "00" * 56, "keys": "00" * 264}
    return doc

def route(h, method, path, q, eng_ok):
    if path == "/api/v1/backup":
        if not eng_ok(): _err(h, "engineering_locked", status=403); return True
        if method == "GET": h.send(200, _backup(q.get("secrets") == "1")); return True
        if method == "POST":
            body = getattr(h, "_json_body", {}) or {}
            parts = [p for p in q.get("parts", "system,network,pumps,engineering,mqtt,auth").split(",") if p]
            if body.get("format") != "lgal-doser-backup": _err(h, "invalid_format", "x"); return True
            present = [p for p in ("system", "network", "pumps", "engineering", "mqtt", "auth") if body.get(p) is not None]
            todo = [p for p in present if p in parts]
            if not todo: _err(h, "nothing_to_restore"); return True
            h.send(200, {"status": "ok", "applied": todo, "network_changed": "network" in todo, "signed_out": "auth" in todo}); return True
    if path == "/api/v1/history/settings":
        if method == "POST":
            if not eng_ok(): _err(h, "engineering_locked", status=403); return True
            try: v = int(q.get("keep_days", ""))
            except ValueError: v = 0
            if not 1 <= v <= KEEP: _err(h, "invalid_param", "keep_days=1..60"); return True
            SET["keep"] = v
            cut = time.time() - (v + 1) * 86400
            EV[:] = [e for e in EV if e["utc"] >= cut]
        h.send(200, {"keep_days": SET["keep"], "min_days": 1, "max_days": KEEP}); return True
    if path == "/api/v1/history" and method == "DELETE":
        if not eng_ok(): _err(h, "engineering_locked", status=403); return True
        EV.clear(); h.send(200, {"status": "ok"}); return True
    if method != "GET": return False
    if path == "/api/v1/history":
        ev = _filter(q)
        if ev is None: _err(h); return True
        try: limit, offset = int(q.get("limit", 100)), int(q.get("offset", 0))
        except ValueError: _err(h); return True
        if not 1 <= limit <= 500 or offset < 0: _err(h); return True
        page = ev[offset:offset + limit]
        h.send(200, {"events": page, "more": len(ev) > offset + limit}); return True
    if path == "/api/v1/history.csv":
        ev = _filter(q)
        if ev is None: _err(h); return True
        lines = ["utc,local,kind,pump,source,code,target_ml,done_ml,value"]
        for e in ev:
            v = e.get("remaining_ml", e.get("ul_per_rev", e.get("hours", e.get("count", ""))))
            lines.append(f'{e["utc"]},{e["local"]},{e["kind"]},{e["pump"]},{e.get("source", "")},{e["code"]},'
                         f'{e.get("target_ml", 0):.3f},{e.get("done_ml", 0):.3f},{v}')
        h.send(200, ("\r\n".join(lines) + "\r\n").encode(), "text/csv; charset=utf-8"); return True
    if path == "/api/v1/history/daily":
        try: days = int(q.get("days", 14))
        except ValueError: _err(h); return True
        if not 1 <= days <= 60: _err(h); return True
        today = time.strftime("%Y-%m-%d")
        lt = time.localtime()
        mid = time.time() - (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec)
        out = []
        for k in range(days - 1, -1, -1):
            date = time.strftime("%Y-%m-%d", time.localtime(mid - k * 86400 + 43200))
            ml, doses, sk = [0.0] * 5, [0] * 5, [0] * 5
            for e in EV:
                if not e["local"].startswith(date) or not 1 <= e["pump"] <= 5: continue
                i = e["pump"] - 1
                if e["kind"] == "dose": ml[i] += e["done_ml"]; doses[i] += 1
                if e["kind"] == "skip": sk[i] += e.get("count", 1)
            out.append({"date": date, "ml": [round(x, 3) for x in ml], "doses": doses, "skipped": sk})
        h.send(200, {"keep_days": SET["keep"], "today": today, "days": out}); return True
    return False
