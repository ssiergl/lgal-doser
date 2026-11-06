#!/usr/bin/env python3
"""Computes the setup Wi-Fi name and setup/admin password of an LGAL device.

Must match the derivation in components/lgal_core/src/device.cpp exactly:
    HMAC-SHA256(key=CONFIG_LGAL_AP_SECRET, msg=b"LGAL-AP" + base MAC)
    -> first 12 bytes, one character per byte from ALPHABET[byte % 31]

Usage:
    tools/ap_password.py AA:BB:CC:DD:EE:FF
    tools/ap_password.py AA:BB:CC:DD:EE:FF --secret "..."   # otherwise from sdkconfig

esptool prints the base MAC on the first flash ("MAC: ...").
"""
import argparse
import hashlib
import hmac
import pathlib
import re
import sys

ALPHABET = "23456789abcdefghjkmnpqrstuvwxyz"
assert len(ALPHABET) == 31


def secret_from_sdkconfig() -> str:
    root = pathlib.Path(__file__).resolve().parent.parent
    for name in ("sdkconfig", "sdkconfig.defaults"):
        p = root / name
        if p.exists():
            m = re.search(r'^CONFIG_LGAL_AP_SECRET="(.*)"$', p.read_text(), re.M)
            if m:
                return m.group(1)
    sys.exit("CONFIG_LGAL_AP_SECRET not found, please pass --secret")


def derive(mac: bytes, secret: str) -> str:
    digest = hmac.new(secret.encode(), b"LGAL-AP" + mac, hashlib.sha256).digest()
    return "".join(ALPHABET[b % 31] for b in digest[:12])


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mac", help="base MAC, e.g. AA:BB:CC:DD:EE:FF")
    ap.add_argument("--secret", help="CONFIG_LGAL_AP_SECRET (default: from sdkconfig)")
    args = ap.parse_args()

    hexstr = re.sub(r"[^0-9a-fA-F]", "", args.mac)
    if len(hexstr) != 12:
        sys.exit("MAC must have 6 bytes")
    mac = bytes.fromhex(hexstr)
    secret = args.secret if args.secret is not None else secret_from_sdkconfig()

    print(f"Setup Wi-Fi: LG-Aquatic-Life-Setup-{mac[4]:02X}{mac[5]:02X}")
    print(f"Password   : {derive(mac, secret)}")
    print(f"Hostname   : lgal-doser-{mac[4]:02x}{mac[5]:02x}.local  (Web-Login: admin)")


if __name__ == "__main__":
    main()
