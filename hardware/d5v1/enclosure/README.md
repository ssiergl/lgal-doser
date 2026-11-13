# d5v1 – enclosure

This folder holds the files for the enclosure and mechanical parts of the LGAL Doser 5:

| File | Purpose |
|---|---|
| `*.stl` / `*.3mf` | print files, one file per part, in print orientation |
| `*.step` | exchange format for adapting in other CAD programs |
| CAD source files | original design |

Each part comes with material, layer height, infill and whether supports are needed; these
details go into this README together with the files.

**Status:** no files published yet.

Notes for your own enclosure:

- Seen from the front, pump 1 is the left one; the pump labels must match the board.
- The Wi-Fi antenna of the ESP32-S3 sits at the edge of the enclosure – no metal and no thick
  wall there.
- Button and RGB light must be reachable and visible from outside; the USB socket of the ESP
  board need not be (updates run over the network).
- Put the rating plate with the device password where it is easy to read but not visible from
  outside.
- Splash water: openings facing down or sideways, cables with a drip loop.

License: CC BY-NC-SA 4.0, see [LICENSE-hardware.md](../../../LICENSE-hardware.md).
