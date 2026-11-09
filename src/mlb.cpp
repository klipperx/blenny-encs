#include "mlb.h"

#include "net.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static String todayDate() {
  struct tm t = {};
  if (getLocalTime(&t, 200)) {
    char buf[16];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
    return String(buf);
  }
  return "";
}

static String abbrOf(JsonVariant team) {
  String a = team["abbreviation"] | "";
  a.trim();
  if (a.length()) {
    a.toUpperCase();
    return a;
  }
  String name = team["teamName"] | "";
  if (!name.length()) {
    name = team["name"] | "";
  }
  name.toUpperCase();
  if (name.length() >= 3) {
    return name.substring(0, 3);
  }
  return name.length() ? name : "???";
}

static String nickOf(JsonVariant team) {
  String nick = team["teamName"] | "";
  nick.trim();
  if (nick.length()) {
    return nick;
  }
  String name = team["name"] | "";
  name.trim();
  return name;
}

uint16_t mlbTeamColor(int team_id) {
  switch (team_id) {
    case 108: return 0xB9E4;  // Angels
    case 109: return 0xA1C6;  // D-backs
    case 110: return 0xA1C6;  // Orioles
    case 111: return 0xC800;  // Red Sox
    case 112: return 0x0393;  // Cubs
    case 113: return 0xC800;  // Reds
    case 114: return 0xC2C0;  // Guardians
    case 115: return 0x5ACB;  // Rockies
    case 116: return 0x0393;  // Tigers
    case 117: return 0xEC80;  // Astros
    case 118: return 0x0393;  // Royals
    case 119: return 0x0393;  // Dodgers
    case 120: return 0xC800;  // Nationals
    case 121: return 0xEC80;  // Mets
    case 133: return 0x04C7;  // Athletics
    case 134: return 0xEC80;  // Pirates
    case 135: return 0x039F;  // Padres
    case 136: return 0x0393;  // Mariners
    case 137: return 0xEC80;  // Giants
    case 138: return 0xC800;  // Cardinals
    case 139: return 0x0393;  // Rays
    case 140: return 0x0393;  // Rangers
    case 141: return 0x0393;  // Blue Jays
    case 142: return 0xC000;  // Twins
    case 143: return 0xC800;  // Phillies
    case 144: return 0xC800;  // Braves
    case 145: return 0x6B4D;  // White Sox
    case 146: return 0x041F;  // Marlins
    case 147: return 0x1A6E;  // Yankees
    case 158: return 0x03E0;  // Brewers
    default: return 0x7BEF;
  }
}

static void sortGames(std::vector<MlbGame> &games) {
  std::sort(games.begin(), games.end(), [](const MlbGame &a, const MlbGame &b) {
    if (a.live != b.live) {
      return a.live;
    }
    return a.pk < b.pk;
  });
}

