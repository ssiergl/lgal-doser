# SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
# SPDX-License-Identifier: GPL-3.0-or-later

# The release text: changelog section of the version (a release candidate uses the section of
# its release) and the commits since the previous version tag (a release skips the candidates).
import subprocess
import sys

import pytest

import release_notes
from conftest import ROOT

TOOL = ROOT / "tools" / "release_notes.py"
CHANGELOG = """# Changelog

## Unreleased – 0.2.0

- New thing that is described
  over two lines.
- Second thing.

## 0.1.0 – 2026-11-20

- Old thing.
"""


@pytest.fixture
def repo(tmp_path, monkeypatch):
    def git(*a):
        subprocess.run(["git", *a], cwd=tmp_path, check=True, capture_output=True)
    git("init", "-q")
    git("config", "user.email", "test@example.com")
    git("config", "user.name", "Test")
    (tmp_path / "CHANGELOG.md").write_text(CHANGELOG, encoding="utf-8")
    git("add", ".")
    git("commit", "-q", "-m", "First")
    git("tag", "v0.1.0")
    for msg in ("Add the new thing", "Add the second thing"):
        git("commit", "-q", "--allow-empty", "-m", msg)
    git("tag", "v0.2.0-rc.1")
    monkeypatch.chdir(tmp_path)
    return tmp_path


def test_changelog_section_by_base_version(repo):
    s = release_notes.changelog_section("0.2.0")
    assert s.startswith("- New thing") and "Old thing" not in s
    assert release_notes.changelog_section("0.1.0") == "- Old thing."
    assert release_notes.changelog_section("0.1.1") == ""
    assert release_notes.changelog_section("1.0") == ""  # 1.0 must not match inside 0.1.0


def test_unwrap_joins_wrapped_lines_only():
    text = "- One\n  continued.\n- Two\n\nParagraph\nwrapped.\n| a | b |\n| c | d |"
    assert release_notes.unwrap(text) == "- One continued.\n- Two\n\nParagraph wrapped.\n| a | b |\n| c | d |"


def test_release_candidate_notes(repo):
    r = subprocess.run([sys.executable, str(TOOL), "v0.2.0-rc.1", "--repo", "o/r"],
                       capture_output=True, text=True, cwd=repo)
    assert r.returncode == 0, r.stderr
    out = r.stdout
    assert "Release candidate of 0.2.0" in out
    assert "- New thing that is described over two lines." in out
    assert "All changes since v0.1.0 (2 commits)" in out
    assert "- Add the second thing (" in out
    assert "https://github.com/o/r/compare/v0.1.0...v0.2.0-rc.1" in out


def test_unknown_tag_fails(repo):
    r = subprocess.run([sys.executable, str(TOOL), "v9.9.9"], capture_output=True, text=True, cwd=repo)
    assert r.returncode != 0


def test_release_skips_its_candidates(repo):
    subprocess.run(["git", "commit", "-q", "--allow-empty", "-m", "Set the version to 0.2.0"], cwd=repo, check=True)
    subprocess.run(["git", "tag", "v0.2.0"], cwd=repo, check=True)
    r = subprocess.run([sys.executable, str(TOOL), "v0.2.0", "--repo", "o/r"],
                       capture_output=True, text=True, cwd=repo)
    assert r.returncode == 0, r.stderr
    assert "Release candidate" not in r.stdout
    assert "All changes since v0.1.0 (3 commits)" in r.stdout
    assert "https://github.com/o/r/compare/v0.1.0...v0.2.0" in r.stdout


def test_first_release_after_candidates(tmp_path):
    def git(*a):
        subprocess.run(["git", *a], cwd=tmp_path, check=True, capture_output=True)
    git("init", "-q")
    git("config", "user.email", "test@example.com")
    git("config", "user.name", "Test")
    (tmp_path / "CHANGELOG.md").write_text(CHANGELOG, encoding="utf-8")
    git("add", ".")
    git("commit", "-q", "-m", "First")
    git("tag", "v0.1.0-rc.1")
    git("commit", "-q", "--allow-empty", "-m", "Set the version to 0.1.0")
    git("tag", "v0.1.0")
    r = subprocess.run([sys.executable, str(TOOL), "v0.1.0"], capture_output=True, text=True, cwd=tmp_path)
    assert r.returncode == 0, r.stderr
    assert "First release: the summary above covers every change." in r.stdout
    assert "All changes" not in r.stdout
