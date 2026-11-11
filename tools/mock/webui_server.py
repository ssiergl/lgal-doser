# Local mock of the device for the web UI (Python stdlib only, localhost).
#
#   python3 tools/mock/webui_server.py [project dir] [port]   -> http://localhost:8767/
#
# Serves components/webui/www straight from the working tree (reload the page
# after an edit) and simulates the REST API and the WebSocket /api/v1/ws with
# in-memory state: pumps, dosing and priming (dead-man heartbeat), schedules,
# history, network, auth, MQTT, backup and the engineering area. Test values
# only: admin password "test-admin-pw", engineering key "test-eng-key".
# Modules: sys_mock (time, network, auth, system), hist_mock (history, backup),
# mqtt_mock (MQTT), eng_mock (engineering, hardware test endpoints).
import base64, hashlib, json, os, socket, struct, sys, threading, time
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler

ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8767
LANG = {"v": "de"}
ENG = {"retract_rev": 0.25, "calib_ml": 10.0, "tier_rpm": [100, 200, 300], "prime_max_s": 60, "prime_heartbeat_ms": 4000,
       "min_dose_ml": 0.02, "tube_life_h": 1000, "limit_mode": "calendar_day", "nominal_ml_per_rev": 0.37, "max_rpm": 600}
LOCK = threading.Lock()
NAMES = ["KH", "Calcium", "Magnesium", "", "Spurenelemente"]
CAP = [2000.0, 2000.0, 1000.0, 0.0, 500.0]
REM = [1312.4, 186.0, 655.0, 0.0, 41.5]
USED = [42.0, 18.5, 6.0, 0.0, 1.2]
PLAN = [60.0, 30.0, 10.0, 0.0, 2.0]
P = [{"activity": "idle", "start": 0, "beat": 0, "done": 0.0, "last": {}, "remaining": REM[i], "owner": None} for i in range(5)]
P[1]["last"] = {"result": "fault", "activity": "dosing", "target_ml": 2.5, "done_ml": 0.8, "at": int(time.time()) - 3600}  # driver fault test
P[2]["last"] = {"result": "success", "activity": "dosing", "target_ml": 2.0, "done_ml": 2.0, "at": int(time.time()) - 1800}
for i, p in enumerate(P):
    p.update(mode="auto" if i != 3 else "disabled", tier=1, rset="global" if i == 0 else "off", rrev=0.25, limit=[80.0, 0, 20.0, 0, 5.0][i],
             mlrev=0.37 if i != 3 else 0.0, calat=int(time.time()) - 86400 * 20 if i != 3 else 0,
             pending=False, ptarget=0.0, target=0.0, tube=12.4, total=345.678, kind="")
GRID = 5
WDN = ["mon", "tue", "wed", "thu", "fri", "sat", "sun"]
D = {"mode": "daily"}
SCHED = [
    {"kind": "spread", "enabled": True, "start": "08:00", "end": "20:00", "count": 12, "ml": 60.0, "days": D, "power_loss": "skip_missed", "catch_up_min": 60},
    {"kind": "spread", "enabled": True, "start": "09:00", "end": "21:00", "count": 6, "ml": 30.0, "days": D, "power_loss": "skip_missed", "catch_up_min": 60},
    {"kind": "times", "enabled": True, "times": [{"time": "10:00", "ml": 5.0}, {"time": "22:00", "ml": 2.5}],
     "days": {"mode": "weekdays", "weekdays": ["mon", "wed", "fri", "sat"]}, "power_loss": "catch_up_window", "catch_up_min": 90},
    {"kind": "none", "enabled": True, "days": D, "power_loss": "skip_missed", "catch_up_min": 60},
    {"kind": "times", "enabled": True, "times": [{"time": "12:00", "ml": 2.0}], "days": D, "power_loss": "skip_missed", "catch_up_min": 60},
]
BOOST = [None] * 5  # {"total_ml", "remaining_ml", "days", "start", "active"}
LOCKS = {"manual": "reject", "rules": [{"pumps": [1, 2], "gap_min": 5}]}

def hm(x):
    h, m = x.split(":"); return int(h) * 60 + int(m)

def dnum(d):
    import datetime; return (datetime.date.fromisoformat(d) - datetime.date(1970, 1, 1)).days

