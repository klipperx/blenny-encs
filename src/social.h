#pragma once

#include <Arduino.h>
#include <vector>

// Leaderboard: other badges' met counts heard over ESP-NOW while in Find Friends.
// Achievements: unlocked locally from met list, leaderboard, nametag styles, eggs.

struct LeaderRow {
  String id;
  int16_t met;  // may be negative when the roster pins someone's score
  bool me;
};

enum Achievement : uint8_t {
  ACH_FIRST = 0,
  ACH_MINGLER,
  ACH_BUTTERFLY,
  ACH_LEGEND,
  ACH_COMPLETIONIST,
  ACH_GOOSE,
  ACH_KLIPPER,
  ACH_ROYALTY,
  ACH_FASHION,
  ACH_TOP3,
  ACH_HR,
  ACH_FOOTWEAR,
  ACH_BIRD_TOP3,
  ACH_DOUBLEMINT,
  ACH_SHEEP,
  ACH_TIMEKEEPERS,
  ACH_ACE_HOLE,
  ACH_GAMBLER,
  ACH_FULL_TABLE,
  ACH_PATIENT_ZERO,
  ACH_SPREADER,
  ACH_IMMUNE,
  ACH_BLOODHOUND,
  ACH_GHOST,
  ACH_COUNT,
};

void socialLoad();
/** Call every loop: folds in nearby scores, checks achievements, saves when dirty. */
void socialLoop();
/** Score to put in our beacon (met count + 1; 0 means "unknown" to receivers). */
uint8_t socialBeaconScore();
/** Record a peer's beacon score byte (0 = old firmware, ignored). */
void socialNoteScore(const char *id, uint8_t beacon_score);
/** Everyone known, best first; ties keep the badge's own row higher. */
std::vector<LeaderRow> socialLeaders();
/** 1-based rank of this badge among known scores. */
int socialMyRank();
int socialKnownCount();

bool socialHasAchievement(Achievement a);
int socialAchievementCount();
const char *socialAchievementTitle(Achievement a);
/** Every nametag style is open once you've met 5 (Mingler); before that only seven-segment. */
bool socialStylesUnlocked();
/** One-off unlock from an event (e.g. Queen egg). */
void socialUnlock(Achievement a);
/** Forget scores and achievements (new person on this badge). */
void socialReset();
/** A blackjack hand was dealt to this badge (natural = 21 in the first two cards). */
void socialNoteBjHand(bool natural, bool full_table);
/** Write any unsaved leaderboard scores now (before deep sleep). */
void socialFlush();
