"""Sports parse mirrors of src/mlb.cpp (ESPN header + MLB schedule)."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Any

from blenny.constants import K_MAX_MLB_GAMES, MLB_TEAM_COLOR_DEFAULT, MLB_TEAM_COLORS


class League(Enum):
    Mlb = 0
    Nhl = 1
    Nba = 2
    Nfl = 3


@dataclass
class SportsGame:
    league: League = League.Mlb
    pk: int = 0
    away_id: int = 0
    home_id: int = 0
    away: str = ""
    home: str = ""
    away_name: str = ""
    home_name: str = ""
    away_score: int = 0
    home_score: int = 0
    away_color: int = 0
    home_color: int = 0
    state: str = ""
    detail: str = ""
    live: bool = False


@dataclass
class SportsBoard:
    league: League = League.Mlb
    pk: int = 0
    away_id: int = 0
    home_id: int = 0
    away: str = ""
    home: str = ""
    away_name: str = ""
    home_name: str = ""
    status: str = ""
    inning: str = ""
    away_runs: int = 0
    home_runs: int = 0
    balls: int = 0
    strikes: int = 0
    outs: int = 0
    away_color: int = 0
    home_color: int = 0
    innings: list[list[int]] = field(default_factory=lambda: [[-1, -1] for _ in range(12)])
    inning_count: int = 0
    live: bool = False
    ok: bool = False


def league_name(league: League) -> str:
    return {
        League.Nhl: "NHL",
        League.Nba: "NBA",
        League.Nfl: "NFL",
        League.Mlb: "MLB",
    }[league]


def league_regulation_periods(league: League) -> int:
    if league == League.Mlb:
        return 9
    if league == League.Nhl:
        return 3
    return 4


def league_period_label(league: League, i: int, count: int = 0) -> str:
    reg = league_regulation_periods(league)
    if league == League.Mlb or i < reg:
        return str(i + 1)
    if i == reg:
        return "OT"
    return f"OT{i - reg + 1}"


def espn_parts(league: League) -> tuple[str, str] | None:
    if league == League.Nhl:
        return "hockey", "nhl"
    if league == League.Nba:
        return "basketball", "nba"
    if league == League.Nfl:
        return "football", "nfl"
    return None


def espn_header_url(league: League, ymd: str = "") -> str:
    parts = espn_parts(league)
    if not parts:
        return ""
    sport, lg = parts
    url = (
        "https://site.web.api.espn.com/apis/v2/scoreboard/header"
        f"?sport={sport}&league={lg}"
    )
    if ymd:
        url += f"&dates={ymd}"
    return url


def _json_int(v: Any) -> int:
    if v is None:
        return 0
    if isinstance(v, bool):
        return int(v)
    if isinstance(v, int):
        return v
    if isinstance(v, float):
        return int(v)
    s = str(v).strip()
    if not s:
        return 0
    try:
        return int(s)
    except ValueError:
        return 0


def _json_long(v: Any) -> int:
    return _json_int(v)


def hex565(hex_str: str | None) -> int:
    if not hex_str:
        return 0
    try:
        v = int(str(hex_str).strip(), 16)
    except ValueError:
        return 0
    r = (v >> 16) & 0xFF
    g = (v >> 8) & 0xFF
    b = v & 0xFF
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def mlb_team_color(team_id: int) -> int:
    return MLB_TEAM_COLORS.get(team_id, MLB_TEAM_COLOR_DEFAULT)


def sports_mark_color(board: SportsBoard, home: bool) -> int:
    c = board.home_color if home else board.away_color
    if c:
        return c
    return mlb_team_color(board.home_id if home else board.away_id)


def parse_espn_header(
    league: League,
    doc: dict[str, Any],
    *,
    want_pk: int = 0,
) -> tuple[list[SportsGame], SportsBoard | None]:
    """Mirror parseEspnHeader: list games, or fill a board when want_pk matches."""
    events = (
        doc.get("sports", [{}])[0]
        .get("leagues", [{}])[0]
        .get("events", [])
    )
    games: list[SportsGame] = []
    board: SportsBoard | None = None

    for ev in events:
        pk = _json_long(ev.get("id"))
        if not pk:
            continue
        row = SportsGame(league=league, pk=pk)
        for c in ev.get("competitors") or []:
            side = c.get("homeAway") or ""
            tid = _json_int(c.get("id"))
            abbr = str(c.get("abbreviation") or "").upper()
            nick = c.get("name") or ""
            if not nick:
                nick = abbr
            score = _json_int(c.get("score"))
            col = hex565(c.get("color"))
            if side == "home":
                row.home_id = tid
                row.home = abbr
                row.home_name = nick
                row.home_score = score
                row.home_color = col
            else:
                row.away_id = tid
                row.away = abbr
                row.away_name = nick
                row.away_score = score
                row.away_color = col

        status_type = (ev.get("fullStatus") or {}).get("type") or {}
        state = status_type.get("state") or ""
        if not state:
            state = ev.get("status") or ""
        detail = status_type.get("shortDetail") or ""
        if not detail:
            detail = ev.get("summary") or ""
        if not detail:
            detail = status_type.get("detail") or ""
        row.state = detail
        row.detail = detail
        row.live = state == "in"

        if want_pk and pk == want_pk:
            board = SportsBoard(
                league=league,
                pk=pk,
                away_id=row.away_id,
                home_id=row.home_id,
                away=row.away,
                home=row.home,
                away_name=row.away_name,
                home_name=row.home_name,
                away_runs=row.away_score,
                home_runs=row.home_score,
                away_color=row.away_color,
                home_color=row.home_color,
                status=detail if detail else league_name(league),
                inning=detail,
                live=row.live,
                inning_count=league_regulation_periods(league),
                ok=True,
            )
            for i in range(12):
                board.innings[i] = [-1, -1]
            return games, board

        if len(games) >= K_MAX_MLB_GAMES:
            break
        games.append(row)

    return games, board


def _abbr_of(team: dict[str, Any]) -> str:
    a = str(team.get("abbreviation") or "").strip().upper()
    if a:
        return a
    name = str(team.get("teamName") or team.get("name") or "").upper()
    if len(name) >= 3:
        return name[:3]
    return name or "???"


def _nick_of(team: dict[str, Any]) -> str:
    nick = str(team.get("teamName") or "").strip()
    if nick:
        return nick
    return str(team.get("name") or "").strip()


def parse_mlb_schedule(doc: dict[str, Any]) -> list[SportsGame]:
    """Mirror mlbLoadGames parse (no network)."""
    games_out: list[SportsGame] = []
    dates = doc.get("dates") or []
    if not dates:
        return games_out
    for g in dates[0].get("games") or []:
        if len(games_out) >= K_MAX_MLB_GAMES:
            break
        pk = _json_long(g.get("gamePk"))
        if not pk:
            continue
        away = (g.get("teams") or {}).get("away", {}).get("team") or {}
        home = (g.get("teams") or {}).get("home", {}).get("team") or {}
        row = SportsGame(
            league=League.Mlb,
            pk=pk,
            away_id=_json_int(away.get("id")),
            home_id=_json_int(home.get("id")),
            away=_abbr_of(away),
            home=_abbr_of(home),
            away_name=_nick_of(away),
            home_name=_nick_of(home),
            away_score=_json_int((g.get("teams") or {}).get("away", {}).get("score")),
            home_score=_json_int((g.get("teams") or {}).get("home", {}).get("score")),
        )
        status = g.get("status") or {}
        abs_state = status.get("abstractGameState") or ""
        row.state = status.get("detailedState") or abs_state
        row.live = (
            abs_state == "Live"
            or "In Progress" in row.state
            or "Manager Challenge" in row.state
        )
        if row.live:
            ls = g.get("linescore") or {}
            inn = ls.get("inningState") or ""
            ord_ = ls.get("currentInningOrdinal") or ""
            detail = inn + ((" " + ord_) if ord_ else "")
            detail = detail.strip()
            row.detail = detail if detail else row.state
        else:
            row.detail = row.state
        games_out.append(row)

    games_out.sort(key=lambda r: (not r.live, r.pk))
    return games_out


def parse_mlb_linescore(doc: dict[str, Any], board: SportsBoard) -> SportsBoard:
    """Mirror mlbLoadBoard parse into an existing board skeleton."""
    teams = doc.get("teams") or {}
    board.away_runs = _json_int((teams.get("away") or {}).get("runs"))
    board.home_runs = _json_int((teams.get("home") or {}).get("runs"))
    board.balls = _json_int(doc.get("balls"))
    board.strikes = _json_int(doc.get("strikes"))
    board.outs = _json_int(doc.get("outs"))
    inn_state = doc.get("inningState") or ""
    ord_ = doc.get("currentInningOrdinal") or ""
    inn = _json_int(doc.get("currentInning"))
    board.inning = inn_state
    if ord_:
        board.inning = (board.inning + " " + ord_).strip()
    elif inn:
        board.inning = (board.inning + " " + str(inn)).strip()
    def runs_or_neg1(side: dict | None) -> int:
        # Mirror ArduinoJson: inning["away"]["runs"] | -1
        if not side or side.get("runs") is None:
            return -1
        return _json_int(side.get("runs"))

    n = 0
    for inning in doc.get("innings") or []:
        if n >= 12:
            break
        board.innings[n] = [
            runs_or_neg1(inning.get("away")),
            runs_or_neg1(inning.get("home")),
        ]
        n += 1
    board.inning_count = n
    scheduled = len(inn_state) == 0 and inn == 0 and n == 0
    board.live = (not scheduled) and len(inn_state) > 0
    if board.inning:
        board.status = board.inning
    elif scheduled:
        board.status = "Pregame"
    else:
        board.status = "MLB"
    board.ok = True
    return board