def active(days, day):
    if days["mode"] == "weekdays": return WDN[(day % 7 + 3) % 7] in days["weekdays"]
    if days["mode"] == "interval": return (day - dnum(days["anchor"])) % days["every"] == 0
    return True

def expand(p):
    """-> (error, entry 1-based or None, [(minute, ul)])"""
    if p["kind"] == "none": return None, None, []
    if p["days"]["mode"] == "weekdays" and not p["days"].get("weekdays"): return "invalid_days", None, []
    if p["kind"] == "times":
        out = []
        for k, t in enumerate(p["times"]):
            m, u = hm(t["time"]), round(t["ml"] * 1000)
            if m % GRID: return "off_grid", k + 1, []
            if out and m == out[-1][0]: return "duplicate_time", k + 1, []
            if u < 20: return "dose_too_small", k + 1, []
            out.append((m, u))
        return None, None, out
    ul = round(p["ml"] * 1000)
    st = hm(p["start"])
    if st % GRID: return "off_grid", None, []
    n = p["count"]
    if not 1 <= n <= 96: return "invalid_count", None, []
    en = hm(p["end"]) if n > 1 else st
    if n > 1 and en % GRID: return "off_grid", None, []
    if n > 1 and en <= st: return "invalid_range", None, []
    base, rest = divmod(ul, n)
    if base < 20: return "dose_too_small", None, []
    out = []
    for i in range(n):
        m = st + (2 * i * (en - st) + (n - 1) * GRID) // (2 * (n - 1) * GRID) * GRID if n > 1 else st
        if out and m <= out[-1][0]: return "slots_too_dense", None, []
        out.append((m, base + (1 if i < rest else 0)))
    return None, None, out

def slots_on(i, day):
    p = SCHED[i]
    return sorted(expand(p)[2]) if p["kind"] != "none" and p["enabled"] and active(p["days"], day) else []

def today_slots(i): return slots_on(i, dnum(time.strftime("%Y-%m-%d")))

def next_slot(i, now):
    """First slot after now within 8 days (like the firmware) -> (utc, ul) or None; DST ignored"""
    lt = time.localtime(now); midnight = now - (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec)
    day0 = dnum(time.strftime("%Y-%m-%d", lt))
    for k in range(9):
        for m, u in slots_on(i, day0 + k):
            if midnight + k * 86400 + m * 60 > now: return midnight + k * 86400 + m * 60, u
    return None

def remaining_days(i):
    """Like sched::remaining_days: container / average of the next 7 days incl. the boost"""
    day0 = dnum(time.strftime("%Y-%m-%d"))
    per_day = sum(u for k in range(7) for _, u in slots_on(i, day0 + k)) / 7000
    if BOOST[i]: per_day += BOOST[i]["remaining_ml"] / max(1, BOOST[i]["days"])
    return round(P[i]["remaining"] / per_day, 1) if per_day > 0 else None

def sched_json(i):
    sl = today_slots(i)
    return {"pump": i + 1, "grid_min": GRID, **SCHED[i], "today": {"date": time.strftime("%Y-%m-%d"), "ml": sum(u for _, u in sl) / 1000,
            "slots": [{"time": "%02d:%02d" % divmod(m, 60), "ml": u / 1000} for m, u in sl]}}

def lock_conflicts(locks):
    """Simplified: today's slots only, start times closer than the gap"""
    out = []
    for r in locks["rules"]:
        a, b = r["pumps"][0] - 1, r["pumps"][1] - 1
        for ma, _ in today_slots(a):
            for mb, _ in today_slots(b):
                if abs(ma - mb) < r["gap_min"]:
                    f, s = ((a, ma), (b, mb)) if ma <= mb else ((b, mb), (a, ma))
                    out.append({"code": "collision_lock", "first": {"pump": f[0] + 1, "time": "%02d:%02d" % divmod(f[1], 60)},
                                "second": {"pump": s[0] + 1, "time": "%02d:%02d" % divmod(s[1], 60)}, "next_day": False, "gap_min": r["gap_min"]})
    return out[:8]

