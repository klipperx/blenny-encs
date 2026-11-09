from __future__ import annotations

from blenny.constants import K_FIRMWARE_VERSION, K_LEAGUE_NAMES
from blenny.display import (
    board_period_labels,
    league_picker_names,
    mark_color,
    splash_firmware_line,
    tag_name,
)
from blenny.sports import League, SportsBoard, mlb_team_color


def test_tag_name_first_only_upper():
    assert tag_name("Shaun Price") == "SHAUN"
    assert tag_name("grace") == "GRACE"
    assert tag_name("") == "?"
    assert tag_name(None) == "?"


def test_splash_firmware_revision():
    assert splash_firmware_line() == f"Firmware Revision : {K_FIRMWARE_VERSION}"
    assert K_FIRMWARE_VERSION == "1.3"
    assert splash_firmware_line("2.0") == "Firmware Revision : 2.0"


def test_league_picker_order():
    assert league_picker_names() == ("MLB", "NHL", "NBA", "NFL")
    assert league_picker_names() == K_LEAGUE_NAMES


def test_board_period_labels_per_league():
    assert board_period_labels(League.Nhl) == ["1", "2", "3"]
    assert board_period_labels(League.Nba) == ["1", "2", "3", "4"]
    assert board_period_labels(League.Nfl) == ["1", "2", "3", "4"]
    assert board_period_labels(League.Mlb) == [str(i) for i in range(1, 10)]


def test_sports_mark_color_prefers_board_then_palette():
    board = SportsBoard(home_color=0x1234, away_color=0, home_id=147, away_id=111)
    assert mark_color(board, home=True) == 0x1234
    assert mark_color(board, home=False) == mlb_team_color(111)
    bare = SportsBoard(home_id=147, away_id=111)
    assert mark_color(bare, home=True) == mlb_team_color(147)
