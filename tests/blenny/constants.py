"""Constants mirrored from src/config.h and src/mlb.h."""

from __future__ import annotations

K_ID_LEN = 16
K_NAME_LEN = 32
K_WORD_LEN = 12
K_MSG_MAGIC = 0x424C4E59  # BLNY
K_MSG_VERSION = 2
K_MAX_MLB_GAMES = 16
K_LEAGUE_COUNT = 4
K_FIRMWARE_VERSION = "1.3"
K_LEAGUE_NAMES = ("MLB", "NHL", "NBA", "NFL")

# mlbTeamColor table from src/mlb.cpp
MLB_TEAM_COLORS: dict[int, int] = {
    108: 0xB9E4,
    109: 0xA1C6,
    110: 0xA1C6,
    111: 0xC800,
    112: 0x0393,
    113: 0xC800,
    114: 0xC2C0,
    115: 0x5ACB,
    116: 0x0393,
    117: 0xEC80,
    118: 0x0393,
    119: 0x0393,
    120: 0xC800,
    121: 0xEC80,
    133: 0x04C7,
    134: 0xEC80,
    135: 0x039F,
    136: 0x0393,
    137: 0xEC80,
    138: 0xC800,
    139: 0x0393,
    140: 0x0393,
    141: 0x0393,
    142: 0xC000,
    143: 0xC800,
    144: 0xC800,
    145: 0x6B4D,
    146: 0x041F,
    147: 0x1A6E,
    158: 0x03E0,
}
MLB_TEAM_COLOR_DEFAULT = 0x7BEF
