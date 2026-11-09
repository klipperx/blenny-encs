from __future__ import annotations

import json
from pathlib import Path

from blenny.constants import K_MAX_MLB_GAMES
from blenny.sports import (
    League,
    SportsBoard,
    espn_header_url,
    espn_parts,
    league_name,
    league_period_label,
    league_regulation_periods,
    parse_espn_header,
    parse_mlb_linescore,
    parse_mlb_schedule,
)

FIXTURES = Path(__file__).resolve().parent / "fixtures"


def _load(name: str):
    return json.loads((FIXTURES / name).read_text(encoding="utf-8"))


def test_espn_parts_and_urls():
    assert espn_parts(League.Nhl) == ("hockey", "nhl")
    assert espn_parts(League.Nba) == ("basketball", "nba")
    assert espn_parts(League.Nfl) == ("football", "nfl")
    assert espn_parts(League.Mlb) is None
    assert "sport=hockey&league=nhl" in espn_header_url(League.Nhl)
    assert espn_header_url(League.Nhl, "20261002").endswith("&dates=20261002")
    assert espn_header_url(League.Mlb) == ""


def test_regulation_periods_and_labels():
    assert league_regulation_periods(League.Mlb) == 9
    assert league_regulation_periods(League.Nhl) == 3
    assert league_regulation_periods(League.Nba) == 4
    assert league_regulation_periods(League.Nfl) == 4
    assert [league_period_label(League.Nhl, i) for i in range(5)] == [
        "1",
        "2",
        "3",
        "OT",
        "OT2",
    ]
    assert [league_period_label(League.Nba, i) for i in range(6)] == [
        "1",
        "2",
        "3",
        "4",
        "OT",
        "OT2",
    ]
    assert league_period_label(League.Mlb, 8) == "9"
    assert league_period_label(League.Mlb, 9) == "10"


def test_nhl_list_live_and_final():
    games, board = parse_espn_header(League.Nhl, _load("espn_nhl_header.json"))
    assert board is None
    assert len(games) == 2
    live = games[0]
    assert live.pk == 401770001
    assert live.live is True
    assert live.away == "TOR"
    assert live.home == "MTL"
    assert live.away_score == 2
    assert live.home_score == 3
    assert live.away_name == "Maple Leafs"
    assert live.detail == "2nd 8:12"
    final = games[1]
    assert final.pk == 401770002
    assert final.live is False
    assert final.away_score == 4
    assert final.home_score == 1
    assert final.detail == "Final"


def test_nhl_board_select_by_pk():
    games, board = parse_espn_header(
        League.Nhl, _load("espn_nhl_header.json"), want_pk=401770001
    )
    assert games == []
    assert board is not None
    assert board.ok is True
    assert board.live is True
    assert board.away_runs == 2
    assert board.home_runs == 3
    assert board.inning_count == 3
    assert board.status == "2nd 8:12"
    assert board.away == "TOR"
    assert board.home == "MTL"


def test_nba_and_nfl_parse():
    nba, _ = parse_espn_header(League.Nba, _load("espn_nba_header.json"))
    assert len(nba) == 1
    assert nba[0].away == "LAL"
    assert nba[0].home_score == 104
    assert nba[0].live is True
    nfl, board = parse_espn_header(
        League.Nfl, _load("espn_nfl_header.json"), want_pk=401770201
    )
    assert board is not None
    assert board.inning_count == 4
    assert board.home_runs == 20
    assert league_name(League.Nfl) == "NFL"


def test_espn_caps_at_max_games():
    events = []
    for i in range(K_MAX_MLB_GAMES + 5):
        events.append(
            {
                "id": str(900000 + i),
                "competitors": [
                    {
                        "homeAway": "away",
                        "id": "1",
                        "abbreviation": "AAA",
                        "name": "Away",
                        "score": "0",
                    },
                    {
                        "homeAway": "home",
                        "id": "2",
                        "abbreviation": "HHH",
                        "name": "Home",
                        "score": "0",
                    },
                ],
                "fullStatus": {"type": {"state": "pre", "shortDetail": "Scheduled"}},
            }
        )
    doc = {"sports": [{"leagues": [{"events": events}]}]}
    games, _ = parse_espn_header(League.Nhl, doc)
    assert len(games) == K_MAX_MLB_GAMES


def test_mlb_schedule_and_linescore():
    games = parse_mlb_schedule(_load("mlb_schedule.json"))
    assert len(games) == 2
    assert games[0].live is True  # live sorted first
    assert games[0].pk == 776001
    assert games[0].away == "NYY"
    assert games[0].home_name == "Red Sox"
    assert games[0].detail == "Top 7th"
    assert games[1].live is False
    assert games[1].detail == "Final"

    board = SportsBoard(league=League.Mlb, pk=776001, away_id=147, home_id=111)
    parse_mlb_linescore(_load("mlb_linescore.json"), board)
    assert board.ok is True
    assert board.live is True
    assert board.away_runs == 3
    assert board.home_runs == 2
    assert board.balls == 2
    assert board.strikes == 1
    assert board.outs == 1
    assert board.status == "Top 7th"
    assert board.inning_count == 7
    assert board.innings[0] == [0, 1]
    assert board.innings[6][1] == -1
