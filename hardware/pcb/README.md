# Circuit board

This folder holds the manufacturing and design data of the LGAL Doser circuit board v1.0
(11/2026):

| File | Purpose |
|---|---|
| schematic (PDF) | for reading and following the circuit |
| project source files | schematic and layout for editing |
| `gerber/` | Gerber and drill data for PCB manufacturing |
| bill of materials (CSV) | all parts with value, package and source |
| placement data (CSV) | position and rotation for assembly |

**Status:** designed, files follow after the hardware test.

The ESP32-S3 pin assignment and the circuit (UART addresses of the drivers, pull-ups, LED
drivers, strapping pins) are fixed and described in [CLAUDE.md](../../CLAUDE.md); the firmware
follows them.

License: CC BY-NC-SA 4.0, see [LICENSE-hardware.md](../../LICENSE-hardware.md).
