# Blenny scoreboard carrier

Alternate hardware on `hardware/scoreboard`: the LilyGO T-Display S3 is the **screen** in a larger vintage baseball scoreboard. HOME and VISITOR are real 2-digit 7-segment displays. Two AAs on the back. Lanyard hole in the pediment.

Gameplay is unchanged. Digits mean:

| Side | Digit | Meaning |
|---|---|---|
| HOME | 00–99 | Facts you guessed right |
| VISITOR | 00–99 | People you have met |

Flash with `-e t-display-s3-scoreboard`.

## What to print

`scoreboard_plate.stl` — fascia + back shell.

- Fascia: face on the bed, green or dark PLA, cream for a second-color cornice if you dual-print
- Back: AA pocket up, no supports if the pocket walls are 45° enough; 0.2 mm layers

The LilyGO sits behind the center window, USB-C out the right notch. 7-seg modules sit in the HOME / VISITOR wells. 2xAA holder drops into the back pocket.

## Electrical

2xAA (nominal 3.0 V) → power switch → boost to **5 V** → LilyGO `5V` pin and both TM1637 VCC.

| Net | LilyGO pin | Goes to |
|---|---|---|
| 5V | 5V | boost output |
| GND | GND | common |
| HOME_CLK | GPIO1 | TM1637 HOME CLK |
| HOME_DIO | GPIO2 | TM1637 HOME DIO |
| VIS_CLK | GPIO3 | TM1637 VISITOR CLK |
| VIS_DIO | GPIO10 | TM1637 VISITOR DIO |

Leave USB-C free for flashing. Do not feed 5 V into 3V3.

### BOM

- LilyGO T-Display S3 Touch (header version, or add 2.54 mm headers)
- 2× 0.56" 2-digit 7-segment, common cathode (or TM1637 2-digit boards)
- 2× TM1637 driver (skip if the digit boards already have one)
- 2×AA holder, rear-mount (Keystone-style, ~57 × 32 mm)
- 5 V boost (TPS61023 / MT3608 class)
- Slide switch
- Carrier PCB (`blenny-scoreboard.kicad_pcb`) — green mask, cream silk looks period

## Board outline

KiCad file `blenny-scoreboard.kicad_pcb` has Edge.Cuts, the 6.5 mm lanyard hole, HOME / VISITOR silk, and the LilyGO **T-Display-S3** footprint (`U1`) under SCREEN (USB to the right). Remaining to place:

1. Two 2-digit 7-seg footprints in the wells
2. Boost + switch along the right, USB notch
3. 2xAA holder on **B.Cu**, centered on the lower panel

The LilyGO symbol/footprint lives in `hardware/kicad/` (ccadic, vendored).

Regenerate STLs / outline:

```bash
python3 hardware/scoreboard/generate_scoreboard.py
```
