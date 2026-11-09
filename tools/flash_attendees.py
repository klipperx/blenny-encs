#!/usr/bin/env python3
"""Hands-free badge check-in: watch USB, identify (or assign next), flash + provision."""

from __future__ import annotations

import argparse
import json
import os
import random
import re
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

# Sibling import when run as `python tools/flash_attendees.py`
sys.path.insert(0, str(Path(__file__).resolve().parent))
from viper_art import viper_flash, viper_provision  # noqa: E402
from provision import seed_badge  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
ROSTER = ROOT / "data" / "attendees.json"
VENV_PY = ROOT / ".venv" / "bin" / "python3"
FLASH = ROOT / "tools" / "flash.py"
PACKAGE = ROOT / "tools" / "package.py"
PIO = next(
    (p for p in (ROOT / ".venv" / "bin" / "pio", Path.home() / ".platformio" / "penv" / "bin" / "pio")
     if p.exists()),
    Path("pio"),
)
# Who has a provisioned badge: persists across station restarts. Gitignored (names + MACs).
LEDGER = ROOT / "station.local.json"
MAC_RE = re.compile(r"\bMAC\s+([0-9A-F]{12})\b", re.I)


def load_ledger() -> dict:
    if LEDGER.exists():
        return json.loads(LEDGER.read_text(encoding="utf-8")).get("provisioned", {})
    return {}


def save_ledger(ledger: dict) -> None:
    tmp = LEDGER.with_suffix(".tmp")
    tmp.write_text(json.dumps({"provisioned": ledger}, indent=2, sort_keys=True) + "\n",
                   encoding="utf-8")
    tmp.replace(LEDGER)


def record_provisioned(ledger: dict, person: dict, who_raw: str) -> None:
    m = MAC_RE.search(who_raw or "")
    ledger[person["id"]] = {
        "name": person["name"],
        "mac": m.group(1).upper() if m else None,
        "at": time.strftime("%Y-%m-%d %H:%M:%S"),
    }
    save_ledger(ledger)


def print_status(people: list[dict], ledger: dict) -> None:
    done = [p for p in people if p["id"] in ledger]
    left = [p for p in people if p["id"] not in ledger]
    print(f"Provisioned {len(done)}/{len(people)} · remaining {len(left)}")
    for p in done:
        e = ledger[p["id"]]
        print(f"  ✓ {p['name']:28} {p['id']:15} {e.get('mac') or '?':12}  {e.get('at', '')}")
    if left:
        print("Remaining:")
        for p in left:
            print(f"  · {p['name']:28} {p['id']}")

ENV_FOR_VARIANT = {
    "cst816": "t-display-s3-touch-cst816",
    "cst328": "t-display-s3-touch",
    "scoreboard": "t-display-s3-scoreboard",
}

ID_RE = re.compile(r"\bID\s+(\S+)", re.I)
OK_RE = re.compile(r"\bOK\s+(\S+)\s+(.+)")


def py() -> str:
    return str(VENV_PY) if VENV_PY.exists() else sys.executable


def load_attendees() -> list[dict]:
    data = json.loads(ROSTER.read_text(encoding="utf-8"))
    people = data.get("attendees", [])
    if not people:
        raise SystemExit(f"no attendees in {ROSTER}")
    return people


def badge_ports() -> list[str]:
    try:
        from serial.tools import list_ports
    except ImportError as exc:
        raise SystemExit("pip install pyserial (use repo .venv)") from exc

    found: list[str] = []
    for p in list_ports.comports():
        blob = f"{p.device} {p.description} {p.hwid}".lower()
        if any(s in blob for s in ("usbmodem", "wch", "esp32", "usbserial", "lilygo", "cdc")):
            found.append(p.device)
    return sorted(set(found))


def talk(port: str, line: str, wait: float = 3.0, settle: float = 1.8) -> str:
    import serial

    ser = serial.Serial(port, 115200, timeout=0.2)
    time.sleep(settle)  # S3 CDC often reboots when the port opens
    ser.reset_input_buffer()
    ser.write(b"\n")
    time.sleep(0.15)
    ser.write((line.strip() + "\n").encode())
    deadline = time.time() + wait
    chunks: list[str] = []
    while time.time() < deadline:
        raw = ser.read(512)
        if raw:
            chunks.append(raw.decode("utf-8", "replace"))
            text = "".join(chunks)
            if "OK " in text or "ERR " in text or re.search(r"\bID\s+", text):
                if text.endswith("\n") or "OK " in text or "ERR " in text:
                    # give a beat for the rest of the line
                    time.sleep(0.15)
                    extra = ser.read(512)
                    if extra:
                        chunks.append(extra.decode("utf-8", "replace"))
                    break
        time.sleep(0.05)
    ser.close()
    return "".join(chunks)


def parse_who(text: str) -> str | None:
    m = ID_RE.search(text)
    if not m:
        return None
    pid = m.group(1).strip()
    if not pid or pid.lower() in ("-", "none", "null", "me="):
        return None
    return pid


def parse_ok_setid(text: str) -> tuple[str, str] | None:
    m = OK_RE.search(text)
    if not m:
        return None
    return m.group(1).strip(), m.group(2).strip()


def wait_port_ready(
    preferred: str | None,
    timeout: float,
    ui: "UIState",
    label: str,
) -> str:
    """Wait until a badge serial port is present and briefly stable.

    ESP32-S3 CDC often keeps the same path across resets; requiring a
    disappear/reappear cycle causes false timeouts (especially pre-WHO).
    """
    deadline = time.time() + timeout
    ui.status = f"{label}: waiting for USB serial…"
    stable = 0
    last_pick: str | None = None
    while time.time() < deadline:
        ports = badge_ports()
        if not ports:
            stable = 0
            last_pick = None
            ui.detail = "no badge serial yet…"
            time.sleep(0.25)
            continue
        pick = preferred if preferred and preferred in ports else ports[0]
        if pick == last_pick:
            stable += 1
        else:
            last_pick = pick
            stable = 1
        ui.detail = f"seen {pick} ({stable})"
        # ~0.5s stable (2 hits) is enough; don't demand a disconnect cycle.
        if stable >= 2:
            time.sleep(0.35)
            if pick in badge_ports():
                return pick
            stable = 0
        time.sleep(0.25)
    raise SystemExit(
        f"badge serial did not return within {timeout:.0f}s after {label} "
        f"(last ports: {badge_ports() or 'none'})"
    )


# --- console art -----------------------------------------------------------------

RESET = "\033[0m"
BOLD = "\033[1m"
DIM = "\033[2m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
CYAN = "\033[96m"
MAGENTA = "\033[95m"
RED = "\033[91m"
WHITE = "\033[97m"
ORANGE = "\033[38;5;208m"
DIM_GREEN = "\033[2m\033[92m"
BRIGHT_GREEN = "\033[1m\033[92m"
MATRIX_GLYPHS = "01ﾊﾐﾋｰｳｼﾅﾓﾆｻﾜﾂｵﾘｱﾎﾃﾏｹﾒｴｶｷﾑﾕﾗｾﾈｽﾀﾇﾍ<>*#$%@&"


def clr() -> None:
    sys.stdout.write("\033[2J\033[H")
    sys.stdout.flush()


def trunc(s: str, n: int) -> str:
    s = s or "—"
    if n <= 1:
        return "…"
    return s if len(s) <= n else s[: n - 1] + "…"


def term_w() -> int:
    return max(80, shutil.get_terminal_size((120, 40)).columns)


def term_h() -> int:
    return max(28, shutil.get_terminal_size((120, 40)).lines)


def visible_len(s: str) -> int:
    return len(re.sub(r"\033\[[0-9;]*m", "", s))


def pad_visible(s: str, width: int) -> str:
    pad = width - visible_len(s)
    if pad > 0:
        return s + (" " * pad)
    if pad < 0:
        # crude trim of plain suffix only
        return s
    return s


