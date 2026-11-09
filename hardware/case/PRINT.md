# Blenny badge case

Two-piece snap case for the LilyGO T-Display S3 Touch. Worn landscape (USB-C on the right, round lanyard tab on top). Back is a 6 mm battery well for a ~5 mm pouch, with a finger cutout over the JST.

Print `blenny_badge_plate.stl` as one plate, or the front/back files separately.

## Print

- Filament: PLA or PETG
- 0.4 mm nozzle, 0.20 mm layers, 3 walls, 15% infill
- Front: screen bezel on the bed
- Back: PCB rim / snap nubs on the bed, battery well opening up
- No supports if those faces are down

## Fit

The board friction-fits in the front tray (0.22 mm per side). Drop it in glass-down against the bezel, then snap the back on. The four side nubs click into the windows.

- USB-C is open on the right
- Rounded scoops near USB on the long edges reach GPIO14 and BOOT from the face
- 6 mm well on the back holds the pouch (shifted away from USB); tape or a wrap keeps it in
- Finger bay at the USB end of the back (20×16 mm) reaches the 1.25 mm JST so you can plug and unplug the pack without emptying the well

If the first farm print is tight, scale XY to 100.4%. If it rattles, scale to 99.7% or reprint after measuring your boards.

Regenerate after changing numbers:

```bash
python3 hardware/case/generate_case.py
```
