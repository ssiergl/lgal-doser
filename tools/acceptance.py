#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
# SPDX-License-Identifier: GPL-3.0-or-later

"""Acceptance test of an LGAL Doser on the network: after every release candidate and at the
hardware test. It reads the device and changes nothing unless asked to (--dose, --ota).

Usage:
    tools/acceptance.py HOST                          checks only
    tools/acceptance.py HOST --snapshot before.json   ... and keep the settings for a later --compare
    tools/acceptance.py HOST --compare before.json    ... and check that the settings are unchanged
    tools/acceptance.py HOST --ota build/lgal_doser.bin --expect-version 0.1.0-rc.9
                                                      update in between: checks before, upload, wait
                                                      for the restart, checks again, settings compared
    tools/acceptance.py HOST --dose 1:2               a test dose of 2 ml on pump 1 (into a measuring
                                                      cup; asks first unless --yes)

Sign-in: an API key in LGAL_API_KEY (created in the web UI settings) or the admin password in
LGAL_PASSWORD; without either the script asks for the password. The checks of the hardware
(drivers on both UART buses, clock module, EEPROM, engineering values in the snapshot) need the
engineering key in LGAL_ENG_KEY; without it they are skipped. Secrets are never taken as
arguments (they would end up in the shell history).

Exit code: 0 all checks passed (warnings allowed), 1 a check failed, 2 the device cannot be
reached or the sign-in failed.
"""
import argparse
import base64
import getpass
import json
import os
import pathlib
import re
import socket
import struct
import sys
import time
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent

# Answered without a sign-in, like the firmware (and tools/mock/webui_server.py OPEN)
PUBLIC_GET = {"/api/v1/auth/status", "/api/v1/system/language"}
# A start the device logs as a crash
CRASH_RESETS = {"PANIC", "INT_WDT", "TASK_WDT", "WDT", "BROWNOUT"}
HEAP_MIN_FREE = 32 * 1024


# --- Output -----------------------------------------------------------------------------------------

class Report:
    def __init__(self):
        self.counts = {"PASS": 0, "WARN": 0, "FAIL": 0, "SKIP": 0}

    def line(self, kind, name, detail=""):
        self.counts[kind] += 1
        print(f"  {kind:4}  {name}" + (f" – {detail}" if detail else ""), flush=True)

    def ok(self, name, detail=""):
        self.line("PASS", name, detail)

    def warn(self, name, detail):
        self.line("WARN", name, detail)

    def fail(self, name, detail):
        self.line("FAIL", name, detail)

    def skip(self, name, detail):
        self.line("SKIP", name, detail)

    def check(self, cond, name, detail_ok="", detail_fail=""):
        (self.ok if cond else self.fail)(name, detail_ok if cond else detail_fail)
        return cond

    @staticmethod
    def section(title):
        print(f"\n{title}", flush=True)


# --- HTTP -------------------------------------------------------------------------------------------

class Device:
    """The device's API. Signed in with an API key or a session; sessions live in the device's RAM,
    so after a restart the client signs in again."""

    def __init__(self, host, key=None, password=None, eng_key=None, timeout=10):
        self.host = host
        self.base = f"http://{host}"
        self.key, self.password, self.eng_key = key, password, eng_key
        self.cookie = None
        self.timeout = timeout

    def raw(self, method, path, body=None, headers=None, auth=True, timeout=None, ctype="application/json"):
        """(status, headers, bytes) – also for 4xx/5xx; raises OSError when the device does not answer"""
        data = body if isinstance(body, bytes) else None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(self.base + path, data=data, method=method)
        if data is not None:
            req.add_header("Content-Type", ctype)
        if auth:
            for k, v in self.auth_headers().items():
                req.add_header(k, v)
        for k, v in (headers or {}).items():
            req.add_header(k, v)
        try:
            with urllib.request.urlopen(req, timeout=timeout or self.timeout) as r:
                return r.status, r.headers, r.read()
        except urllib.error.HTTPError as e:
            return e.code, e.headers, e.read()

    def auth_headers(self):
        if self.key:
            h = {"X-API-Key": self.key}
            if self.eng_key:
                h["X-Engineering-Key"] = self.eng_key
            return h
        return {"Cookie": self.cookie} if self.cookie else {}

    def login(self):
        if self.key:
            return
        status, headers, body = self.raw("POST", "/api/v1/auth/login", {"password": self.password}, auth=False)
        if status != 200:
            raise LoginError(f"sign-in refused ({status}): {error_code(body)}")
        self.cookie = (headers.get("Set-Cookie") or "").split(";", 1)[0]
        if self.eng_key:
            status, _, body = self.raw("POST", "/api/v1/auth/engineering", {"key": self.eng_key})
            if status != 200:
                raise LoginError(f"engineering key refused ({status}): {error_code(body)}")

    def json(self, path, method="GET", body=None, timeout=None):
        status, _, data = self.raw(method, path, body, timeout=timeout)
        if status == 401 and not self.key:  # the session is gone (restart): once more
            self.login()
            status, _, data = self.raw(method, path, body, timeout=timeout)
        if status >= 300:
            raise ApiError(path, status, error_code(data))
        return json.loads(data or b"null")

    def lock_engineering(self):
        if self.eng_key and not self.key:
            self.raw("DELETE", "/api/v1/auth/engineering")


