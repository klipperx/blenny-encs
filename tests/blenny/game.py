"""Quiz builder mirror of src/game.cpp with seeded RNG."""

from __future__ import annotations

from dataclasses import dataclass, field
import random
from typing import Sequence


@dataclass
class Attendee:
    id: str
    name: str
    truth: str


@dataclass
class Quiz:
    target_id: str = ""
    target_name: str = ""
    options: list[str] = field(default_factory=lambda: ["", "", ""])
    truth_index: int = 0


def same_fact(a: str, b: str) -> bool:
    return a.casefold() == b.casefold()


def shuffle3(opts: list[str], truth_index: int, rng: random.Random) -> int:
    for i in range(2, 0, -1):
        j = rng.randrange(i + 1)
        if i == j:
            continue
        opts[i], opts[j] = opts[j], opts[i]
        if truth_index == i:
            truth_index = j
        elif truth_index == j:
            truth_index = i
    return truth_index


def build_quiz(
    people: Sequence[Attendee],
    facts: Sequence[str],
    my_id: str,
    target_id: str,
    rng: random.Random | None = None,
) -> Quiz | None:
    """Mirror gameBuildQuiz. Returns None when firmware would return false."""
    rng = rng or random.Random()
    by_id = {a.id: a for a in people}
    target = by_id.get(target_id)
    me = by_id.get(my_id)
    if not target or not me:
        return None

    stolen = ""
    if len(people) >= 3:
        for _ in range(24):
            a = people[rng.randrange(len(people))]
            if a.id == me.id or a.id == target.id:
                continue
            if same_fact(a.truth, target.truth):
                continue
            stolen = a.truth
            break
    if not stolen:
        for a in people:
            if a.id != me.id and a.id != target.id and not same_fact(a.truth, target.truth):
                stolen = a.truth
                break

    generated = ""
    if facts:
        for _ in range(40):
            f = facts[rng.randrange(len(facts))]
            if not same_fact(f, target.truth) and not same_fact(f, stolen):
                generated = f
                break
        if not generated:
            generated = facts[0]
    if not stolen:
        for f in facts:
            if not same_fact(f, target.truth) and not same_fact(f, generated):
                stolen = f
                break
    if not stolen or not generated:
        return None

    quiz = Quiz(
        target_id=target.id,
        target_name=target.name,
        options=[target.truth, stolen, generated],
        truth_index=0,
    )
    quiz.truth_index = shuffle3(quiz.options, quiz.truth_index, rng)
    return quiz
