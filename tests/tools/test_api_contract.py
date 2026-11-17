# SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
# SPDX-License-Identifier: GPL-3.0-or-later

# Contract between the firmware, the web pages and the mock (tools/mock): the end-to-end tests run
# against the mock only, so a mock that drifts from the firmware lets them pass while the device
# fails. These tests read the routes, field names and error codes from the firmware sources and
# check the pages and the mock against them:
#   - every API path and method the pages call exists in the firmware,
#   - the mock answers every firmware route (not its "no such route" fallback),
#   - the mock knows no API path the firmware does not have,
#   - every field the mock answers and every error code it sends is a name the firmware uses.
# The last check is loose on purpose (the name occurs in the firmware as a string), but it catches
# a renamed or invented field. The WebSocket (/api/v1/ws) is covered by the end-to-end tests.
import http.cookiejar
import json
import os
import pathlib
import re
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
MOCK = ROOT / "tools" / "mock"


def _read(paths):
    return "\n".join(p.read_text(encoding="utf-8", errors="replace") for p in paths)


# Firmware sources without the web files (they are the other side of the contract)
FIRMWARE = _read(p for d in ("components", "main") for p in sorted((ROOT / d).rglob("*"))
                 if p.suffix in (".cpp", ".h", ".c") and "www" not in p.parts)
PAGES = _read(p for d in ("components/webui/www", "components/lgal_core/www")
              for p in sorted((ROOT / d).rglob("*"))
              if p.suffix in (".js", ".html") and "vendor" not in p.parts)
MOCKS = {p.name: p.read_text(encoding="utf-8") for p in sorted(MOCK.glob("*.py"))}

# http::route(server, "/path", HTTP_GET, fn) and reg(server, ...); the two raw handlers set uri/method
ROUTES = set(re.findall(r'\b(?:route|reg)\(\s*server,\s*"([^"]+)",\s*HTTP_([A-Z]+)', FIRMWARE))
ROUTES |= set(re.findall(r'\.uri\s*=\s*"([^"]+)";\s*\w+\.method\s*=\s*HTTP_([A-Z]+)', FIRMWARE))
PATHS = {p for p, _ in ROUTES}
# Every name in quotes, also inside printf formats (\"heap_free\":%u)
NAMES = set(re.findall(r'\\?"([A-Za-z0-9_]+)\\?"', FIRMWARE))


def test_firmware_routes_found():
    # guards the patterns above: when the firmware registers routes differently, they find nothing
    assert len(ROUTES) > 80
    assert {("/api/v1/ws", "GET"), ("/api/v1/ota/firmware", "POST"), ("/ui/*", "GET")} <= ROUTES


def test_pages_call_firmware_routes():
    paths = set(re.findall(r"""['"`](/api/v1/[^'"`?$\s]*)""", PAGES))
    assert len(paths) > 50
    assert sorted(paths - PATHS) == []
    calls = set(re.findall(r"""\(\s*'(GET|PUT|POST|DELETE)',\s*['"`](/api/v1/[^'"`?$\s]*)""", PAGES))
    assert len(calls) > 50
    assert sorted((p, m) for m, p in calls if (p, m) not in ROUTES) == []


def test_mock_paths_exist_in_firmware():
    found = {(name, p) for name, text in MOCKS.items()
             for p in re.findall(r'"(/api/v1/[^"?\s]*)', text)}
    # a path ending in "/" is a prefix (startswith)
    wrong = sorted((name, p) for name, p in found
                   if not (p in PATHS or p.endswith("/") and any(q.startswith(p) for q in PATHS)))
    assert wrong == []


def test_mock_error_codes_exist_in_firmware():
    codes = set()
    for name, text in MOCKS.items():
        for c in re.findall(r'\b_?err\(\s*(?:h|self),\s*(?:\d+,\s*)?(?:code=)?"([a-z_]+)"', text):
            codes.add((name, c))
        for c in re.findall(r'"code":\s*"([a-z_]+)"', text):
            codes.add((name, c))
    assert len(codes) > 30
    # "supply_" + state: a prefix
    wrong = sorted((name, c) for name, c in codes
                   if not (c in NAMES or c.endswith("_") and any(n.startswith(c) for n in NAMES)))
    assert wrong == []


