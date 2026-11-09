"""Hide and Seek: the "found" rule (a mirror of the ping handling in src/hideseek.cpp), with its
constants read from src/hideseek.h, plus the radio frame's magic and size."""

import re
import struct
from pathlib import Path

SRC = Path(__file__).resolve().parents[1] / "src"
HDR = (SRC / "hideseek.h").read_text()
CPP = (SRC / "hideseek.cpp").read_text()


def const(name):
    return int(re.search(rf"{name}\s*=\s*(-?\d+)", HDR).group(1))


CLOSE = const("kHideSeekCloseRssi")
FOUND = const("kHideSeekFoundMs")
GAP = const("kHideSeekGapMs")


class Seeker:
    """A seeker's view of it, fed it's pings: (time ms, rssi dBm)."""

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

    def progress(self, now):
        if not self.close_since or now - self.last_close > GAP:
            return 0.0
        return min(1.0, (self.last_close - self.close_since) / FOUND)


def run(pings):
    s = Seeker()
    for t, r in pings:
        s.hear(t, r)
    return s


def every_second(start, end, rssi):
    return [(t, rssi) for t in range(start, end + 1, 1000)]


def test_constants_are_the_agreed_rules():
    assert CLOSE == -45 and FOUND == 10000


def test_ten_seconds_on_top_of_them_finds_them():
    assert run(every_second(1000, 11000, -40)).progress(11000) >= 1.0


def test_warm_is_not_found():
    assert run(every_second(1000, 60000, -55)).progress(60000) == 0.0


def test_brushing_past_is_not_found():
    assert run(every_second(1000, 4000, -40)).progress(4000) < 1.0


def test_stepping_away_restarts_the_clock():
    pings = every_second(1000, 6000, -40) + every_second(12000, 15000, -40)  # 6 s gap
    assert run(pings).progress(15000) < 1.0


def test_one_dropped_ping_keeps_the_clock():
    pings = [p for p in every_second(1000, 12000, -40) if p[0] != 6000]
    assert run(pings).progress(12000) >= 1.0


def test_mirror_matches_the_firmware_rule():
    # If the firmware's rule changes, this mirror (and the tests above) must change with it.
    assert "now - g_last_close > kHideSeekGapMs" in CPP
    assert "g_my_rssi >= kHideSeekCloseRssi" in CPP
    assert "(g_my_rssi * 3 + local[i].rssi) / 4" in CPP


def test_hsek_magic_on_the_wire():
    m = int(re.search(r"kHsMagic\s*=\s*(0x[0-9A-Fa-f]+)", CPP).group(1), 16)
    assert struct.pack("<I", m) == b"HSEK"


def test_frame_fits_esp_now():
    # magic, version, kind, found, rssi, round, id[16], by[16]
    assert struct.calcsize("<IBBBbI16s16s") <= 250


def test_only_the_rosters_it_can_ping():
    body = CPP[CPP.index("if (p.kind == HS_PING)"):CPP.index("if (p.kind == HS_READ")]
    assert "strncmp(p.id, it, 16) != 0" in body


def test_heat_words_climb_with_signal():
    order = ["Cold", "Cool", "Warm", "Hot", "ON TOP OF THEM"]
    found = [CPP.index(f'"{w}"') for w in order]
    assert found == sorted(found, reverse=True)  # checked hottest first


# ---- Radar: the distance model and the layout solver (mirrors solve() in hideseek.cpp) ----

import math

AT1M = const("kHideSeekRssiAt1m")
PL = float(re.search(r"kHideSeekPathLoss\s*=\s*([\d.]+)f", HDR).group(1))
MAXM = float(re.search(r"kHideSeekMaxMetres\s*=\s*([\d.]+)f", HDR).group(1))


def metres(rssi):
    d = 10 ** ((AT1M - rssi) / (10 * PL))
    return min(max(d, 0.5), MAXM)


def rssi_at(d):
    return round(AT1M - 10 * PL * math.log10(d))


def solve(pos, links, iters):
    """pos: {name: [x, y]}, links: {(a, b): metres}; me is pos['me'], kept at the origin."""
    names = list(pos)
    for _ in range(iters):
        for i, a in enumerate(names):
            for b in names[i + 1:]:
                dx, dy = pos[b][0] - pos[a][0], pos[b][1] - pos[a][1]
                cur = math.hypot(dx, dy)
                if cur < 0.01:
                    dx, dy, cur = 0.01, 0.0, 0.01
                want = links.get((a, b)) or links.get((b, a))
                if want:
                    k = 0.25 * min(1.0, 1.0 / want) * (cur - want) / cur
                elif cur < 1.5:
                    k = -0.05 * (1.5 - cur) / cur
                else:
                    continue
                pos[a][0] += k * dx; pos[a][1] += k * dy
                pos[b][0] -= k * dx; pos[b][1] -= k * dy
        ox, oy = pos["me"]
        for n in names:
            pos[n][0] -= ox; pos[n][1] -= oy
    return pos


def test_distance_model_round_trips():
    for d in (1, 3, 8, 20):
        assert abs(metres(rssi_at(d)) - d) / d < 0.15
    assert metres(-10) == 0.5 and metres(-120) == MAXM


def test_solver_recovers_a_room_layout():
    # Three seekers and it in a 12 m room; every pair hears every other.
    truth = {"me": (0, 0), "matt": (8, 0), "kev": (0, 9), "it": (6, 6)}
    links = {}
    names = list(truth)
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            d = math.dist(truth[a], truth[b])
            links[(a, b)] = metres(rssi_at(d))
    # Start where the firmware does: on a 5 m circle at hashed angles.
    start = {"me": [0.0, 0.0], "matt": [5.0, 0.3], "kev": [-1.0, 4.9], "it": [-3.5, -3.5]}
    pos = solve(start, links, 12 * 4 * 30)  # ~30 s of solving
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            got = math.dist(pos[a], pos[b])
            want = math.dist(truth[a], truth[b])
            assert abs(got - want) < 0.2 * want + 0.5, (a, b, got, want)


def test_radar_tail_fits_and_old_badges_ignore_it():
    tail_max = int(re.search(r"kTailMax\s*=\s*(\d+)", CPP).group(1))
    assert struct.calcsize("<IBBBbI16s16s") + 5 + 2 * tail_max <= 250
    # 3.1.0 badges accept any frame at least as long as the header and read only the header.
    assert "if (len < (int)sizeof(HsPkt))" in CPP
    assert "kHsVersion = 1;" in CPP


def test_solver_mirror_matches_the_firmware():
    assert "k = 0.25f * w * (cur - want) / cur;" in CPP
    assert "k = -0.05f * (1.5f - cur) / cur;" in CPP
    assert "for (int it = 0; it < 12; it++)" in CPP
