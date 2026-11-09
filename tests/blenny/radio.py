"""BadgeMsg packing / accept gate mirrored from src/config.h + src/radio.cpp."""

from __future__ import annotations

import struct
from dataclasses import dataclass

from blenny.constants import (
    K_ID_LEN,
    K_MSG_MAGIC,
    K_MSG_VERSION,
    K_NAME_LEN,
    K_WORD_LEN,
)

# magic u32, version/type/color/pad u8x4, id[16], name[32], word[12]
BADGE_MSG_STRUCT = struct.Struct(f"<IBBBB{K_ID_LEN}s{K_NAME_LEN}s{K_WORD_LEN}s")
assert BADGE_MSG_STRUCT.size == 68
assert BADGE_MSG_STRUCT.size <= 250


@dataclass
class BadgeMsg:
    magic: int = K_MSG_MAGIC
    version: int = K_MSG_VERSION
    type: int = 0
    color: int = 0
    pad: int = 0
    id: str = ""
    name: str = ""
    word: str = ""


def _c_field(s: str, n: int) -> bytes:
    raw = s.encode("utf-8", "replace")[:n]
    return raw + b"\x00" * (n - len(raw))


def _from_c_field(b: bytes) -> str:
    return b.split(b"\x00", 1)[0].decode("utf-8", "replace")


def pack_msg(msg: BadgeMsg) -> bytes:
    return BADGE_MSG_STRUCT.pack(
        msg.magic,
        msg.version,
        msg.type,
        msg.color,
        msg.pad,
        _c_field(msg.id, K_ID_LEN),
        _c_field(msg.name, K_NAME_LEN),
        _c_field(msg.word, K_WORD_LEN),
    )


def unpack_msg(data: bytes) -> BadgeMsg:
    magic, version, typ, color, pad, id_b, name_b, word_b = BADGE_MSG_STRUCT.unpack(
        data[: BADGE_MSG_STRUCT.size]
    )
    return BadgeMsg(
        magic=magic,
        version=version,
        type=typ,
        color=color,
        pad=pad,
        id=_from_c_field(id_b),
        name=_from_c_field(name_b),
        word=_from_c_field(word_b),
    )


def accept_msg(msg: BadgeMsg) -> bool:
    """Mirror radio.cpp gate: magic + version must match."""
    return msg.magic == K_MSG_MAGIC and msg.version == K_MSG_VERSION
