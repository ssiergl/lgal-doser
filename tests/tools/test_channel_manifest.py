# SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
# SPDX-License-Identifier: GPL-3.0-or-later

# The beta channel must pick versions the way the device compares them
# (tests/host/test_ota_manifest.cpp): otherwise the channel and the device disagree on "newer".
import json
import subprocess
import sys

import pytest

import channel_manifest
from conftest import ROOT

TOOL = ROOT / "tools" / "channel_manifest.py"
BASE = "https://github.com/o/r/releases/download/v0.2.0"


@pytest.mark.parametrize("a,b,want", [
    ("0.1.0", "0.1.0", 0), ("v0.1.0", "0.1.0+other", 0), ("0.1.0", "0.2.0", -1),
    ("0.10.0", "0.9.9", 1), ("1.0.0", "0.99.99", 1), ("0.1.1", "0.1.0", 1),
    ("1.0.0-rc.1", "1.0.0", -1), ("1.0.0", "1.0.0-rc.1", 1), ("1.0.0-rc.2", "1.0.0-rc.10", -1),
    ("1.0.0-alpha", "1.0.0-alpha.1", -1), ("1.0.0-alpha.1", "1.0.0-beta", -1),
    ("1.0.0-1", "1.0.0-alpha", -1), ("1.0.0-rc.01", "1.0.0-rc.1", 0), ("0.9.0", "1.0.0-rc.1", -1),
])
def test_compare_like_the_device(a, b, want):
    assert channel_manifest.compare(a, b) == want


def manifest(path, version):
    path.write_text(json.dumps({"version": version, "builds": [
        {"board": "d5v1", "variant": "N16R8", "url": "lgal_doser-d5v1-N16R8.bin", "sha256": "0" * 64, "size": 1}]}))
    return path


def run(tmp_path, version, current=None, base=BASE):
    args = [sys.executable, str(TOOL), str(manifest(tmp_path / "release.json", version)), base,
            "-o", str(tmp_path / "out" / "manifest.json")]
    if current:
        args += ["--current", str(manifest(tmp_path / "current.json", current))]
    return subprocess.run(args, capture_output=True, text=True)


def test_newer_version_is_written_with_absolute_urls(tmp_path):
    r = run(tmp_path, "0.2.0", current="0.2.0-rc.3")
    assert r.returncode == 0, r.stderr
    doc = json.loads((tmp_path / "out" / "manifest.json").read_text())
    assert doc["version"] == "0.2.0"
    assert doc["builds"][0]["url"] == BASE + "/lgal_doser-d5v1-N16R8.bin"


@pytest.mark.parametrize("version,current", [("0.1.9", "0.2.0-rc.1"), ("0.2.0-rc.1", "0.2.0-rc.1")])
def test_older_or_same_version_keeps_the_channel(tmp_path, version, current):
    r = run(tmp_path, version, current=current)
    assert r.returncode == 0
    assert "keeps" in r.stdout
    assert not (tmp_path / "out" / "manifest.json").exists()


def test_without_current_manifest_writes(tmp_path):
    assert run(tmp_path, "0.1.0-rc.1").returncode == 0
    assert (tmp_path / "out" / "manifest.json").exists()


def test_base_must_be_https(tmp_path):
    assert run(tmp_path, "0.2.0", base="http://example.com/x").returncode != 0
