# SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
# SPDX-License-Identifier: GPL-3.0-or-later

# tools/acceptance.py against the mock in its strict sign-in mode (MOCK_STRICT_AUTH=1: sessions and
# API keys per client like the firmware), board v1.1. Checks that the script finds what it should:
# the locked API, a recorded crash, the hardware with the engineering key, a changed setting
# after an update, a test dose and an update to the other partition.
import json
import os
import re
import subprocess
import sys
import time

import pytest

from test_api_contract import ROOT, Client, _free_port, _key

SCRIPT = ROOT / "tools" / "acceptance.py"


@pytest.fixture(scope="module")
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


def run(device, *args, key=None, eng=False, password=True):
    env = {k: v for k, v in os.environ.items() if not k.startswith("LGAL_")}
    if key:
        env["LGAL_API_KEY"] = key
    elif password:
        env["LGAL_PASSWORD"] = _key(r'AUTH = \{"password": "([^"]+)"')
    if eng:
        env["LGAL_ENG_KEY"] = _key(r'ENGKEY = "([^"]+)"')
    p = subprocess.run([sys.executable, str(SCRIPT), f"127.0.0.1:{device.port}", *args], env=env,
                       stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=180)
    lines = {}
    for m in re.finditer(r"^  (PASS|WARN|FAIL|SKIP)  (.+?)(?: – (.*))?$", p.stdout, re.M):
        lines.setdefault(m[2], (m[1], m[3] or ""))
    return p.returncode, lines, p.stdout + p.stderr


def test_without_engineering_key(device, tmp_path):
    code, r, out = run(device, "--snapshot", str(tmp_path / "s.json"))
    assert code == 1, out  # the mock has a core dump stored
    for name in ("API locked without a sign-in", "WebSocket needs a sign-in", "wrong password refused",
                 "changes from a foreign page refused", "WebSocket from a foreign page refused",
                 "engineering area locked", "24 V supply", "status arrives at once", "ping answered"):
        assert r[name][0] == "PASS", (name, r.get(name))
    assert r["signed in"] == ("PASS", "session")
    assert r["no core dump"][0] == "FAIL"
    assert r["drivers, clock module, EEPROM"][0] == "SKIP"
    assert r["pump 2 (Calcium)"] == ("WARN", "last run fault")
    snap = json.loads((tmp_path / "s.json").read_text())
    assert snap["settings"]["pumps"][0]["name"] == "KH" and "engineering" not in snap["settings"]
    # once the core dump is fetched and deleted, nothing fails
    assert device.call("DELETE", "/api/v1/system/coredump")[0] == 200
    code, r, out = run(device)
    assert code == 0, out


def test_no_sign_in(device):
    code, _, out = run(device, password=False)
    assert code == 1 and "Sign-in missing: LGAL_API_KEY or LGAL_PASSWORD" in out, out


def test_api_key_with_engineering_key(device, tmp_path):
    status, _, body = device.call("POST", "/api/v1/auth/keys", {"name": "acceptance"})
    assert status == 200
    key = json.loads(body)["key"]
    snap = tmp_path / "s.json"
    code, r, out = run(device, "--snapshot", str(snap), key=key, eng=True)
    assert code == 1, out  # the mock's driver 4 does not answer
    assert r["signed in"] == ("PASS", "api_key")
    assert r["driver 4"] == ("FAIL", "does not answer on UART")
    for n in (1, 2, 3, 5):
        assert r[f"driver {n}"][0] == "PASS", out
    for name in ("drivers off at rest", "UART bus A", "UART bus B", "clock module", "EEPROM", "history storage"):
        assert r[name][0] == "PASS", (name, r.get(name))
    assert "engineering" in json.loads(snap.read_text())["settings"]
    # the same state compared: unchanged; a renamed pump is found
    code, r, out = run(device, "--compare", str(snap), key=key, eng=True)
    assert r["settings unchanged"][0] == "PASS", out
    device.call("POST", "/api/v1/pumps/settings?pump=1&name=Renamed", {})
    try:
        code, r, out = run(device, "--compare", str(snap), key=key, eng=True)
        assert code == 1 and r["settings unchanged"][0] == "FAIL"
        assert 'pumps[0].name: "KH" → "Renamed"' in out
    finally:
        device.call("POST", "/api/v1/pumps/settings?pump=1&name=KH", {})


def test_dose_needs_a_yes_without_a_terminal(device):
    code, r, out = run(device, "--dose", "1:0.5")
    assert r["test dose"] == ("SKIP", "needs --yes without a terminal"), out


def test_dose(device):
    code, r, out = run(device, "--dose", "1:0.5", "--yes")
    assert r["dose started"][0] == "PASS" and r["dose finished"][0] == "PASS", out
    assert r["dose finished"][1].startswith("0.5 ml"), out


def test_update(device, tmp_path):
    image = tmp_path / "fw.bin"
    image.write_bytes(b"\xe9" + bytes(63))  # the mock checks the magic byte only
    code, r, out = run(device, "--ota", str(image), "--expect-version", "0.5.1-dev")
    assert code == 0, out
    for name in ("uploaded", "device back", "other partition", "version", "settings unchanged"):
        assert r[name][0] == "PASS", (name, out)