def put_sched(h, i, body):
    kind = body.get("kind", "none")
    if kind not in ("none", "spread", "times"): return h.send(400, {"error": {"code": "invalid_param", "message": "kind", "field": "kind"}})
    new = {"kind": kind, "enabled": body.get("enabled", True), "days": body.get("days") or D,
           "power_loss": body.get("power_loss", "skip_missed"), "catch_up_min": body.get("catch_up_min", 60)}
    if kind == "spread":
        new.update(start=body["start"], count=body["count"], end=body.get("end", body["start"]) if body["count"] > 1 else body["start"], ml=body["ml"])
    if kind == "times":
        if not 1 <= len(body.get("times", [])) <= 24: return h.send(400, {"error": {"code": "invalid_param", "message": "times", "field": "times"}})
        new["times"] = sorted(body["times"], key=lambda t: hm(t["time"]))
    e, k, _ = expand(new)
    if e: return h.send(400, {"error": {"code": e, "message": e, "pump": i + 1, **({"entry": k} if k else {})}})
    old = SCHED[i]; SCHED[i] = new
    cf = lock_conflicts(LOCKS)
    if cf:
        SCHED[i] = old
        return h.send(409, {"error": {"code": "conflict", "message": "x", "conflicts": cf}})
    lim = P[i]["limit"]
    if lim and sum(u for _, u in today_slots(i)) / 1000 > lim:
        SCHED[i] = old; return h.send(409, {"error": {"code": "exceeds_daily_limit", "message": "x", "pump": i + 1}})
    return h.send(200, sched_json(i))

def put_locks(h, body):
    global LOCKS
    if body.get("manual", "reject") not in ("reject", "queue"):
        return h.send(400, {"error": {"code": "invalid_param", "message": "manual", "field": "manual"}})
    rules = body.get("rules", [])
    if len(rules) > 10: return h.send(400, {"error": {"code": "invalid_param", "message": "rules", "field": "rules"}})
    for k, r in enumerate(rules):
        p = r.get("pumps", [])
        if len(p) != 2 or not all(isinstance(x, int) and 1 <= x <= 5 for x in p):
            return h.send(400, {"error": {"code": "invalid_param", "message": "pumps", "field": "pumps", "rule": k + 1}})
        if p[0] == p[1] or not 1 <= r.get("gap_min", 0) <= 1440:
            return h.send(400, {"error": {"code": "invalid_lock", "message": "x", "rule": k + 1}})
    new = {"manual": body.get("manual", "reject"), "rules": [{"pumps": r["pumps"], "gap_min": r["gap_min"]} for r in rules]}
    cf = lock_conflicts(new)
    if cf: return h.send(409, {"error": {"code": "conflict", "message": "x", "conflicts": cf}})
    LOCKS = new
    return h.send(200, LOCKS)

CLIENTS = {}  # id -> socket
LOG = []

def log(*a):
    LOG.append(" ".join(str(x) for x in a)); print(*a, flush=True)

def tick():
    now = time.time()
    with LOCK:
        for i, p in enumerate(P):
            if p["activity"] in ("dosing", "calibrating"):
                rate = [37.0, 74.0, 111.0][p["tier"]] / 60 / 4
                p["done"] = min(p["target"], (now - p["start"]) * rate)
                if p["done"] >= p["target"]:
                    act = p["activity"]; p["activity"] = "idle"
                    if act == "dosing":
                        USED[i] += p["target"]; p["remaining"] = max(0.0, p["remaining"] - p["target"]); p["total"] += p["target"]
                    else:
                        p["pending"] = True; p["ptarget"] = p["target"]
                    p["last"] = {"result": "success", "activity": act, "target_ml": p["target"], "done_ml": p["target"], "at": int(now)}
                continue
            if p["activity"] != "priming": continue
            p["done"] = (now - p["start"]) * 74.0 / 60
            if now - p["beat"] > ENG["prime_heartbeat_ms"] / 1000: end(i, "heartbeat_lost")

def end(i, reason):
    p = P[i]; p["activity"] = "idle"; p["owner"] = None
    p["last"] = {"result": reason, "activity": "priming", "target_ml": 0, "done_ml": round(p["done"], 3), "at": int(time.time())}
    log("pump", i + 1, "priming ended:", reason, "%.2f ml" % p["done"])

