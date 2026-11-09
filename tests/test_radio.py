from __future__ import annotations

from blenny.constants import K_MSG_MAGIC, K_MSG_VERSION
from blenny.radio import (
    BADGE_MSG_STRUCT,
    BadgeMsg,
    accept_msg,
    pack_msg,
    unpack_msg,
)


def test_payload_size_contract():
    assert BADGE_MSG_STRUCT.size == 68
    assert BADGE_MSG_STRUCT.size <= 250


def test_pack_roundtrip_and_defaults():
    msg = BadgeMsg(type=1, color=3, id="klipper", name="Shaun", word="ALPHA")
    raw = pack_msg(msg)
    assert len(raw) == 68
    back = unpack_msg(raw)
    assert back.magic == K_MSG_MAGIC
    assert back.version == K_MSG_VERSION
    assert back.type == 1
    assert back.color == 3
    assert back.id == "klipper"
    assert back.name == "Shaun"
    assert back.word == "ALPHA"
    assert accept_msg(back) is True


def test_fields_truncated_to_limits():
    msg = BadgeMsg(
        id="x" * 40,
        name="n" * 40,
        word="w" * 40,
    )
    back = unpack_msg(pack_msg(msg))
    assert len(back.id) == 16
    assert len(back.name) == 32
    assert len(back.word) == 12


def test_reject_wrong_magic_or_version():
    bad_magic = BadgeMsg(magic=0xDEADBEEF)
    bad_ver = BadgeMsg(version=1)
    assert accept_msg(bad_magic) is False
    assert accept_msg(bad_ver) is False
    assert accept_msg(BadgeMsg()) is True
