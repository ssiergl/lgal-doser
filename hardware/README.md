# Hardware

Design files of the LGAL Doser, one folder per version. The folder name is the board ID the
firmware uses: the release files of a version are called `lgal_doser-<board ID>-<module>.bin`.

| Board ID | Device | Pumps | Board | Status |
|---|---|---|---|---|
| [d5v1](d5v1/) | LGAL Doser 5 | 5 | v1.0 (11/2026) | designed, bill of materials published; other files follow after the hardware test |

Each version folder holds:

| Folder | Content |
|---|---|
| `pcb/` | schematic, PCB layout, Gerber and drill data, bill of materials, placement data |
| `enclosure/` | enclosure and mechanical parts: STL, STEP, 3MF and CAD source files |

A new board revision gets its own folder (e.g. `d5v2`). If it keeps the enclosure of an earlier
version, its README points there instead of copying the files.

Pin assignment and fixed circuit: [CLAUDE.md](../CLAUDE.md). Assembly and parts list:
[build guide](../docs/build-guide.md).

**License:** all files in this folder are licensed under CC BY-NC-SA 4.0, see
[LICENSE-hardware.md](../LICENSE-hardware.md). Commercial use (e.g. selling boards, kits,
enclosures or finished devices) only with written permission.