class LoginError(Exception):
    pass


class ApiError(Exception):
    def __init__(self, path, status, code):
        super().__init__(f"{path}: {status} {code}")
        self.status, self.code = status, code


def error_code(body):
    try:
        return json.loads(body)["error"]["code"]
    except (ValueError, KeyError, TypeError):
        return body[:80].decode(errors="replace") if isinstance(body, bytes) else str(body)


# --- WebSocket (just enough of RFC 6455 for text frames) --------------------------------------------

class WebSocket:
    def __init__(self, dev, origin=None, auth=True):
        host, _, port = dev.host.partition(":")
        self.s = socket.create_connection((host, int(port or 80)), timeout=5)
        key = base64.b64encode(os.urandom(16)).decode()
        lines = [f"GET /api/v1/ws HTTP/1.1", f"Host: {dev.host}", "Upgrade: websocket", "Connection: Upgrade",
                 f"Sec-WebSocket-Key: {key}", "Sec-WebSocket-Version: 13"]
        if origin:
            lines.append(f"Origin: {origin}")
        if auth:
            lines += [f"{k}: {v}" for k, v in dev.auth_headers().items()]
        self.s.sendall(("\r\n".join(lines) + "\r\n\r\n").encode())
        head = b""
        while b"\r\n\r\n" not in head:
            c = self.s.recv(1)
            if not c:
                break
            head += c
        self.status = int(head.split(b" ", 2)[1]) if head.startswith(b"HTTP/1.1 ") else 0

    def _exact(self, n):
        buf = b""
        while len(buf) < n:
            c = self.s.recv(n - len(buf))
            if not c:
                raise ConnectionError("connection closed")
            buf += c
        return buf

    def send(self, msg):
        b, mask = json.dumps(msg).encode(), os.urandom(4)
        n = len(b)
        head = bytes([0x81, 0x80 | n]) if n < 126 else bytes([0x81, 0xFE]) + struct.pack(">H", n)
        self.s.sendall(head + mask + bytes(c ^ mask[i % 4] for i, c in enumerate(b)))

    def recv(self):
        b0, b1 = self._exact(2)
        n = b1 & 127
        if n == 126:
            n = struct.unpack(">H", self._exact(2))[0]
        elif n == 127:
            n = struct.unpack(">Q", self._exact(8))[0]
        data = self._exact(n)
        return json.loads(data) if b0 & 15 == 1 else None

    def close(self):
        self.s.close()


# --- Routes from the firmware sources ---------------------------------------------------------------

def firmware_get_routes():
    """GET routes of the API as the firmware registers them (the script lives in the repository)"""
    text = "\n".join(p.read_text(encoding="utf-8", errors="replace")
                     for d in ("components", "main") for p in sorted((ROOT / d).rglob("*"))
                     if p.suffix in (".cpp", ".c") and "www" not in p.parts)
    routes = set(re.findall(r'\b(?:route|reg)\(\s*server,\s*"(/api/[^"]+)",\s*HTTP_GET', text))
    routes |= {p for p, m in re.findall(r'\.uri\s*=\s*"(/api/[^"]+)";\s*\w+\.method\s*=\s*HTTP_([A-Z]+)', text)
               if m == "GET"}
    return sorted(routes - {"/api/v1/ws"})


