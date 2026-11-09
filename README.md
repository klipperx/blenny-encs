# blenny

Conference badge for the LilyGO T-Display S3 Touch. Nametag by default. Play starts an ESP-NOW icebreaker: find someone, both confirm, then guess which of three facts is true about them.

Each quiz is:

1. That person's real fact
2. Someone else's real fact
3. One line from `data/generated_facts.json`

Same person can be played again after 60 seconds.

## Hardware

LilyGO T-Display S3 **Touch**. GPIO15 is turned on in firmware so the screen works on battery.

If touch does nothing, flash the CST816 env instead of the default CST328 env.

## Check-in flow

1. Put everyone in `data/attendees.json` (`id`, `name`, `truth`). Keep truths third person and under ~80 characters, same voice as the generated pool.
2. Flash firmware **and** the filesystem onto each badge (the filesystem is the shared roster + 200 fake facts).
3. Plug the badge in, pick that attendee, provision `my_id`.

```bash
pip install -r tools/requirements.txt
pio run -e t-display-s3-touch -t upload
pio run -e t-display-s3-touch -t uploadfs
python3 tools/provision.py --list
python3 tools/provision.py --id ada
```

CST816 touch boards:

```bash
pio run -e t-display-s3-touch-cst816 -t upload
```

### Chuck a release image on a badge

GitHub Releases ship a zip per board (firmware + LittleFS roster). Plug the badge in USB-C and:

```bash
pip install -r tools/requirements.txt
python3 tools/flash.py --variant cst816
```

`--variant` is `cst816` (older T-Display S3 Touch), `cst328` (newer), or `scoreboard`. Pass `--image path/to/blenny-cst816.zip` if you downloaded the zip from the release. Then provision:

```bash
python3 tools/provision.py --id ada
```

Serial commands if you do not want the script: `WHO`, `LIST`, `SETID ada`, `COLOR 3`.

To rebuild the merged images locally:

```bash
pio run -e t-display-s3-touch -e t-display-s3-touch-cst816 -e t-display-s3-scoreboard
pio run -e t-display-s3-touch -t buildfs
python3 tools/package.py
```

## Use

- Nametag: name fills the screen. Tap the name to cycle color. Tap **Play** to hunt.
- Play: tap a nearby name. Matching color + warmth bar. Both tap **That's you**.
- Guess the true fact. Continue goes back to Play. The physical button (GPIO14) backs out to nametag.

All badges must stay on Wi-Fi channel 1 (set in firmware). You need two provisioned boards to try the game.
