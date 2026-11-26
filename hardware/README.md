# Hardware

OpenSCAD models for the frame's printed parts. Open a file in OpenSCAD and set `part` to the piece you want,
or export from the command line, e.g. `openscad -o tray.stl -D 'part="tray"' mount.scad`.

| File | Parts | Filament |
| --- | --- | --- |
| `mount.scad` | `feather_plate`, `clamp`, `plunger_boot`, `plunger_reset`, `tray`, `usb_cradle`, `usb_strap` | black PLA |
| `faceplate.scad` | `mat`, `name_h` / `name_v` (set `name_text`, `name_stack`), `panel_clip` (x6) | white PLA (names change to black at 0.6 mm), clips black |
| `frame.scad` | the whole stack, for checking depth and layout (not printed) | |

Hardware (nylon M3): 2 male-female 6 mm standoffs, 2 M3x6 (clamp), 8 M3x8 (plates, tray, cradle), 2 M3x10
(USB strap), 12 nuts.

Backboard holes: 4 mm for the plungers, 3 mm for the two lights, 3.4 mm for bolts, and a slot for the USB-C
plug (about 21 x 14.5 mm at 30 degrees). Use the printed plates as drilling guides.

Values marked MEASURE or CHECK in the files are guesses until measured on the real parts.

`stl/` has every part exported in its print orientation. `bambu/` has sliced Bambu Studio projects for the
A1 mini (0.4 mm nozzle, 0.20 mm Standard, Bambu PLA Basic, Textured PEI plate except the mat): `1_black.3mf`
(all the black parts), `2_mat.3mf` (Smooth PEI plate, for a smooth front) and `3_names.3mf` (both name plates; the print pauses before the 0.6 mm layer to swap
white for black).
