# Changelog

Firmware versions as shown on the badge (Settings → About, or `WHO` over USB).
Over-the-air releases are served from `klipperx.github.io/blenny-ota`; roster and schedule
updates ship separately and don't change the firmware version (see the end of this file).

## 3.2.0 (2026-11-06)

- **Hide and Seek radar.** Tap the Hide & Seek screen to switch between the board and a radar.
  - Every player now also broadcasts who it hears and how strongly (a short list added to the
    end of its existing packet; 3.1.0 badges ignore the extra bytes and keep playing).
  - Each badge turns everyone's readings into rough distances and fits a best-guess map:
    you in the centre, teammates as named blips, and it as a pulsing blob sized by how unsure
    the fit is. With fewer than three players hearing it, it shows as a distance ring instead.
  - The map's shape is the estimate; its rotation isn't (the badge has no compass), so turn
    it to match where your teammates actually are. Accuracy is rough, roughly ±50% indoors.
  - A warmer/colder arrow tracks your own reading of it over the last ~10 s: walk a few
    steps and it tells you whether you're heading the right way.
  - It's radar shows the seekers around them and how close the nearest one is.
- Host tests: the distance model and the layout solver recover a known room layout.

## 3.1.0 (2026-11-06)

- **Hide and Seek**, a new tile on the Diversions page (now a 2×2 grid). The roster names one
  person "it" and a round ("hideseek", "it"). The radio is on only while the game is open.
  - It's badge pings once a second. Each seeker's badge smooths the signal strength of those
    pings and broadcasts its reading, so every seeker sees a hot/cold meter for their own
    reading (Cold, Cool, Warm, Hot, ON TOP OF THEM) plus the team's three strongest readings.
    If it stops pinging for 5 s, seekers see that it has gone dark.
  - It sees a Safe / Careful / DANGER meter for the closest seeker, who is closing in, and how
    long they've been hidden.
  - A seeker who holds about -45 dBm or stronger for 10 s finds them. Everyone playing gets a
    toast and an LED blink, and the round is over until the roster starts a new one; anyone
    who opens the game later learns it from it's pings.
  - Only the roster's "it" can send pings. Two achievements: Bloodhound (find them) and Ghost
    (stay unfound for 30 minutes of play).
- `WHO` over USB adds a hide and seek line.
- Host tests: the found rule, the frame and the heat words (`tests/test_hideseek.py`).

## 3.0.ElectricBoogaloo (2026-11-05)

- **Outbreak**, a new game under Diversions → Games. Opening it shows your nametag with a live
  heartbeat (red while healthy) and brings up the radio; leaving turns it off again.
  - Spend about 30 s close to an infected player and you catch it: the nametag goes sickly
    green with a glitch, a biohazard mark and an irregular heartbeat, and you're told who did it.
  - Spend 30 s close to a medic and you're cured and immune for an hour.
  - Score is survival time: it runs while you're playing and healthy or immune, stops while
    you're infected, and medics don't score. It shows on the status chip and the Games tile.
  - Patient zero, the medics and the round number come from the roster ("outbreak", "zero",
    "medic"); without a round the game says it isn't running. State and time are kept
    between plays.
  - Three achievements: Patient Zero, Super-spreader (infect 5) and Immune (get cured).
- **Fix: signal strength in busy rooms.** The radio's signal table recorded every Wi-Fi
  device on the channel and never let go, so in an office it filled within seconds and badges
  that arrived later read as a flat -70 dBm. It now tracks badges only and drops the stalest.
  Find Friends' hot/cold bars read correctly too.
- `WHO` over USB adds an outbreak line (round, state, roles, spread).
- Versions: badges read this as 3.0.0, so the next release must be 3.0.1 or later.
- Host tests: the close-contact and survival-clock rules (`tests/test_outbreak.py`).

## 2.6.17 (2026-11-05)

