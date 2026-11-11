#!/usr/bin/env python3
"""Packs the web UI (www/) into one blob that is embedded in the firmware.

Every file is gzip-compressed (deterministic: no name, mtime 0) and served
as-is with "Content-Encoding: gzip". Format (little endian):

  "LGWU" | u16 version=1 | u16 count
  count x { u8 path_len | path (without leading "/") | u8 mime_len | mime |
            u32 offset (from blob start) | u32 length | char etag[16] }
  data

Usage: pack.py <www dir> <output file> [<path>=<file> ...]

Each <path>=<file> adds a file from outside www/ under that path, e.g. the
license texts from the repository root as licenses/gpl-3.0.txt.
"""
import gzip
import hashlib
import io
import os
import struct
import sys

MIME = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".mjs": "text/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".svg": "image/svg+xml",
    ".json": "application/json",
    ".ico": "image/x-icon",
    ".png": "image/png",
    ".txt": "text/plain; charset=utf-8",
}


def collect(root):
    files = []
    for d, dirs, names in os.walk(root):
        dirs.sort()
        for n in sorted(names):
            ext = os.path.splitext(n)[1]
            if ext not in MIME:
                continue  # README, LICENSE: repository only
            full = os.path.join(d, n)
            files.append((os.path.relpath(full, root).replace(os.sep, "/"), MIME[ext], full))
    return files


def gz(data):
    buf = io.BytesIO()
    with gzip.GzipFile(filename="", mode="wb", fileobj=buf, compresslevel=9, mtime=0) as f:
        f.write(data)
    return buf.getvalue()


def main():
    root, out = sys.argv[1], sys.argv[2]
    files = collect(root)
    for extra in sys.argv[3:]:
        path, full = extra.split("=", 1)
        files.append((path, MIME[os.path.splitext(path)[1]], full))
    entries, blobs = [], []
    for path, mime, full in files:
        raw = open(full, "rb").read()
        etag = hashlib.sha256(raw).hexdigest()[:16].encode()
        entries.append((path.encode(), mime.encode(), etag))
        blobs.append(gz(raw))
    head = b"LGWU" + struct.pack("<HH", 1, len(files))
    table_len = sum(1 + len(p) + 1 + len(m) + 8 + 16 for p, m, _ in entries)
    offset = len(head) + table_len
    table = b""
    for (p, m, e), b in zip(entries, blobs):
        table += struct.pack("<B", len(p)) + p + struct.pack("<B", len(m)) + m + struct.pack("<II", offset, len(b)) + e
        offset += len(b)
    data = head + table + b"".join(blobs)
    tmp = out + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, out)
    raw_total = sum(os.path.getsize(f[2]) for f in files)
    print(f"webui: {len(files)} files, {raw_total} -> {len(data)} bytes")


if __name__ == "__main__":
    main()