# Clean block KLIPPER — shimmer band sweeps these rows.
KLIPPER_MARK = [
    r" _  __  _      ___  ____   ____   ___  ____  ",
    r"| |/ / | |    |_ _||  _ \ |  _ \ | __||  _ \ ",
    r"| ' /  | |     | | | |_) || |_) || _| | |_) |",
    r"| . \  | |___  | | |  __/ |  __/ |___||  _ < ",
    r"|_|\_\ |_____||___||_|    |_|    |___||_| \_\ ",
]

FLASH_QUIPS = [
    "mm yes, delicious silicon…",
    "hold still, little badge",
    "stamping with enthusiasm!",
    "firmware goes brrrr",
    "one bite, fully cooked",
    "the belt provides",
]
PROVISION_QUIPS = [
    "signing the guestbook…",
    "you are who I say you are",
    "identity: applied",
    "ink is permanent, darling",
    "SETID with prejudice",
    "nameplate looks good on you",
]
UPDATE_QUIPS = [
    "Let'ssssss Update your Firmware",
    "shedding an old skin…",
    "sssoftware upgrade in progress",
    "no identity bite — just code",
    "keep your nick, keep your friendsss",
    "firmware molt: commence",
    "hissstorically compatible",
    "NVS is sacred. I shall not touch.",
]
IDLE_QUIPS = [
    "…waiting for prey",
    "the belt is lonely",
    "feed me a badge",
    "coiled and judgmental",
    "Ctrl-C is for quitters",
    "I can smell USB from here",
]
UPDATE_IDLE_QUIPS = [
    "…waiting to molt a badge",
    "plug in for a firmware shed",
    "update station coiled",
    "I only bite silicon today",
    "strangers get the cold shoulder",
]
ROSTER_QUIPS = [
    "feeding the nesssst a new name…",
    "Amy who? Amy now.",
    "roster refresh — hold sssstill",
    "no identity bite — just the guest list",
    "your nick and friendsss stay put",
    "LittleFS lunch is sssserved",
    "one more monkey on the list",
]
ROSTER_IDLE_QUIPS = [
    "…waiting to feed the roster",
    "plug in for a guest-list bite",
    "roster station coiled",
    "strangers get no guestbook",
    "attendees.json hungers",
]
ID_MODE_QUIPS = [
    "sniff sniff… who owns this?",
    "label check — no biting",
    "reading the guest list…",
    "mystery badge, hmm?",
    "name tag duty only",
]
ID_IDLE_QUIPS = [
    "…waiting to sniff a badge",
    "plug in for a name check",
    "identify station coiled",
    "I only read today — no flash",
    "who goes there?",
]
IDENTIFY_QUIPS = [
    "sniff sniff… WHO?",
    "reading your soul…",
    "ah, a familiar MAC",
]
REFUSE_QUIPS = [
    "unknown badge — no bite",
    "not on the roster, darling",
    "provision me first, then return",
    "I don't update strangers",
]
ALREADY_FED_QUIPS = [
    "already fed that one",
    "sssecond helpings? no.",
    "this badge already ate",
    "move along — roster's done for them",
    "duplicate guest — ignored",
]


def pick_quip(mode: str, who: str, *, session: str = "checkin") -> str:
    if session == "update":
        if mode == "flashing":
            pool = UPDATE_QUIPS
        elif mode == "identifying":
            pool = IDENTIFY_QUIPS
        elif mode == "done_one":
            pool = ["molt complete ✓", "new skin looks good", "updated. ssslither on."]
        elif mode == "error":
            pool = ["update hissed off", "try unplugging that attitude"]
        elif mode == "refused":
            pool = REFUSE_QUIPS
        else:
            pool = UPDATE_IDLE_QUIPS
        return random.choice(pool)

    if session == "roster":
        if mode == "flashing":
            pool = ROSTER_QUIPS
        elif mode == "identifying":
            pool = IDENTIFY_QUIPS
        elif mode == "done_one":
            pool = ["roster digested ✓", "guest list updated", "new friend unlocked in FS"]
        elif mode == "error":
            pool = ["roster hissed off", "try unplugging that attitude"]
        elif mode == "refused":
            pool = REFUSE_QUIPS
        else:
            pool = ROSTER_IDLE_QUIPS
        return random.choice(pool)

    if session == "identify":
        if mode == "identifying":
            pool = IDENTIFY_QUIPS
        elif mode == "done_one":
            pool = ["that's the one", "name acquired ✓", "label recovered"]
        elif mode == "refused":
            pool = ["unknown / unprovisioned", "not on the roster", "blank slate, darling"]
        elif mode == "error":
            pool = ["sniff failed", "try unplugging that attitude"]
        else:
            pool = ID_IDLE_QUIPS
        return random.choice(pool)

    if mode == "flashing":
        pool = FLASH_QUIPS if who == "flash" else ["…"]
    elif mode == "provisioning":
        pool = ["nice stamp."] if who == "flash" else PROVISION_QUIPS
    elif mode == "identifying":
        pool = IDENTIFY_QUIPS
    elif mode == "done_one":
        pool = ["another satisfied customer"] if who == "flash" else ["fang-certified ✓"]
    elif mode == "error":
        pool = ["rude."] if who == "flash" else ["try unplugging that attitude"]
    else:
        pool = IDLE_QUIPS
    return random.choice(pool)


