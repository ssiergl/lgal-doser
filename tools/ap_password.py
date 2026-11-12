#!/usr/bin/env python3
"""Computes the setup Wi-Fi name and passwords of an LGAL device.

Every device keeps its own secret in the factory partition "fctry"
(lgal/device.h). tools/provision.py writes it at the first USB flash as
    device secret = HMAC-SHA256(key=master, msg=b"LGAL-DEVICE" + base MAC), hex
with the manufacturer master from factory.secret (repository root, not in
git, never in the firmware). The passwords follow from the device secret as
in lgal::auth::derive_password (components/lgal_core/src/auth_core.cpp):
    HMAC-SHA256(key=device secret, msg=label + base MAC)
    -> one character per byte from ALPHABET[byte % 31]
    label b"LGAL-AP":  device password (label, setup Wi-Fi, first login,
                       admin password recovery), 12 characters
    label b"LGAL-ENG": engineering key (manufacturer only), 16 characters

Usage:
    tools/ap_password.py AA:BB:CC:DD:EE:FF                      # master from factory.secret
    tools/ap_password.py AA:BB:CC:DD:EE:FF --master-file FILE
    tools/ap_password.py AA:BB:CC:DD:EE:FF --secret <64 hex>    # device secret itself
    tools/ap_password.py AA:BB:CC:DD:EE:FF --engineering        # also the engineering key

A device that was not provisioned created a random secret on its first
start and showed its passwords once on the serial console; they cannot be
computed here.
"""
import argparse
import hashlib
import hmac
import pathlib
import re
import sys

ALPHABET = "23456789abcdefghjkmnpqrstuvwxyz"
assert len(ALPHABET) == 31


ROOT = pathlib.Path(__file__).resolve().parent.parent
MASTER_FILE = ROOT / "factory.secret"


def read_master(path) -> str:
    p = pathlib.Path(path)
    if not p.exists():
        sys.exit(f"{p} missing: put the manufacturer master secret there (one line, keep a backup)")
    master = p.read_text().strip()
    if len(master) < 16:
        sys.exit(f"{p}: the master secret must have at least 16 characters")
    return master


def device_secret(master: str, mac: bytes) -> str:
    return hmac.new(master.encode(), b"LGAL-DEVICE" + mac, hashlib.sha256).hexdigest()


def parse_mac(text: str) -> bytes:
    hexstr = re.sub(r"[^0-9a-fA-F]", "", text)
    if len(hexstr) != 12:
        sys.exit("MAC must have 6 bytes")
    return bytes.fromhex(hexstr)


def secret_from_args(args, mac: bytes) -> str:
    if args.secret is not None:
        return args.secret
    return device_secret(read_master(args.master_file), mac)


def derive(mac: bytes, secret: str, label: bytes = b"LGAL-AP", chars: int = 12) -> str:
    digest = hmac.new(secret.encode(), label + mac, hashlib.sha256).digest()
    return "".join(ALPHABET[b % 31] for b in digest[:chars])


def print_label(mac: bytes, secret: str, engineering: bool) -> None:
    print(f"MAC        : {mac.hex(':').upper()}")
    print(f"Setup Wi-Fi: LG-Aquatic-Life-Setup-{mac[4]:02X}{mac[5]:02X}")
    print(f"Password   : {derive(mac, secret)}  (setup Wi-Fi, first login, password recovery)")
    print(f"Hostname   : lgal-doser-{mac[4]:02x}{mac[5]:02x}.local")
    if engineering:
        print(f"Engineering: {derive(mac, secret, b'LGAL-ENG', 16)}  (manufacturer only, not on the label)")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mac", help="base MAC, e.g. AA:BB:CC:DD:EE:FF")
    ap.add_argument("--master-file", default=str(MASTER_FILE), help="manufacturer master (default: factory.secret)")
    ap.add_argument("--secret", help="the device secret itself (64 hex digits) instead of the master")
    ap.add_argument("--engineering", action="store_true", help="also print the engineering key")
    args = ap.parse_args()
    mac = parse_mac(args.mac)
    print_label(mac, secret_from_args(args, mac), args.engineering)


if __name__ == "__main__":
    main()
