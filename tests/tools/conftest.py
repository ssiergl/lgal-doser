# SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
# SPDX-License-Identifier: GPL-3.0-or-later

# Tests of the Python tools in tools/ (pytest):  python3 -m pytest tests/tools
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
