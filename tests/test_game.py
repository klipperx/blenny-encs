from __future__ import annotations

import json
import random
from pathlib import Path

from blenny.game import Attendee, build_quiz

FIXTURES = Path(__file__).resolve().parent / "fixtures"


def _roster() -> tuple[list[Attendee], list[str]]:
    people = [
        Attendee(**a)
        for a in json.loads((FIXTURES / "attendees.json").read_text())["attendees"]
    ]
    facts = json.loads((FIXTURES / "generated_facts.json").read_text())
    return people, facts


def test_build_quiz_requires_me_and_target():
    people, facts = _roster()
    assert build_quiz(people, facts, "missing", "ada", random.Random(1)) is None
    assert build_quiz(people, facts, "klipper", "missing", random.Random(1)) is None


def test_build_quiz_truth_among_distinct_options():
    people, facts = _roster()
    rng = random.Random(42)
    quiz = build_quiz(people, facts, "klipper", "ada", rng)
    assert quiz is not None
    assert quiz.target_id == "ada"
    assert quiz.target_name == "Ada Lovelace"
    assert len(quiz.options) == 3
    assert len(set(o.casefold() for o in quiz.options)) == 3
    truth = next(a.truth for a in people if a.id == "ada")
    assert quiz.options[quiz.truth_index] == truth
    assert truth in quiz.options


def test_build_quiz_deterministic_with_seed():
    people, facts = _roster()
    a = build_quiz(people, facts, "klipper", "grace", random.Random(7))
    b = build_quiz(people, facts, "klipper", "grace", random.Random(7))
    assert a is not None and b is not None
    assert a.options == b.options
    assert a.truth_index == b.truth_index


def test_build_quiz_tiny_roster_fallback():
    people = [
        Attendee("me", "Me", "Fact A"),
        Attendee("them", "Them", "Fact B"),
    ]
    facts = ["Fact C", "Fact D", "Fact E"]
    quiz = build_quiz(people, facts, "me", "them", random.Random(3))
    assert quiz is not None
    assert quiz.options[quiz.truth_index] == "Fact B"
    assert len(set(quiz.options)) == 3
