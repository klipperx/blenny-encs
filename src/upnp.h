#pragma once

#include <Arduino.h>
#include <vector>

enum class LanSource : uint8_t { Upnp, Cast };

struct UpnpRenderer {
  LanSource source = LanSource::Upnp;
  String usn;
  String name;
  String location;
  String base_url;
  String avt_control;
  String cast_host;  // IPv4 string when source == Cast
  String transport;  // PLAYING / PAUSED_PLAYBACK / STOPPED / PAUSED / ...
  String title;
  String artist;
  String album;
  String art_url;
  bool ok = false;
};

static constexpr size_t kUpnpMaxRenderers = 16;
static constexpr uint32_t kUpnpDiscoverMs = 4000;
static constexpr uint32_t kUpnpPollMs = 2500;

void upnpReset();
void upnpStartDiscover();
bool upnpDiscoverBusy();
void upnpDiscoverLoop();
bool upnpDiscoverDone();

size_t upnpCount();
int upnpSelected();
void upnpSelect(int idx);
void upnpCycle(int dir);
const UpnpRenderer *upnpCurrent();
const UpnpRenderer *upnpAt(size_t idx);

/** Poll transport + media metadata for the selected renderer. */
bool upnpRefreshSelected();
bool upnpPlay();
bool upnpPause();
bool upnpNext();
bool upnpPrevious();
bool upnpFetchArt(const String &url, std::vector<uint8_t> *out, size_t cap = 24576);

const char *upnpLastError();
