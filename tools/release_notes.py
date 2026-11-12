#!/usr/bin/env python3
"""Writes the notes of a GitHub release (Markdown) to stdout:

  1. the section of CHANGELOG.md for the version: the summary in bullet
     points. A release candidate (0.2.0-rc.1) uses the section of the
     version it leads to (0.2.0); a heading matches when it names the
     version, e.g. "## 0.2.0 – 2026-12-01" or "## Unreleased – 0.2.0".
  2. every commit since the previous version tag, folded away under
     "All changes", with a link to the full comparison on GitHub.

Usage: release_notes.py <tag> [--repo owner/name]   (e.g. v0.2.0-rc.1)

Needs the git history with tags (actions/checkout with fetch-depth: 0).
"""
import argparse
import re
import subprocess
import sys

VERSION = re.compile(r"^v?(\d+\.\d+\.\d+)(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$")


def git(*args):
    r = subprocess.run(["git", *args], capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


def changelog_section(base):
    try:
        lines = open("CHANGELOG.md", encoding="utf-8").read().splitlines()
    except OSError:
        return ""
    want = re.compile(r"^## .*(?<![\d.])" + re.escape(base) + r"(?![\d.])")
    out, inside = [], False
    for line in lines:
        if line.startswith("## "):
            if inside:
                break
            inside = bool(want.match(line))
            continue
        if inside:
            out.append(line)
    return "\n".join(out).strip()


def unwrap(text):
    """Joins wrapped lines: GitHub shows every line break of a release text."""
    out = []
    for line in text.splitlines():
        block = re.match(r"^\s*([-*+]\s|\d+\.\s|#|\||```|<|>)", line)
        if line.strip() and out and out[-1].strip() and not block:
            out[-1] += " " + line.strip()
        else:
            out.append(line)
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tag")
    ap.add_argument("--repo", default="")
    a = ap.parse_args()
    m = VERSION.match(a.tag)
    if not m:
        sys.exit(f"invalid tag: {a.tag}")
    base = m[1]
    parts = []
    if a.tag.lstrip("v") != base:
        parts.append(f"**Release candidate of {base}** – for testing; devices on the beta channel are offered it.\n")
    section = changelog_section(base)
    parts.append(unwrap(section) if section else f"No changelog entry for {base} yet.")

    prev = git("describe", "--tags", "--abbrev=0", "--match", "v[0-9]*", f"{a.tag}^")
    if prev:
        log = git("log", "--no-merges", "--format=- %s (%h)", f"{prev}..{a.tag}")
        count = len(log.splitlines()) if log else 0
        parts.append(f"\n<details><summary>All changes since {prev} ({count} commits)</summary>\n\n{log}\n\n</details>")
        if a.repo:
            parts.append(f"\n**Full comparison:** https://github.com/{a.repo}/compare/{prev}...{a.tag}")
    else:
        parts.append("\nFirst release: the summary above covers every change.")
    print("\n".join(parts))


if __name__ == "__main__":
    main()
