# SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
# SPDX-License-Identifier: GPL-3.0-or-later

# The manifest is what devices read before an update: version, one entry per build, SHA-256 and
# size of each image, at most lgal::ota::kMaxManifest bytes.
import hashlib
import json
import subprocess
import sys

import pytest

from conftest import ROOT

TOOL = ROOT / "tools" / "make_manifest.py"


def run(dist, *args):
    return subprocess.run([sys.executable, str(TOOL), str(dist), *args], capture_output=True, text=True)


@pytest.fixture
def dist(tmp_path):
    for v in ("N8R8", "N16R8"):
        (tmp_path / f"lgal_doser-d5v1-{v}.bin").write_bytes(v.encode() * 100)
    return tmp_path


def test_manifest(dist):
    r = run(dist, "d5v1-N8R8", "d5v1-N16R8", "--version", "0.2.0-rc.1",
            "--notes", "https://example.com/notes")
    assert r.returncode == 0, r.stderr
    doc = json.loads((dist / "manifest.json").read_text())
    assert doc["version"] == "0.2.0-rc.1"
    assert doc["notes"] == "https://example.com/notes"
    assert [(b["board"], b["variant"]) for b in doc["builds"]] == [("d5v1", "N8R8"), ("d5v1", "N16R8")]
    for b in doc["builds"]:
        data = (dist / b["url"]).read_bytes()
        assert b["url"] == f"lgal_doser-d5v1-{b['variant']}.bin"  # relative: works from any release address
        assert b["sha256"] == hashlib.sha256(data).hexdigest()
        assert b["size"] == len(data)


@pytest.mark.parametrize("args", [
    ["d5v1-N16R8", "--version", "0.2"],                                  # not a version
    ["d5v1_N16R8", "--version", "0.2.0"],                                # not BOARD-VARIANT
    ["d5v1-N16R8", "--version", "0.2.0", "--notes", "ftp://example.com"],  # notes not http(s)
])
def test_rejects(dist, args):
    assert run(dist, *args).returncode != 0
    assert not (dist / "manifest.json").exists()


def test_rejects_missing_image(dist):
    assert run(dist, "d5v1-N32R8", "--version", "0.2.0").returncode != 0


def test_rejects_manifest_the_device_cannot_read(tmp_path):
    builds = [f"b{i}-N16R8" for i in range(40)]
    for b in builds:
        (tmp_path / f"lgal_doser-{b}.bin").write_bytes(b"x")
    r = run(tmp_path, *builds, "--version", "0.2.0")
    assert r.returncode != 0
    assert "at most 4096" in r.stderr