# --- The mock as it runs --------------------------------------------------------------------------

def _free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _key(pattern):
    m = re.search(pattern, MOCKS["sys_mock.py"])
    assert m, f"sys_mock.py no longer matches {pattern}"
    return m[1]


class Client:
    def __init__(self, port):
        self.base = f"http://127.0.0.1:{port}"
        self.op = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))

    def login(self):
        status, _, _ = self.call("POST", "/api/v1/auth/login", {"password": _key(r'AUTH = \{"password": "([^"]+)"')})
        assert status == 200

    def call(self, method, path, body=None, ctype="application/json"):
        data = body if isinstance(body, bytes) else None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(self.base + path, method=method, data=data, headers={"Content-Type": ctype})
        try:
            with self.op.open(req, timeout=10) as r:
                return r.status, dict(r.headers), r.read()
        except urllib.error.HTTPError as e:
            return e.code, dict(e.headers), e.read()


@pytest.fixture(scope="module")
def mock():
    port = _free_port()
    # board 1.1: the answers include the 24 V supply fields as well
    env = dict(os.environ, MOCK_BOARD_REV="1.1")
    proc = subprocess.Popen([sys.executable, str(MOCK / "webui_server.py"), str(ROOT), str(port)], env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    c = Client(port)
    try:
        for _ in range(100):
            try:
                c.call("GET", "/api/v1/auth/status")
                break
            except OSError:
                time.sleep(0.1)
        c.login()
        status, _, _ = c.call("POST", "/api/v1/auth/engineering", {"key": _key(r'ENGKEY = "([^"]+)"')})
        assert status == 200
        yield c
    finally:
        proc.terminate()
        proc.wait(10)


# Routes that end the session or restart the device go last (the mock then is "down" for a few
# seconds, so the reboot is the very last, after a new sign-in); the WebSocket needs an upgrade
LAST = [("/api/v1/auth/engineering", "DELETE"), ("/api/v1/system/factory_reset", "POST"),
        ("/api/v1/auth/logout", "POST"), ("/api/v1/system/reboot", "POST")]
SKIP = {("/api/v1/ws", "GET")}
QUERY = {"/api/v1/pumps/schedule": "?pump=1", "/api/v1/hw/tmc": "?pump=1"}


def _probe_order():
    rest = sorted(r for r in ROUTES if r not in SKIP and r not in LAST)
    return rest + [r for r in LAST if r in ROUTES]


def _fields(value, out, where=""):
    if isinstance(value, dict):
        for k, v in value.items():
            out.add((k, where or "/"))
            _fields(v, out, f"{where}.{k}")
    elif isinstance(value, list):
        for v in value:
            _fields(v, out, where + "[]")


def test_mock_answers_firmware_routes(mock):
    unknown, fields = [], {}
    for path, method in _probe_order():
        url = "/ui/app.js" if path == "/ui/*" else path + QUERY.get(path, "")
        if (path, method) == LAST[-1]:
            mock.login()
        try:
            if path == "/api/v1/ota/firmware":
                status, headers, body = mock.call(method, url, b"\0" * 64, "application/octet-stream")
            elif path == "/api/v1/mqtt/ca" and method == "PUT":
                status, headers, body = mock.call(method, url, b"x", "text/plain")
            else:
                status, headers, body = mock.call(method, url, {} if method in ("POST", "PUT") else None)
        except OSError as e:  # the mock broke off (an error in the mock)
            unknown.append(f"{method} {path} -> {e}")
            continue
        if headers.get("X-Mock-No-Route") or status == 401:
            unknown.append(f"{method} {path} -> {status}")
            continue
        if headers.get("Content-Type", "").startswith("application/json"):
            found = set()
            _fields(json.loads(body), found)
            for k, where in found:
                if k not in NAMES and not k.isdigit():
                    fields.setdefault(f"{method} {path}", []).append(f"{k} ({where})")
    # field names the firmware never writes
    wrong = [f"{r}: {', '.join(sorted(f))}" for r, f in sorted(fields.items())]
    assert not unknown and not wrong, "\n".join(["the mock does not answer:", *unknown, "fields not in the firmware:", *wrong])
