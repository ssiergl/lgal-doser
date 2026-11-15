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

**GPIO changes:** allowed if they make the layout clearly smaller, but each one changes the
firmware pin map. Then the board can no longer share the firmware build with v1.0 and needs a
new board ID (see `hardware/CLAUDE.md`). Every proposed GPIO change is put to the user first.

## Layout

- Re-place everything for minimum depth: TMC2209 StepSticks may be rotated, ESP32-S3 dev kit,
  USR-ES1 and supply placed beside the drivers instead of in front of them.
- Keep: ESP antenna at a board edge without copper underneath, Ethernet module and 24 V jack
  reachable from outside, button SW1 reachable with a paper clip through the lid, LED header J9,
  pump connectors J2–J6 in pump order (pump 1 on the left seen from the front).
- Driver cooling: keep room for StepStick heatsinks and airflow.
- Silkscreen in English: "LGAL Doser 5", "v1.1 – <date>", "PUMP 1" … "PUMP 5", no "FÜLLSTAND",
  no "5CH".
- Clean DRC: replace the `MountingHole_3.2mm` footprint (gone in KiCad 10), fix silkscreen
  overlaps, PCB net names in sync with the schematic, zones refilled before export.

## Open points

- Target depth (and maximum length) – from a placement study or the planned enclosure.
- ~~Pinout of the RTC cable connector.~~ Same as J16: 3.3 V, SDA, SCL, GND.
- New enclosure for v1.1 in `d5v1/enclosure/` (the narrower board does not fit the v1.0 one).

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
   Next: routing (4 layers, GND plane), silkscreen, DRC with parity.
4. BOM v1.1 (renumber the items), Gerber/placement files after review.
