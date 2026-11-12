#!/usr/bin/env python3
"""Writes the device secret into the factory partition "fctry" of a freshly
flashed LGAL device and prints the label values.

The secret is HMAC-SHA256(master, b"LGAL-DEVICE" + base MAC) with the
manufacturer master from factory.secret (repository root, not in git,
never in the firmware), so tools/ap_password.py can compute the passwords
again later from the MAC alone. The firmware reads the secret at every start;
neither the factory reset nor an OTA update touches the partition.

Optionally it also writes the factory pump population (--pumps): the pumps
fitted on delivery, again after every factory reset. Without it all pumps of
the board are fitted.

Run it after the firmware flash: a merged image (…-full.bin written at 0x0)
also overwrites "fctry", so provision again with the same values. Needs the
ESP-IDF environment (export.sh).

Usage:
    tools/provision.py -p /dev/cu.usbmodem1101
    tools/provision.py -p /dev/cu.usbmodem1101 --pumps 1,2,3
    tools/provision.py -p /dev/cu.usbmodem1101 --master-file /path/to/master
    tools/provision.py --mac AA:BB:CC:DD:EE:FF --dry-run   # only build and print
"""
import argparse
import csv
import os
import pathlib
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import ap_password  # noqa: E402

ROOT = ap_password.ROOT
PARTITION = "fctry"
NAMESPACE = "lgal"  # lgal::device, components/lgal_core/src/device.cpp
KEY = "secret"
KEY_PUMPS = "pumps"  # factory population, doser::hwconfig (components/doser_app/src/hwconfig.cpp)
MAX_PUMPS = 5        # doser::kMaxPumps


def parse_pumps(text: str) -> int:
    """'1,2,4' -> bit mask (bit i = pump i+1)."""
    mask = 0
    for part in text.split(","):
        part = part.strip()
        if not part.isdigit() or not 1 <= int(part) <= MAX_PUMPS:
            raise ValueError(f"pump numbers 1..{MAX_PUMPS}, separated by commas: {text!r}")
        mask |= 1 << (int(part) - 1)
    return mask


def pump_list(mask: int) -> str:
    return ", ".join(str(i + 1) for i in range(MAX_PUMPS) if mask >> i & 1)


def factory_partition():
    """Offset and size of "fctry", equal in all partition tables."""
    found = set()
    for table in sorted(ROOT.glob("partitions*.csv")):
        for row in csv.reader(line for line in table.read_text().splitlines() if not line.startswith("#")):
            if row and row[0].strip() == PARTITION:
                found.add((int(row[3], 0), int(row[4], 0)))
    if len(found) != 1:
        sys.exit(f"partition '{PARTITION}' is not the same in all partition tables: {found}")
    return found.pop()


def esptool(args, port, capture=False):
    cmd = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "-p", port] + args
    r = subprocess.run(cmd, check=False, text=True, capture_output=capture)
    if r.returncode != 0:
        sys.exit(f"esptool failed: {' '.join(args)}" + (f"\n{r.stdout}{r.stderr}" if capture else ""))
    return r.stdout if capture else ""


def read_mac(port) -> bytes:
    out = esptool(["read-mac"], port, capture=True)
    m = re.search(r"^BASE MAC:\s*([0-9a-fA-F:]{17})", out, re.M) or re.search(r"^MAC:\s*([0-9a-fA-F:]{17})", out, re.M)
    if not m:
        sys.exit("no MAC in the esptool output:\n" + out)
    return ap_password.parse_mac(m.group(1))


def nvs_image(secret: str, pumps: int, size: int, out: pathlib.Path) -> None:
    idf = os.environ.get("IDF_PATH")
    if not idf:
        sys.exit("IDF_PATH is not set (source export.sh first)")
    gen = pathlib.Path(idf) / "components" / "nvs_flash" / "nvs_partition_generator" / "nvs_partition_gen.py"
    src = out.with_suffix(".csv")
    rows = f"key,type,encoding,value\n{NAMESPACE},namespace,,\n{KEY},data,string,{secret}\n"
    if pumps:
        rows += f"{KEY_PUMPS},data,u8,{pumps}\n"
    src.write_text(rows)
    try:
        r = subprocess.run([sys.executable, str(gen), "generate", str(src), str(out), hex(size)],
                           check=False, text=True, capture_output=True)
    finally:
        src.unlink()
    if r.returncode != 0 or not out.exists():
        sys.exit("nvs_partition_gen failed:\n" + r.stdout + r.stderr)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--port", help="serial port of the device")
    ap.add_argument("--mac", help="base MAC instead of reading it from the device")
    ap.add_argument("--master-file", default=str(ap_password.MASTER_FILE),
                    help="manufacturer master (default: factory.secret)")
    ap.add_argument("--pumps", help="factory pump population, e.g. 1,2,3 (default: all pumps of the board)")
    ap.add_argument("--dry-run", action="store_true", help="build the partition image, write nothing")
    args = ap.parse_args()
    if not args.port and not (args.dry_run and args.mac):
        sys.exit("--port is needed (or --mac with --dry-run)")
    try:
        pumps = parse_pumps(args.pumps) if args.pumps else 0
    except ValueError as e:
        sys.exit(str(e))

    master = ap_password.read_master(args.master_file)
    offset, size = factory_partition()
    mac = ap_password.parse_mac(args.mac) if args.mac else read_mac(args.port)
    secret = ap_password.device_secret(master, mac)

    with tempfile.TemporaryDirectory() as tmp:
        image = pathlib.Path(tmp) / "fctry.bin"
        nvs_image(secret, pumps, size, image)
        if args.dry_run:
            print(f"Partition image ok ({image.stat().st_size} bytes for 0x{offset:X}), nothing written.")
        else:
            esptool(["write-flash", hex(offset), str(image)], args.port)
            print(f"Device secret written to '{PARTITION}' at 0x{offset:X}.")
    print(f"Pumps fitted from the factory: {pump_list(pumps) if pumps else 'all of the board'}")
    print()
    ap_password.print_label(mac, secret, engineering=True)


if __name__ == "__main__":
    main()
