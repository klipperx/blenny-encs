"""The "I know you" radio frame: its magic must not collide with the other ESP-NOW protocols,
and it must fit an ESP-NOW frame."""

import re
import struct
from pathlib import Path

SRC = Path(__file__).resolve().parents[1] / "src"


def _magics():
    found = {}
    for f in SRC.glob("*.[ch]*"):
        for name, val in re.findall(r"k(\w*Magic)\s*=\s*(0x[0-9A-Fa-f]+)", f.read_text()):
            found[name] = int(val, 16)
    return found


def test_know_magic_reads_KNOW_on_the_wire():
    m = _magics()
    assert struct.pack("<I", m["KnowMagic"]) == b"KNOW"


def test_radio_magics_are_unique():
    m = _magics()
    vals = list(m.values())
    assert len(vals) == len(set(vals)), m


def test_know_frame_fits_esp_now():
    id_len = int(re.search(r"kIdLen\s*=\s*(\d+)", (SRC / "config.h").read_text()).group(1))
    assert 4 + 2 * id_len <= 250
