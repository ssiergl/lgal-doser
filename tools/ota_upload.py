#!/usr/bin/env python3
"""Uploads firmware to an LGAL device via OTA.

Usage:
    tools/ota_upload.py lgal-doser-eeff.local
    tools/ota_upload.py 192.168.1.50 --bin build/lgal_doser.bin
    tools/ota_upload.py 192.168.4.1 --password abcd...   # via setup Wi-Fi

Password: --password, environment variable LGAL_PASSWORD or --mac (then
derived from MAC + secret as in tools/ap_password.py).
Afterwards the script waits until the device answers with the new version.
"""
import argparse
import base64
import json
import os
import pathlib
import sys
import time
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent


def request(url, auth, data=None, method="GET", timeout=10):
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("Authorization", "Basic " + base64.b64encode(f"admin:{auth}".encode()).decode())
    if data is not None:
        req.add_header("Content-Type", "application/octet-stream")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.status, r.read()


def info(base, auth):
    _, body = request(f"{base}/api/v1/system/info", auth, timeout=5)
    return json.loads(body)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host")
    ap.add_argument("--bin", default=str(ROOT / "build" / "lgal_doser.bin"))
    ap.add_argument("--password", default=os.environ.get("LGAL_PASSWORD"))
    ap.add_argument("--mac", help="base MAC to derive the password")
    args = ap.parse_args()

    password = args.password
    if not password and args.mac:
        sys.path.insert(0, str(ROOT / "tools"))
        import ap_password  # noqa: E402

        mac = bytes.fromhex("".join(c for c in args.mac if c.isalnum()))
        password = ap_password.derive(mac, ap_password.secret_from_sdkconfig())
    if not password:
        sys.exit("Password missing (--password, LGAL_PASSWORD or --mac)")

    image = pathlib.Path(args.bin).read_bytes()
    base = f"http://{args.host}"

    before = info(base, password)
    print(f"Device {before['device']['hostname']}: Firmware {before['firmware']['version']} "
          f"({before['firmware']['partition']})")
    print(f"Uploading {len(image) / 1024:.0f} KiB …")
    t0 = time.time()
    try:
        status, body = request(f"{base}/api/v1/ota/firmware", password, data=image, method="POST", timeout=180)
    except urllib.error.HTTPError as e:
        sys.exit(f"Error {e.code}: {e.read().decode(errors='replace')}")
    print(f"Upload done after {time.time() - t0:.1f} s: {body.decode()}")

    print("Waiting for reboot …")
    time.sleep(5)
    deadline = time.time() + 90
    while time.time() < deadline:
        try:
            after = info(base, password)
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
