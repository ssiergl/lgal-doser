<!-- SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life -->
<!-- SPDX-License-Identifier: CC-BY-NC-SA-4.0 -->

# d5v1 – LGAL Doser 5

Five pumps, circuit boards v1.0 (11/2026, built) and v1.1; both have the
same pin assignment and run the same firmware. Firmware board `d5v1`, release files
`lgal_doser-d5v1-<module>.bin` (module variants N8R8, N16R8, N32R8). Fewer than five pumps can
be fitted on this board, see the [build guide](../../docs/build-guide.md#5-flashing-and-provisioning-the-firmware)
(`provision.py --pumps`). Board v1.1 is provisioned with `provision.py --board-rev 1.1`.

| Folder | Content |
|---|---|
| [pcb/](pcb/) | circuit boards: KiCad projects, bills of materials, schematic PDFs, Gerber and drill files, one folder per board version |
| [enclosure/](enclosure/) | enclosure: OpenSCAD source and STL files, one folder per board version |

Pin assignment and fixed circuit: [CLAUDE.md](../../CLAUDE.md). Assembly, modules and device
parts: [build guide](../../docs/build-guide.md).

License: CC BY-NC-SA 4.0, see [LICENSE-hardware.md](../../LICENSE-hardware.md).
