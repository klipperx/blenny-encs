#pragma once

#include <Arduino.h>

// Hide and Seek: launched from Diversions. The roster names one person "it" ("it": true) and a
// round ("hideseek"). While the game is open, it's badge pings once a second; every seeker
// smooths the signal strength of those pings and broadcasts its reading, so each seeker sees
// the whole team's hot/cold board and can close in on whoever reads hottest. It sees the
// seekers closing in. A seeker who holds kHideSeekCloseRssi or better for kHideSeekFoundMs
// finds them, and the round is over until the roster starts a new one. The radio is only on
// while the game is open. State is kept in NVS "hideseek" between plays.

static constexpr int kHideSeekCloseRssi = -45;         // "on top of them": smoothed dBm
static constexpr uint32_t kHideSeekFoundMs = 10000;    // hold that close this long to find them
static constexpr uint32_t kHideSeekGapMs = 3000;       // a longer drop restarts the found clock
static constexpr uint32_t kHideSeekDarkMs = 5000;      // no ping for this long: it has gone dark
static constexpr uint32_t kHideSeekGhostSecs = 1800;   // it, unfound for this long: Ghost

// Radar: rough metres from a reading (log-distance model; about 1 m at -45 dBm).
static constexpr int kHideSeekRssiAt1m = -45;
static constexpr float kHideSeekPathLoss = 2.5f;
static constexpr float kHideSeekMaxMetres = 40.0f;
static constexpr int kHideSeekBlips = 10;

struct HideSeekBlip {
  char id[16];
  float x, y;     // metres from me; the map's rotation is arbitrary
  float metres;   // my own reading of them as metres (0 = I can't hear them directly)
};

struct HideSeekRadar {
  int n;
  HideSeekBlip blips[kHideSeekBlips];  // other players (not me, not it)
  int it_fix;      // 0 = no idea, 1 = distance only (it_ring around me), 2 = position (it_x/it_y +- it_r)
  int it_anchors;  // players with a fresh reading of it
  float it_x, it_y, it_r, it_ring;
};

struct HideSeekReading {
  char id[16];
  int rssi;  // seekers: their reading of it; on it's badge: how strongly it hears them
};

void hideseekLoad();
void hideseekOpen();
void hideseekClose();
void hideseekLoop();
bool hideseekChanged();

/** True when the roster has a round running and someone is "it". */
bool hideseekActive();
bool hideseekIAmIt();
/** Display name of it ("" if none). */
String hideseekItName();
/** Seekers: my smoothed reading of it (dBm), or 0 when it can't be heard. */
int hideseekMyRssi();
/** Seekers: true once it has been heard this play but not for kHideSeekDarkMs. */
bool hideseekDark();
/** Seekers: 0..1 progress toward finding it. */
float hideseekFoundProgress();
bool hideseekFound();
/** Roster id of whoever found it ("" if nobody yet). */
const String &hideseekFoundBy();
/** The board, strongest first: seekers' readings of it (on a seeker's badge, not counting
 *  me), or the seekers it can hear (on it's badge). Returns how many were written. */
int hideseekBoard(HideSeekReading *out, int max);
/** It: seconds hidden (game open, not yet found) this round. */
uint32_t hideseekHiddenSecs();
/** "Cold" .. "ON TOP OF THEM" for a reading. */
const char *hideseekHeatWord(int rssi);
/** Best-guess map from everyone's readings, me at the centre. */
void hideseekRadar(HideSeekRadar *out);
/** My reading of it over the last ~10 s: 1 warmer, -1 colder, 0 steady, 2 not enough yet. */
int hideseekTrend();
float hideseekRssiToMetres(int rssi);
/** One line for the WHO reply. */
String hideseekDescribe();
