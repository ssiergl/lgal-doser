# Circuit board

This folder holds the manufacturing and design data of the LGAL Doser circuit board v1.0
(11/2026):

| File | Purpose |
|---|---|
| schematic (PDF) | for reading and following the circuit |
| project source files | schematic and layout for editing |
| `gerber/` | Gerber and drill data for PCB manufacturing |
| [`bom.csv`](bom.csv) | bill of materials: all parts with reference, value, footprint and example part number |
| placement data (CSV) | position and rotation for assembly |

**Status:** designed; the bill of materials is published, the other files follow after the
hardware test. The quantities in `bom.csv` are for one device; plug-in modules, pumps, power
supply and other device parts are listed in the [build guide](../../docs/build-guide.md#1-parts-list).

The ESP32-S3 pin assignment and the circuit (UART addresses of the drivers, pull-ups, LED
drivers, strapping pins) are fixed and described in [CLAUDE.md](../../CLAUDE.md); the firmware
follows them.

License: CC BY-NC-SA 4.0, see [LICENSE-hardware.md](../../LICENSE-hardware.md).
