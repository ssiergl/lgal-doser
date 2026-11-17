#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
# SPDX-License-Identifier: GPL-3.0-or-later

"""Fails when the app image of a build fills its OTA slot beyond a limit.

An image that no longer fits its slot cannot be installed over the air, and the partition
table cannot be changed by OTA (see the partition tables). So the limit warns early:

    python3 tests/size/check_image_size.py build-d5v1-N8R8 [--limit 85]

The slot size comes from the partition table the build used (sdkconfig).
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def slot_size(build):
    sdk = open(os.path.join(build, "sdkconfig"), encoding="utf-8").read()
    table = re.search(r'^CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="([^"]+)"', sdk, re.M).group(1)
    for line in open(os.path.join(ROOT, table), encoding="utf-8"):
        cols = [c.strip() for c in line.split("#")[0].split(",")]
        if len(cols) >= 5 and cols[0] == "ota_0":
            return table, int(cols[4], 0)
    sys.exit(f"{table}: no ota_0 partition")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build")
    ap.add_argument("--limit", type=float, default=85, help="percent of the OTA slot (default 85)")
    a = ap.parse_args()
    table, slot = slot_size(a.build)
    image = os.path.getsize(os.path.join(a.build, "lgal_doser.bin"))
    used = 100 * image / slot
    print(f"{a.build}: {image} of {slot} bytes ({table}, ota_0) = {used:.1f} %, limit {a.limit:g} %")
    sys.exit(1 if used > a.limit else 0)


if __name__ == "__main__":
    main()
