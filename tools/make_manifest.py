#!/usr/bin/env python3
"""Writes the update manifest (manifest.json) for the release files in a
directory, in the format components/lgal_core/include/lgal/ota_manifest.h
reads:

  {"version":"0.2.0","notes":"https://…/releases/tag/v0.2.0",
   "builds":[{"board":"d5v1","variant":"N16R8","url":"lgal_doser-d5v1-N16R8.bin",
              "sha256":"…","size":…}]}

Usage: make_manifest.py <dist dir> --version 0.2.0 [--notes URL] BOARD-VARIANT...
       (e.g. d5v1-N16R8 for dist/lgal_doser-d5v1-N16R8.bin)

The file names are relative, so the manifest works from any release
address (e.g. GitHub's releases/latest/download/manifest.json).
"""
import argparse
import hashlib
import json
import os
import re
import sys

PROJECT = "lgal_doser"
MAX_MANIFEST = 4096  # lgal::ota::kMaxManifest
VERSION = re.compile(r"^v?\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$")
BUILD = re.compile(r"^([a-z0-9]{1,15})-(N\d+R\d+)$")  # board (lgal/ota_image.h) - module variant


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dist")
    ap.add_argument("builds", nargs="+", metavar="BOARD-VARIANT")
    ap.add_argument("--version", required=True)
    ap.add_argument("--notes", default="")
    a = ap.parse_args()
    if not VERSION.match(a.version):
        sys.exit(f"invalid version: {a.version}")
    if a.notes and not a.notes.startswith(("https://", "http://")):
        sys.exit("--notes must be an http(s) address")
    builds = []
    for b in a.builds:
        m = BUILD.match(b)
        if not m:
            sys.exit(f"invalid build {b!r}, expected BOARD-VARIANT such as d5v1-N16R8")
        name = f"{PROJECT}-{b}.bin"
        data = open(os.path.join(a.dist, name), "rb").read()
        builds.append({"board": m[1], "variant": m[2], "url": name,
                       "sha256": hashlib.sha256(data).hexdigest(), "size": len(data)})
    doc = {"version": a.version}
    if a.notes:
        doc["notes"] = a.notes
    doc["builds"] = builds
    text = json.dumps(doc, separators=(",", ":")) + "\n"
    if len(text) > MAX_MANIFEST:
        sys.exit(f"manifest has {len(text)} bytes, the device reads at most {MAX_MANIFEST}")
    with open(os.path.join(a.dist, "manifest.json"), "w") as f:
        f.write(text)
    print(text, end="")


if __name__ == "__main__":
    main()