def live():
    out = []
    with LOCK:
        for i, p in enumerate(P):
            l = {"pump": i + 1, "activity": p["activity"], "phase": "main" if p["activity"] != "idle" else "none"}
            if p["activity"] == "priming":
                l.update(trigger="api", target_ml=0, done_ml=round(p["done"], 3), rpm=200.0, prime_s=round(time.time() - p["start"], 1))
            elif p["activity"] != "idle":
                l.update(trigger="api", target_ml=p["target"], done_ml=round(p["done"], 3), rpm=[100, 200, 300][p["tier"]])
            l.update(remaining_ml=p["remaining"], used_today_ml=USED[i])
            if CAP[i]: l["percent"] = round(100 * p["remaining"] / CAP[i], 1)
            if p["last"]: l["last"] = {k: p["last"][k] for k in ("result", "activity", "done_ml", "at")}
            out.append(l)
    return {"pumps": out}

def full(i):
    p = P[i]; n = i + 1
    d = {"pump": n, "name": NAMES[i], "mode": p["mode"], "direction": "ccw" if eng_mock.CFG[i]["invert"] else "cw", "activity": p["activity"],
         "phase": "main" if p["activity"] != "idle" else "none", "last": dict(p["last"]),
         "calibration": {"calibrated": p["mlrev"] > 0, "ml_per_rev": p["mlrev"] or 0.37, "steps_per_ml": 3200 / (p["mlrev"] or 0.37),
                         "calibrated_at": p["calat"], "pending": p["pending"],
                         **({"pending_steps": int(p["ptarget"] / 0.37 * 3200), "pending_target_ml": p["ptarget"]} if p["pending"] else {})},
         "speed": {"tier": p["tier"], "tiers": [{"rpm": 100, "ml_min": 37.0}, {"rpm": 200, "ml_min": 74.0}, {"rpm": 300, "ml_min": 111.0}]},
         "retract": (lambda r: {"setting": p["rset"], "global_rev": 0.25, "rev": r, "ml": round(r * 0.37, 3), "steps": int(r * 3200), "pending_rev": r})(0.0 if p["rset"] == "off" else 0.25 if p["rset"] == "global" else p["rrev"]),
         "daily": {"limit_ml": p["limit"], "used_ml": USED[i], "mode": "calendar_day", "time_valid": True},
         "container": {"tracked": CAP[i] > 0, "capacity_ml": CAP[i], "remaining_ml": p["remaining"],
                       "percent": 100 * p["remaining"] / CAP[i] if CAP[i] else 0},
         "tube": {"hours": p["tube"], "limit_h": ENG["tube_life_h"], "percent": round(100 * p["tube"] / ENG["tube_life_h"], 1)}, "total_ml": p["total"]}
    if p["activity"] == "priming":
        d.update(trigger="api", target_ml=0, done_ml=p["done"], rpm=200.0, prime_s=time.time() - p["start"])
    elif p["activity"] != "idle":
        d.update(trigger="api", target_ml=p["target"], done_ml=p["done"], rpm=[100, 200, 300][p["tier"]])
    return d

def err(h, code, msg="x", status=400, **extra):
    return h.send(status, {"error": dict(code=code, message=msg, **extra)})