# --- Checks -----------------------------------------------------------------------------------------

def check_gate(r, dev):
    r.section("Access without a sign-in")
    status, _, body = dev.raw("GET", "/api/v1/auth/status", auth=False)
    if status != 200:
        raise LoginError(f"/api/v1/auth/status answers {status}")
    st = json.loads(body)
    r.ok("device answers", st.get("device", ""))
    r.check(st.get("setup_completed"), "first setup completed", detail_fail="finish the setup wizard first")
    routes = firmware_get_routes()
    if not routes:
        r.skip("API locked without a sign-in", "firmware sources not found next to the script")
    else:
        q = {"/api/v1/pumps/schedule": "?pump=1", "/api/v1/hw/tmc": "?pump=1"}
        wrong = []
        for p in routes:
            status, _, _ = dev.raw("GET", p + q.get(p, ""), auth=False, headers={"Accept": "application/json"})
            if (status == 401) == (p in PUBLIC_GET) or (p in PUBLIC_GET and status != 200):
                wrong.append(f"{p} → {status}")
        r.check(not wrong, "API locked without a sign-in", f"{len(routes)} GET routes, only {len(PUBLIC_GET)} open",
                "; ".join(wrong))
    ws = WebSocket(dev, auth=False)
    r.check(ws.status == 401, "WebSocket needs a sign-in", detail_fail=f"answers {ws.status}")
    ws.close()
    status, _, body = dev.raw("POST", "/api/v1/auth/login", {"password": "acceptance-wrong-password"}, auth=False)
    r.check(status in (401, 403) and error_code(body) == "wrong_password", "wrong password refused",
            detail_fail=f"{status} {error_code(body)}")


def check_signed_in(r, dev):
    r.section("Sign-in")
    st = dev.json("/api/v1/auth/status")
    r.check(st.get("logged_in"), "signed in", st.get("via", ""), "the device does not take the sign-in")
    # foreign pages must not change anything (CSRF): a harmless stop of pump 1
    status, _, body = dev.raw("POST", "/api/v1/pumps/stop?pump=1", headers={"Origin": "http://foreign.example"})
    r.check(status == 403 and error_code(body) == "forbidden_origin", "changes from a foreign page refused",
            detail_fail=f"{status} {error_code(body)}")
    ws = WebSocket(dev, origin="http://foreign.example")
    r.check(ws.status == 403, "WebSocket from a foreign page refused", detail_fail=f"answers {ws.status}")
    ws.close()
    if not dev.eng_key:
        status, _, body = dev.raw("GET", "/api/v1/hw/status")
        r.check(status == 403 and error_code(body) == "engineering_locked", "engineering area locked",
                detail_fail=f"{status} {error_code(body)}")
    return st


