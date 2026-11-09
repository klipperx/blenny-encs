#pragma once

#include <Arduino.h>

// Outbreak: a game you launch from Games. While it's open the badge shows your nametag with a
// heartbeat, and the radio trades a small status packet every second with everyone else
// playing. Spend kOutbreakExposeMs close to an infected player and you catch it; spend the
// same time close to a medic and you're cured and immune for an hour. Patient zero, the
// medics and the round number come from the roster ("outbreak", "zero", "medic"); with no
// round in the roster the game just says so. State is kept in NVS "outbreak" between plays.
// Score: time survived while playing (see outbreakAliveSecs).

enum OutbreakState : uint8_t {
  OB_HEALTHY = 0,
  OB_INFECTED = 1,
  OB_MEDIC = 2,
  OB_IMMUNE = 3,
};

static constexpr uint32_t kOutbreakExposeMs = 30000;   // close contact needed to catch / cure
static constexpr uint32_t kOutbreakGapMs = 20000;      // a longer break restarts the clock
static constexpr uint32_t kOutbreakImmuneMs = 3600000; // immunity after a cure
static constexpr int kOutbreakCloseRssi = -62;         // "close": smoothed dBm at or above this
static constexpr uint8_t kOutbreakSpreader = 5;        // Super-spreader threshold

void outbreakLoad();
/** Entering the Outbreak screen (radio already up). */
void outbreakOpen();
void outbreakClose();
void outbreakLoop();
bool outbreakChanged();

/** True when the roster has an outbreak round running. */
bool outbreakActive();
OutbreakState outbreakState();
/** 0..1 progress toward catching it (or toward a cure while infected); 0 when nobody's close. */
float outbreakExposure();
/** Who infected you (roster id), empty if nobody / patient zero. */
const String &outbreakInfectedBy();
uint8_t outbreakSpreadCount();
/** Players heard in the last few seconds (any state). */
int outbreakNearbyCount();
/** One line for the WHO reply: round, state, roles, spread, who infected you. */
String outbreakDescribe();
/** Strongest smoothed signal among players heard recently (dBm), or 0 if none. */
int outbreakBestRssi();
/** Survival time this round (seconds): counts while the game is open and you're healthy or
 *  immune. Infection stops it, a cure resumes it; medics don't score. */
uint32_t outbreakAliveSecs();
