# d5v1 – circuit board

This folder holds the manufacturing and design data of the LGAL Doser 5 circuit board v1.0
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
supply and other device parts are listed in the [build guide](../../../docs/build-guide.md#1-parts-list).

The ESP32-S3 pin assignment and the circuit (UART addresses of the drivers, pull-ups, LED
drivers, strapping pins) are fixed and described in [CLAUDE.md](../../../CLAUDE.md); the firmware
follows them.

## Rework: RX lines of the driver UART

Board v1.0 connects each driver bus to only one GPIO (38, 39) through its 1 kΩ series resistor
(R3, R4). On such a single-wire bus the pull-downs on PDN_UART of the driver modules pull the line
LOW while the ESP listens, which makes the replies of the drivers unreliable. The firmware
therefore uses the two-pin standard circuit: TX keeps the GPIO and the 1 kΩ, RX is a second GPIO
directly on the bus. On board v1.0 two wires are added for this:

| Bus | Pumps | TX (on the board, through 1 kΩ) | RX (wire, directly on the bus) |
|---|---|---|---|
| A | 1–4 | GPIO 38 | GPIO 4 |
| B | 5 | GPIO 39 | GPIO 5 |

Solder each wire, without power, from the GPIO pin of the ESP socket header to the driver side
of the series resistor of its bus, i.e. to the PDN_UART line of the drivers. Without these wires
the firmware gets no replies from the drivers and the pumps do not run. A later board revision
routes the RX lines on the board. The board ID stays `d5v1`.

License: CC BY-NC-SA 4.0, see [LICENSE-hardware.md](../../../LICENSE-hardware.md).