def check_firmware(r, dev, expect_version=None):
    r.section("Firmware and system")
    info = dev.json("/api/v1/system/info")
    fw, sysi = info["firmware"], info["system"]
    v = fw["version"]
    if expect_version:
        r.check(v == expect_version, "version", v, f"{v}, expected {expect_version}")
    else:
        r.ok("version", f"{v} ({fw['partition']}, IDF {fw['idf']})")
    state = fw.get("ota_state")
    if state == "VALID":
        r.ok("update confirmed", state)
    elif state == "PENDING_VERIFY":
        r.warn("update confirmed", "PENDING_VERIFY: the device confirms it after a stable runtime")
    elif state in (None, "UNDEFINED"):
        r.ok("update confirmed", "factory flash, no update yet")
    else:
        r.fail("update confirmed", str(state))
    if fw.get("last_invalid_partition"):
        r.warn("no rollback", f"an update was rolled back from {fw['last_invalid_partition']} (also an older one)")
    else:
        r.ok("no rollback")
    reset = sysi.get("reset_reason", "")
    r.check(reset not in CRASH_RESETS, "last start", f"{reset}, up {sysi['uptime_s'] // 3600} h",
            f"{reset} (crash), up {sysi['uptime_s']} s")
    r.check(not sysi.get("coredump"), "no core dump",
            detail_fail="a crash was recorded: GET /api/v1/system/coredump (signed in) gets it")
    m = sysi.get("heap_min_free", 0)
    (r.ok if m >= HEAP_MIN_FREE else r.warn)("free memory", f"{sysi['heap_free'] // 1024} KiB, at least "
                                              f"{m // 1024} KiB since the start")
    t = info["time"]
    r.check(t.get("valid"), "clock", f"{t.get('local')} ({t.get('source')})", "no valid time")
    r.check(t.get("rtc_ok", True), "clock module answers", detail_fail="the DS3231 does not answer on I²C")
    if t.get("rtc_lost"):
        r.warn("clock module kept the time", "it lost the time: backup battery?")
    net = info["network"]
    if net.get("active") == "lan":
        r.ok("network", f"LAN {net.get('eth_ip')}")
    else:
        r.warn("network", f"{net.get('active')} (Ethernet link {net.get('eth_link')})")
    if info.get("board_rev") == "1.1":
        s = info.get("supply") or {}
        volts = s.get("volts") or 0
        r.check(s.get("state") == "ok" and 20 <= volts <= 28, "24 V supply", f"{volts:.1f} V",
                f"{s.get('state')}, {volts:.1f} V")
    else:
        r.skip("24 V supply", f"board {info['device'].get('board')} rev {info.get('board_rev', '1.0')} measures none")
    ota = dev.json("/api/v1/ota/status")
    r.check(ota.get("state") in ("idle", "done"), "no update running", ota.get("state"), ota.get("state"))
    return info


def check_pumps(r, dev):
    r.section("Pumps and schedules")
    pumps = dev.json("/api/v1/pumps")["pumps"]
    sch = dev.json("/api/v1/schedule/status")
    r.check(sch.get("clock_valid"), "schedules have a clock", detail_fail="schedules wait for a valid time")
    by = {p["pump"]: p for p in sch.get("pumps", [])}
    for p in pumps:
        n = p["pump"]
        name = f"pump {n}" + (f" ({p['name']})" if p.get("name") else "")
        if not p.get("fitted"):
            r.skip(name, "not fitted")
            continue
        notes = []
        if p.get("activity") != "idle":
            notes.append(f"busy: {p['activity']}")
        last = p.get("last") or {}
        if last.get("result") not in (None, "success", "aborted"):
            notes.append(f"last run {last['result']}")
        cal = p.get("calibration") or {}
        if not cal.get("calibrated"):
            notes.append("not calibrated")
        elif cal.get("due"):
            notes.append("calibration due")
        s = by.get(n) or {}
        if s.get("skipped"):
            notes.append(f"{s['skipped']} doses skipped today ({(s.get('last_skip') or {}).get('reason', '?')})")
        detail = f"{p['mode']}, {p['daily']['used_ml']:.1f} of {p['daily']['limit_ml'] or '∞'} ml today"
        (r.warn if notes else r.ok)(name, "; ".join(notes) if notes else detail)


