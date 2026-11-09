#pragma once

#include <Arduino.h>
#include <vector>

struct Attendee {
  String id;
  String name;
  String truth;
  bool goose = false;  // nametag alternates with the shame goose
  String sign;         // goose sign text; empty = the default "I didn't follow instructions"
  bool fixed_score = false;  // leaderboard shows `score` for this person on every badge
  int16_t score = 0;
  bool zero = false;   // outbreak: starts infected
  bool medic = false;  // outbreak: cures, can't be infected
  bool it = false;     // hide and seek: the one everyone is looking for
};

bool rosterLoad();
const Attendee *rosterFind(const char *id);
const Attendee *rosterMe();
const std::vector<Attendee> &rosterAll();
const std::vector<String> &rosterFacts();
size_t rosterCount();
size_t factsCount();
void rosterSetMyId(const String &id);
const String &rosterMyId();
bool rosterIsReady();
// Roster version from attendees.json ("version", 0 if absent). OTA only installs newer ones.
uint32_t rosterVersion();
// Outbreak round from attendees.json ("outbreak", 0 = no outbreak). A new number restarts it.
uint32_t rosterOutbreakRound();
// Hide and Seek round ("hideseek", 0 = none) and who is "it" (first attendee with "it": true).
uint32_t rosterHideSeekRound();
const Attendee *rosterHideSeekIt();
// Validate and atomically replace /attendees.json, then reload. Refuses a roster that
// drops this badge's own id, so an update can never orphan a provisioned badge.
bool rosterReplace(const uint8_t *json, size_t len);