bool mlbLoadGames(std::vector<MlbGame> *out, String *err) {
  if (!out) {
    return false;
  }
  out->clear();
  String date = todayDate();
  String url = "https://statsapi.mlb.com/api/v1/schedule?sportId=1&hydrate=team,linescore";
  if (date.length()) {
    url += "&date=" + date;
  }
  url += "&fields=dates,games,gamePk,status,abstractGameState,detailedState,teams,away,home,"
         "team,id,abbreviation,name,teamName,score,linescore,currentInning,inningState,"
         "currentInningOrdinal";
  String body = httpsGet(url.c_str());
  if (!body.length()) {
    if (err) {
      *err = "Couldn't reach MLB";
    }
    return false;
  }

  JsonDocument filter;
  filter["dates"][0]["games"][0]["gamePk"] = true;
  filter["dates"][0]["games"][0]["status"]["abstractGameState"] = true;
  filter["dates"][0]["games"][0]["status"]["detailedState"] = true;
  filter["dates"][0]["games"][0]["teams"]["away"]["team"]["id"] = true;
  filter["dates"][0]["games"][0]["teams"]["away"]["team"]["abbreviation"] = true;
  filter["dates"][0]["games"][0]["teams"]["away"]["team"]["name"] = true;
  filter["dates"][0]["games"][0]["teams"]["away"]["team"]["teamName"] = true;
  filter["dates"][0]["games"][0]["teams"]["away"]["score"] = true;
  filter["dates"][0]["games"][0]["teams"]["home"]["team"]["id"] = true;
  filter["dates"][0]["games"][0]["teams"]["home"]["team"]["abbreviation"] = true;
  filter["dates"][0]["games"][0]["teams"]["home"]["team"]["name"] = true;
  filter["dates"][0]["games"][0]["teams"]["home"]["team"]["teamName"] = true;
  filter["dates"][0]["games"][0]["teams"]["home"]["score"] = true;
  filter["dates"][0]["games"][0]["linescore"]["currentInning"] = true;
  filter["dates"][0]["games"][0]["linescore"]["inningState"] = true;
  filter["dates"][0]["games"][0]["linescore"]["currentInningOrdinal"] = true;

  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  body = "";
  if (e) {
    if (err) {
      *err = "Bad MLB data";
    }
    return false;
  }

  JsonArray games = doc["dates"][0]["games"].as<JsonArray>();
  for (JsonObject g : games) {
    if (out->size() >= kMaxMlbGames) {
      break;
    }
    MlbGame row;
    row.pk = g["gamePk"] | 0;
    if (!row.pk) {
      continue;
    }
    JsonVariant away = g["teams"]["away"]["team"];
    JsonVariant home = g["teams"]["home"]["team"];
    row.away_id = away["id"] | 0;
    row.home_id = home["id"] | 0;
    row.away = abbrOf(away);
    row.home = abbrOf(home);
    row.away_name = nickOf(away);
    row.home_name = nickOf(home);
    row.league = League::Mlb;
    row.away_score = g["teams"]["away"]["score"] | 0;
    row.home_score = g["teams"]["home"]["score"] | 0;
    String abs = g["status"]["abstractGameState"] | "";
    row.state = g["status"]["detailedState"] | abs;
    row.live = abs == "Live" || row.state.indexOf("In Progress") >= 0 ||
               row.state.indexOf("Manager Challenge") >= 0;
    if (row.live) {
      String inn = g["linescore"]["inningState"] | "";
      String ord = g["linescore"]["currentInningOrdinal"] | "";
      row.detail = inn + (ord.length() ? (" " + ord) : "");
      row.detail.trim();
      if (!row.detail.length()) {
        row.detail = row.state;
      }
    } else {
      row.detail = row.state;
    }
    out->push_back(std::move(row));
  }
  sortGames(*out);
  if (err) {
    err->clear();
  }
  return true;
}

bool mlbLoadBoard(MlbBoard *out) {
  if (!out || !out->pk) {
    return false;
  }
  char url[96];
  snprintf(url, sizeof(url), "https://statsapi.mlb.com/api/v1/game/%ld/linescore", out->pk);
  String body = httpsGet(url);
  if (!body.length()) {
    return false;
  }

  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, body);
  body = "";
  if (e) {
    return false;
  }

  out->away_runs = doc["teams"]["away"]["runs"] | 0;
  out->home_runs = doc["teams"]["home"]["runs"] | 0;
  out->balls = doc["balls"] | 0;
  out->strikes = doc["strikes"] | 0;
  out->outs = doc["outs"] | 0;
  String inn_state = doc["inningState"] | "";
  String ord = doc["currentInningOrdinal"] | "";
  int inn = doc["currentInning"] | 0;
  out->inning = inn_state;
  if (ord.length()) {
    if (out->inning.length()) {
      out->inning += " ";
    }
    out->inning += ord;
  } else if (inn) {
    if (out->inning.length()) {
      out->inning += " ";
    }
    out->inning += String(inn);
  }
  JsonArray innings = doc["innings"].as<JsonArray>();
  int n = 0;
  for (JsonObject inning : innings) {
    if (n >= 12) {
      break;
    }
    out->innings[n][0] = inning["away"]["runs"] | -1;
    out->innings[n][1] = inning["home"]["runs"] | -1;
    n++;
  }
  out->inning_count = n;
  bool scheduled = inn_state.length() == 0 && inn == 0 && n == 0;
  out->live = !scheduled && inn_state.length() > 0;
  out->status = out->inning.length() ? out->inning : (scheduled ? String("Pregame") : String("MLB"));
  out->ok = true;
  return true;
}