def check_hardware(r, dev, info):
    r.section("Hardware (engineering)")
    if not dev.eng_key:
        r.skip("drivers, clock module, EEPROM", "no LGAL_ENG_KEY")
        return None
    hw = dev.json("/api/v1/hw/status")
    r.check(not hw.get("drivers_enabled") or any(p.get("running") for p in hw["pumps"]), "drivers off at rest",
            detail_fail="EN is active while no pump runs")
    for p in hw["pumps"]:
        n = p["pump"]
        if not p.get("fitted"):
            r.skip(f"driver {n}", "not fitted")
            continue
        f = p.get("flags") or {}
        bad = [k for k in ("overtemp", "overtemp_warn", "short") if f.get(k)]
        if not p.get("present"):
            r.fail(f"driver {n}", "does not answer on UART")
        elif not p.get("configured"):
            r.fail(f"driver {n}", "answers, but its registers are not set")
        elif p.get("fault") or bad:
            r.fail(f"driver {n}", ", ".join(filter(None, [p.get("fault")] + bad)))
        else:
            r.ok(f"driver {n}", f"version 0x{p.get('version', 0):02x}, {p['config']['run_ma']} mA, "
                                f"{p['config']['microsteps']} microsteps")
    for b in hw.get("uart", []):
        errors = b.get("timeouts", 0) + b.get("crc_errors", 0) + b.get("echo_errors", 0)
        rate = errors / max(1, b.get("transfers", 0))
        (r.ok if rate <= 0.01 else r.warn)(f"UART bus {b['bus'].split()[0]}",
                                           f"{b.get('transfers', 0)} transfers, {errors} errors")
    rtc = hw.get("rtc") or {}
    r.check(rtc.get("present"), "clock module", f"{rtc.get('temp_c')} °C, aging {rtc.get('aging')}",
            "not found on I²C (0x68)")
    if rtc.get("battery_warning") or rtc.get("time_lost"):
        r.warn("backup battery", "the clock module lost its time")
    if (hw.get("eeprom") or {}).get("present"):
        r.ok("EEPROM", "found")
    else:
        r.fail("EEPROM", "not found on I²C (0x57); " + i2c_scan(dev))
    tel = dev.json("/api/v1/engineering/telemetry")
    st = tel.get("storage") or {}
    (r.ok if not st.get("history_dropped") else r.warn)(
        "history storage", f"{st.get('history_days')} days, {st.get('history_dropped', 0)} events dropped")
    return hw


def i2c_scan(dev):
    """The bus scan the firmware logs at start (in the RAM log until it is overwritten)"""
    status, _, body = dev.raw("GET", "/api/v1/system/log")
    found = re.findall(r"Bus scan: [^\r\n]*", body.decode(errors="replace")) if status == 200 else []
    return f"at the last start: {found[-1]}" if found else "the bus scan of the start is no longer in the log"


def check_live(r, dev):
    r.section("Live connection")
    ws = WebSocket(dev)
    if not r.check(ws.status == 101, "WebSocket opens", detail_fail=f"answers {ws.status}"):
        return
    try:
        first = ws.recv()
        r.check(first and first.get("type") == "status" and "seq" in first, "status arrives at once",
                detail_fail=str(first)[:80])
        ws.send({"type": "ping", "id": 1})
        for _ in range(20):
            m = ws.recv()
            if m and m.get("type") != "status":
                break
        r.check(m == {"type": "ping", "id": 1, "ok": True}, "ping answered", detail_fail=str(m)[:80])
    except (OSError, ValueError) as e:
        r.fail("WebSocket", str(e))
    finally:
        ws.close()


# --- Settings snapshot ------------------------------------------------------------------------------

def _pick(d, *keys):
    return {k: d.get(k) for k in keys if k in d}


def snapshot(dev, info):
    """The settings that must survive an update (no counters, no live values)"""
    pumps = []
    for p in dev.json("/api/v1/pumps")["pumps"]:
        pumps.append({
            **_pick(p, "pump", "fitted", "name", "mode", "direction"),
            "calibration": _pick(p.get("calibration") or {}, "ml_per_rev", "calibrated_at", "remind_months", "target_ml"),
            "speed": (p.get("speed") or {}).get("tier"),
            "retract": _pick(p.get("retract") or {}, "setting", "rev"),
            "daily": _pick(p.get("daily") or {}, "limit_ml", "mode"),
            "container": _pick(p.get("container") or {}, "tracked", "capacity_ml", "own_warn_days", "own_low_days"),
            "tube_limit_h": (p.get("tube") or {}).get("limit_h"),
        })
    schedules = {}
    for p in pumps:
        s = dev.json(f"/api/v1/pumps/schedule?pump={p['pump']}")
        s.pop("today", None)
        schedules[str(p["pump"])] = s
    led = dev.json("/api/v1/system/led")
    led.pop("shown", None)
    (led.get("night") or {}).pop("active", None)
    t = dev.json("/api/v1/system/time")
    mqtt = dev.json("/api/v1/mqtt")
    snap = {
        "device": dev.json("/api/v1/system/device"),
        "language": dev.json("/api/v1/system/language").get("language"),
        "time": _pick(t, "timezone", "ntp_server"),
        "network": dev.json("/api/v1/net/settings"),
        "mqtt": {"settings": mqtt.get("settings"), "ca": (mqtt.get("ca") or {}).get("present")},
        "led": led,
        "updates": dev.json("/api/v1/ota/settings"),
        "history": _pick(dev.json("/api/v1/history/settings"), "keep_days"),
        "locks": dev.json("/api/v1/schedule/locks"),
        "pumps": pumps,
        "schedules": schedules,
    }
    if not dev.key:  # the device lists its keys only to a session
        snap["api_keys"] = sorted(k["name"] for k in dev.json("/api/v1/auth/keys").get("keys", []))
    if dev.eng_key:
        hw = dev.json("/api/v1/hw/status")
        snap["engineering"] = {
            "dosing": dev.json("/api/v1/engineering/dosing"),
            "scheduler": dev.json("/api/v1/engineering/scheduler"),
            "fitted": hw.get("fitted"),
            "drivers": {str(p["pump"]): p.get("config") for p in hw.get("pumps", [])},
        }
    return {"format": "lgal-doser-acceptance", "version": 1, "firmware": info["firmware"]["version"],
            "hostname": info["device"]["hostname"], "taken": int(time.time()), "settings": snap}


