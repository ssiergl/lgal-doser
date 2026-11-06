#!/usr/bin/env python3
"""Uploads firmware to an LGAL device via OTA.

Usage:
    tools/ota_upload.py lgal-doser-eeff.local --key lgal_...
    tools/ota_upload.py 192.168.1.50 --bin build/lgal_doser.bin --password ...
    tools/ota_upload.py 192.168.4.1 --mac AA:BB:CC:DD:EE:FF   # before the setup

Login: an API key (--key or LGAL_API_KEY, created on /login), the admin
password (--password or LGAL_PASSWORD) or, as long as no admin password is
set, the device password (--mac: derived from MAC + secret as in
tools/ap_password.py).
Afterwards the script waits until the device answers with the new version.
"""
import argparse
import json
import os
import pathlib
import sys
import time
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent


class Client:
    """API key or session login (sessions live in RAM: after the reboot the
    client signs in again)."""

    def __init__(self, base, key=None, password=None):
        self.base, self.key, self.password, self.cookie = base, key, password, None

    def login(self):
        body = json.dumps({"password": self.password}).encode()
        req = urllib.request.Request(f"{self.base}/api/v1/auth/login", data=body, method="POST")
        req.add_header("Content-Type", "application/json")
        with urllib.request.urlopen(req, timeout=10) as r:
            cookie = r.headers.get("Set-Cookie", "")
        self.cookie = cookie.split(";", 1)[0]

    def request(self, path, data=None, method="GET", timeout=10, retry=True):
        if not self.key and not self.cookie:
            self.login()
        req = urllib.request.Request(f"{self.base}{path}", data=data, method=method)
        if self.key:
            req.add_header("X-API-Key", self.key)
        else:
            req.add_header("Cookie", self.cookie)
        if data is not None:
            req.add_header("Content-Type", "application/octet-stream")
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                return r.status, r.read()
        except urllib.error.HTTPError as e:
            if e.code == 401 and retry and not self.key:
                self.cookie = None  # session lost (reboot)
                return self.request(path, data, method, timeout, retry=False)
            raise


def info(client):
    _, body = client.request("/api/v1/system/info", timeout=5)
    return json.loads(body)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host")
    ap.add_argument("--bin", default=str(ROOT / "build" / "lgal_doser.bin"))
    ap.add_argument("--key", default=os.environ.get("LGAL_API_KEY"), help="API key (lgal_...)")
    ap.add_argument("--password", default=os.environ.get("LGAL_PASSWORD"), help="admin password")
    ap.add_argument("--mac", help="base MAC to derive the device password (before the setup)")
    args = ap.parse_args()

    password = args.password
    if not password and args.mac:
        sys.path.insert(0, str(ROOT / "tools"))
        import ap_password  # noqa: E402

        mac = bytes.fromhex("".join(c for c in args.mac if c.isalnum()))
        password = ap_password.derive(mac, ap_password.secret_from_sdkconfig())
    if not password and not args.key:
        sys.exit("Login missing (--key, --password or --mac)")

    image = pathlib.Path(args.bin).read_bytes()
    client = Client(f"http://{args.host}", key=args.key, password=password)

    try:
        before = info(client)
    except urllib.error.HTTPError as e:
        sys.exit(f"Login failed ({e.code}): {e.read().decode(errors='replace')}")
    print(f"Device {before['device']['hostname']}: Firmware {before['firmware']['version']} "
          f"({before['firmware']['partition']})")
    print(f"Uploading {len(image) / 1024:.0f} KiB …")
    t0 = time.time()
    try:
        status, body = client.request("/api/v1/ota/firmware", data=image, method="POST", timeout=180)
    except urllib.error.HTTPError as e:
        sys.exit(f"Error {e.code}: {e.read().decode(errors='replace')}")
    print(f"Upload done after {time.time() - t0:.1f} s: {body.decode()}")

    print("Waiting for reboot …")
    time.sleep(5)
    deadline = time.time() + 90
    while time.time() < deadline:
        try:
            after = info(client)
            fw = after["firmware"]
            print(f"Device running: firmware {fw['version']} ({fw['partition']}, {fw['ota_state']})")
            if fw["partition"] == before["firmware"]["partition"]:
                print("WARNING: same partition as before – update was not applied or was rolled back.")
                sys.exit(1)
            print(f"The image is confirmed automatically after stable runtime (state now: {fw['ota_state']}).")
            return
        except Exception:
            time.sleep(2)
    sys.exit("Device does not answer after the update – wait for rollback and check the log.")


if __name__ == "__main__":
    main()
