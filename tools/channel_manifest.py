#!/usr/bin/env python3
"""Writes the update manifest of the beta channel from the manifest of one
release.

GitHub's releases/latest skips pre-releases, so the beta channel has its own
fixed address: the asset manifest.json of the pre-release "channel-beta"
(.github/workflows/release.yml keeps it up to date). It always names the
newest version, release candidate or release, and its file addresses point
into the release of that version (absolute, so no file is uploaded twice).

Usage: channel_manifest.py <release manifest> <release download address> -o OUT [--current FILE]
       (address e.g. https://github.com/<owner>/<repo>/releases/download/v0.2.0-rc.1)

With --current (the manifest the channel has now) OUT is only written when
the release is newer than it, so an older hotfix release does not push the
channel back. Exit code 0 also when nothing is written; the script says
which.

Versions compare as on the device (components/lgal_core/src/ota_manifest.cpp):
Semantic Versioning, a pre-release is older than its release.
"""
import argparse
import json
import os
import re
import sys

MAX_MANIFEST = 4096  # lgal::ota::kMaxManifest
VERSION = re.compile(r"^v?(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$")


def parse(v):
    m = VERSION.match(v or "")
    if not m:
        sys.exit(f"invalid version: {v!r}")
    return (int(m[1]), int(m[2]), int(m[3])), m[4] or ""


def compare_part(a, b):
    na, nb = a.isdigit(), b.isdigit()
    if na and nb:
        return (int(a) > int(b)) - (int(a) < int(b))
    if na != nb:
        return -1 if na else 1  # numeric parts sort before words
    return (a > b) - (a < b)


def compare(a, b):
    (ca, pa), (cb, pb) = parse(a), parse(b)
    if ca != cb:
        return -1 if ca < cb else 1
    if not pa or not pb:
        return 0 if pa == pb else (1 if not pa else -1)
    xa, xb = pa.split("."), pb.split(".")
    for x, y in zip(xa, xb):
        if c := compare_part(x, y):
            return c
    return (len(xa) > len(xb)) - (len(xa) < len(xb))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("manifest")
    ap.add_argument("base")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--current", default="")
    a = ap.parse_args()
    if not a.base.startswith("https://"):
        sys.exit("the release download address must be https")
    doc = json.load(open(a.manifest))
    if a.current and os.path.exists(a.current):
        have = json.load(open(a.current)).get("version", "")
        if have and compare(doc["version"], have) <= 0:
            print(f"channel keeps {have} ({doc['version']} is not newer)")
            return
    for b in doc["builds"]:
        if not b["url"].startswith(("https://", "http://")):
            b["url"] = a.base.rstrip("/") + "/" + b["url"]
    text = json.dumps(doc, separators=(",", ":")) + "\n"
    if len(text) > MAX_MANIFEST:
        sys.exit(f"manifest has {len(text)} bytes, the device reads at most {MAX_MANIFEST}")
    os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
    with open(a.out, "w") as f:
        f.write(text)
    print(f"channel now {doc['version']}")


if __name__ == "__main__":
    main()
