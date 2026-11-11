# Mock of the time, network and auth endpoints (test values only)
import re, secrets, threading, time

OFFSET = 600  # device clock runs 10 min ahead of the host -> "differs" warning
TIME = {"timezone": "CET-1CEST,M3.5.0,M10.5.0/3", "ntp_server": "pool.ntp.org", "source": "rtc", "valid": True,
        "rtc_ok": True, "last_ntp": time.time() - 3 * 3600}
NET = {"mode": "auto",
       "lan": {"dhcp": True, "ip": "", "netmask": "", "gateway": "", "dns1": "", "dns2": ""},
       "wlan": {"ssid": "HomeNet", "password_set": True, "dhcp": True, "ip": "", "netmask": "", "gateway": "", "dns1": "", "dns2": ""}}
SCAN = {"state": "idle", "t": 0}
TEST = {"state": "idle", "t": 0, "ok": True}
AUTH = {"password": "test-admin-pw", "password_set": True}
KEYS = [{"slot": 1, "name": "Home Assistant", "created": int(time.time()) - 86400 * 12}]
MAX_KEYS = 4
ENGKEY = "test-eng-key"
ENG = {"until": 0}
SYS = {"coredump": True, "boot": time.time(), "down_until": 0, "version": "0.5.0-dev", "partition": "ota_0", "invalid": None}
LINKS = [{"id": "app", "name": "Web interface", "path": "/"},
         {"id": "network", "name": "Network", "path": "/network"}, {"id": "setup", "name": "Setup wizard", "path": "/setup"},
         {"id": "account", "name": "Account", "path": "/login"}]

LED = {"enabled": False, "start": "22:00", "end": "07:00", "brightness": 0}
HM = re.compile(r"^([01]\d|2[0-3]):[0-5]\d$")

def led_json():
    lt = time.localtime(time.time() + OFFSET); m = lt.tm_hour * 60 + lt.tm_min
    st = int(LED["start"][:2]) * 60 + int(LED["start"][3:]); en = int(LED["end"][:2]) * 60 + int(LED["end"][3:])
    act = LED["enabled"] and st != en and ((st <= m < en) if st < en else (m >= st or m < en))
    return {"shown": "warning", "idle_brightness": LED["brightness"] if act else 100, "night": {**LED, "active": act}}

def down(): return time.time() < SYS["down_until"]

def restart(delay=6):
    SYS["down_until"] = time.time() + delay; SYS["boot"] = time.time() + delay; ENG["until"] = 0

def info_json():
    return {"device": {"vendor": "LG Aquatic Life", "model": "LGAL-Doser", "hostname": "lgal-doser-e4f5", "mac": "AA:BB:CC:00:11:22", "setup_completed": True},
            "firmware": {"project": "lgal_doser", "version": SYS["version"], "idf": "v6.0.3", "built": "Nov  7 2026 10:00:00",
                         "partition": SYS["partition"], "ota_state": "VALID", "last_invalid_partition": SYS["invalid"]},
            "system": {"uptime_s": int(max(0, time.time() - SYS["boot"])) + 2 * 86400 + 3 * 3600, "reset_reason": "SW", "heap_free": 182000,
                       "heap_min_free": 151000, "heap_largest_block": 98000, "psram_free": 8200000, "coredump": SYS["coredump"]},
            "network": {"active": "lan", "eth_present": True, "eth_link": True, "eth_ip": "192.168.1.50", "wlan": "off", "wlan_ip": None, "ap_active": False, "ap_clients": 0},
            "time": time_json(), "language": "de", "languages": ["en", "de"], "links": LINKS}
IPRE = re.compile(r"^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$")

# URL update: a ".bin" address downloads 1.6 MB in 6 s and restarts, anything else gets HTTP 404 after 2 s
OTA = {"allow_http": False, "t0": 0, "url": "", "http": False, "rebooted": False}

