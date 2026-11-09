#pragma once

#include <Arduino.h>
#include <vector>

struct SpotifyNowPlaying {
  bool ok = false;
  bool playing = false;
  bool active = false;
  String title;
  String artist;
  String album;
  String art_url;
  int progress_ms = 0;
  int duration_ms = 0;
};

void spotifyLoad();
bool spotifyHasClientId();
bool spotifyHasRefresh();
bool spotifyAuthed();
const String &spotifyClientId();
void spotifySetClientId(const String &id);
void spotifySetRefreshToken(const String &token);
void spotifyClearAuth();

/** Last human-readable API/auth error (empty if none). */
const char *spotifyLastError();

bool spotifyEnsureAccess();
bool spotifyFetchNowPlaying(SpotifyNowPlaying *out);
bool spotifyPlay();
bool spotifyPause();
bool spotifyNext();
bool spotifyPrevious();
bool spotifyFetchArt(const String &url, std::vector<uint8_t> *jpeg, size_t cap = 24576);
