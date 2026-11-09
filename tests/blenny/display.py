"""Pure UI string / layout contracts mirrored from src/ui.cpp + sports helpers."""

from __future__ import annotations

from blenny.constants import K_FIRMWARE_VERSION, K_LEAGUE_NAMES
from blenny.sports import (
    League,
    SportsBoard,
    league_period_label,
    league_regulation_periods,
    sports_mark_color,
)


def tag_name(name: str | None) -> str:
    """Mirror ui.cpp tagName: first token only, uppercased."""
    n = name if name else "?"
    if not n:
        n = "?"
    sp = n.find(" ")
    if sp > 0:
        n = n[:sp]
    return n.upper()


def splash_firmware_line(version: str = K_FIRMWARE_VERSION) -> str:
    return f"Firmware Revision : {version}"


def league_picker_names() -> tuple[str, ...]:
    return K_LEAGUE_NAMES


def board_period_labels(league: League) -> list[str]:
    n = league_regulation_periods(league)
    # Show regulation columns; OT labels exist for overtime columns.
    return [league_period_label(league, i) for i in range(n)]


def mark_color(board: SportsBoard, home: bool) -> int:
    return sports_mark_color(board, home)


# ---- Find Friends name hints (ui.cpp: idHash / glitchStage / nameHint) ----
GLITCH_NONE, GLITCH_NOISE, GLITCH_HINT = 0, 1, 2


def id_hash(id_: str) -> int:
    """FNV-1a, 32-bit, as idHash() in ui.cpp."""
    h = 2166136261
    for b in id_.encode():
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def glitch_stage(id_: str, now_ms: int) -> int:
    """0.15 s noise, 0.3 s hint, 0.15 s noise once per 3-6 s period (per-badge)."""
    h = id_hash(id_)
    period = 3000 + (h % 3000)
    local = (now_ms + h * 7919) % 0x100000000 % period
    if local < 150 or 450 <= local < 600:
        return GLITCH_NOISE
    return GLITCH_HINT if local < 450 else GLITCH_NONE


def name_hint(full_name: str, warm: int) -> str:
    """Far: initial; closer (warm >= 20): three letters; close (>= 40): first + last initial."""
    full = full_name.strip()
    first, _, last = full.partition(" ")
    first, last = first.upper(), last.upper()
    if not first:
        return "?"
    if warm >= 40:
        return f"{first} {last[0]}." if last else first
    show = 3 if warm >= 20 else 1
    return "".join(c if i < show else "_" for i, c in enumerate(first))
