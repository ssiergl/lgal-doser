# Board d5v1 v1.1 – revision plan

Agreed with the user on 2026-11-15. Board v1.0 (11/2026) stays frozen as ordered: files in this folder
(they move to `v1.0/` when v1.1 starts); it has no git tag of its own.

## Goal

A complete re-layout of the board, **as small as possible and above all with as little depth as
possible** (v1.0: 224 × 75 mm, pump connectors in a row along the 224 mm side). Board ID stays
`d5v1`, version v1.1, same firmware build as v1.0.

## Circuit changes (schematic first, then "Update PCB from Schematic")

| # | Change | Root `CLAUDE.md` / firmware impact |
|---|---|---|
| 1 | Route the driver UART RX lines on the board: GPIO 4 → bus A (`UART_A`, driver side of R3), GPIO 5 → bus B (`UART_B`, driver side of R4). Replaces the rework wires of v1.0. | Pin table unchanged; drop the "rework" note for v1.1. No firmware change. |
| 2 | Remove J11–J15 and their nets (`LEVEL1`–`LEVEL5`). GPIO 7, 15–18 become free. | Pin table and section 4: J11–J15 removed, GPIO 7, 15–18 "free". No firmware change (firmware never used them). |
| 3 | RTC no longer sits on the board: replace socket J10 by its own JST-XH 4-pin cable connector next to J16. I²C pull-ups R13/R14 stay on the board. Pinout to be decided (match J16 or the DS3231 module). | Section 4 (RTC by cable). No firmware change. |
| 4 | Keep the I²C extension J16 for a later display. | none |
| 5 | Net names: `STEPn` = pump n (v1.0 has them reversed: `STEP1` drives pump 5); J11–J15 nets gone. | none (names only) |
| 6 | Supply review: ESP 3V3 stays separate from the board +3V3 (as on v1.0); check the USB-only back-feed through the ESP I/O pins and USB 5 V vs. `5V_ESP`/D3. Results are reported before anything is changed. | possibly section 1 (supply) |
| 7 | 24 V sense on GPIO 6: +24 V – 100 kΩ (R15) – GPIO 6 – 10 kΩ (R16) – GND, 100 nF (C16). Added 2026-11-16 after the supply review. | Pin table GPIO 6, section 4. **Firmware impact:** new ADC input on v1.1 only; the firmware needs to know the board version (v1.0: GPIO 6 floats). |
| 8 | Tried 2026-11-16 and dropped again the same day: a separate I/O board (barrel jack, input protection, USR-ES1, button). Everything stays on one board; the schematic is the one of step 2. | none |

**GPIO changes:** allowed if they make the layout clearly smaller, but each one changes the
firmware pin map. Then the board can no longer share the firmware build with v1.0 and needs a
new board ID (see `hardware/CLAUDE.md`). Every proposed GPIO change is put to the user first.

## Layout

Third concept, agreed 2026-11-16 together with the new enclosure (replaces the two-row layout
of commit 8f85c67a and the upright main board with a separate I/O board):

- Enclosure about 282 × 89 × 65 mm (depth without the pump heads). The pumps sit low, their tube
  nipples (22 mm, pointing down) overhang the bottom by about 8 mm like on market dosers.
- **One board, upright 6 mm behind the motors, components facing the back wall**, about
  205 × 50 mm. The gap leaves room for the solder joints and the motor cables.
  The motor cables (single wires) run over the top edge of the board to the pump plugs J2–J6 on
  the component side at the top edge. Pump 1 is on the left seen from the front, so in the
  KiCad top view (= view from the back) it is on the right.
- **U shape:** barrel jack J1 and the RJ45 of the USR-ES1 sit in a notch in the middle of the
  bottom edge and point down into a niche in the back of the enclosure, centred behind pump 3,
  open to the back and to the bottom (like the slot of v1.0).
- The board slides in from the top into guide slots at its left and right edge (no screws)
  and stands on blocks 4 mm above the floor; 2.5 mm along both side edges stay free of parts
  and tracks. The lid holds it down.
- The ESP32-S3 dev kit sits fully on the board (no overhang in a guide slot), no copper under the
  antenna.
- The button faces the back wall and is pressed with a paper clip through a hole in the back
  wall. The lid has no hole and stays stackable.
- The DS3231 RTC module stands in rails at the left side wall (seen from the front), cable
  to J10.
- Status light in the front panel under pump 3.
- Silkscreen in English, no "FÜLLSTAND", no "5CH". Clean DRC with schematic parity.

## Open points

- ~~Target depth (and maximum length).~~ 205 × 50 mm (U shape), see step 6.
- ~~Pinout of the RTC cable connector.~~ Same as J16: 3.3 V, SDA, SCL, GND.
- ~~New enclosure for v1.1 in `d5v1/enclosure/`.~~ See step 7.

## Steps

1. ~~Move v1.0 to `pcb/v1.0/`, copy it to `pcb/v1.1/` as the starting point.~~ Done.
2. ~~Schematic changes 1–5, ERC, pin-table check, root `CLAUDE.md`.~~ Done 2026-11-16: the
   netlist differs from v1.0 only by these changes; ERC as v1.0 (one harmless warning).
   Schematic values are in English. Supply review (6): no other circuit change needed; the
   +24 V sense input (7) was added on the user's request.
   The PCB of v1.1 is still the v1.0 layout and is updated from the schematic in step 3.