def ota_status():
    if not OTA["t0"]: return {"state": "idle"}
    dt, total = time.time() - OTA["t0"], 1658880
    s = {"source": "api", "http": OTA["http"], "received": 0, "total": total}
    if not OTA["url"].lower().endswith(".bin"):
        if dt < 2: return {"state": "running", **s, "received": int(dt * 30000), "total": None}
        return {"state": "failed", **s, "error": "http_status", "detail": "HTTP 404"}
    if dt < 6: return {"state": "running", **s, "received": int(total * dt / 6)}
    if OTA["rebooted"] and time.time() > OTA["rebooted"]:  # the new firmware is up
        OTA["t0"] = 0; return {"state": "idle"}
    if not OTA["rebooted"]:
        OTA["rebooted"] = time.time() + 7
        SYS["version"] = "0.5.1-dev"; SYS["partition"] = "ota_1" if SYS["partition"] == "ota_0" else "ota_0"
        threading.Timer(1.0, restart).start()
    return {"state": "rebooting", **s, "received": total}

def time_json():
    now = time.time() + OFFSET
    return {"valid": TIME["valid"], "utc": int(now) if TIME["valid"] else 0,
            "local": time.strftime("%Y-%m-%d %H:%M:%S CET", time.localtime(now)) if TIME["valid"] else "",
            "source": TIME["source"] if TIME["valid"] else "none", "last_ntp_sync": int(TIME["last_ntp"]),
            "rtc_ok": TIME["rtc_ok"], "rtc_lost": False, "timezone": TIME["timezone"], "ntp_server": TIME["ntp_server"]}

def err(h, status, code, msg="x", **extra):
    return h.send(status, {"error": dict(code=code, message=msg, **extra)})

def ip_ok(s): return bool(IPRE.match(s)) and all(int(x) < 256 for x in IPRE.match(s).groups())

def check_ip(part, c):
    if c.get("dhcp", True): return None
    for k in ("ip", "netmask"):
        if not ip_ok(c.get(k, "")): return part + "." + k
    for k in ("gateway", "dns1", "dns2"):
        if c.get(k) and not ip_ok(c[k]): return part + "." + k
    return None

def net_status():
    lan = NET["lan"]
    addr = ({"dhcp": True, "ip": "192.168.1.50", "netmask": "255.255.255.0", "gateway": "192.168.1.1", "dns1": "192.168.1.1", "dns2": ""}
            if lan["dhcp"] else dict(lan))
    return {"active": "lan" if NET["mode"] != "wlan" else "wlan", "mode": NET["mode"],
            "lan": {"present": True, "running": NET["mode"] != "wlan", "link": True, "ip": addr["ip"], "address": addr, "down_s": 0},
            "wlan": {"configured": bool(NET["wlan"]["ssid"]), "ssid": NET["wlan"]["ssid"], "state": "off" if NET["mode"] == "auto" else "connected",
                     "ip": "" if NET["mode"] == "auto" else "192.168.1.77", "rssi": -61,
                     "address": None if NET["mode"] == "auto" else {"dhcp": True, "ip": "192.168.1.77", "netmask": "255.255.255.0", "gateway": "192.168.1.1", "dns1": "192.168.1.1", "dns2": ""}},
            "ap": {"active": False, "clients": 0}}

