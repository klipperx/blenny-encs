#pragma once

#include <Arduino.h>

// Auto-update: when Wi-Fi connects, fetch a signed manifest; if it names a newer firmware
// for this board, download it into the spare OTA slot, verify, and reboot into it.
//
// Manifest (JSON) lives at the configured URL; its ECDSA P-256 signature (DER) at URL + ".sig".
//   {"version":"1.7.0","builds":{"cst816":{"url":"…","size":123,"sha256":"…"}, …}}

void otaBegin();
// Marks a freshly-updated image healthy after it has run a while; runs the update check
// once per Wi-Fi connection. Blocks (drawing its own screen) while an update installs.
// may_wake: the current screen doesn't use the radio, so the badge may briefly bring Wi-Fi
// up itself to check — at boot, then every 10 minutes. False during the game and on
// screens that manage Wi-Fi themselves.
void otaLoop(bool may_wake);

const char *otaVariant();
String otaUrl();
void otaSetUrl(const String &url);  // empty = back to the build default
void otaCheckNow();                 // re-check on the next loop if Wi-Fi is up
bool otaSetRosterKey(const String &hex);  // 64 hex chars → NVS; false if malformed
bool otaHasRosterKey();
