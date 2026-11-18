# SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
# SPDX-License-Identifier: GPL-3.0-or-later

# tools/endurance.py: the matching of planned doses and history entries, and short runs against the
# mock (strict sign-in, board v1.1) – a quiet run, the hardware with the engineering key, a restart.
import datetime
import importlib.util
import json
import os
import re
import subprocess
import sys
import threading
import time

import pytest

from test_api_contract import ROOT, Client, _free_port, _key

SCRIPT = ROOT / "tools" / "endurance.py"
_spec = importlib.util.spec_from_file_location("endurance", SCRIPT)
endurance = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(endurance)


def at(text):
    return datetime.datetime.strptime("2026-11-" + text, "%Y-%m-%d %H:%M:%S")


def ev(kind, local, code, **extra):
    return {"kind": kind, "local": "2026-11-" + local, "pump": 1, "source": "schedule", "code": code, **extra}


def test_match_doses():
    plan = [(at("18 " + t), 5.0) for t in ("07:55:00", "09:00:00", "10:00:00", "11:00:00", "12:00:00",
                                         "13:00:00", "14:00:00", "14:05:00")] + [(at("19 07:50:00"), 5.0)]
    events = [
        ev("dose", "18 08:00:30", "success", target_ml=5.0, done_ml=5.0),   # the slot before the start
        ev("dose", "18 09:00:40", "success", target_ml=5.0, done_ml=5.0),
        ev("dose", "18 10:20:00", "success", target_ml=5.0, done_ml=4.5),   # late (lock time) and short
        ev("skip", "18 11:00:01", "daily_limit", count=1, target_ml=5.0),
        # 12:00: nothing
        ev("dose", "18 13:00:30", "fault", target_ml=5.0, done_ml=1.0),
        ev("skip", "18 16:30:00", "missed", count=2),                       # after an outage, for 14:00 and 14:05
        ev("dose", "18 20:00:00", "success", target_ml=2.0, done_ml=2.0),   # no plan for it
        ev("dose", "19 07:50:20", "success", target_ml=5.0, done_ml=5.0),   # too close to the end: not counted
    ]
    m = endurance.match_doses(plan, events, at("18 08:00:00"), at("19 08:00:00"), 130 * 60)
    assert m["planned"] == 7
    assert m["done"] == 2 and m["ml"] == pytest.approx(9.5) and m["short"] == 1
    assert m["skipped"] == {"daily_limit": 1, "missed": 2}
    assert m["failed"] == {"fault": 1}
    assert m["missing"] == [at("18 12:00:00")]
    assert m["extra"] == 1


def test_match_doses_too_late_is_missing():
    m = endurance.match_doses([(at("18 09:00:00"), 5.0)],
                              [ev("dose", "18 12:00:00", "success", target_ml=5.0, done_ml=5.0)],
                              at("18 08:00:00"), at("18 20:00:00"), 130 * 60)
    assert m["missing"] == [at("18 09:00:00")] and m["extra"] == 1


def test_live_watch_pings(monkeypatch):
    sent = []

    class FakeSocket:
        def settimeout(self, t):
            pass

    class FakeWs:
        status = 101
        s = FakeSocket()

        def __init__(self, dev):
            pass

        def send(self, msg):
            sent.append(msg)

        def recv(self):
            time.sleep(0.02)
            return {"type": "status"}

        def close(self):
            pass

    monkeypatch.setattr(endurance, "WebSocket", FakeWs)
    monkeypatch.setattr(endurance, "WS_PING_S", 0.1)
    live = endurance.LiveWatch(None)
    live.start()
    time.sleep(0.5)
    live.stop()
    live.join(1)
    assert 3 <= len(sent) <= 6 and all(m == {"type": "ping"} for m in sent), sent
    assert live.frames > 10 and live.drops == 0


@pytest.fixture
def device():
    port = _free_port()
    env = dict(os.environ, MOCK_STRICT_AUTH="1", MOCK_BOARD_REV="1.1")
    proc = subprocess.Popen([sys.executable, str(ROOT / "tools" / "mock" / "webui_server.py"), str(ROOT), str(port)],
                            env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    c = Client(port)
    try:
        for _ in range(100):
            try:
                c.call("GET", "/api/v1/auth/status")
                break
            except OSError:
                time.sleep(0.1)
        c.login()
        yield c
    finally:
        proc.terminate()
        proc.wait(10)


def run(device, *args, eng=False):
    env = {k: v for k, v in os.environ.items() if not k.startswith("LGAL_")}
    env["LGAL_PASSWORD"] = _key(r'AUTH = \{"password": "([^"]+)"')
    if eng:
        env["LGAL_ENG_KEY"] = _key(r'ENGKEY = "([^"]+)"')
    p = subprocess.run([sys.executable, str(SCRIPT), f"127.0.0.1:{device.port}", "--interval", "1", *args],
                       env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=120)
    lines = {}
    for m in re.finditer(r"^  (PASS|WARN|FAIL|SKIP)  (.+?)(?: – (.*))?$", p.stdout, re.M):
        lines.setdefault(m[2], (m[1], m[3] or ""))
    return p.returncode, lines, p.stdout + p.stderr


def test_quiet_run(device, tmp_path):
    log = tmp_path / "run.csv"
    code, r, out = run(device, "--hours", "0.002", "--log", str(log))
    assert code == 0, out
    for name in ("device answers", "no restart", "valid time", "network", "WebSocket", "no alarm"):
        assert r[name][0] == "PASS", (name, out)
    assert r["24 V supply"] == ("PASS", "24.1 – 24.1 V"), out
    assert r["no crash"][0] == "WARN"           # the mock keeps an older core dump
    assert r["no leak"][0] == "SKIP"            # too short for a trend
    assert r["pump 4"] == ("SKIP", "mode disabled: no schedule")
    assert r["drivers, UART, temperature"][0] == "SKIP"
    rows = log.read_text().splitlines()
    assert rows[0].startswith("time,device_utc,uptime_s,heap_free") and len(rows) >= 6


def test_hardware_with_the_engineering_key(device):
    code, r, out = run(device, "--hours", "0.002", eng=True)
    assert code == 1, out
    assert r["driver 4"][0] == "FAIL"           # the mock's driver 4 does not answer
    assert r["UART bus A"][0] == "PASS" and r["chip temperature"][0] == "PASS", out
    # the engineering area is locked again afterwards
    assert json.loads(device.call("GET", "/api/v1/auth/status")[2])["engineering"] is False


def test_a_restart_fails_the_run(device):
    threading.Timer(4, lambda: device.call("POST", "/api/v1/system/reboot")).start()
    code, r, out = run(device, "--hours", "0.004")
    assert code == 1, out
    assert r["no restart"][0] == "FAIL" and r["no restart"][1].endswith(" SW"), out
    assert r["device answers"][0] == "WARN", out   # unanswered while restarting, but not for long
    assert "the device restarted (SW)" in out