def diff(a, b, path=""):
    if isinstance(a, dict) and isinstance(b, dict):
        out = []
        for k in sorted(set(a) | set(b)):
            out += diff(a.get(k, "<missing>"), b.get(k, "<missing>"), f"{path}.{k}" if path else k)
        return out
    if isinstance(a, list) and isinstance(b, list) and len(a) == len(b):
        return [d for i, (x, y) in enumerate(zip(a, b)) for d in diff(x, y, f"{path}[{i}]")]
    return [] if a == b else [f"{path}: {json.dumps(a)} → {json.dumps(b)}"]


def compare(r, before, now, label):
    r.section(f"Settings compared with {label}")
    old, new = before["settings"], now["settings"]
    if "engineering" in old and "engineering" not in new:
        r.skip("engineering values", "no LGAL_ENG_KEY now")
        old = {k: v for k, v in old.items() if k != "engineering"}
    elif "engineering" in new and "engineering" not in old:
        new = {k: v for k, v in new.items() if k != "engineering"}
    changes = diff(old, new)
    r.check(not changes, "settings unchanged", f"{len(old)} groups ({before['firmware']} → {now['firmware']})",
            "\n          " + "\n          ".join(changes))


# --- Actions ----------------------------------------------------------------------------------------

def test_dose(r, dev, pump, ml, yes):
    r.section(f"Test dose: pump {pump}, {ml} ml")
    if not yes:
        if not sys.stdin.isatty():
            r.skip("test dose", "needs --yes without a terminal")
            return
        a = input(f"  Measuring cup under pump {pump}? It doses {ml} ml (counts toward the daily amount). [y/N] ")
        if a.strip().lower() not in ("y", "yes", "j", "ja"):
            r.skip("test dose", "not confirmed")
            return
    try:
        dev.json(f"/api/v1/pumps/dose?pump={pump}&ml={ml}", method="POST")
    except ApiError as e:
        r.fail("dose started", f"{e.status} {e.code}")
        return
    r.ok("dose started")
    t0 = time.time()
    while time.time() - t0 < 300:
        time.sleep(0.5)
        p = dev.json("/api/v1/pumps")["pumps"][pump - 1]
        if p.get("activity") == "idle":
            break
    else:
        dev.raw("POST", f"/api/v1/pumps/stop?pump={pump}")
        r.fail("dose finished", "still running after 5 min, stopped")
        return
    last = p.get("last") or {}
    r.check(last.get("result") == "success" and abs((last.get("done_ml") or 0) - ml) < 0.01,
            "dose finished", f"{last.get('done_ml')} ml in {time.time() - t0:.0f} s – now weigh or read the cup",
            f"{last.get('result')}, {last.get('done_ml')} of {ml} ml")