def speech_bubble(text: str, width: int) -> list[str]:
    """Tiny comic bubble perched above a snake head."""
    if not text:
        return [" " * width] * 3
    msg = trunc(text, max(8, width - 6))
    inner = max(len(msg) + 2, 10)
    inner = min(inner, max(10, width - 2))
    pad = max(0, (width - (inner + 2)) // 2)
    top = (" " * pad) + "╭" + ("─" * inner) + "╮"
    mid = (" " * pad) + "│ " + msg.ljust(inner - 1)[: inner - 1] + "│"
    bot = (" " * pad) + "╰──v" + ("─" * max(0, inner - 3)) + "╯"
    return [
        (top + (" " * width))[:width],
        (mid + (" " * width))[:width],
        (bot + (" " * width))[:width],
    ]


def maybe_refresh_quips(ui: "UIState") -> None:
    """Random quips with 5–30s cooldown; each snake independent."""
    now = time.time()
    single = ui.session in ("update", "sync", "roster", "identify")
    snakes = ("flash",) if single else ("flash", "prov")
    for who in snakes:
        until_attr = f"{who}_quip_until"
        next_attr = f"{who}_quip_next"
        text_attr = f"{who}_quip"
        if getattr(ui, until_attr) and now >= getattr(ui, until_attr):
            setattr(ui, text_attr, "")
            setattr(ui, until_attr, 0.0)
        if now < getattr(ui, next_attr):
            continue
        # cooldown elapsed — sometimes speak, always schedule next window
        speak_chance = 0.85 if single else 0.65
        if random.random() < speak_chance:
            setattr(ui, text_attr, pick_quip(ui.mode, who, session=ui.session))
            setattr(ui, until_attr, now + random.uniform(2.5, 5.0))
        else:
            setattr(ui, text_attr, "")
            setattr(ui, until_attr, 0.0)
        setattr(ui, next_attr, now + random.uniform(5.0, 30.0))


def matrix_strip(height: int, tick: int, width: int = 12) -> list[str]:
    """Dim cascading glyphs for the right rail (one string per row)."""
    rows: list[str] = []
    n = len(MATRIX_GLYPHS)
    for y in range(height):
        cells: list[str] = []
        for x in range(width):
            # staggered falling heads
            head = (tick * 2 + x * 7) % (height + 8)
            dist = (head - y) % (height + 8)
            if dist == 0:
                ch = MATRIX_GLYPHS[(tick + x * 3 + y) % n]
                cells.append(f"{BRIGHT_GREEN}{ch}{RESET}")
            elif 1 <= dist <= 5:
                ch = MATRIX_GLYPHS[(tick + x * 5 + y * 2) % n]
                cells.append(f"{DIM_GREEN}{ch}{RESET}")
            elif dist <= 10 and (x + y + tick) % 3 == 0:
                ch = MATRIX_GLYPHS[(x * 11 + y) % n]
                cells.append(f"{DIM}{DIM_GREEN}{ch}{RESET}")
            else:
                cells.append(" ")
        rows.append("".join(cells))
    return rows


def klipper_shimmer(tick: int, width: int) -> list[str]:
    """ASCII-gif of the boot logo shimmer: bright band sweeping the mark."""
    mark = KLIPPER_MARK
    mark_w = max(len(r) for r in mark)
    left = max(0, (width - mark_w) // 2)
    sweep = tick % (mark_w + 18) - 5
    out: list[str] = [f"{DIM}{'─' * width}{RESET}"]
    for row in mark:
        chars: list[str] = []
        for i, ch in enumerate(row.ljust(mark_w)):
            if ch == " ":
                chars.append(" ")
                continue
            d = abs(i - sweep)
            if d <= 1:
                chars.append(f"{BOLD}{WHITE}{ch}{RESET}")
            elif d <= 3:
                chars.append(f"{BOLD}{CYAN}{ch}{RESET}")
            elif d <= 6:
                chars.append(f"{CYAN}{ch}{RESET}")
            else:
                chars.append(f"{DIM}{MAGENTA}{ch}{RESET}")
        out.append(pad_visible((" " * left) + "".join(chars), width))
    sparks = [" "] * width
    for dx in range(-3, 4):
        x = left + sweep + dx
        if 0 <= x < width:
            sparks[x] = "*" if abs(dx) <= 1 else ("·" if abs(dx) <= 2 else ".")
    out.append(
        "".join(
            f"{BOLD}{WHITE}{c}{RESET}"
            if c == "*"
            else (f"{DIM}{CYAN}{c}{RESET}" if c != " " else " ")
            for c in sparks
        )
    )
    return out


def with_matrix_rail(
    lines: list[str],
    tick: int,
    cols: int,
    stop_at: int,
    rail_w: int = 12,
) -> list[str]:
    """Append dim matrix rain on the right through row stop_at (inclusive)."""
    left_w = max(40, cols - rail_w - 2)
    rain = matrix_strip(stop_at + 1, tick, rail_w)
    out: list[str] = []
    for i, ln in enumerate(lines):
        if i <= stop_at:
            body = pad_visible(ln, left_w)
            out.append(body + "  " + rain[i])
        else:
            out.append(ln)
    return out


@dataclass
class UIState:
    mode: str = "waiting"  # waiting | identifying | flashing | provisioning | done_one | error | refused
    flashing: str = "—"
    provisioning: str = "—"
    next_up: str = "—"
    last_done: str = "—"
    done_ids: list[str] = field(default_factory=list)
    total: int = 0
    status: str = "waiting for a badge…"
    detail: str = ""
    frame: int = 0
    stop: bool = False
    session: str = "checkin"  # checkin | update | roster | sync | identify
    roster_people: list[dict] = field(default_factory=list)
    identify_name: str = "—"
    identify_id: str = "—"
    flash_quip: str = ""
    prov_quip: str = ""
    flash_quip_until: float = 0.0
    prov_quip_until: float = 0.0
    flash_quip_next: float = 0.0
    prov_quip_next: float = 0.0

    @property
    def update_mode(self) -> bool:
        # sync reuses the single-station update chrome (fw+FS, still no SETID).
        return self.session in ("update", "sync")

    @property
    def roster_mode(self) -> bool:
        return self.session == "roster"

    @property
    def identify_mode(self) -> bool:
        return self.session == "identify"

    def remaining_roster(self) -> list[str]:
        done = set(self.done_ids)
        return [label(p) for p in self.roster_people if p["id"] not in done]


def monitor_box(title: str, body: str, width: int) -> list[str]:
    inner = width - 2
    return [
        "┌" + "─" * inner + "┐",
        "│" + trunc(f" {title}", inner).ljust(inner) + "│",
        "│" + trunc(f" {body}", inner).ljust(inner) + "│",
        "└" + "─" * inner + "┘",
    ]


def hstack(blocks: list[list[str]], gap: str = "  ") -> list[str]:
    height = max(len(b) for b in blocks)
    padded = [b + [" " * len(b[0])] * (height - len(b)) for b in blocks]
    rows = []
    for r in range(height):
        rows.append(gap.join(padded[i][r] for i in range(len(padded))))
    return rows


def conveyor(tick: int, label: str, width: int) -> list[str]:
    w = max(40, width - 4)
    pattern = ">=--"
    shift = tick % len(pattern)
    belt = (pattern[shift:] + pattern * (w // len(pattern) + 2))[:w]
    badge = f"«{trunc(label, min(24, w // 3))}»"
    pos = (tick * 3) % max(1, w - len(badge))
    row = [" "] * w
    for i, ch in enumerate(badge):
        if 0 <= pos + i < w:
            row[pos + i] = ch
    return [
        "  " + "".join(row),
        "  " + belt,
        "  " + "‾" * w,
    ]


def colorize_block(lines: list[str], color: str) -> list[str]:
    return [f"{color}{ln}{RESET}" for ln in lines]


def render(ui: UIState) -> None:
    if ui.session in ("update", "sync"):
        render_update(ui)
        return
    if ui.session == "roster":
        render_roster(ui)
        return
    if ui.session == "identify":
        render_identify(ui)
        return

    cols = term_w()
    rail_w = 12 if cols >= 100 else 8
    content_cols = cols - rail_w - 2
    remaining = max(0, ui.total - len(ui.done_ids))
    done_n = len(ui.done_ids)
    mon_w = max(18, min(26, (content_cols - 10) // 4))

    mons = hstack(
        [
            monitor_box("FLASHING", ui.flashing, mon_w),
            monitor_box("PROVISION", ui.provisioning, mon_w),
            monitor_box("NEXT", ui.next_up, mon_w),
            monitor_box("LAST", ui.last_done, mon_w),
        ]
    )

    flash_on = ui.mode == "flashing"
    prov_on = ui.mode == "provisioning"
    flash_art = viper_flash(flash_on, ui.frame)
    prov_art = viper_provision(prov_on, ui.frame)
    flash_w = max(len(ln) for ln in flash_art)
    prov_w = max(len(ln) for ln in prov_art)
    flash_bubble = [
        f"{ORANGE}{ln}{RESET}" if ui.flash_quip else ln
        for ln in speech_bubble(ui.flash_quip, flash_w)
    ]
    prov_bubble = [
        f"{MAGENTA}{ln}{RESET}" if ui.prov_quip else ln
        for ln in speech_bubble(ui.prov_quip, prov_w)
    ]
    left = flash_bubble + colorize_block(
        flash_art,
        ORANGE if flash_on else DIM,
    )
    right = prov_bubble + colorize_block(
        prov_art,
        MAGENTA if prov_on else DIM,
    )
    vipers = hstack([left, right], gap="        ")

    belt_label = (
        ui.flashing
        if ui.mode == "flashing"
        else (ui.provisioning if ui.mode == "provisioning" else "waiting…")
    )
    belt = colorize_block(
        conveyor(ui.frame, belt_label, content_cols),
        YELLOW if ui.mode in ("flashing", "provisioning") else DIM,
    )

    inner = max(50, content_cols - 4)
    lines: list[str] = [
        f"{BOLD}{GREEN}╔{'═' * inner}╗{RESET}",
        f"{BOLD}{GREEN}║{RESET} {MAGENTA}{BOLD}VIPER CHECK-IN{RESET}  "
        f"{DIM}dual-station · flash stamp + provision ink{RESET}",
        f"{BOLD}{GREEN}╠{'═' * inner}╣{RESET}",
        f"{GREEN}║{RESET} {CYAN}done{RESET} {BOLD}{done_n}{RESET}/{ui.total}   "
        f"{YELLOW}remaining{RESET} {BOLD}{remaining}{RESET}   "
        f"{DIM}mode:{RESET} {BOLD}{ui.mode}{RESET}",
        f"{BOLD}{GREEN}╚{'═' * inner}╝{RESET}",
        "",
        *mons,
        "",
        *vipers,
        "",
        f"{YELLOW}{BOLD}  conveyor belt{RESET}",
        *belt,
    ]
    # Matrix rain down the right edge through the conveyor (last belt line).
    stop_at = len(lines) - 1
    lined = with_matrix_rail(lines, ui.frame, cols, stop_at, rail_w=rail_w)

    lined.append("")
    lined.append(f"  {BOLD}{ui.status}{RESET}")
    if ui.detail:
        lined.append(f"  {DIM}{trunc(ui.detail, cols - 4)}{RESET}")
    lined.append(
        f"  {DIM}Ctrl-C quit · replug re-flashes · unknown → next unflashed · "
        f"provision in-line after USB re-detect{RESET}"
    )
    lined.append("")
    lined.extend(klipper_shimmer(ui.frame, cols))

    max_rows = term_h() - 1
    if len(lined) > max_rows:
        lined = lined[: max_rows - 1] + [f"{DIM}…{RESET}"]

    clr()
    sys.stdout.write("\n".join(lined) + "\n")
    sys.stdout.flush()


def render_update(ui: UIState) -> None:
    """Single-station firmware update UI — no provision snake."""
    cols = term_w()
    rail_w = 12 if cols >= 100 else 8
    content_cols = cols - rail_w - 2
    done_n = len(ui.done_ids)
    mon_w = max(22, min(32, (content_cols - 8) // 3))

    mons = hstack(
        [
            monitor_box("UPDATING", ui.flashing, mon_w),
            monitor_box("LAST", ui.last_done, mon_w),
            monitor_box("UPDATED", f"{done_n}", mon_w),
        ]
    )

    active = ui.mode == "flashing"
    art = viper_flash(active, ui.frame)
    art_w = max(len(ln) for ln in art)
    # Prefer the signature line when idle so the banner always reads.
    bubble_text = ui.flash_quip or (
        "Let'ssssss Update your Firmware" if ui.mode == "waiting" else ""
    )
    bubble = [
        f"{CYAN}{ln}{RESET}" if bubble_text else ln
        for ln in speech_bubble(bubble_text, art_w)
    ]
    viper = bubble + colorize_block(art, CYAN if active else (RED if ui.mode == "refused" else DIM))
    # Center the single snake in the content column.
    viper_w = max(visible_len(ln) for ln in viper)
    pad = max(0, (content_cols - viper_w) // 2)
    vipers = [(" " * pad) + ln for ln in viper]

    belt_label = ui.flashing if ui.mode == "flashing" else "waiting…"
    belt_color = (
        YELLOW
        if ui.mode == "flashing"
        else (RED if ui.mode == "refused" else (GREEN if ui.mode == "done_one" else DIM))
    )
    belt = colorize_block(conveyor(ui.frame, belt_label, content_cols), belt_color)

    inner = max(50, content_cols - 4)
    lines: list[str] = [
        f"{BOLD}{CYAN}╔{'═' * inner}╗{RESET}",
        f"{BOLD}{CYAN}║{RESET} {GREEN}{BOLD}VIPER {'SYNC' if ui.session == 'sync' else 'UPDATE'}{RESET}  "
        f"{DIM}{'firmware + LittleFS' if ui.session == 'sync' else 'firmware-only'} · "
        f"keep id / nick / met / wifi{RESET}",
        f"{BOLD}{CYAN}╠{'═' * inner}╣{RESET}",
        f"{CYAN}║{RESET} {GREEN}updated{RESET} {BOLD}{done_n}{RESET}   "
        f"{DIM}mode:{RESET} {BOLD}{ui.mode}{RESET}   "
        f"{DIM}no SETID · {'fw+FS' if ui.session == 'sync' else 'no LittleFS'}{RESET}",
        f"{BOLD}{CYAN}╚{'═' * inner}╝{RESET}",
        "",
        *mons,
        "",
        *vipers,
        "",
        f"{YELLOW}{BOLD}  conveyor belt{RESET}",
        *belt,
    ]
    stop_at = len(lines) - 1
    lined = with_matrix_rail(lines, ui.frame, cols, stop_at, rail_w=rail_w)

    lined.append("")
    lined.append(f"  {BOLD}{ui.status}{RESET}")
    if ui.detail:
        lined.append(f"  {DIM}{trunc(ui.detail, cols - 4)}{RESET}")
    lined.append(
        f"  {DIM}Ctrl-C quit · firmware-only flash · unrecognized badges refused · "
        f"NVS (nick/met/id) preserved{RESET}"
    )
    lined.append("")
    lined.extend(klipper_shimmer(ui.frame, cols))

    max_rows = term_h() - 1
    if len(lined) > max_rows:
        lined = lined[: max_rows - 1] + [f"{DIM}…{RESET}"]

    clr()
    sys.stdout.write("\n".join(lined) + "\n")
    sys.stdout.flush()


def render_identify(ui: UIState) -> None:
    """Single-station identity sniff — WHO only, never flash / SETID / FS."""
    cols = term_w()
    rail_w = 12 if cols >= 100 else 8
    content_cols = cols - rail_w - 2
    seen_n = len(ui.done_ids)
    mon_w = max(22, min(34, (content_cols - 8) // 3))

    mons = hstack(
        [
            monitor_box("ID", ui.identify_id, mon_w),
            monitor_box("OWNER", ui.identify_name, mon_w),
            monitor_box("SEEN", f"{seen_n}", mon_w),
        ]
    )

    active = ui.mode == "identifying"
    art = viper_flash(active, ui.frame)
    art_w = max(len(ln) for ln in art)
    bubble_text = ui.flash_quip or (
        "sniff sniff… who are you?" if ui.mode == "waiting" else ""
    )
    bubble = [
        f"{YELLOW}{ln}{RESET}" if bubble_text else ln
        for ln in speech_bubble(bubble_text, art_w)
    ]
    viper = bubble + colorize_block(
        art, YELLOW if active else (RED if ui.mode == "refused" else (GREEN if ui.mode == "done_one" else DIM))
    )
    viper_w = max(visible_len(ln) for ln in viper)
    pad = max(0, (content_cols - viper_w) // 2)
    vipers = [(" " * pad) + ln for ln in viper]

    belt_label = (
        ui.identify_name
        if ui.mode in ("done_one", "refused") and ui.identify_name != "—"
        else ("sniffing…" if ui.mode == "identifying" else "waiting…")
    )
    belt_color = (
        YELLOW
        if ui.mode == "identifying"
        else (RED if ui.mode == "refused" else (GREEN if ui.mode == "done_one" else DIM))
    )
    belt = colorize_block(conveyor(ui.frame, belt_label, content_cols), belt_color)

    inner = max(50, content_cols - 4)
    lines: list[str] = [
        f"{BOLD}{YELLOW}╔{'═' * inner}╗{RESET}",
        f"{BOLD}{YELLOW}║{RESET} {GREEN}{BOLD}VIPER IDENTIFY{RESET}  "
        f"{DIM}read-only WHO · no flash · no SETID · no FS{RESET}",
        f"{BOLD}{YELLOW}╠{'═' * inner}╣{RESET}",
        f"{YELLOW}║{RESET} {GREEN}seen{RESET} {BOLD}{seen_n}{RESET}   "
        f"{DIM}mode:{RESET} {BOLD}{ui.mode}{RESET}   "
        f"{DIM}label recovery only{RESET}",
        f"{BOLD}{YELLOW}╚{'═' * inner}╝{RESET}",
        "",
        *mons,
        "",
        *vipers,
        "",
        f"{YELLOW}{BOLD}  conveyor belt{RESET}",
        *belt,
    ]
    stop_at = len(lines) - 1
    lined = with_matrix_rail(lines, ui.frame, cols, stop_at, rail_w=rail_w)

    lined.append("")
    lined.append(f"  {BOLD}{ui.status}{RESET}")
    if ui.detail:
        lined.append(f"  {DIM}{trunc(ui.detail, cols - 4)}{RESET}")
    lined.append(
        f"  {DIM}Ctrl-C quit · plug mystery badges · roster match by id · "
        f"nothing written to the badge{RESET}"
    )
    lined.append("")
    lined.extend(klipper_shimmer(ui.frame, cols))

    max_rows = term_h() - 1
    if len(lined) > max_rows:
        lined = lined[: max_rows - 1] + [f"{DIM}…{RESET}"]

    clr()
    sys.stdout.write("\n".join(lined) + "\n")
    sys.stdout.flush()


def remaining_panel(ui: UIState, width: int) -> list[str]:
    """Left-rail list of badges that still need a roster push."""
    remaining = ui.remaining_roster()
    inner = max(12, width - 2)
    title = f" STILL NEED ({len(remaining)})"
    lines = [
        f"{CYAN}┌{'─' * inner}┐{RESET}",
        f"{CYAN}│{RESET}{BOLD}{trunc(title, inner).ljust(inner)}{RESET}{CYAN}│{RESET}",
        f"{CYAN}├{'─' * inner}┤{RESET}",
    ]
    if not remaining:
        lines.append(
            f"{CYAN}│{RESET}{GREEN}{trunc(' ALL FED ✓', inner).ljust(inner)}{RESET}{CYAN}│{RESET}"
        )
    else:
        for entry in remaining:
            lines.append(
                f"{CYAN}│{RESET}{trunc(' ' + entry, inner).ljust(inner)}{CYAN}│{RESET}"
            )
    lines.append(f"{CYAN}└{'─' * inner}┘{RESET}")
    return lines


def render_roster(ui: UIState) -> None:
    """Single-station LittleFS roster push — no provision snake, no SETID."""
    cols = term_w()
    left_w = min(40, max(28, cols // 3))
    right_cols = max(50, cols - left_w - 2)
    done_n = len(ui.done_ids)
    rem_n = len(ui.remaining_roster())
    mon_w = max(18, min(28, (right_cols - 8) // 3))

    left = remaining_panel(ui, left_w)

    mons = hstack(
        [
            monitor_box("ROSTER", ui.flashing, mon_w),
            monitor_box("LAST", ui.last_done, mon_w),
            monitor_box("FED", f"{done_n}/{ui.total}", mon_w),
        ]
    )

    active = ui.mode == "flashing"
    art = viper_flash(active, ui.frame)
    art_w = max(len(ln) for ln in art)
    bubble_text = ui.flash_quip or (
        "feeding the nesssst a new name…" if ui.mode == "waiting" else ""
    )
    bubble = [
        f"{GREEN}{ln}{RESET}" if bubble_text else ln
        for ln in speech_bubble(bubble_text, art_w)
    ]
    viper = bubble + colorize_block(
        art, GREEN if active else (RED if ui.mode == "refused" else DIM)
    )
    viper_w = max(visible_len(ln) for ln in viper)
    pad = max(0, (right_cols - viper_w) // 2)
    vipers = [(" " * pad) + ln for ln in viper]

    belt_label = ui.flashing if ui.mode == "flashing" else "waiting…"
    belt_color = (
        YELLOW
        if ui.mode == "flashing"
        else (RED if ui.mode == "refused" else (GREEN if ui.mode == "done_one" else DIM))
    )
    belt = colorize_block(conveyor(ui.frame, belt_label, right_cols), belt_color)

    inner = max(40, right_cols - 4)
    right: list[str] = [
        f"{BOLD}{GREEN}╔{'═' * inner}╗{RESET}",
        f"{BOLD}{GREEN}║{RESET} {CYAN}{BOLD}VIPER ROSTER{RESET}  "
        f"{DIM}LittleFS-only · keep id / nick / met / wifi{RESET}",
        f"{BOLD}{GREEN}╠{'═' * inner}╣{RESET}",
        f"{GREEN}║{RESET} {CYAN}fed{RESET} {BOLD}{done_n}{RESET}/{ui.total}   "
        f"{YELLOW}need{RESET} {BOLD}{rem_n}{RESET}   "
        f"{DIM}mode:{RESET} {BOLD}{ui.mode}{RESET}",
        f"{BOLD}{GREEN}╚{'═' * inner}╝{RESET}",
        "",
        *mons,
        "",
        *vipers,
        "",
        f"{YELLOW}{BOLD}  conveyor belt{RESET}",
        *belt,
        "",
        f"  {BOLD}{ui.status}{RESET}",
    ]
    if ui.detail:
        right.append(f"  {DIM}{trunc(ui.detail, right_cols - 4)}{RESET}")
    right.append(
        f"  {DIM}Ctrl-C quit · each id once · strangers/already-fed refused · "
        f"NVS preserved{RESET}"
    )
    right.append("")
    right.extend(klipper_shimmer(ui.frame, right_cols))

    # Pad columns to equal height, then hstack left remaining + right stage.
    height = max(len(left), len(right))
    left_pad = left + [f"{DIM}{' ' * left_w}{RESET}"] * (height - len(left))
    right_pad = right + [" " * min(10, right_cols)] * (height - len(right))
    # Normalize left line visible widths for join.
    left_norm = [pad_visible(ln, left_w) for ln in left_pad]
    lined = [left_norm[i] + "  " + right_pad[i] for i in range(height)]

    # Allow the remaining list to spill past the terminal height (user OK with that).
    clr()
    sys.stdout.write("\n".join(lined) + "\n")
    sys.stdout.flush()


def ui_loop(ui: UIState, interval: float = 0.18) -> None:
    # Stagger first quips so they don't open in sync.
    now = time.time()
    ui.flash_quip_next = now + random.uniform(1.0, 4.0)
    ui.prov_quip_next = now + random.uniform(3.0, 8.0)
    while not ui.stop:
        maybe_refresh_quips(ui)
        render(ui)
        ui.frame += 1
        time.sleep(interval)


def next_unflashed(people: list[dict], done: set[str], cursor: int) -> tuple[dict | None, int]:
    """Next person in this station's pool not yet provisioned; None once the pool is used up
    (never wrap around and hand out someone twice)."""
    n = len(people)
    for step in range(n):
        i = (cursor + step) % n
        if people[i]["id"] not in done:
            return people[i], (i + 1) % n
    return None, cursor


def label(person: dict) -> str:
    return f"{person['name']} ({person['id']})"


def preview_next(people: list[dict], done: set[str], cursor: int) -> str:
    person = next_unflashed(people, done, cursor)[0]
    return label(person) if person else "— (this station's list is done)"


def ensure_fresh_image(variant: str, ui: UIState | None = None) -> None:
    """Rebuild LittleFS from data/ and repackage dist so roster matches attendees.json."""
    env = ENV_FOR_VARIANT[variant]
    pio = str(PIO) if PIO.exists() else "pio"
    steps = [
        ([pio, "run", "-e", env, "-t", "buildfs"], "building LittleFS from data/"),
        ([py(), str(PACKAGE), variant], f"packaging dist/blenny-{variant}"),
    ]
    for cmd, msg in steps:
        if ui:
            ui.status = msg
            ui.detail = " ".join(cmd)
        print(f"+ {' '.join(cmd)}")
        subprocess.check_call(cmd, cwd=ROOT)


def run_flash(
    variant: str,
    port: str,
    ui: UIState,
    *,
    firmware_only: bool = False,
    littlefs_only: bool = False,
) -> None:
    if firmware_only and littlefs_only:
        raise SystemExit("firmware_only and littlefs_only are mutually exclusive")
    cmd = [py(), str(FLASH), "--variant", variant, "--port", port]
    if firmware_only:
        cmd.append("--firmware-only")
        tag = " (firmware-only)"
    elif littlefs_only:
        cmd.append("--littlefs-only")
        tag = " (littlefs-only)"
    else:
        tag = ""
    ui.detail = f"esptool → {port}{tag}"
    # Show flash tool output in detail on failure; hide spam while animating.
    proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if proc.returncode != 0:
        err = (proc.stderr or proc.stdout or "").strip().splitlines()
        tail = " | ".join(err[-4:]) if err else f"exit {proc.returncode}"
        raise SystemExit(f"flash failed: {tail}")


def flash_and_provision(person: dict, variant: str, port: str, ui: UIState) -> str:
    """Flash image, re-find serial, SETID in-line, verify WHO. Returns live port."""
    ui.mode = "flashing"
    ui.flashing = label(person)
    ui.provisioning = "—"
    ui.status = f"FLASH station — stamping firmware+FS for {person['id']}"
    run_flash(variant, port, ui, firmware_only=False)

    ui.status = "FLASH done — waiting for badge to reboot onto USB…"
    live = wait_port_ready(port, timeout=45.0, ui=ui, label="post-flash")

    ui.mode = "provisioning"
    ui.provisioning = label(person)
    ui.status = f"PROVISION station — fang-ink SETID {person['id']} on {live}"
    ui.detail = f"serial {live}"

    # Extra settle: first open after flash is especially noisy.
    raw = talk(live, f"SETID {person['id']}", wait=4.0, settle=2.2)
    ok = parse_ok_setid(raw)
    if not ok or ok[0] != person["id"]:
        # Port may have jumped mid-command — one retry on rediscovered port.
        live = wait_port_ready(live, timeout=20.0, ui=ui, label="SETID-retry")
        raw = talk(live, f"SETID {person['id']}", wait=4.0, settle=2.2)
        ok = parse_ok_setid(raw)
    if not ok or ok[0] != person["id"]:
        raise SystemExit(f"SETID failed for {person['id']}: {raw!r}")

    ui.status = f"PROVISION — verifying WHO ({ok[1]})…"
    who_raw = talk(live, "WHO", wait=3.5, settle=1.8)
    got = parse_who(who_raw)
    if got != person["id"]:
        raise SystemExit(
            f"WHO mismatch: wanted {person['id']}, got {got!r} :: {who_raw!r}"
        )

    ui.status = "PROVISION — seeding Wi-Fi + roster key…"
    notes = seed_badge(live, lambda port, line: talk(port, line, wait=3.0, settle=1.0))
    ui.last_who = talk(live, "WHO", wait=3.5, settle=1.0) or who_raw
    ui.detail = f"OK {ok[0]} {ok[1]} · WHO confirmed · " + " · ".join(notes)
    return live


def flash_firmware_only(person: dict, variant: str, port: str, ui: UIState) -> str:
    """Firmware-only flash; never SETID. Preserves NVS (id/nick/met/wifi)."""
    ui.mode = "flashing"
    ui.flashing = label(person)
    ui.provisioning = "—"
    ui.status = f"UPDATE — shedding firmware skin for {person['id']} (no SETID, no FS)"
    run_flash(variant, port, ui, firmware_only=True)

    ui.status = "UPDATE flash done — waiting for badge to reboot onto USB…"
    live = wait_port_ready(port, timeout=45.0, ui=ui, label="post-update")

    ui.status = f"UPDATE — verifying WHO still {person['id']}…"
    ui.detail = f"serial {live}"
    who_raw = talk(live, "WHO", wait=3.5, settle=1.8)
    got = parse_who(who_raw)
    if got != person["id"]:
        # One retry if CDC re-enumerated.
        live = wait_port_ready(live, timeout=20.0, ui=ui, label="WHO-retry")
        who_raw = talk(live, "WHO", wait=3.5, settle=1.8)
        got = parse_who(who_raw)
    if got != person["id"]:
        raise SystemExit(
            f"WHO mismatch after update: wanted {person['id']}, got {got!r} :: {who_raw!r}"
        )

    ui.detail = f"WHO {got} confirmed · nick/met/wifi untouched"
    return live


def flash_roster_only(person: dict, variant: str, port: str, ui: UIState) -> str:
    """LittleFS-only flash; never SETID. Preserves NVS (id/nick/met/wifi)."""
    ui.mode = "flashing"
    ui.flashing = label(person)
    ui.provisioning = "—"
    ui.status = f"ROSTER — feeding guest list to {person['id']} (no SETID, FS only)"
    run_flash(variant, port, ui, littlefs_only=True)

    ui.status = "ROSTER flash done — waiting for badge to reboot onto USB…"
    live = wait_port_ready(port, timeout=45.0, ui=ui, label="post-roster")

    ui.status = f"ROSTER — verifying WHO still {person['id']}…"
    ui.detail = f"serial {live}"
    who_raw = talk(live, "WHO", wait=3.5, settle=1.8)
    got = parse_who(who_raw)
    if got != person["id"]:
        live = wait_port_ready(live, timeout=20.0, ui=ui, label="WHO-retry")
        who_raw = talk(live, "WHO", wait=3.5, settle=1.8)
        got = parse_who(who_raw)
    if got != person["id"]:
        raise SystemExit(
            f"WHO mismatch after roster: wanted {person['id']}, got {got!r} :: {who_raw!r}"
        )

    ui.detail = f"WHO {got} confirmed · roster pushed · nick/met/wifi untouched"
    return live


def flash_fw_and_fs(person: dict, variant: str, port: str, ui: UIState) -> str:
    """Firmware + LittleFS; never SETID. Preserves NVS (id/nick/met/wifi)."""
    ui.mode = "flashing"
    ui.flashing = label(person)
    ui.provisioning = "—"
    ui.status = f"SYNC — firmware + roster for {person['id']} (no SETID · met kept)"
    run_flash(variant, port, ui, firmware_only=False, littlefs_only=False)

    ui.status = "SYNC flash done — waiting for badge to reboot onto USB…"
    live = wait_port_ready(port, timeout=45.0, ui=ui, label="post-sync")

    ui.status = f"SYNC — verifying WHO still {person['id']}…"
    ui.detail = f"serial {live}"
    who_raw = talk(live, "WHO", wait=3.5, settle=1.8)
    got = parse_who(who_raw)
    if got != person["id"]:
        live = wait_port_ready(live, timeout=20.0, ui=ui, label="WHO-retry")
        who_raw = talk(live, "WHO", wait=3.5, settle=1.8)
        got = parse_who(who_raw)
    if got != person["id"]:
        raise SystemExit(
            f"WHO mismatch after sync: wanted {person['id']}, got {got!r} :: {who_raw!r}"
        )

    ui.detail = f"WHO {got} confirmed · fw+FS synced · nick/met/wifi untouched"
    return live


def wait_for_port(ui: UIState) -> str:
    ui.mode = "waiting"
    ui.flashing = "—"
    ui.provisioning = "—"
    if ui.session == "update":
        ui.status = "Let'ssssss Update your Firmware — waiting for a badge…"
    elif ui.session == "sync":
        ui.status = "SYNC fw+roster — waiting for a badge… (met/nick kept)"
    elif ui.session == "roster":
        ui.status = "feeding the nesssst — waiting for a badge…"
    elif ui.session == "identify":
        ui.status = "sniff sniff… who are you? — waiting for a badge…"
        ui.identify_name = "—"
        ui.identify_id = "—"
    else:
        ui.status = "both stations idle — waiting for a badge on the belt…"
    ui.detail = ""
    while not ui.stop:
        ports = badge_ports()
        if ports:
            return ports[0]
        time.sleep(0.35)
    raise SystemExit(0)


def wait_for_unplug(port: str, ui: UIState) -> None:
    ui.mode = "waiting"
    ui.status = f"unplug when ready for the next victim… ({port})"
    while not ui.stop:
        if port not in badge_ports():
            time.sleep(0.45)
            if port not in badge_ports():
                return
        time.sleep(0.25)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Auto flash+provision badges with dual viper stations "
        "(or --update / --roster / --identify single-station modes)"
    )
    parser.add_argument(
        "--variant",
        choices=("cst816", "cst328", "scoreboard"),
        default="cst816",
    )
    parser.add_argument(
        "--settle",
        type=float,
        default=1.0,
        help="seconds after plug before first WHO",
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--update",
        action="store_true",
        help="firmware-only update mode: never SETID, never rewrite LittleFS; "
        "refuse unrecognized badges",
    )
    mode.add_argument(
        "--roster",
        action="store_true",
        help="LittleFS-only roster push: refresh attendees/facts, never SETID; "
        "refuse unrecognized badges",
    )
    mode.add_argument(
        "--sync",
        action="store_true",
        help="firmware + LittleFS update: never SETID; keeps id/nick/met/wifi; "
        "refuse unrecognized badges",
    )
    parser.add_argument(
        "--assign",
        metavar="IDS.json",
        help="check-in hands out only these ids (JSON list), so two stations never "
        "assign the same person; badges still get the full roster",
    )
    mode.add_argument(
        "--status",
        action="store_true",
        help="print who is provisioned / remaining from station.local.json and exit",
    )
    mode.add_argument(
        "--identify",
        action="store_true",
        help="read-only identity sniff: WHO + roster name lookup; never flash, "
        "never SETID, never rewrite LittleFS",
    )
    args = parser.parse_args()

    if args.status:
        print_status(load_attendees(), load_ledger())
        return

    folder = ROOT / "dist" / f"blenny-{args.variant}"
    zpath = ROOT / "dist" / f"blenny-{args.variant}.zip"
    if args.update:
        session = "update"
    elif args.roster:
        session = "roster"
    elif args.sync:
        session = "sync"
    elif args.identify:
        session = "identify"
    else:
        session = "checkin"

    if session == "identify":
        # Read-only — no dist image required.
        print("Identify mode — WHO + roster lookup only (no flash)")
    elif session == "update":
        # Use existing package; do not rebuild/refresh LittleFS.
        if not folder.exists() and not zpath.exists():
            raise SystemExit(
                f"missing dist image for {args.variant} — "
                f"build once with: pio run -e {ENV_FOR_VARIANT[args.variant]} && "
                f"python3 tools/package.py {args.variant}"
            )
        print(f"Update mode — using existing dist for {args.variant} (no FS refresh)")
    else:
        # Check-in, roster, sync: refresh FS image from current attendees.json.
        print("Refreshing dist image from data/attendees.json …")
        try:
            ensure_fresh_image(args.variant, ui=None)
        except (subprocess.CalledProcessError, FileNotFoundError) as exc:
            if not folder.exists() and not zpath.exists():
                raise SystemExit(
                    f"missing dist image for {args.variant} and refresh failed: {exc}"
                ) from exc
            print(f"warning: could not refresh dist ({exc}); using existing image")

        if not folder.exists() and not zpath.exists():
            raise SystemExit(
                f"missing dist image for {args.variant} — run tools/package.py {args.variant}"
            )
        if session == "roster":
            print(f"Roster mode — LittleFS-only push for {args.variant} (no SETID)")
        elif session == "sync":
            print(
                f"Sync mode — firmware + LittleFS for {args.variant} "
                "(no SETID · nick/met/wifi kept)"
            )

    people = load_attendees()
    by_id = {p["id"]: p for p in people}
    ledger = load_ledger()
    # Who this station may hand out (default: everyone, in roster order).
    pool = people
    if args.assign:
        wanted = json.loads(Path(args.assign).read_text(encoding="utf-8"))
        missing = [i for i in wanted if i not in by_id]
        if missing:
            raise SystemExit(f"--assign ids not in roster: {', '.join(missing)}")
        pool = [by_id[i] for i in wanted]
        print(f"Assigning only {len(pool)} ids from {args.assign} "
              f"({pool[0]['id']} … {pool[-1]['id']})")
    done: set[str] = set()
    done_order: list[str] = []
    if session == "checkin":
        # Resume: anyone already in the ledger is not handed out again.
        for p in people:
            if p["id"] in ledger:
                done.add(p["id"])
                done_order.append(p["id"])
        if done:
            print(f"Resuming — {len(done)}/{len(people)} already provisioned per {LEDGER.name}")
    cursor = 0
    refused_n = 0
    unknown_n = 0

    idle_status = {
        "update": "Let'ssssss Update your Firmware — waiting for a badge…",
        "sync": "SYNC fw+roster — waiting for a badge… (met/nick kept)",
        "roster": "feeding the nesssst — waiting for a badge…",
        "identify": "sniff sniff… who are you? — waiting for a badge…",
        "checkin": "waiting for a badge…",
    }[session]
    ui = UIState(
        total=len(pool) if session == "checkin" else len(people),
        next_up="—" if session != "checkin" else preview_next(pool, done, cursor),
        session=session,
        status=idle_status,
        roster_people=list(people) if session == "roster" else [],
    )
    if session == "checkin":
        pool_ids = {p["id"] for p in pool}
        ui.done_ids = [i for i in done_order if i in pool_ids]
    # Seed the signature quip immediately in single-station modes.
    if session == "update":
        ui.flash_quip = "Let'ssssss Update your Firmware"
        ui.flash_quip_until = time.time() + 8.0
        ui.flash_quip_next = time.time() + 10.0
    elif session == "sync":
        ui.flash_quip = "SYNC — fw + roster, friends kept"
        ui.flash_quip_until = time.time() + 8.0
        ui.flash_quip_next = time.time() + 10.0
    elif session == "roster":
        ui.flash_quip = "feeding the nesssst a new name…"
        ui.flash_quip_until = time.time() + 8.0
        ui.flash_quip_next = time.time() + 10.0
    elif session == "identify":
        ui.flash_quip = "sniff sniff… who are you?"
        ui.flash_quip_until = time.time() + 8.0
        ui.flash_quip_next = time.time() + 10.0
    threading.Thread(target=ui_loop, args=(ui,), daemon=True).start()

    try:
        while True:
            if session == "checkin":
                ui.next_up = preview_next(pool, done, cursor)
            port = wait_for_port(ui)
            if session == "update":
                ui.status = "sniffing WHO — strangers get no molt…"
            elif session == "sync":
                ui.status = "sniffing WHO — strangers get no sync…"
            elif session == "roster":
                ui.status = "sniffing WHO — strangers get no guestbook…"
            elif session == "identify":
                ui.status = "sniffing WHO — label check only…"
            time.sleep(args.settle)

            ui.mode = "identifying"
            ui.status = f"sniffing WHO on {port}…"
            who_id: str | None = None
            try:
                live = wait_port_ready(port, timeout=15.0, ui=ui, label="pre-WHO")
                who_id = parse_who(talk(live, "WHO", wait=3.0, settle=1.8))
                port = live
            except Exception as exc:  # noqa: BLE001
                ui.detail = f"WHO failed: {exc}"
                who_id = None

            if session == "identify":
                if who_id and who_id in by_id:
                    person = by_id[who_id]
                    ui.identify_id = person["id"]
                    ui.identify_name = person["name"]
                    ui.flashing = label(person)
                    ui.mode = "done_one"
                    ui.status = f"OWNER — {person['name']}"
                    ui.detail = f"id {person['id']} · roster match · nothing written"
                    ui.flash_quip = random.choice(ID_MODE_QUIPS)
                    ui.flash_quip_until = time.time() + 6.0
                    if who_id not in done:
                        done.add(who_id)
                        done_order.append(who_id)
                    ui.done_ids = list(done_order)
                    ui.last_done = person["name"]
                else:
                    unknown_n += 1
                    why = f"unknown id {who_id!r}" if who_id else "unprovisioned / no id"
                    ui.identify_id = who_id or "—"
                    ui.identify_name = "UNPROVISIONED / UNKNOWN"
                    ui.flashing = "—"
                    ui.mode = "refused"
                    ui.status = f"UNKNOWN — {why}"
                    ui.detail = "not on the roster · badge unchanged"
                    ui.flash_quip = random.choice(
                        ["unknown / unprovisioned", "not on the roster", "blank slate, darling"]
                    )
                    ui.flash_quip_until = time.time() + 6.0
                wait_for_unplug(port, ui)
                ui.mode = "waiting"
                ui.status = idle_status
                ui.detail = ""
                continue

            if session in ("update", "roster", "sync"):
                if not who_id or who_id not in by_id:
                    refused_n += 1
                    why = f"unknown id {who_id!r}" if who_id else "unprovisioned / no id"
                    ui.mode = "refused"
                    ui.flashing = "—"
                    ui.status = f"REFUSED — {why}. Unplug and provision first."
                    ui.detail = (
                        "firmware update will not run on unrecognized badges"
                        if session == "update"
                        else (
                            "sync will not run on unrecognized badges"
                            if session == "sync"
                            else "roster push will not run on unrecognized badges"
                        )
                    )
                    ui.flash_quip = random.choice(REFUSE_QUIPS)
                    ui.flash_quip_until = time.time() + 6.0
                    wait_for_unplug(port, ui)
                    ui.mode = "waiting"
                    ui.status = idle_status
                    ui.detail = ""
                    continue

                # Roster/sync: each id only once per session.
                if session in ("roster", "sync") and who_id in done:
                    ui.mode = "refused"
                    ui.flashing = "—"
                    ui.status = f"ALREADY FED — {label(by_id[who_id])}. Unplug."
                    ui.detail = "this badge already got the push this session"
                    ui.flash_quip = random.choice(ALREADY_FED_QUIPS)
                    ui.flash_quip_until = time.time() + 6.0
                    wait_for_unplug(port, ui)
                    ui.mode = "waiting"
                    ui.status = idle_status
                    ui.detail = ""
                    continue

                person = by_id[who_id]
                try:
                    if session == "update":
                        ui.status = f"recognized {label(person)} — firmware-only update"
                        live_port = flash_firmware_only(person, args.variant, port, ui)
                        ok_line = (
                            f"UPDATED {label(person)} · {len(done) + (0 if person['id'] in done else 1)} "
                            f"molted · nick/met/wifi preserved"
                        )
                    elif session == "sync":
                        ui.status = f"recognized {label(person)} — firmware + LittleFS sync"
                        live_port = flash_fw_and_fs(person, args.variant, port, ui)
                        ok_line = (
                            f"SYNCED {label(person)} · "
                            f"{len(done) + 1}/{ui.total} · "
                            f"nick/met/wifi preserved"
                        )
                    else:
                        ui.status = f"recognized {label(person)} — LittleFS roster push"
                        live_port = flash_roster_only(person, args.variant, port, ui)
                        ok_line = (
                            f"ROSTER {label(person)} · "
                            f"{len(done) + 1}/{ui.total} fed · "
                            f"{ui.total - len(done) - 1} still need · "
                            f"nick/met/wifi preserved"
                        )
                except (subprocess.CalledProcessError, SystemExit, OSError) as exc:
                    ui.mode = "error"
                    ui.status = f"FAILED {session} {person['id']}"
                    ui.detail = str(exc)
                    wait_for_unplug(port, ui)
                    continue

                if person["id"] not in done:
                    done.add(person["id"])
                    done_order.append(person["id"])
                ui.done_ids = list(done_order)
                ui.last_done = label(person)
                ui.flashing = "—"
                ui.mode = "done_one"
                ui.status = ok_line
                wait_for_unplug(live_port, ui)
                ui.mode = "waiting"
                ui.status = (
                    "ALL FED — nest is full. Ctrl-C to retract fangs."
                    if session in ("roster", "sync") and len(done) >= ui.total
                    else idle_status
                )
                continue

            # --- check-in (default) path ---
            if who_id and who_id in by_id:
                # Already provisioned: refresh fw + FS but never SETID — SETID wipes the
                # badge's met list, nickname and colour.
                person = by_id[who_id]
                ui.status = f"recognized {label(person)} — refresh fw + roster (met kept)"
                try:
                    live_port = flash_fw_and_fs(person, args.variant, port, ui)
                    notes = seed_badge(
                        live_port, lambda p, line: talk(p, line, wait=3.0, settle=1.0)
                    )
                    who_raw = talk(live_port, "WHO", wait=3.5, settle=1.0)
                except (subprocess.CalledProcessError, SystemExit, OSError) as exc:
                    ui.mode = "error"
                    ui.status = f"FAILED refresh {person['id']}"
                    ui.detail = str(exc)
                    wait_for_unplug(port, ui)
                    continue
                if person["id"] not in done:
                    done.add(person["id"])
                    done_order.append(person["id"])
                if person["id"] not in ledger:
                    record_provisioned(ledger, person, who_raw)
                ui.done_ids = list(done_order)
                ui.last_done = label(person)
                ui.flashing = "—"
                ui.provisioning = "—"
                ui.mode = "done_one"
                ui.status = (
                    f"REFRESHED {label(person)} · met/nick kept · " + " · ".join(notes)
                )
                ui.next_up = preview_next(pool, done, cursor)
                wait_for_unplug(live_port, ui)
                continue
            else:
                person, cursor = next_unflashed(pool, done, cursor)
                if person is None:
                    ui.mode = "refused"
                    ui.status = "This station's list is fully assigned — unplug. Badge unchanged."
                    ui.detail = "use the other station, or a new --assign list"
                    wait_for_unplug(port, ui)
                    ui.mode = "waiting"
                    ui.status = idle_status
                    ui.detail = ""
                    continue
                why = f"unknown id {who_id!r}" if who_id else "unprovisioned/new"
                ui.status = f"{why} → assigning {label(person)}"

            ui.next_up = preview_next(pool, done | {person["id"]}, cursor)

            try:
                live_port = flash_and_provision(person, args.variant, port, ui)
            except (subprocess.CalledProcessError, SystemExit, OSError) as exc:
                ui.mode = "error"
                ui.status = f"FAILED {person['id']}"
                ui.detail = str(exc)
                wait_for_unplug(port, ui)
                continue

            if person["id"] not in done:
                done.add(person["id"])
                done_order.append(person["id"])
            record_provisioned(ledger, person, getattr(ui, "last_who", ""))
            ui.done_ids = list(done_order)
            ui.last_done = label(person)
            ui.flashing = "—"
            ui.provisioning = "—"
            ui.mode = "done_one"
            pool_done = sum(1 for p in pool if p["id"] in done)
            rem = len(pool) - pool_done
            ui.status = f"OK {label(person)} · {pool_done}/{len(pool)} · {rem} remaining"
            ui.next_up = preview_next(pool, done, cursor)
            wait_for_unplug(live_port, ui)

    except KeyboardInterrupt:
        ui.stop = True
        time.sleep(0.3)
        clr()
        print(f"{BOLD}Session stopped.{RESET}")
        if session == "update":
            print(f"Updated: {len(done)}")
            for pid in done_order:
                p = by_id[pid]
                print(f"  ✓ {p['name']} ({pid})")
            if refused_n:
                print(f"Refused (unrecognized): {refused_n}")
            print("Molting complete. Fangs retracted.")
        elif session == "sync":
            print(f"Synced (fw+FS): {len(done)}/{len(people)}")
            for pid in done_order:
                p = by_id[pid]
                print(f"  ✓ {p['name']} ({pid})")
            if refused_n:
                print(f"Refused (unrecognized): {refused_n}")
            left = [p for p in people if p["id"] not in done]
            if left:
                print(f"Not yet synced ({len(left)}):")
                for p in left:
                    print(f"  · {p['name']} ({p['id']})")
            print("Sync complete. Friends kept. Fangs retracted.")
        elif session == "roster":
            print(f"Roster fed: {len(done)}/{len(people)}")
            for pid in done_order:
                p = by_id[pid]
                print(f"  ✓ {p['name']} ({pid})")
            if refused_n:
                print(f"Refused (unrecognized): {refused_n}")
            left = [p for p in people if p["id"] not in done]
            if left:
                print(f"Not yet fed ({len(left)}):")
                for p in left:
                    print(f"  · {p['name']} ({p['id']})")
            print("Guestbook closed. Fangs retracted.")
        elif session == "identify":
            print(f"Identified: {len(done)}")
            for pid in done_order:
                p = by_id[pid]
                print(f"  ✓ {p['name']} ({pid})")
            if unknown_n:
                print(f"Unknown / unprovisioned: {unknown_n}")
            print("Sniffing complete. Fangs retracted.")
        else:
            print(f"Flashed+provisioned: {len(done)}/{len(people)}")
            for pid in done_order:
                p = by_id[pid]
                print(f"  ✓ {p['name']} ({pid})")
            left = [p for p in people if p["id"] not in done]
            if left:
                print(f"Remaining ({len(left)}):")
                for p in left:
                    print(f"  · {p['name']} ({p['id']})")
            print("Fangs retracted. For now.")


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        try:
            sys.stdout.reconfigure(encoding="utf-8")
        except Exception:
            pass
    os.environ.setdefault("PYTHONUNBUFFERED", "1")
    main()