def route(h, method, path, q, body):
    """True when handled"""
    if down() and path.startswith("/api/"):
        h.close_connection = True
        try: h.connection.shutdown(2)
        except OSError: pass
        return True
    import hist_mock
    h._json_body = body
    if hist_mock.route(h, method, path, q, lambda: ENG["until"] > time.time()): return True
    import mqtt_mock
    if mqtt_mock.route(h, method, path, q, body): return True
    import eng_mock
    if eng_mock.route(h, method, path, q, lambda: ENG["until"] > time.time()): return True
    if path == "/api/v1/system/info" and method == "GET": h.send(200, info_json()); return True
    if path == "/api/v1/system/led":
        if method == "GET": h.send(200, led_json()); return True
        if method == "PUT":
            n = body.get("night")
            if not isinstance(n, dict): err(h, 400, "invalid_param", "night", field="night"); return True
            new = dict(LED)
            for k in ("start", "end"):
                if k in n:
                    if not isinstance(n[k], str) or not HM.match(n[k]): err(h, 400, "invalid_param", "night." + k, field="night." + k); return True
                    new[k] = n[k]
            if "brightness" in n:
                if not isinstance(n["brightness"], int) or not 0 <= n["brightness"] <= 100: err(h, 400, "invalid_param", "night.brightness", field="night.brightness"); return True
                new["brightness"] = n["brightness"]
            if "enabled" in n: new["enabled"] = bool(n["enabled"])
            if new["enabled"] and new["start"] == new["end"]: err(h, 400, "invalid_param", "night.end", field="night.end"); return True
            LED.update(new); h.send(200, led_json()); return True
    if path == "/api/v1/system/log" and method == "GET":
        h.send(200, ("I (1234) lgal: boot\nI (1250) net: LAN up 192.168.1.50\nW (5000) dose: pump 2 daily limit near\n" * 30).encode(), "text/plain; charset=utf-8"); return True
    if path == "/api/v1/system/coredump":
        if method == "GET":
            if not SYS["coredump"]: err(h, 404, "no_coredump", "No coredump stored"); return True
            h.send(200, b"\x7fELF-test-dump", "application/octet-stream"); return True
        if method == "DELETE": SYS["coredump"] = False; h.send(200, {"status": "deleted"}); return True
    if path == "/api/v1/system/reboot" and method == "POST": h.send(200, {"status": "rebooting"}); restart(); return True
    if path == "/api/v1/setup" and method == "GET":
        h.send(200, {"completed": True, "password_set": AUTH["password_set"], "connected": True, "hostname": "lgal-doser-e4f5",
                     "urls": ["http://lgal-doser-e4f5.local/", "http://" + net_status()["lan"]["ip"] + "/"]}); return True
    if path == "/api/v1/setup/complete" and method == "POST":
        h.send(200, {"status": "ok", "ap_off_in_s": 20, "urls": ["http://lgal-doser-e4f5.local/", "http://" + net_status()["lan"]["ip"] + "/"]}); return True
    if path == "/api/v1/system/factory_reset" and method == "POST":
        if body.get("password") != AUTH["password"]: err(h, 403, "wrong_password", "Wrong password"); return True
        h.send(200, {"status": "rebooting"}); restart(); return True  # the mock keeps its state
    if path == "/api/v1/ota/settings":
        if method == "PUT":
            if ENG["until"] <= time.time(): err(h, 403, "engineering_locked", "Engineering area is locked"); return True
            if "allow_http" in body:
                if not isinstance(body["allow_http"], bool): err(h, 400, "invalid_param", "allow_http"); return True
                OTA["allow_http"] = body["allow_http"]
        h.send(200, {"allow_http": OTA["allow_http"]}); return True
    if path == "/api/v1/ota/status" and method == "GET": h.send(200, ota_status()); return True
    if path == "/api/v1/ota/url" and method == "POST":
        if ENG["until"] <= time.time(): err(h, 403, "engineering_locked", "Engineering area is locked"); return True
        u = str(body.get("url", "")).strip()
        if not re.match(r"^https?://[^/\s:?#][^\s]*$", u, re.I) or len(u) > 512: err(h, 400, "invalid_url", "Invalid URL"); return True
        if u.lower().startswith("http://") and not OTA["allow_http"]: err(h, 400, "http_not_allowed", "Plain http is not allowed"); return True
        if ota_status()["state"] == "running": err(h, 409, "ota_in_progress", "An update is already in progress"); return True
        OTA.update(t0=time.time(), url=u, http=u.lower().startswith("http://"), rebooted=False)
        h.send(202, {"status": "started"}); return True
    if path == "/api/v1/ota/firmware" and method == "POST":
        data = body if isinstance(body, bytes) else b""
        if len(data) < 32: err(h, 400, "file_too_short", "File too short"); return True
        if data[0] != 0xE9: err(h, 400, "not_esp_image", "Not an ESP image"); return True
        time.sleep(1.0)
        SYS["version"] = "0.5.1-dev"; SYS["partition"] = "ota_1" if SYS["partition"] == "ota_0" else "ota_0"
        h.send(200, {"status": "ok", "reboot": True}); restart(); return True
    if path == "/api/v1/auth/engineering":
        if method == "POST":
            if body.get("key") != ENGKEY: err(h, 403, "wrong_key", "Wrong engineering key"); return True
            ENG["until"] = time.time() + 15 * 60; h.send(200, {"status": "ok"}); return True
        if method == "DELETE": ENG["until"] = 0; h.send(200, {"status": "ok"}); return True
    if path == "/api/v1/system/time":
        if method == "GET": h.send(200, time_json()); return True
        if method == "PUT":
            if "timezone" in body and (not body["timezone"] or len(body["timezone"]) > 63 or not re.match(r"^[A-Za-z<]", body["timezone"])):
                err(h, 400, "invalid_param", "timezone", field="timezone"); return True
            if "ntp_server" in body and (len(body["ntp_server"]) > 63 or " " in body["ntp_server"]):
                err(h, 400, "invalid_param", "ntp_server", field="ntp_server"); return True
            if "utc" in body and body["utc"] < 1735689600:
                err(h, 400, "invalid_param", "utc", field="utc"); return True
            if "local" in body:
                m = re.match(r"^(\d{4}-\d{2}-\d{2})[T ](\d{2}:\d{2})(:\d{2})?$", str(body["local"]))
                try:
                    if "utc" in body or not m: raise ValueError
                    lt = time.strptime(m.group(1) + " " + m.group(2) + (m.group(3) or ":00"), "%Y-%m-%d %H:%M:%S")
                    if not 2025 <= lt.tm_year <= 2099: raise ValueError
                    body["utc"] = int(time.mktime(lt))  # mock: host time zone stands in for the device's
                except ValueError:
                    err(h, 400, "invalid_param", "local", field="local"); return True
            for k in ("timezone", "ntp_server"):
                if k in body: TIME[k] = body[k]
            if "utc" in body:
                global OFFSET
                OFFSET = body["utc"] - time.time(); TIME["valid"] = True; TIME["source"] = "manual"
            h.send(200, time_json()); return True
    if path == "/api/v1/net/status" and method == "GET": h.send(200, net_status()); return True
    if path == "/api/v1/net/settings":
        if method == "GET": h.send(200, NET); return True
        if method == "PUT":
            if body.get("mode") not in ("auto", "lan", "wlan"): err(h, 400, "invalid_param", "mode", field="mode"); return True
            f = check_ip("lan", body.get("lan", {})) or check_ip("wlan", body.get("wlan", {}))
            if f: err(h, 400, "invalid_param", f, field=f); return True
            w = body.get("wlan", {})
            if "password" in w and w["password"] and len(w["password"]) < 8: err(h, 400, "invalid_param", "wlan.password", field="wlan.password"); return True
            if body["mode"] == "wlan" and not w.get("ssid"): err(h, 400, "wlan_not_configured", "Wi-Fi only needs a network"); return True
            NET["mode"] = body["mode"]
            NET["lan"] = {k: body["lan"].get(k, "") for k in NET["lan"]}; NET["lan"]["dhcp"] = body["lan"].get("dhcp", True)
            pw_set = bool(w["password"]) if "password" in w else NET["wlan"]["password_set"]
            NET["wlan"] = {**{k: w.get(k, "") for k in ("ip", "netmask", "gateway", "dns1", "dns2")}, "dhcp": w.get("dhcp", True), "ssid": w.get("ssid", ""), "password_set": pw_set and bool(w.get("ssid"))}
            h.send(200, NET); return True
    if path == "/api/v1/net/scan":
        if method == "POST":
            if SCAN["state"] == "running": err(h, 409, "busy", "Scan already running"); return True
            SCAN.update(state="running", t=time.time()); h.send(202, {"state": "running"}); return True
        if method == "GET":
            if SCAN["state"] == "running" and time.time() - SCAN["t"] > 1.5: SCAN["state"] = "done"
            nets = [{"ssid": "HomeNet", "rssi": -58, "channel": 6, "secure": True}, {"ssid": "HomeNet-Guest", "rssi": -66, "channel": 6, "secure": True},
                    {"ssid": "Nachbar 5G", "rssi": -82, "channel": 11, "secure": True}, {"ssid": "Cafe Open", "rssi": -88, "channel": 1, "secure": False}]
            h.send(200, {"state": SCAN["state"], "networks": nets if SCAN["state"] == "done" else []}); return True
    if path == "/api/v1/net/wlan/test":
        if method == "POST":
            f = check_ip("wlan", body)
            if f: err(h, 400, "invalid_param", f, field=f); return True
            TEST.update(state="running", t=time.time(), ok=body.get("ssid") != "Nachbar 5G" and body.get("password") != "wrongpass1")
            h.send(202, {"state": "running"}); return True
        if method == "GET":
            if TEST["state"] == "running" and time.time() - TEST["t"] > 2: TEST["state"] = "ok" if TEST["ok"] else "failed"
            r = {"state": TEST["state"]}
            if r["state"] == "ok": r["ip"] = "192.168.1.77"
            if r["state"] == "failed": r.update(reason="wrong_password", reason_id=15)
            h.send(200, r); return True
    if path == "/api/v1/system/language" and method == "GET" and down():
        return True
    if path == "/api/v1/auth/status" and method == "GET":
        h.send(200, {"logged_in": True, "via": "session", "password_set": AUTH["password_set"], "setup_completed": True,
                     "engineering": ENG["until"] > time.time(), "engineering_left_s": int(max(0, ENG["until"] - time.time())), "recovery": False, "device": "lgal-doser-e4f5"}); return True
    if path == "/api/v1/auth/password" and method == "POST":
        if AUTH["password_set"] and body.get("current") != AUTH["password"]: err(h, 403, "wrong_password", "Current password is wrong"); return True
        n = body.get("new", "")
        if len(n) < 8: err(h, 400, "password_too_short", "Password too short"); return True
        if len(n) > 64: err(h, 400, "password_too_long", "Password too long"); return True
        AUTH.update(password=n, password_set=True); h.send(200, {"status": "ok"}); return True
    if path == "/api/v1/auth/logout" and method == "POST": h.send(200, {"status": "ok"}); return True
    if path == "/api/v1/auth/keys":
        if method == "GET": h.send(200, {"max": MAX_KEYS, "keys": KEYS}); return True
        if method == "POST":
            name = (body.get("name") or "").strip()
            if not name or len(name) > 23: err(h, 400, "invalid_param", "name (1..23 characters)"); return True
            if len(KEYS) >= MAX_KEYS: err(h, 409, "too_many_keys", "All key slots are in use"); return True
            slot = min(set(range(1, MAX_KEYS + 1)) - {k["slot"] for k in KEYS})
            KEYS.append({"slot": slot, "name": name, "created": int(time.time())}); KEYS.sort(key=lambda k: k["slot"])
            h.send(200, {"slot": slot, "name": name, "key": "lgal_test_" + secrets.token_hex(16)}); return True
        if method == "DELETE":
            s = int(q.get("slot", 0))
            KEYS[:] = [k for k in KEYS if k["slot"] != s]; h.send(200, {"status": "ok"}); return True
    return False
