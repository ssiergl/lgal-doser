# Hardware – rules for working on the boards

This folder holds the circuit boards and enclosures of the LGAL Doser, one folder per board ID
(see [README.md](README.md)). The root [CLAUDE.md](../CLAUDE.md) describes the hardware the
firmware is written against. Read it first.

## Who may change what

- **The firmware treats the circuit as fixed.** A firmware session never changes the pin
  assignment, addresses or wiring to make the code easier.
- **A hardware session may change the circuit.** It edits schematic and layout here, but every
  change below is also a firmware change and must be handled as one:
  - GPIO assignment (any pin in the root `CLAUDE.md` pin table, including reserved and free pins)
  - TMC2209 UART buses, UART addresses (MS1/MS2 straps), PDN_UART wiring, series resistors
  - pull-ups and pull-downs (EN, I²C, ETH reset, button), the EN polarity
  - strapping pins (GPIO 0, 3, 45, 46) and anything connected to them
  - RGB LED wiring (channel to GPIO, common anode, NPN low side, polarity)
  - I²C addresses, SPI wiring of the W5500, supply rails the modules depend on
- For each such change:
  1. update the pin table and the affected sections of the root `CLAUDE.md` in the same commit;
  2. tell the user explicitly that it has a **firmware impact** and what the firmware must do;
  3. never change the pin assignment of a board that has already been ordered or built – a
     changed circuit is a new board revision (see below).
- If something in the root `CLAUDE.md` and the schematic disagree, report it to the user. Ask,
  do not guess which one is right.

## Board revisions

- A board that has been ordered is frozen. Its KiCad files stay exactly as ordered (git tag
  `hw-<board ID>-v<version>`, e.g. `hw-d5v1-v1.1`; d5v1 v1.0 has none – it is everything before
  `hw-d5v1-v1.1`); only documentation (README, BOM notes,
  rework instructions) may change afterwards. Work on a new version starts from a copy.
- Board versions with the same firmware pin map share the board ID and live side by side in
  `<board ID>/pcb/v<version>/` (e.g. `d5v1/pcb/v1.0/`, `d5v1/pcb/v1.1/`); their enclosures in
  `<board ID>/enclosure/v<version>/`.
- A change of the firmware pin map (any GPIO the firmware uses, UART addresses, LED or EN
  polarity) needs a new board ID and folder (`d5v2`, …), because the firmware selects its pin map
  by board ID. Ask the user before such a change.

## KiCad project

- **Every circuit change is made in the schematic first**, then transferred to the layout with
  "Update PCB from Schematic". Never add or change connections in the layout only.
- KiCad 10. One project per board version, named `<board ID>.kicad_pro`, with its own `bom.csv`.
- Libraries are project-local: symbols in `lgal_doser.kicad_sym`, footprints in
  `lgal_doser.pretty/`, registered in `sym-lib-table` / `fp-lib-table` with `${KIPRJMOD}`.
  No absolute paths, no references to libraries outside the repository except the KiCad
  standard libraries.
- Local and generated files (`*-backups/`, `_autosave-*`, lock files, `*.kicad_prl`, DRC/ERC
  reports, Freerouting files) are not committed, see `d5v1/pcb/.gitignore`.
- Before committing a schematic or layout change: run ERC and DRC (with schematic parity and
  zone refill), and cross-check the netlist against the root `CLAUDE.md` pin table. Report every
  new violation or mismatch to the user.
- Keep `bom.csv` in sync with the schematic (references, values, footprints).

## Conventions

- Everything in English: net names, labels, silkscreen, comments, commit messages. The project
  name is "LGAL Doser" (two words, no hyphen; no "5CH").
- One commit per change; author Lars Greiss (repo-local git identity).
- License of all files in this folder: CC BY-NC-SA 4.0, see
  [LICENSE-hardware.md](../LICENSE-hardware.md).
