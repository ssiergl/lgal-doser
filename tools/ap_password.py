#!/usr/bin/env python3
"""Computes the setup Wi-Fi name and device password of an LGAL device.

Must match lgal::auth::derive_password (components/lgal_core/src/auth_core.cpp):
    HMAC-SHA256(key=CONFIG_LGAL_AP_SECRET, msg=label + base MAC)
    -> one character per byte from ALPHABET[byte % 31]
    label b"LGAL-AP":  device password (label, setup Wi-Fi, first login,
                       admin password recovery), 12 characters
    label b"LGAL-ENG": engineering key (manufacturer only), 16 characters

Usage:
    tools/ap_password.py AA:BB:CC:DD:EE:FF
    tools/ap_password.py AA:BB:CC:DD:EE:FF --secret "..."   # otherwise from sdkconfig
    tools/ap_password.py AA:BB:CC:DD:EE:FF --engineering    # also the engineering key

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


def derive(mac: bytes, secret: str, label: bytes = b"LGAL-AP", chars: int = 12) -> str:
    digest = hmac.new(secret.encode(), label + mac, hashlib.sha256).digest()
    return "".join(ALPHABET[b % 31] for b in digest[:chars])


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mac", help="base MAC, e.g. AA:BB:CC:DD:EE:FF")
    ap.add_argument("--secret", help="CONFIG_LGAL_AP_SECRET (default: from sdkconfig)")
    ap.add_argument("--engineering", action="store_true", help="also print the engineering key")
    args = ap.parse_args()

    hexstr = re.sub(r"[^0-9a-fA-F]", "", args.mac)
    if len(hexstr) != 12:
        sys.exit("MAC must have 6 bytes")
    mac = bytes.fromhex(hexstr)
    secret = args.secret if args.secret is not None else secret_from_sdkconfig()

    print(f"Setup Wi-Fi: LG-Aquatic-Life-Setup-{mac[4]:02X}{mac[5]:02X}")
    print(f"Password   : {derive(mac, secret)}  (setup Wi-Fi, first login, password recovery)")
    print(f"Hostname   : lgal-doser-{mac[4]:02x}{mac[5]:02x}.local")
    if args.engineering:
        print(f"Engineering: {derive(mac, secret, b'LGAL-ENG', 16)}")


if __name__ == "__main__":
    main()
