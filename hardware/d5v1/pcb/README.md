# d5v1 – circuit board

Circuit boards of the LGAL Doser 5 (board ID `d5v1`). All versions share the same ESP32-S3 pin
assignment and run the same firmware build.

| Version | Folder | Status |
|---|---|---|
| v1.0 (11/2026) | [`v1.0/`](v1.0/) | built; needs two rework wires, see [below](#rework-rx-lines-of-the-driver-uart) |
| v1.1 | [`v1.1/`](v1.1/) | [Gerber and drill files](v1.1/gerber/), see the [revision plan](v1.1/plan.md) |

Each version folder holds:

| File | Purpose |
|---|---|
| `d5v1.kicad_pro`, `d5v1.kicad_sch`, `d5v1.kicad_pcb` | KiCad 10 project: schematic and layout for editing |
| `lgal_doser.kicad_sym`, `lgal_doser.pretty/` | project symbol and footprint library (ESP32-S3 socket, TMC2209 StepStick socket, Multimec 3F button), registered with project-relative paths in `sym-lib-table` and `fp-lib-table` |
| `bom.csv` | bill of materials: all parts with reference, value, footprint and example part number |
| `d5v1-schematic.pdf` | schematic for reading without KiCad |
| `gerber/` | Gerber and drill files for the board maker |

**Status v1.0:** the KiCad project (4 layers), the [bill of materials](v1.0/bom.csv), the
[schematic PDF](v1.0/d5v1-schematic.pdf) and the [Gerber and drill files](v1.0/gerber/) are
published (renamed from the old project name to `d5v1-*`; the drawing matches the layout in
`v1.0/`, plotted with "subtract soldermask from silkscreen"). Manufacturing options for both
versions: 4 layers, 1.6 mm, no specified stackup, 1 oz outer and 0.5 oz inner copper, HASL. The quantities in `bom.csv` are for one device; plug-in modules, pumps, power
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
the firmware gets no replies from the drivers and the pumps do not run. Board v1.1 routes the RX
lines on the board. The board ID stays `d5v1`.

License: CC BY-NC-SA 4.0, see [LICENSE-hardware.md](../../../LICENSE-hardware.md).