def pumps_post(h, path, q):
    try: n = int(q.get("pump", "x"))
    except ValueError: return err(h, "invalid_param", "pump")
    if path == "/api/v1/pumps/stop":
        with LOCK:
            for i, p in enumerate(P):
                if (n == 0 or n == i + 1) and p["activity"] != "idle":
                    p["last"] = {"result": "aborted", "activity": p["activity"], "target_ml": p["target"], "done_ml": round(p["done"], 3), "at": int(time.time())}
                    p["activity"] = "idle"; p["owner"] = None
        return h.send(200, {"status": "stopped"})
    if not 1 <= n <= 5: return err(h, "invalid_param", "pump")
    i = n - 1; p = P[i]
    if path == "/api/v1/pumps/boost":
        if q.get("action") == "stop": BOOST[i] = None; return h.send(200, {"status": "ok"})
        try: ml, days = float(q["ml"]), int(q["days"])
        except (KeyError, ValueError): return err(h, "invalid_param", "ml/days")
        if not (0.02 <= ml <= 10000 and 1 <= days <= 30): return err(h, "invalid_param", "ml/days")
        if SCHED[i]["kind"] == "none" or not SCHED[i]["enabled"]: return err(h, "no_slots", "No scheduled doses")
        BOOST[i] = {"active": True, "total_ml": ml, "remaining_ml": round(ml * 0.8, 3) if ml > 20 else ml, "days": days, "start": time.strftime("%Y-%m-%d")}
        return h.send(200, {"status": "ok"})
    num = lambda k: float(q[k])
    if path == "/api/v1/pumps/prime":  # prime() takes the lock itself
        e = prime(None, {"pump": n, "action": q.get("action")})
        return err(h, e, status=409) if e else h.send(200, {"status": "ok"})
    with LOCK:
        if path == "/api/v1/pumps/dose":
            ml = num("ml")
            if p["mode"] == "disabled": return err(h, "pump_disabled", status=409)
            if p["activity"] != "idle": return err(h, "busy", status=409)
            if q.get("ml") == "13": return err(h, "collision_lock", "Lock time", 409, retry_at=int(time.time()) + 600,
                                                retry_local=time.strftime("%Y-%m-%d %H:%M", time.localtime(time.time() + 600)))
            if q.get("ml") == "14": return h.send(202, {"status": "queued"})
            if "tier" in q: p["tier_dose"] = int(q["tier"])
            p.update(activity="dosing", start=time.time(), done=0.0, target=ml); return h.send(200, {"status": "dosing"})
        if path == "/api/v1/pumps/calibrate":
            a = q.get("action")
            if a == "start":
                if p["activity"] != "idle": return err(h, "busy", status=409)
                p.update(activity="calibrating", start=time.time(), done=0.0, target=num("ml") if "ml" in q else ENG["calib_ml"], pending=False)
                return h.send(200, {"status": "calibrating"})
            if a == "apply":
                if not p["pending"]: return err(h, "no_calibration_run", status=409)
                m = num("measured_ml")
                if not (0.5 * p["ptarget"] <= m <= 2 * p["ptarget"]): return err(h, "measured_out_of_range")
                p["mlrev"] = round(0.37 * m / p["ptarget"], 4); p["calat"] = int(time.time()); p["pending"] = False
                return h.send(200, {"status": "ok"})
            if a == "cancel": p["pending"] = False; return h.send(200, {"status": "ok"})
            return err(h, "invalid_param", "action")
        if path == "/api/v1/pumps/settings":
            if "name" in q: NAMES[i] = q["name"][:23]
            if "mode" in q: p["mode"] = q["mode"]
            if "tier" in q: p["tier"] = int(q["tier"])
            if "daily_limit_ml" in q: p["limit"] = num("daily_limit_ml")
            if "retract_rev" in q:
                v = q["retract_rev"]
                if v in ("global", "off"): p["rset"] = v
                elif float(v) == 0: p["rset"] = "off"
                else: p.update(rset="own", rrev=num("retract_rev"))
            if "direction" in q:
                if q["direction"] not in ("cw", "ccw"): return err(h, "invalid_param", "direction=cw|ccw")
                eng_mock.CFG[i]["invert"] = q["direction"] == "ccw"
            return h.send(200, {"status": "ok"})
        if path == "/api/v1/pumps/container":
            if "capacity_ml" in q:
                CAP[i] = num("capacity_ml"); p["remaining"] = num("remaining_ml") if "remaining_ml" in q else CAP[i]
            elif "remaining_ml" in q: p["remaining"] = min(CAP[i], num("remaining_ml"))
            return h.send(200, {"status": "ok"})
        if path == "/api/v1/pumps/tube": p["tube"] = num("hours"); return h.send(200, {"status": "ok"})
    return err(h, "not_found", status=404)

def frame(text):
    b = text.encode(); n = len(b)
    h = bytes([0x81]) + (bytes([n]) if n < 126 else bytes([126]) + struct.pack(">H", n) if n < 65536 else bytes([127]) + struct.pack(">Q", n))
    return h + b

def recv_exact(s, n):
    buf = b""
    while len(buf) < n:
        c = s.recv(n - len(buf))
        if not c: raise ConnectionError
        buf += c
    return buf

def prime(cid, m):
    pump, action = m.get("pump"), m.get("action")
    if not isinstance(pump, int) or not 1 <= pump <= 5: return "invalid_param"
    i = pump - 1; p = P[i]; now = time.time()
    with LOCK:
        if action == "start":
            if p["activity"] == "priming": p["beat"] = now; p["owner"] = cid; return None
            p.update(activity="priming", start=now, beat=now, done=0.0, owner=cid); log("pump", pump, "priming started by client", cid)
            return None
        if action == "beat":
            if p["activity"] != "priming": return "not_priming"
            p["beat"] = now; return None
        if action == "stop":
            if p["activity"] != "priming": return "not_priming"
            end(i, "success"); return None
    return "invalid_param"