- **"Knows you" hearts in Find Friends.** A badge that has met you now tells your badge so
  (a small broadcast every 20 s while you're in range), and a pink heart appears beside that
  person in your Find Friends list: they know you, so go find them to make it mutual. Badges
  remember who knows them until they restart; older firmware ignores the new message.
- Host tests: the new radio frame's magic is checked against the other ESP-NOW protocols
  (`tests/test_know.py`).

## 2.6.16 (2026-11-05)

- **Klippy Bird flags are easier to read.** Each leader's flag now sits on the bottom pipe just
  above the ground: their first name in large bold white on a dark tag, with a red pennant.
  The score is gone from the flag, since the pipe it's on already shows it.
- **"Clear all" is gone from People you've met**, so a stray tap can no longer wipe the list.
  Assigning a badge to a new person still starts their list fresh.

## 2.6.15 (2026-11-05)

- **Klippy Bird** (Diversions → Games): tap or press the top button to flap klipper's head
  through procedurally generated pipes. Best scores spread badge to badge over ESP-NOW and the
  Find Friends beacon; the title screen shows a top 5 and runs draw ghost flags where the
  leaders crashed. Blackjack moves into the same Games picker.
- **Seven new achievements:** Klippy Top 3, Doublemint (meet two people with the same first
  name), Sheep Herder, Timekeepers, Ace in the hole (dealt a 21), Gambling Problem (more than
  30 hands) and A friend in need (a full four-player table).
- The Achievements screen now scrolls.

## 2.6.14 (2026-11-04)

- **Find Friends name hints.** Each nearby callsign glitches every 3–6 s (static, then a hint,
  then static again, 0.6 s in all) and briefly shows part of the person's first name: the
  initial when far, three letters when closer, first name and last initial when close. The
  matching radar blip flickers in step.
- Host tests: a mirror of the hint and glitch timing (`tests/test_hints.py`), and the
  provisioning test now uses its own fake Wi-Fi and roster key instead of reading local files.

## 2.6.13 (2026-11-04)

- **Auto mode** (Settings, third page): the nametag alternates with a favourite animation,
  10 s nametag then 6 s favourite. Pick the favourite from the 7 built-ins or any uploaded GIF,
  with On/Off and Preview buttons. Goose badges cycle nametag, goose, then favourite.
- Long-pressing any Silly animation, custom GIFs included, makes it the favourite and turns
  Auto on; long-pressing the nametag turns it off.
- GIF favourites are saved by name, so other uploads or deletes don't change them; a deleted
  favourite falls back to the nametag only. Badges that had a long-press rotation keep it.

## 2.6.12 (2026-11-04)

- **Fix: Find Friends pairing in busy rooms.** Once a badge knew four unmet people it stopped
  tracking new badges, so a hunt request from anyone outside that set was dropped and the two
  badges couldn't pair. New badges are now always tracked (32 slots, stalest dropped first),
  so you can accept a request from anyone in range; the on-screen four still come from the
  strongest-signal re-pick.

## 2.6.11 (2026-11-04)

- **Backlight dims** from 200 to 48 after 60 seconds with no touch or button press, and comes
  back on any input; the first tap on a dimmed screen only wakes it. Stays bright during Find
  Friends, hunts, blackjack and the Queen egg.
- **Idle CPU clock.** Still screens (static nametag, menus, lists, schedule, settings) run at
  80 MHz with a 15 ms rest per loop instead of spinning at 240 MHz; animations, games, Wi-Fi
  and USB work stay at full speed.
- **Touch reads gated on the controller's interrupt** (GPIO 16) instead of every loop, with a
  once-a-second safety read; gating only engages after the interrupt has been seen to fire.
- Battery savings are estimated, not yet measured.

## 2.6.10 (2026-11-04)

- **Blackjack splits.** Split any same-rank pair once per hand when you have the chips to match
  the bet: each hand is played in turn (fresh timer for the second) and settled on its own.
  Split aces take one card each and stand; 21 after a split pays even money, not 3:2.
- Table packet carries a second hand per seat (hands now cap at 8 cards, 6 for a split hand) and
  the blackjack protocol moves to version 2, so tables only match badges on 2.6.10 or newer.
- Not yet tested on badges.

## 2.6.9.4.2.0 (2026-11-04)

- **Your own GIFs.** Plug the badge into a laptop, open
  `klipperx.github.io/blenny-ota/gif/` in Chrome or Edge and drop in a GIF: the page shrinks it
  to fit (320×170, ≤120 frames, ≤1 MB), shows a badge-sized preview and sends it over USB
  serial. Up to 8 GIFs per badge, listed and deletable from the page, which also shows an
  80s-dash storage gauge. GIFs appear after the built-in Silly Animations.
- New last Silly page, **Add a GIF**, with a QR code to the uploader.
- Uploads are chunked with offsets and a CRC-32 check; lost or garbled chunks are resent
  without starting over (`GIFBEGIN` / `GIFDATA` / `GIFEND` / `GIFLIST` / `GIFDEL`).
- Over-the-air checks pause while anything talks to the badge over USB, so station work and
  uploads aren't stalled by a Wi-Fi check.
- Versioning: badges compare the first three numbers only, so the next release must be at
  least 2.6.10.

## 2.2.2 (2026-11-04)

- **Pinned leaderboard scores.** A roster entry can carry a fixed `"score"`; every badge shows
  that number for the person (and ranks them by it) instead of the count heard over the radio,
  so it holds whatever their own badge broadcasts. Leaderboard counts can now be negative.

## 2.2.1 (2026-11-04)

- **Custom goose signs.** A roster entry can carry its own shame-goose sign text (`"sign"`);
  the board splits it over two balanced lines and widens to fit. Without one it still says
  "I didn't follow instructions".

## 2.2.0 (2026-11-04)

- **Optional carrier LEDs** are back for badges that have them (GPIO 11, 12, 13, 43): breathe
  at boot, sweep while searching, pulse during a hunt, blink on right and wrong guesses. Off
  by default; switch a badge on over USB with `SETLEDS 1 [0-2]` (stored on the badge, so one
  firmware serves every badge). `WHO` reports `LED on/off`.

## 2.1.0 (2026-11-04)

- **Blackjack** for up to four badges over ESP-NOW. Start table makes your badge the house
  (and gives you a seat); others pick it from the nearby list. Everyone seated is dealt in
  after a 6-second betting window (bet 5–50). The house stands on 17; blackjack pays 3:2.
  Play money only: 100 chips to start, a fresh 100 when broke. End table / Leave, or leaving
  the screen, closes or leaves the table; idle seats drop after 10 seconds.
- **Diversions** page under More holds Games, Silly Animations and Scores.
- **Fix:** swiping through Silly Animations no longer crashes the badge. The frame
  decompressor needs ~11 KB of stack and the main loop only had 8 KB; it now has 24 KB.
  Affected 2.0.0 and 2.0.1.

## 2.0.1 (2026-11-04)

- **Split-flap style** idle burst now steps each tile through the alphabet like a real board,
  spells CNET, holds, then flips back to the name.
- **About** is its own screen under More (no page dots or swiping); back returns to More.
- **Deep sleep** pill in the main menu bar. A confirmation screen (word-wrapped, drag to
  scroll) says what sleep keeps and pauses, and suggests removing the battery for desk use on
  USB. Wakes only on the top button.
- The klipper badge gets every achievement and every nametag style.

## 2.0.0 (2026-11-04)

- **Leaderboard.** Badges share their met count in the Find Friends beacon. Each badge keeps
  the scores it has heard and shows the top 5 plus your own rank. Compatible with 1.8.x
  badges (they show as unknown).
- **Achievements.** Twelve, unlocked live with a toast and counted retroactively from existing
  met lists: First Contact, Mingler, Social Butterfly, Networking Legend, Completionist,
  Goose Whisperer, Found klipper, Royalty, Fashionista, Top 3, A visit to HR, Fashionable
  footwear.
- **Nametag styles unlock at 5 met.** Before then only the seven-segment style is available;
  other styles preview dimmed with "Meet 5 people to unlock more styles" and snap back after
  5 seconds.
- **Event schedule.** New Schedule screen with day paging, "now" highlighting and drag
  scrolling. Delivered encrypted over the air like the roster (`tools/pack_schedule.py`,
  `ota_release.py --schedule`).
- **Menu.** Main menu is now 3×2: Play, Met, Leaders, Achievements, Schedule, More.
  Silly Animations moved under More.
- **Find Friends** re-picks the four strongest nearby signals every 5 seconds; badges still in
  the top four keep their row.
- **Mountain time.** The clock was set to Eastern; badges now use the event's timezone.
- **Smaller firmware.** Silly Animation frames are stored compressed
  (`tools/pack_toys.py`), taking flash use from 97.4% to 56.2%.
- Reprovisioning a badge (`SETID`) also clears its leaderboard and achievements.

## 1.8.2 (2026-11-03)

- Menu rebuilt as big 2×2 tiles (about 8 mm tall, up from about 3 mm rows) with a
  "< Nametag" back pill and a More page for settings and post-con apps.
- New About page under Settings: shimmering klipper logo, version, met count, your fun fact
  and "made with ♥ in SLC by klipper".

## 1.8.1 (2026-11-03)

- A "Roster updated: N people" pill appears for 5 seconds when a roster installs over the
  air.

## 1.8.0 (2026-11-03)

- Encrypted over-the-air roster updates (AES-256, key set over USB at provisioning). Met
  lists are kept, and a roster that would drop the badge's own ID is refused.
- Shame goose: goose-flagged attendees alternate their nametag (30 s) with a waddling
  "I didn't follow instructions" goose (10 s).
- Viper station keeps a resumable ledger of provisioned badges (`station.local.json`); known
  badges are refreshed without wiping their met list. Later: `--assign` lists so two
  stations never hand out the same person.

## 1.7.0 (2026-11-03)

- Signed over-the-air firmware updates (ECDSA P-256 manifest, SHA-256 image check,
  automatic rollback if a new image doesn't prove healthy). Checks at boot and every
  10 minutes outside the game; skipped on battery below 3.8 V.
- CNET-Guest Wi-Fi seeded at provisioning.
- Four new nametag styles: neon script tubes, split-flap board, CRT with blinking cursor,
  big pixel text.
- Silly Animations menu (long-press one to rotate it with your nametag), the Queen easter
  egg, and LEDs and boss faces removed for the ENCS badges.

## 1.6.0 and earlier (2026-10)

Version numbers in this stretch were bumped on parallel branches, so they don't run in
strict order.

- **1.6.0 / 1.5.3:** final klipper-toy face deck with glow clips and boss pages.
- **1.5.1:** LAN Now Playing (UPnP and Cast) and a gentler Wi-Fi wake.
- **1.4.0:** Post-Con Spotify remote with laptop PKCE linking.
- **1.4:** klipper-toy boss mode, idle spectrum and extra egg toys.
- **1.3.1:** nearby list capped to the screen; LED brightness settings.
- **1.3:** swipeable egg toys. Radio power cut on the nametag for battery life.
- **1.2:** custom nametag nickname.
- **1.1:** BIOS-style splash. Earlier work: Wi-Fi rejoin, NHL / NBA / NFL / MLB scoreboards,
  battery HUD and low-voltage sleep, viper check-in station.

## Roster and schedule updates

These ship over the air without a firmware change.

| Version | Change |
|---|---|
| Roster 2611031104 | First full roster (77 people) from the fun-facts CSV |
| Roster 2611031312 | Goose exemption for one attendee |
| Roster 2611031336 | Two attendees added |
| Roster 2611031345 | One fact corrected, one goose exemption |
| Roster 2611031350 | One attendee added (goose) |
| Roster 2611031414 | Re-issued unchanged to exercise the 1.8.1 toast |
| Roster 2611031442 | One attendee added (81 people) |
| Roster 2611041534 | One attendee added (82 people), one fact corrected, one custom goose sign |
| Roster 2611041542 | One attendee made a goose with a custom sign and a pinned score |
| Roster 2611051013 | Three facts filled in and three goose pardons |
| Roster 2611051039 | One attendee added (83 people) |
| Roster 2611051649 | Outbreak round 1: one patient zero, two medics |
| Roster 2611051708 | Outbreak round 2: everyone reset, patient zero re-infected |
| Roster 2611060954 | Hide and Seek round 1 with one person as it |
| Roster 2611061024 | Hide and Seek round 2: a new person is it |
| Schedule 2611041001 | First event schedule, Mon 11/2 – Fri 11/6 |
