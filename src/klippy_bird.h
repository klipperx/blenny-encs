#pragma once

#include <Arduino.h>
#include <vector>

// Klippy Bird: tap-to-flap through procedurally generated pipes with klipper's head.
// Best scores spread badge-to-badge: a small ESP-NOW packet while someone is on the Klippy
// Bird screen, and the Find Friends beacon's word field ("B<best>"). Every score heard is
// kept (NVS "bird"), so the title screen shows a top 5 and the game draws ghost flags at the
// pipes where the leaders crashed.

enum BirdPhase : uint8_t { BIRD_TITLE = 0, BIRD_PLAY, BIRD_OVER };

struct BirdPipe {
  float x;        // left edge, screen px
  int16_t gap_y;  // gap centre
  int16_t gap_h;
  uint16_t index; // 0-based pipe number in this run
};

struct BirdGhost {
  uint16_t score;  // they crashed at pipe `score`
  String label;    // first name, e.g. "KEVIN"
};

struct BirdScore {
  String id;
  uint16_t best;
  bool me;
};

struct BirdState {
  uint8_t phase;
  float y;         // bird centre
  float vy;
  uint16_t score;
  uint16_t best;
  bool new_best;
  float world;     // distance flown (px), drives the parallax
  uint32_t over_at;
  std::vector<BirdPipe> pipes;
  std::vector<BirdGhost> ghosts;
};

static constexpr int kBirdX = 80;
static constexpr int kBirdR = 11;
static constexpr int kBirdGround = 14;
static constexpr int kBirdPipeW = 36;

void birdLoad();
/** Entering the Klippy Bird screen (radio already up): share scores, reset to the title. */
void birdOpen();
void birdClose();
void birdLoop();
/** Flap; on the title or game-over screen, start a run. */
void birdTap();

const BirdState &birdState();
/** Everyone known including you, best first. */
std::vector<BirdScore> birdLeaders();
int birdMyRank();
int birdKnownCount();
uint16_t birdBest();
/** Your best for the Find Friends beacon word ("B123"), or "" with no score yet. */
const char *birdBeaconWord();
/** A best score heard over the radio (thread-safe; folded in from birdLoop/birdOpen). */
void birdNoteScore(const char *id, uint16_t best);
/** Fold in scores heard while not on the Klippy Bird screen (called from the main loop). */
void birdDrain();
bool birdChanged();