static void loadOneLogo(int team_id, std::vector<uint8_t> *dst) {
  if (!dst || !team_id || !dst->empty()) {
    return;
  }
  char url[96];
  snprintf(url, sizeof(url), "https://midfield.mlbstatic.com/v1/team/%d/spots/48", team_id);
  httpsGetBin(url, dst);
}

void mlbLoadLogos(MlbBoard *out) {
  if (!out) {
    return;
  }
  loadOneLogo(out->away_id, &out->away_logo);
  loadOneLogo(out->home_id, &out->home_logo);
}

const char *leagueName(League l) {
  switch (l) {
    case League::Nhl:
      return "NHL";
    case League::Nba:
      return "NBA";
    case League::Nfl:
      return "NFL";
    default:
      return "MLB";
  }
}

static uint16_t hex565(const char *hex) {
  if (!hex || !hex[0]) {
    return 0;
  }
  unsigned v = strtoul(hex, nullptr, 16);
  int r = (v >> 16) & 0xFF;
  int g = (v >> 8) & 0xFF;
  int b = v & 0xFF;
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static bool espnParts(League l, const char **sport, const char **lg) {
  switch (l) {
    case League::Nhl:
      *sport = "hockey";
      *lg = "nhl";
      return true;
    case League::Nba:
      *sport = "basketball";
      *lg = "nba";
      return true;
    case League::Nfl:
      *sport = "football";
      *lg = "nfl";
      return true;
    default:
      return false;
  }
}

static int jsonInt(JsonVariant v) {
  if (v.isNull()) {
    return 0;
  }
  if (v.is<int>() || v.is<long>() || v.is<unsigned int>() || v.is<unsigned long>()) {
    return v.as<int>();
  }
  if (v.is<float>() || v.is<double>()) {
    return (int)v.as<double>();
  }
  const char *s = v.as<const char *>();
  if (!s || !s[0]) {
    return 0;
  }
  return atoi(s);
}

static long jsonLong(JsonVariant v) {
  if (v.isNull()) {
    return 0;
  }
  if (v.is<long>() || v.is<int>() || v.is<unsigned long>() || v.is<unsigned int>()) {
    return v.as<long>();
  }
  const char *s = v.as<const char *>();
  if (!s || !s[0]) {
    return 0;
  }
  return atol(s);
}

static const char *espnLogoSport(League l) {
  switch (l) {
    case League::Nhl:
      return "nhl";
    case League::Nba:
      return "nba";
    case League::Nfl:
      return "nfl";
    default:
      return nullptr;
  }
}

int leagueRegulationPeriods(League l) {
  if (l == League::Mlb) {
    return 9;
  }
  if (l == League::Nhl) {
    return 3;
  }
  return 4;
}

void leaguePeriodLabel(League l, int i, int count, char *out, size_t n) {
  if (!out || !n) {
    return;
  }
  int reg = leagueRegulationPeriods(l);
  if (l == League::Mlb || i < reg) {
    snprintf(out, n, "%d", i + 1);
    return;
  }
  if (i == reg) {
    snprintf(out, n, "%s", l == League::Nhl ? "OT" : "OT");
    return;
  }
  snprintf(out, n, "OT%d", i - reg + 1);
  (void)count;
}

static String espnDate() {
  struct tm t = {};
  if (!getLocalTime(&t, 200)) {
    return "";
  }
  char buf[16];
  strftime(buf, sizeof(buf), "%Y%m%d", &t);
  return String(buf);
}

static String espnYmd(const char *iso) {
  // "2026-09-19T07:00Z" -> "20260919"
  if (!iso || strlen(iso) < 10) {
    return "";
  }
  String out;
  out.reserve(8);
  for (int i = 0; i < 10; i++) {
    if (iso[i] >= '0' && iso[i] <= '9') {
      out += iso[i];
    }
  }
  return out.length() == 8 ? out : String("");
}

static void espnFilter(JsonDocument &filter) {
  filter["sports"][0]["leagues"][0]["events"][0]["id"] = true;
  filter["sports"][0]["leagues"][0]["events"][0]["status"] = true;
  filter["sports"][0]["leagues"][0]["events"][0]["summary"] = true;
  filter["sports"][0]["leagues"][0]["events"][0]["fullStatus"]["type"]["state"] = true;
  filter["sports"][0]["leagues"][0]["events"][0]["fullStatus"]["type"]["shortDetail"] = true;
  filter["sports"][0]["leagues"][0]["events"][0]["fullStatus"]["type"]["detail"] = true;
  filter["sports"][0]["leagues"][0]["events"][0]["competitors"][0]["homeAway"] = true;
  filter["sports"][0]["leagues"][0]["events"][0]["competitors"][0]["id"] = true;
  filter["sports"][0]["leagues"][0]["events"][0]["competitors"][0]["abbreviation"] = true;
  filter["sports"][0]["leagues"][0]["events"][0]["competitors"][0]["name"] = true;
  filter["sports"][0]["leagues"][0]["events"][0]["competitors"][0]["color"] = true;
  filter["sports"][0]["leagues"][0]["events"][0]["competitors"][0]["score"] = true;
  filter["sports"][0]["leagues"][0]["smartdates"][0] = true;
}

static bool parseEspnHeader(League league, JsonDocument &doc, std::vector<MlbGame> *out,
                            long want_pk, MlbBoard *board) {
  JsonArray events = doc["sports"][0]["leagues"][0]["events"].as<JsonArray>();
  for (JsonObject ev : events) {
    long pk = jsonLong(ev["id"]);
    if (!pk) {
      continue;
    }
    MlbGame row;
    row.league = league;
    row.pk = pk;
    for (JsonObject c : ev["competitors"].as<JsonArray>()) {
      String side = c["homeAway"] | "";
      int id = jsonInt(c["id"]);
      String abbr = c["abbreviation"] | "";
      abbr.toUpperCase();
      String nick = c["name"] | "";
      if (!nick.length()) {
        nick = abbr;
      }
      int score = jsonInt(c["score"]);
      uint16_t col = hex565(c["color"] | "");
      if (side == "home") {
        row.home_id = id;
        row.home = abbr;
        row.home_name = nick;
        row.home_score = score;
        row.home_color = col;
      } else {
        row.away_id = id;
        row.away = abbr;
        row.away_name = nick;
        row.away_score = score;
        row.away_color = col;
      }
    }
    String state = ev["fullStatus"]["type"]["state"] | "";
    if (!state.length()) {
      state = ev["status"] | "";
    }
    String detail = ev["fullStatus"]["type"]["shortDetail"] | "";
    if (!detail.length()) {
      detail = ev["summary"] | "";
    }
    if (!detail.length()) {
      detail = ev["fullStatus"]["type"]["detail"] | "";
    }
    row.state = detail;
    row.detail = detail;
    row.live = state == "in";
    if (board && want_pk && pk == want_pk) {
      board->league = league;
      board->pk = pk;
      board->away_id = row.away_id;
      board->home_id = row.home_id;
      board->away = row.away;
      board->home = row.home;
      board->away_name = row.away_name;
      board->home_name = row.home_name;
      board->away_runs = row.away_score;
      board->home_runs = row.home_score;
      board->away_color = row.away_color;
      board->home_color = row.home_color;
      board->status = detail.length() ? detail : String(leagueName(league));
      board->inning = detail;
      board->live = row.live;
      int n = leagueRegulationPeriods(league);
      board->inning_count = n;
      for (int i = 0; i < 12; i++) {
        board->innings[i][0] = -1;
        board->innings[i][1] = -1;
      }
      board->ok = true;
      return true;
    }
    if (out) {
      if (out->size() >= kMaxMlbGames) {
        break;
      }
      out->push_back(std::move(row));
    }
  }
  return board ? board->ok : true;
}

static String espnHeaderUrl(League league, const String &ymd) {
  const char *sport = nullptr;
  const char *lg = nullptr;
  if (!espnParts(league, &sport, &lg)) {
    return "";
  }
  String url = String("https://site.web.api.espn.com/apis/v2/scoreboard/header?sport=") + sport +
               "&league=" + lg;
  if (ymd.length()) {
    url += "&dates=" + ymd;
  }
  return url;
}

static bool espnParseBody(League league, const String &body, JsonDocument *doc, String *err) {
  JsonDocument filter;
  espnFilter(filter);
  // ESPN buries goalie/athlete link trees ~15 levels down. The default
  // ArduinoJson limit is 10, so a live NHL board fails as "Bad NHL data"
  // even though the bytes are valid JSON. Skipping those fields still
  // walks them, so the limit has to cover the whole document.
  DeserializationError e =
      deserializeJson(*doc, body, DeserializationOption::Filter(filter),
                      DeserializationOption::NestingLimit(20));
  if (e) {
    if (err) {
      *err = String("Bad ") + leagueName(league) + " data";
    }
    return false;
  }
  return true;
}

static bool espnFetch(League league, JsonDocument *doc, String *err) {
  if (!doc) {
    return false;
  }
  String date = espnDate();
  String url = espnHeaderUrl(league, date);
  if (!url.length()) {
    return false;
  }
  String body = httpsGet(url.c_str());
  if (!body.length()) {
    if (err) {
      *err = String("Couldn't reach ") + leagueName(league);
    }
    return false;
  }
  if (!espnParseBody(league, body, doc, err)) {
    return false;
  }
  body = "";
  JsonArray events = (*doc)["sports"][0]["leagues"][0]["events"].as<JsonArray>();
  if (!events.size()) {
    JsonArray smart = (*doc)["sports"][0]["leagues"][0]["smartdates"].as<JsonArray>();
    String next = espnYmd(smart[0] | "");
    if (next.length() && next != date) {
      url = espnHeaderUrl(league, next);
      body = httpsGet(url.c_str());
      if (body.length()) {
        doc->clear();
        if (!espnParseBody(league, body, doc, nullptr)) {
          doc->clear();
        }
      }
    }
  }
  return true;
}

static void applyLinescoreItems(JsonDocument &doc, MlbBoard *board, int which) {
  int maxp = board->inning_count;
  int idx = 0;
  for (JsonObject ls : doc["items"].as<JsonArray>()) {
    int period = jsonInt(ls["period"]);
    if (period <= 0) {
      period = idx + 1;
    }
    if (period < 1 || period > 12) {
      idx++;
      continue;
    }
    JsonVariant disp = ls["displayValue"];
    board->innings[period - 1][which] = disp.isNull() ? jsonInt(ls["value"]) : jsonInt(disp);
    if (period > maxp) {
      maxp = period;
    }
    idx++;
  }
  if (maxp > 12) {
    maxp = 12;
  }
  if (maxp > 0) {
    board->inning_count = maxp;
  }
}

// The header feed has the game total only. Each club's period goals are a few hundred bytes.
static void fillOneLinescore(MlbBoard *board, int teamId, int which) {
  const char *sport = nullptr;
  const char *lg = nullptr;
  if (!board || !teamId || !espnParts(board->league, &sport, &lg)) {
    return;
  }
  char url[240];
  snprintf(url, sizeof(url),
           "https://sports.core.api.espn.com/v2/sports/%s/leagues/%s/events/%ld/competitions/%ld/"
           "competitors/%d/linescores?lang=en&region=us",
           sport, lg, board->pk, board->pk, teamId);
  String body = httpsGet(url);
  if (!body.length()) {
    Serial.printf("linescore empty %d\n", teamId);
    return;
  }
  JsonDocument filter;
  filter["items"][0]["displayValue"] = true;
  filter["items"][0]["value"] = true;
  filter["items"][0]["period"] = true;
  JsonDocument doc;
  DeserializationError e =
      deserializeJson(doc, body, DeserializationOption::Filter(filter),
                      DeserializationOption::NestingLimit(20));
  body = "";
  if (e) {
    Serial.printf("linescore %s\n", e.c_str());
    return;
  }
  applyLinescoreItems(doc, board, which);
}

static void fillEspnPeriodScores(MlbBoard *board) {
  if (!board) {
    return;
  }
  fillOneLinescore(board, board->away_id, 0);
  fillOneLinescore(board, board->home_id, 1);
}

static bool espnLoadGames(League league, std::vector<MlbGame> *out, String *err) {
  if (!out) {
    return false;
  }
  out->clear();
  JsonDocument doc;
  if (!espnFetch(league, &doc, err)) {
    return false;
  }
  parseEspnHeader(league, doc, out, 0, nullptr);
  sortGames(*out);
  if (err) {
    err->clear();
  }
  return true;
}

static bool espnLoadBoard(MlbBoard *out) {
  if (!out || !out->pk) {
    return false;
  }
  JsonDocument doc;
  String err;
  if (!espnFetch(out->league, &doc, &err)) {
    return false;
  }
  long pk = out->pk;
  League league = out->league;
  uint16_t ac = out->away_color;
  uint16_t hc = out->home_color;
  std::vector<uint8_t> away_logo = std::move(out->away_logo);
  std::vector<uint8_t> home_logo = std::move(out->home_logo);
  bool ok = parseEspnHeader(league, doc, nullptr, pk, out);
  doc.clear();
  if (ok) {
    if (!out->away_color) {
      out->away_color = ac;
    }
    if (!out->home_color) {
      out->home_color = hc;
    }
    out->away_logo = std::move(away_logo);
    out->home_logo = std::move(home_logo);
    fillEspnPeriodScores(out);
  }
  return ok;
}

static void espnLoadLogo(League league, const String &abbr, std::vector<uint8_t> *dst) {
  const char *sport = espnLogoSport(league);
  if (!dst || !sport || !abbr.length() || !dst->empty()) {
    return;
  }
  String a = abbr;
  a.toLowerCase();
  char url[192];
  snprintf(url, sizeof(url),
           "https://a.espncdn.com/combiner/i?img=/i/teamlogos/%s/500/scoreboard/%s.png&w=48&h=48",
           sport, a.c_str());
  httpsGetBin(url, dst);
}

uint16_t sportsMarkColor(const MlbBoard &board, bool home) {
  uint16_t c = home ? board.home_color : board.away_color;
  if (c) {
    return c;
  }
  return mlbTeamColor(home ? board.home_id : board.away_id);
}

bool sportsLoadGames(League league, std::vector<MlbGame> *out, String *err) {
  if (league == League::Mlb) {
    return mlbLoadGames(out, err);
  }
  return espnLoadGames(league, out, err);
}

bool sportsLoadBoard(MlbBoard *out) {
  if (!out) {
    return false;
  }
  if (out->league == League::Mlb) {
    return mlbLoadBoard(out);
  }
  return espnLoadBoard(out);
}

void sportsLoadLogos(MlbBoard *out) {
  if (!out) {
    return;
  }
  if (out->league == League::Mlb) {
    mlbLoadLogos(out);
    return;
  }
  espnLoadLogo(out->league, out->away, &out->away_logo);
  espnLoadLogo(out->league, out->home, &out->home_logo);
}
