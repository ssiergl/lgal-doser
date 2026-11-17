<!-- SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life -->
<!-- SPDX-License-Identifier: CC-BY-NC-SA-4.0 -->

# Hardware

Design files of the LGAL Doser, one folder per board ID. The board ID is the one the firmware
uses: the release files are called `lgal_doser-<board ID>-<module>.bin`. Board versions with the
same pin assignment share the board ID and the firmware build.

| Board ID | Device | Pumps | Board | Status |
|---|---|---|---|---|
| [d5v1](d5v1/) | LGAL Doser 5 | 5 | v1.0 (11/2026), v1.1 | v1.0 built, v1.1 designed. KiCad projects, bills of materials, schematic PDFs, Gerber and drill files and enclosures of both are published |

Each board ID folder holds:

| Folder | Content |
|---|---|
| `pcb/` | schematic (KiCad and PDF), PCB layout, Gerber and drill data, bill of materials |
| `enclosure/` | enclosure and mechanical parts: OpenSCAD source and STL files |

Both hold one subfolder per board version (`v1.0/`, `v1.1/`, …).

A changed pin assignment gets a new board ID and folder (e.g. `d5v2`). If it keeps the
enclosure of an earlier version, its README points there instead of copying the files.

Pin assignment and fixed circuit: [CLAUDE.md](../CLAUDE.md). Assembly and parts list:
[build guide](../docs/build-guide.md).

**License:** all files in this folder are licensed under CC BY-NC-SA 4.0, see
[LICENSE-hardware.md](../LICENSE-hardware.md). Commercial use (e.g. selling boards, kits,
enclosures or finished devices) only with written permission.