3. ~~Placement study with depth/length options for the user.~~ Agreed 2026-11-16: two rows,
   through-hole only, pump plugs on the front edge, 24 V and Ethernet on the rear edge.
   Placed 2026-11-16: **166.5 × 54 mm** (v1.0: 224 × 75 mm; depth −28 %, area −46 %).
   - Front row, pump 1 on the left (top view = view from the front): driver rotated with its
     motor pins to the front, 100 µF beside it, 100 nF and pump plug in front.
   - Rear row: ESP32-S3 dev kit (antenna overhangs the left edge by 2.3 mm), USR-ES1 with the
     RJ45 at the rear edge, 24 V jack at the rear edge, supply, LED header J9, J10 (RTC), J16.
   - Small parts (UART and pull-up resistors, LED drivers, sense divider) sit under the ESP dev
     kit between its socket rows; the ESP footprint courtyard covers only the socket strips and
     the antenna end. Height limit there: 8 mm (kit PCB about 9.5 mm above the board).
   - Five M3 mounting holes: three in the front row, two in the rear row.
   Confirmed by the user 2026-11-16: height under the ESP kit, vertical pump plugs, button and
   mounting holes as placed.
4. ~~Routing, silkscreen, DRC.~~ Done 2026-11-16, autorouted with Freerouting 2.5:
   - Layers: F.Cu and B.Cu signals with GND fill, In1.Cu solid GND, In2.Cu +3V3 plane. All parts
     are through-hole, so every GND and +3V3 pin connects to its plane directly.
   - Net classes: +24V / VIN_* 1.5 mm, motor lines and +5V / 5V_ESP 0.8 mm, signals 0.3 mm,
     vias 0.7 / 0.35 mm.
   - No copper fill under the ESP antenna (x < 104.6 mm on all layers); the only tracks there are
     two short stubs at the antenna-end pins of the kit (GND, 3V3).
   - Mounting holes: `MountingHole_3.2mm_M3`. Silkscreen: pump numbers 1–5 next to the plugs,
     connector names, "LGAL Doser 5 / d5v1 v1.1" (under the ESP kit and on the bottom side),
     "PUMP 1" … "PUMP 5" on the bottom side.
   - DRC: 0 errors, 0 unconnected. Remaining warnings: the SW1 footprint outline crosses its own
     pads (as in v1.0), the ESP kit outline overhangs the edge (antenna, intended), and the
     schematic parity reports the TMC2209 symbol pins 17/18 that have no pad (as in v1.0).
5. ~~BOM v1.1 (renumber the items).~~ Done 2026-11-16.
6. ~~U-shaped upright layout of the one board.~~ Done 2026-11-16, **205 × 50 mm**, notch
   40 × 23 mm, 4 layers, autorouted with Freerouting 2.5 (same layer stack and net classes as
   step 4). Top view = view from the back (the components face the back wall), so pump 1 is on
   the right:
   - Top row: pump plugs J2–J6 at the top edge with their 100 nF beside them, below them the
     five drivers with their 100 µF to the left (pitch 32.5 mm); pumps 5 and 4 left of the
     notch, pumps 3, 2 and 1 right of it.
   - Above the notch: USR-ES1 on J7/J8 (RJ45 1.5 mm into the notch) and the barrel jack J1
     (opening 1.5 mm into the notch), R11 above J1. The notch is centred behind pump 3.
   - Bottom left: ESP32-S3 dev kit, fully on the board, antenna at the left edge (no copper fill
     and no tracks under it except the two short stubs at its end pins), the small parts between
     its socket rows as in step 3.
   - Bottom right: input protection (F1, Q1, R1, D1, D2, C1) and R15 next to C1, regulators
     U2/U3 with C2–C4, D3, I²C pull-ups, J9 (LED), J10 (RTC), J16 (I²C), SW1 with C15.
   - No mounting holes; 2.5 mm along both side edges are free of parts and tracks for the
     guide slots of the enclosure.
   - DRC: 0 errors, 0 unconnected. Remaining warnings: the barrel jack outline reaches into the
     notch (intended), the SW1 footprint outline crosses its own pads and the schematic parity
     reports the TMC2209 symbol pins 17/18 without a pad (both as in v1.0).
7. ~~Enclosure for the upright board.~~ Designed 2026-11-16 as OpenSCAD source
   [`enclosure_v1.1.scad`](../../enclosure/enclosure_v1.1.scad), about 282 × 89 × 65 mm: guide
   slots and lid tabs instead of screws, niche behind pump 3, RTC module in rails at the left
   side wall, DIN clips symmetric. Renders cleanly; no collisions with the motors, the board and
   its parts (also while sliding in) and the RTC module. Not yet printed, see the
   [enclosure README](../../enclosure/README.md).
8. ~~Gerber and drill files.~~ Exported 2026-11-16 to [`gerber/`](gerber/) (4 copper layers, mask,
   silkscreen with soldermask subtracted, outline, PTH/NPTH drill files with maps), stackup
   1.6 mm. Only through-hole parts, so no paste layers and no placement file.
