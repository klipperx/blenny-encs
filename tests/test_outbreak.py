"""Outbreak: the close-contact rule (a mirror of drain()/exposureTo() in src/outbreak.cpp),
with its constants read from src/outbreak.h, plus the radio frame's magic."""

import re
import struct
from pathlib import Path

SRC = Path(__file__).resolve().parents[1] / "src"
HDR = (SRC / "outbreak.h").read_text()
CPP = (SRC / "outbreak.cpp").read_text()


def const(name):
    return int(re.search(rf"{name}\s*=\s*(-?\d+)", HDR).group(1))


EXPOSE = const("kOutbreakExposeMs")
GAP = const("kOutbreakGapMs")
CLOSE = const("kOutbreakCloseRssi")


class Contact:
    """One other player, fed status packets: (time ms, rssi dBm)."""

    def __init__(self):
        self.rssi = -90
        self.close_since = 0
        self.last_close = 0

    def hear(self, now, rssi):
        self.rssi = rssi if self.rssi == -90 else int((self.rssi * 3 + rssi) / 4)
        if self.rssi >= CLOSE:
            if not self.close_since or now - self.last_close > GAP:
                self.close_since = now
            self.last_close = now

    def exposure(self, now):
        if not self.close_since or now - self.last_close > GAP:
            return 0
        return self.last_close - self.close_since


def run(packets):
    c = Contact()
    for t, r in packets:
        c.hear(t, r)
    return c


def every_second(start, end, rssi):
    return [(t, rssi) for t in range(start, end + 1, 1000)]


def test_constants_are_the_agreed_rules():
    assert EXPOSE == 30000 and GAP == 20000
    assert const("kOutbreakImmuneMs") == 3600000
    assert const("kOutbreakSpreader") == 5


def test_walking_past_does_not_infect():
    c = run(every_second(1000, 6000, -50))
    assert c.exposure(6000) < EXPOSE


def test_thirty_seconds_close_infects():
    c = run(every_second(1000, 31000, -50))
    assert c.exposure(31000) >= EXPOSE


def test_far_away_never_counts():
    c = run(every_second(1000, 120000, -80))
    assert c.exposure(120000) == 0


def test_short_break_keeps_the_clock_running():
    pkts = every_second(1000, 16000, -50) + every_second(30000, 31000, -50)  # 14 s gap
    assert run(pkts).exposure(31000) >= EXPOSE


def test_long_break_restarts_the_clock():
    pkts = every_second(1000, 16000, -50) + every_second(40000, 50000, -50)  # 24 s gap
    c = run(pkts)
    assert c.exposure(50000) == 10000


def test_mirror_matches_the_firmware_rule():
    # If the firmware's rule changes, this mirror (and the tests above) must change with it.
    assert "now - c->last_close > kOutbreakGapMs" in CPP
    assert "c->rssi >= kOutbreakCloseRssi" in CPP
    assert "(c->rssi * 3 + local[i].rssi) / 4" in CPP


def test_obrk_magic_on_the_wire():
    m = int(re.search(r"kObMagic\s*=\s*(0x[0-9A-Fa-f]+)", CPP).group(1), 16)
    assert struct.pack("<I", m) == b"OBRK"


def test_survival_clock_runs_only_when_not_infected_and_not_a_medic():
    # Healthy and immune players score; infected players and medics don't.
    body = CPP[CPP.index("// Survival clock"):]
    body = body[:body.index("g_tick_at = now;")]
    assert "g_state == OB_HEALTHY || g_state == OB_IMMUNE" in body
    assert "OB_INFECTED" not in body and "OB_MEDIC" not in body


def test_survival_clock_resets_with_the_round():
    sync = CPP[CPP.index("static void syncRound()"):CPP.index("void outbreakLoad()")]
    assert "g_alive_ms = 0;" in sync
