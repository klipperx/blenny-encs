"""Find Friends glitch hints: what a nearby badge reveals, and when."""

from blenny.display import GLITCH_HINT, GLITCH_NOISE, GLITCH_NONE, glitch_stage, id_hash, name_hint


def test_far_shows_initial_and_length():
    assert name_hint("Grace Hopper", 0) == "G____"
    assert name_hint("Grace Hopper", 19) == "G____"


def test_closer_shows_three_letters():
    assert name_hint("Grace Hopper", 20) == "GRA__"
    assert name_hint("Grace Hopper", 39) == "GRA__"


def test_close_shows_first_name_and_last_initial():
    assert name_hint("Grace Hopper", 40) == "GRACE H."
    assert name_hint("Grace Hopper", 60) == "GRACE H."


def test_single_names_and_short_names():
    assert name_hint("Linus", 60) == "LINUS"
    assert name_hint("Linus", 0) == "L____"
    assert name_hint("Al Bo", 25) == "AL"  # shorter than the reveal: no padding past the name


def test_blank_name_is_a_question_mark():
    assert name_hint("", 50) == "?"
    assert name_hint("   ", 0) == "?"


def test_glitch_window_is_600ms_per_period():
    for id_ in ("ghopper", "klipper", "aturing", "linus"):
        period = 3000 + id_hash(id_) % 3000
        assert 3000 <= period < 6000
        stages = [glitch_stage(id_, t) for t in range(period)]  # exactly one cycle
        assert sum(1 for s in stages if s != GLITCH_NONE) == 600
        assert sum(1 for s in stages if s == GLITCH_HINT) == 300
        assert sum(1 for s in stages if s == GLITCH_NOISE) == 300


def test_badges_glitch_out_of_step():
    a = [glitch_stage("ghopper", t) for t in range(0, 6000, 10)]
    b = [glitch_stage("aturing", t) for t in range(0, 6000, 10)]
    assert a != b
