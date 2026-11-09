#pragma once

#include "config.h"

#include <Arduino.h>
#include <vector>

enum class League : uint8_t { Mlb, Nhl, Nba, Nfl };

static constexpr uint8_t kLeagueCount = 4;

struct MlbGame {
  League league = League::Mlb;
  long pk = 0;
  int away_id = 0;
  int home_id = 0;
  String away;
  String home;
  String away_name;
  String home_name;
  int away_score = 0;
  int home_score = 0;
  uint16_t away_color = 0;
  uint16_t home_color = 0;
  String state;
  String detail;
  bool live = false;
};

struct MlbBoard {
  League league = League::Mlb;
  long pk = 0;
  int away_id = 0;
  int home_id = 0;
  String away;
  String home;
  String away_name;
  String home_name;
  String status;
  String inning;
  int away_runs = 0;
  int home_runs = 0;
  int balls = 0;
  int strikes = 0;
  int outs = 0;
  uint16_t away_color = 0;
  uint16_t home_color = 0;
  int innings[12][2] = {};
  int inning_count = 0;
  std::vector<uint8_t> away_logo;
  std::vector<uint8_t> home_logo;
  bool live = false;
  bool ok = false;
};

const char *leagueName(League l);
int leagueRegulationPeriods(League l);
void leaguePeriodLabel(League l, int i, int count, char *out, size_t n);
uint16_t mlbTeamColor(int team_id);
uint16_t sportsMarkColor(const MlbBoard &board, bool home);
bool sportsLoadGames(League league, std::vector<MlbGame> *out, String *err);
bool sportsLoadBoard(MlbBoard *out);
void sportsLoadLogos(MlbBoard *out);

bool mlbLoadGames(std::vector<MlbGame> *out, String *err);
bool mlbLoadBoard(MlbBoard *out);
void mlbLoadLogos(MlbBoard *out);