def update(r, dev, image, before_info):
    r.section(f"Update with {image.name} ({image.stat().st_size // 1024} KiB)")
    data = image.read_bytes()
    t0 = time.time()
    status, _, body = dev.raw("POST", "/api/v1/ota/firmware", data, ctype="application/octet-stream", timeout=240)
    if not r.check(status == 200, "uploaded", f"{time.time() - t0:.0f} s", f"{status} {error_code(body)}"):
        return False
    time.sleep(5)
    dev.cookie = None
    t0 = time.time()
    while time.time() - t0 < 120:
        try:
            dev.login()
            info = dev.json("/api/v1/system/info", timeout=5)
            break
        except (OSError, LoginError, ApiError):
            time.sleep(2)
    else:
        r.fail("device back", "no answer 2 min after the upload: wait for the rollback, then check the log")
        return False
    r.ok("device back", f"after {time.time() - t0 + 5:.0f} s")
    r.check(info["firmware"]["partition"] != before_info["firmware"]["partition"], "other partition",
            info["firmware"]["partition"], "the same partition: the update was not taken or was rolled back")
    return True


# --- Main -------------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host", help="address of the device, e.g. lgal-doser-e4f5.local or 192.168.1.50")
    ap.add_argument("--expect-version", help="the firmware version the device must run (after --ota: the new one)")
    ap.add_argument("--snapshot", type=pathlib.Path, help="write the settings to this file")
    ap.add_argument("--compare", type=pathlib.Path, help="compare the settings with this file")
    ap.add_argument("--ota", type=pathlib.Path, help="firmware image (.bin) to install in between")
    ap.add_argument("--dose", action="append", default=[], metavar="PUMP:ML", help="test dose (repeatable)")
    ap.add_argument("--yes", action="store_true", help="do not ask before a test dose")
    args = ap.parse_args()

    doses = []
    for d in args.dose:
        m = re.fullmatch(r"([1-5]):(\d+(?:\.\d+)?)", d)
        if not m or not 0 < float(m[2]) <= 50:
            ap.error(f"--dose {d}: PUMP:ML with pump 1..5 and up to 50 ml")
        doses.append((int(m[1]), float(m[2])))
    if args.ota and not args.ota.is_file():
        ap.error(f"--ota {args.ota}: no such file")
    before = json.loads(args.compare.read_text()) if args.compare else None

    key, password = os.environ.get("LGAL_API_KEY"), os.environ.get("LGAL_PASSWORD")
    if not key and not password:
        if not sys.stdin.isatty():
            sys.exit("Sign-in missing: LGAL_API_KEY or LGAL_PASSWORD")
        password = getpass.getpass("Admin password of the device: ")
    dev = Device(args.host, key=key, password=password, eng_key=os.environ.get("LGAL_ENG_KEY"))
    r = Report()
    print(f"LGAL Doser acceptance test: {args.host}")
    try:
        check_gate(r, dev)
        dev.login()
        check_signed_in(r, dev)
        info = check_firmware(r, dev, None if args.ota else args.expect_version)
        check_pumps(r, dev)
        check_hardware(r, dev, info)
        check_live(r, dev)
        snap = snapshot(dev, info)
        if before:
            compare(r, before, snap, str(args.compare))
        for pump, ml in doses:
            test_dose(r, dev, pump, ml, args.yes)
        if args.ota and update(r, dev, args.ota, info):
            info = check_firmware(r, dev, args.expect_version)
            check_pumps(r, dev)
            check_hardware(r, dev, info)
            check_live(r, dev)
            new = snapshot(dev, info)
            compare(r, snap, new, "the state before the update")
            snap = new
        if args.snapshot:
            args.snapshot.write_text(json.dumps(snap, indent=2, ensure_ascii=False) + "\n")
            print(f"\nSettings written to {args.snapshot}")
    except LoginError as e:
        print(f"\nCannot sign in: {e}")
        sys.exit(2)
    except (OSError, ApiError) as e:
        print(f"\nThe device stopped answering: {e}")
        r.fail("device answers", str(e))
    finally:
        dev.lock_engineering()
    c = r.counts
    print(f"\n{c['PASS']} passed, {c['WARN']} warnings, {c['FAIL']} failed, {c['SKIP']} skipped")
    sys.exit(1 if c["FAIL"] else 0)


if __name__ == "__main__":
    main()
