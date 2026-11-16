# d5v1 – enclosure

This folder holds the files for the enclosure and mechanical parts of the LGAL Doser 5:

| File | Purpose |
|---|---|
| [`enclosure_v1.0.scad`](enclosure_v1.0.scad) | OpenSCAD source of the enclosure for board v1.0 (flat board) |
| [`enclosure_v1.1.scad`](enclosure_v1.1.scad) | OpenSCAD source of the enclosure for board v1.1 (upright board) |
| `enclosure_v1.x_base.stl`, `_lid.stl`, `_plug.stl` | print files, one file per part, in print orientation, exported from the source |
| `*.step` | exchange format for adapting in other CAD programs (not yet published) |

The STL files are exported with OpenSCAD 2026.10.11 (manifold backend, binary STL), e.g.
`openscad --backend=manifold -D 'part="base"' -o enclosure_v1.1_base.stl enclosure_v1.1.scad`.

**Status:** the v1.0 enclosure is the one designed for the first devices (see below). The v1.1 enclosure renders without errors in OpenSCAD 2026.10.11 (manifold
backend). It does not collide with the motors, the board, the outlines of its parts (also while
the board slides in) and the RTC module. It is not yet printed or checked against the assembled
board.

## Enclosure for board v1.1

About **282 × 89 × 65 mm** (width × depth × height, depth without the pump heads). Two parts:
the base with the front panel and a smooth lid without openings, so devices can be stacked.
Export a part with `part = "base"`, `"lid"` or `"plug"`; `"assembly"` shows everything with the
board and pumps as a preview.

- **Pumps** sit low (flange bottom 14 mm above the floor). Their tube nipples point down and
  overhang the bottom by about 8 mm, like on doser heads on the market. To stand on a table the
  device needs feet of at least 10 mm (recesses for 10 mm rubber feet are in the corners of the floor), or it
  stands at the edge of a shelf.
- **Board** d5v1 v1.1 (205 × 50 mm, U-shaped) stands upright 6 mm behind the motors, components
  facing the back wall. The gap leaves room for the solder joints and the motor cables. It slides in from the top into two guide slots at its side edges and
  stands on four blocks 4 mm above the floor, so water on the floor does not reach it; three
  tabs on the lid hold its top edge. No screws. The motor cables run
  over the top edge to the pump plugs.
- **Niche** behind pump 3, open to the back and to the bottom. It passes through the notch of
  the board; barrel jack and RJ45 point down into it through cut-outs in its ceiling. On the DIN
  rail the cables hang straight down; standing, they leave to the back (angled plugs fit best).
- **Button:** pressed with a paper clip through a 3 mm hole in the back wall below the DIN rail;
  a guide tube leads the clip to the button, a TPU plug (`part = "plug"`) closes the hole.
- **RTC module** (DS3231 + AT24C32, the common 38 × 22 mm module with CR2032/LIR2032 holder)
  stands upside down in two rails at the left side wall (seen from the front), battery towards
  the wall. Its angled pin headers point down, so the plug and the cable to J10 hang below it;
  the lid holds its top edge (0.3 mm play).
- **Status light** (Mentor 2660.8302, 8 mm hole, pressed in) in the front under pump 3. Its cable
  (about 85 mm without the plug) does not reach J9 behind pumps 1 and 2: add a 2 × 2 extension
  (2.54 mm, socket to pins, about 10 cm) and run it under the bottom edge of the board.
- **DIN rail:** two clips (35 × 47 mm, holes 20 mm apart) at the back, symmetric to the niche,
  M3 countersunk from the inside (flush), nuts on the clips.
- **Floor:** a drain funnel under each pump right behind the front.

### Print and parts

All walls, ribs, lips and tabs are at least 2 mm thick (floor 3 mm, lid and outer walls 2.5 mm),
so the parts print on FDM as well as MJF and nothing thin breaks off.

| Part | Print | Note |
|---|---|---|
| base | PA12 (MJF), or PETG/ASA on FDM with supports in the niche and pump openings | front down for FDM |
| lid | PA12 (MJF), or PETG/ASA, top side down | |
| plug | TPU | press fit, check the fit (`plug_fit`) |

| Hardware | Qty | Use |
|---|---|---|
| heat-set insert M3 × 5.7 (e.g. ruthex RX-M3x5.7) | 6 | lid screw columns |
| M3 × 8 countersunk (ISO 10642) | 6 | lid |
| M3 countersunk (length to suit the clip) + M3 nut | 4 | DIN rail clips |
| DIN rail clip 35 × 47 mm | 2 | |
| rubber foot Ø 10 mm, at least 10 mm high | 4 | only when standing |

Assembly order: DIN clips, pumps from the front, RTC module into its rails, then the board from
the top with the motor cables over its top edge; plug in the cables, close the lid.

### To check on the first print

- Fit of the board in the guide slots (`slot_clr`) and of the lid tabs on its top edge.
- Cut-outs for the barrel jack and the RJ45 in the niche ceiling against the assembled board
  (position of the RJ45 on the USR-ES1).
- Rails against the RTC module actually used (`rtc_*`) and room for its plug below it.
- Height of the button guide tube against the Multimec button (`btn_h`).
- Room for the motor cables over the top edge of the board and in the gap behind the motors.

## Enclosure for board v1.0

The enclosure of the first devices: the board lies flat 12 mm above the floor behind the pumps
and is screwed onto five domes with heat-set inserts. Base with front panel and a smooth lid,
screwed on with nine M3 countersunk screws. Barrel jack and RJ45 point into a slot in the middle
of the back, open to the back and to the bottom. The button is pressed with a paper clip through
a hole in the lid (TPU plug), the status light sits in the front right of pump 5.

## Notes for your own enclosure

- Seen from the front, pump 1 is the left one; the pump labels must match the board.
- The Wi-Fi antenna of the ESP32-S3 sits at the edge of the enclosure – no metal and no thick
  wall there.
- Button and RGB light must be reachable and visible from outside; the USB socket of the ESP
  board need not be (updates run over the network).
- Put the rating plate with the device password where it is easy to read but not visible from
  outside.
- Splash water: openings facing down or sideways, cables with a drip loop.

License: CC BY-NC-SA 4.0, see [LICENSE-hardware.md](../../../LICENSE-hardware.md).
