#pragma once

#include <Arduino.h>
#include <vector>

void settingsLoad();
void settingsSave();
uint8_t settingsColor();
void settingsSetColor(uint8_t i);
void settingsCycleColor();
uint8_t settingsTagStyle();
void settingsSetTagStyle(uint8_t i);
void settingsCycleTag(int dir);
void settingsResetForNewPerson();
uint16_t settingsColorRgb();
void settingsMarkMet(const char *id);
bool settingsHasMet(const char *id);
const std::vector<String> &settingsMet();
const String &settingsWifiSsid();
const String &settingsWifiPass();
void settingsSetWifi(const String &ssid, const String &pass);
void settingsClearWifi();
bool settingsHasWifi();
const String &settingsNickname();
void settingsSetNickname(const String &nick);
void settingsClearNickname();
bool settingsHasNickname();
/** Nametag text: custom nick if set, else roster first name. Uppercased. */
String settingsDisplayName();

/** Nametag face page: classic styles only (toys live under Silly Animations). */
int settingsFacePage();
void settingsSetFacePage(int page);
void settingsCycleFace(int dir);
int settingsFaceCount();
/** Silly Animations toy that rotates with the nametag, or -1 when rotation is off. */
/** Auto mode: the nametag alternates with a favourite Silly animation. */
bool settingsAutoMode();
void settingsSetAutoMode(bool on);
/** Favourite built-in Silly animation (0..kToyFaceCount-1), or -1 when the favourite is a GIF. */
int settingsFavToy();
/** Favourite custom GIF by name ("" when the favourite is built-in or unset). */
const String &settingsFavGif();
void settingsSetFavToy(int toy);
void settingsSetFavGif(const String &name);
bool settingsShowNameOverlay();
void settingsSetShowNameOverlay(bool on);
void settingsToggleShowNameOverlay();