import eng_mock, sys_mock
from urllib.parse import urlsplit as _us, parse_qsl as _pq

def sysr(h, method, body=None):
    u = _us(h.path)
    return sys_mock.route(h, method, u.path, dict(_pq(u.query)), body or {})

class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *a): pass
    def send(self, code, body, ctype="application/json"):
        b = body if isinstance(body, bytes) else json.dumps(body).encode()
        self.send_response(code); self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(b))); self.send_header("Cache-Control", "no-store")
        self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        p = self.path.split("?")[0]
        if p == "/api/v1/ws": return self.ws()
        if sysr(self, "GET"): return
        if p in ("/login", "/setup", "/recovery"): return self.send(200, open(ROOT + "/components/lgal_core/www" + p + ".html", "rb").read(), "text/html; charset=utf-8")
        if p == "/": return self.send(200, open(ROOT + "/components/webui/www/index.html", "rb").read(), "text/html; charset=utf-8")
        if p.startswith("/ui/"):
            ext = p.rsplit(".", 1)[-1]
            ct = {"js": "text/javascript", "mjs": "text/javascript", "css": "text/css", "svg": "image/svg+xml", "html": "text/html"}.get(ext, "application/octet-stream")
            try: return self.send(200, open(ROOT + "/components/webui/www/" + p[4:], "rb").read(), ct + "; charset=utf-8")
            except OSError: return self.send(404, {"error": {"code": "not_found", "message": "x"}})
        if p == "/api/v1/pumps/schedule":
            return self.send(200, sched_json(int(self.path.split("pump=")[1][0]) - 1))
        if p == "/api/v1/schedule/locks":
            return self.send(200, LOCKS)
        if p == "/api/v1/schedule/status":
            now = int(time.time())
            pumps = []
            for i in range(5):
                auto = P[i]["mode"] == "auto"
                PLAN[i] = sum(u for _, u in today_slots(i)) / 1000 if auto else 0
                rd = remaining_days(i) if auto and CAP[i] else None
                nx = next_slot(i, now) if auto else None
                pumps.append({"pump": i + 1, "next": {"at": nx[0], "local": time.strftime("%Y-%m-%d %H:%M", time.localtime(nx[0])), "ml": round(nx[1] / 1000, 3)} if nx else None,
                              "delayed": None, "queued": None, "skipped": 0, "last_skip": None, "last_run": None,
                              "planned_today_ml": PLAN[i], "remaining_days": rd, "boost": BOOST[i]})
            return self.send(200, {"clock_valid": True, "now": now, "now_local": time.strftime("%Y-%m-%d %H:%M"), "pumps": pumps})
        if p == "/api/v1/system/info":
            return self.send(200, {"device": {"hostname": "lgal-doser-e4f5", "setup_completed": True},
                                   "firmware": {"version": "0.5.0-dev", "partition": "ota_0"}, "time": {"valid": True}})
        if p == "/i18n.js": return self.send(200, open(ROOT + "/components/lgal_core/www/i18n.js", "rb").read(), "text/javascript")
        if p == "/api/v1/system/language": return self.send(200, {"language": LANG["v"], "languages": ["en", "de"]})
        if p == "/api/v1/engineering/dosing": return self.send(200, ENG)
        if p == "/api/v1/pumps":
            with LOCK: return self.send(200, {"pumps": [full(i) for i in range(5)]})
        if p == "/mock/log": return self.send(200, LOG)
        self.send(404, {"error": {"code": "not_found", "message": "x"}})
    def do_PUT(self):
        n = int(self.headers.get("Content-Length", 0))
        if self.path.startswith("/api/v1/mqtt/ca"):  # PEM text, not JSON
            import mqtt_mock
            if mqtt_mock.route(self, "PUT", "/api/v1/mqtt/ca", {}, self.rfile.read(n).decode("utf-8", "replace")): return
        body = json.loads(self.rfile.read(n) or b"{}")
        if sysr(self, "PUT", body): return
        if self.path.startswith("/api/v1/pumps/schedule"): return put_sched(self, int(self.path.split("pump=")[1][0]) - 1, body)
        if self.path.startswith("/api/v1/schedule/locks"): return put_locks(self, body)
        self.send(404, {"error": {"code": "not_found", "message": "x"}})
    def do_DELETE(self):
        if sysr(self, "DELETE"): return
        self.send(404, {"error": {"code": "not_found", "message": "x"}})
    def do_POST(self):
        p = self.path
        n = int(self.headers.get("Content-Length", 0) or 0)
        if not p.startswith("/api/v1/pumps/"):
            raw = self.rfile.read(n) if n else b""
            if p.startswith("/api/v1/ota/firmware"): pb = raw
            else:
                try: pb = json.loads(raw or b"{}")
                except ValueError: pb = {}
            if sysr(self, "POST", pb): return
        if p.startswith("/api/v1/system/language"):
            LANG["v"] = p.split("lang=")[1]; return self.send(200, {"language": LANG["v"], "languages": ["en", "de"]})
        if p.startswith("/mock/kill"):  # drop all WebSocket connections (connection loss)
            for s in list(CLIENTS.values()):
                try: s.shutdown(socket.SHUT_RDWR)
                except OSError: pass
            return self.send(200, {"status": "ok"})
        if p.startswith("/api/v1/pumps/"):
            from urllib.parse import urlsplit, parse_qsl
            u = urlsplit(p); return pumps_post(self, u.path, dict(parse_qsl(u.query)))
        self.send(200, {"status": "ok"})
    def ws(self):
        origin, host = self.headers.get("Origin"), self.headers.get("Host")
        if origin is not None and origin.lower() not in ("http://" + host.lower(), "https://" + host.lower()):
            return self.send(403, {"error": {"code": "forbidden_origin", "message": "x"}})
        key = self.headers["Sec-WebSocket-Key"]
        acc = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        self.send_response(101); self.send_header("Upgrade", "websocket"); self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", acc); self.end_headers(); self.wfile.flush()
        s = self.connection; cid = id(s) % 1000
        with LOCK: CLIENTS[cid] = s
        log("client", cid, "connected")
        s.sendall(frame(json.dumps(dict(type="status", seq=0, utc=int(time.time()), time_valid=True, **live()))))
        try:
            while True:
                b0, b1 = recv_exact(s, 2); op = b0 & 15; n = b1 & 127
                if n == 126: n = struct.unpack(">H", recv_exact(s, 2))[0]
                elif n == 127: n = struct.unpack(">Q", recv_exact(s, 8))[0]
                mask = recv_exact(s, 4) if b1 & 128 else b"\0\0\0\0"
                data = bytes(c ^ mask[k % 4] for k, c in enumerate(recv_exact(s, n)))
                if op == 8: break
                if op != 1: continue
                m = json.loads(data); r = {"type": m.get("type")}
                if "id" in m: r["id"] = m["id"]
                e = prime(cid, m) if m.get("type") == "prime" else (None if m.get("type") == "ping" else "unknown_type")
                if m.get("type") == "prime": r.update(pump=m.get("pump"), action=m.get("action"))
                r["ok"] = e is None
                if e: r["error"] = e
                with LOCK: s.sendall(frame(json.dumps(r)))
        except (ConnectionError, OSError, ValueError) as e:
            log("ws error", repr(e))
        with LOCK:
            CLIENTS.pop(cid, None)
            for i, p in enumerate(P):
                if p["owner"] == cid and p["activity"] == "priming": end(i, "success (connection closed)")
        log("client", cid, "disconnected")
        self.close_connection = True

def pusher():
    last, sent, seq = None, 0, 0
    while True:
        time.sleep(0.25); tick()
        body = live(); h = json.dumps(body, sort_keys=True)
        if h == last and time.time() - sent < 10: continue
        last, sent, seq = h, time.time(), seq + 1
        msg = frame(json.dumps(dict(type="status", seq=seq, utc=int(time.time()), time_valid=True, **body)))
        with LOCK:
            for s in list(CLIENTS.values()):
                try: s.sendall(msg)
                except OSError: pass

threading.Thread(target=pusher, daemon=True).start()
print("LGAL doser mock on http://localhost:%d/ (project %s)" % (PORT, ROOT), flush=True)
ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
