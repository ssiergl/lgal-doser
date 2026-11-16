# d5v1 – enclosure

This folder holds the files for the enclosure and mechanical parts of the LGAL Doser 5:

| File | Purpose |
|---|---|
| [`enclosure_v1.0.scad`](enclosure_v1.0.scad) | OpenSCAD source of the enclosure for board v1.0 (flat board) |
| [`enclosure_v1.1.scad`](enclosure_v1.1.scad) | OpenSCAD source of the enclosure for board v1.1 (upright board) |
| `enclosure_v1.x_base.stl`, `_lid.stl`, `_plug.stl` (v1.1 also `_plate.stl`) | print files, one file per part, in print orientation, exported from the source |
| `*.step` | exchange format for adapting in other CAD programs (not yet published) |

The STL files are exported with OpenSCAD 2026.10.11 (manifold backend, binary STL), e.g.
`openscad --backend=manifold -D 'part="base"' -o enclosure_v1.1_base.stl enclosure_v1.1.scad`.

**Status:** the v1.0 enclosure is the one designed for the first devices (see below). The v1.1 enclosure renders without errors in OpenSCAD 2026.10.11 (manifold
backend). It does not collide with the motors, the board, the outlines of its parts (also while
the board slides in), the RTC module and the front plate with the pumps (also while it slides in). It is not yet printed or checked against the assembled
board.

## Enclosure for board v1.1

About **282 × 90 × 65 mm** (width × depth × height, depth without the pump heads). Three parts:
the base, a removable front plate that carries the pumps, and a smooth lid without openings, so
devices can be stacked. Export a part with `part = "base"`, `"plate"`, `"lid"` or `"plug"`;
`"assembly"` shows everything with the board and pumps as a preview.

- **Front plate:** the threads of the Kamoer mounting plates (42 × 41.9 × 7 mm) are blind holes
  in their front face, and the lower screws cannot be reached in a fixed front. So the pumps are
  screwed to the front plate on the bench, and the plate with the pumps slides in from the top.
  It covers the whole front including the ends of the side walls and the floor: from the front
  there is no joint, only a seam on the sides 2.5 mm behind the front and on the bottom. Legs
  on its back edges carry a groove that slides over a tongue on the inside of each side wall and
  on the floor; the lid holds it down and is screwed into two columns on the plate. A pump can
  be changed by taking out the plate without removing the board.

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
- **RTC module** (DS3231 + AT24C32, ZS-042 type, about 38.3 × 21.7 mm, battery holder 8 mm
  high) stands upright in a pocket at the left side wall (seen from the front), battery holder
  towards the wall, the 6-pin angled header at the bottom. Both long edges carry parts in the
  middle, so only its corners are held: it stands in two short channels at the bottom corners,
  and two forks on the lid catch the top corners. No screws. The plug of its cable hangs below
  it; the cable runs through an opening at the bottom of the left guide rib to J10.
- **Status light** (Mentor 2660.8302, 8 mm hole, pressed in) in the front plate under pump 3. Its cable
  (about 85 mm without the plug) does not reach J9 behind pumps 1 and 2: add a 2 × 2 extension
  (2.54 mm, socket to pins, about 10 cm) and run it under the bottom edge of the board.
- **DIN rail:** two clips (35 × 47 mm, holes 20 mm apart) at the back, symmetric to the niche,
  M3 countersunk from the inside (flush), nuts on the clips.
- **Floor:** a drain funnel under each pump right behind the bottom leg of the front plate.

### Print and parts

All walls, ribs, lips and tabs are at least 2 mm thick (floor 3 mm, lid and outer walls 2.5 mm),
so the parts print on FDM as well as MJF and nothing thin breaks off.

| Part | Print | Note |
|---|---|---|
| base | PA12 (MJF), or PETG/ASA on FDM with supports in the niche | floor down |
| plate | PA12 (MJF), or PETG/ASA | front face down: smooth front, no supports |
| lid | PA12 (MJF), or PETG/ASA, top side down | |
| plug | TPU | press fit, check the fit (`plug_fit`) |

| Hardware | Qty | Use |
|---|---|---|
| heat-set insert M3 × 5.7 (e.g. ruthex RX-M3x5.7) | 6 | lid screw columns (4 in the base, 2 on the front plate) |
| M3 × 8 countersunk (ISO 10642) | 6 | lid |
| M3 countersunk (length to suit the clip) + M3 nut | 4 | DIN rail clips |
| DIN rail clip 35 × 47 mm | 2 | |
| rubber foot Ø 10 mm, at least 10 mm high | 4 | only when standing |

Assembly order: DIN clips; pumps onto the front plate on the bench (heads and status light from
the front); RTC module with its cable plugged in into its pocket (cable through the rib opening);
the front plate with the pumps from the top; then the board from the top with the motor cables
over its top edge; plug in the cables, close the lid.

### To check on the first print

- Fit of the front plate on the tongues (`fp_clr`) and the seam on the sides and the bottom.
- Fit of the board in the guide slots (`slot_clr`) and of the lid tabs on its top edge.
- Cut-outs for the barrel jack and the RJ45 in the niche ceiling against the assembled board
  (position of the RJ45 on the USR-ES1).
- RTC pocket against the module actually used (`rtc_*`): corner channels, lid forks, room for
  its plug below it and the cable opening in the rib.
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
