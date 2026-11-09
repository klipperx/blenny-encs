#include "settings.h"

#include "config.h"
#include "roster.h"
#include "spotify.h"

#include <Preferences.h>

static uint8_t g_color = 0;
static uint8_t g_tag = 0;
static int g_face = 0;
static int g_fav_toy = -1;      // NVS "rot" (was the long-press rotation toy)
static String g_fav_gif;        // NVS "rotgif"
static bool g_auto = false;     // NVS "auto"
static bool g_name_overlay = false;
static std::vector<String> g_met;
static String g_ssid;
static String g_psk;
static String g_nick;

static constexpr size_t kNickMaxLen = 16;

static uint8_t hashId(const String &id) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < id.length(); i++) {
    h ^= (uint8_t)id[i];
    h *= 16777619u;
  }
  return (uint8_t)(h % kColorCount);
}

static String packMet() {
  String out;
  for (size_t i = 0; i < g_met.size(); i++) {
    if (i) {
      out += ',';
    }
    out += g_met[i];
  }
  return out;
}

static void unpackMet(const String &packed) {
  g_met.clear();
  int start = 0;
  while (start < (int)packed.length()) {
    int comma = packed.indexOf(',', start);
    String id = (comma < 0) ? packed.substring(start) : packed.substring(start, comma);
    id.trim();
    if (id.length() && !settingsHasMet(id.c_str())) {
      g_met.push_back(id);
    }
    if (comma < 0) {
      break;
    }
    start = comma + 1;
  }
}

void settingsLoad() {
  Preferences prefs;
  prefs.begin("blenny", true);
  int stored = prefs.getInt("color", -1);
  int tag = prefs.getInt("tag", 0);
  String met = prefs.getString("met", "");
  g_ssid = prefs.getString("ssid", "");
  g_psk = prefs.getString("psk", "");
  g_nick = prefs.getString("nick", "");
  int face = prefs.getInt("face", 0);
  g_fav_toy = prefs.getInt("rot", -1);
  if (g_fav_toy < -1 || g_fav_toy >= (int)kToyFaceCount) {
    g_fav_toy = -1;
  }
  g_fav_gif = prefs.getString("rotgif", "");
  // Before Auto mode existed, a saved rotation toy meant "rotating".
  g_auto = prefs.isKey("auto") ? prefs.getBool("auto", false) : g_fav_toy >= 0;
  g_name_overlay = prefs.getBool("nameov", false);
  prefs.end();
  if (stored >= 0 && stored < kColorCount) {
    g_color = (uint8_t)stored;
  } else {
    g_color = hashId(rosterMyId());
  }
  g_tag = (tag >= 0 && tag < kTagStyleCount) ? (uint8_t)tag : 0;
  if (g_nick.length() > kNickMaxLen) {
    g_nick = g_nick.substring(0, kNickMaxLen);
  }
  unpackMet(met);
  g_face = face;
  if (g_face < 0) {
    g_face = 0;
  }
  if (g_face >= settingsFaceCount()) {
    // Old toy/boss pages moved to Silly Animations; fall back to the saved classic style.
    g_face = g_tag % settingsFaceCount();
  }
}

void settingsSave() {
  Preferences prefs;
  prefs.begin("blenny", false);
  prefs.putInt("color", g_color);
  prefs.putInt("tag", g_tag);
  prefs.putInt("face", g_face);
  prefs.putInt("rot", g_fav_toy);
  prefs.putString("rotgif", g_fav_gif);
  prefs.putBool("auto", g_auto);
  prefs.putBool("nameov", g_name_overlay);
  prefs.putString("met", packMet());
  prefs.putString("ssid", g_ssid);
  prefs.putString("psk", g_psk);
  prefs.putString("nick", g_nick);
  prefs.end();
}

uint8_t settingsColor() { return g_color; }

void settingsSetColor(uint8_t i) {
  g_color = i % kColorCount;
  settingsSave();
}

void settingsCycleColor() { settingsSetColor((g_color + 1) % kColorCount); }

uint8_t settingsTagStyle() { return g_tag; }

void settingsSetTagStyle(uint8_t i) {
  g_tag = i % kTagStyleCount;
  settingsSave();
}

void settingsCycleTag(int dir) {
  int n = (int)g_tag + dir;
  while (n < 0) {
    n += kTagStyleCount;
  }
  settingsSetTagStyle((uint8_t)(n % kTagStyleCount));
}

int settingsFaceCount() { return (int)kClassicFaceCount; }

int settingsFacePage() { return g_face; }

void settingsSetFacePage(int page) {
  int n = settingsFaceCount();
  if (n <= 0) {
    g_face = 0;
    return;
  }
  while (page < 0) {
    page += n;
  }
  g_face = page % n;
  // Keep classic tag style in sync when on a classic page.
  if (g_face < (int)kClassicFaceCount) {
    g_tag = (uint8_t)g_face;
  }
  settingsSave();
}

void settingsCycleFace(int dir) {
  settingsSetFacePage(g_face + dir);
}

bool settingsAutoMode() { return g_auto; }

void settingsSetAutoMode(bool on) {
  g_auto = on;
  settingsSave();
}

int settingsFavToy() { return g_fav_toy; }
const String &settingsFavGif() { return g_fav_gif; }

void settingsSetFavToy(int toy) {
  g_fav_toy = (toy >= 0 && toy < (int)kToyFaceCount) ? toy : -1;
  g_fav_gif = "";
  settingsSave();
}

void settingsSetFavGif(const String &name) {
  g_fav_toy = -1;
  g_fav_gif = name;
  settingsSave();
}

bool settingsShowNameOverlay() { return g_name_overlay; }

void settingsSetShowNameOverlay(bool on) {
  g_name_overlay = on;
  settingsSave();
}

void settingsToggleShowNameOverlay() {
  settingsSetShowNameOverlay(!g_name_overlay);
}

void settingsResetForNewPerson() {
  g_color = hashId(rosterMyId());
  g_met.clear();
  g_nick = "";
  g_auto = false;
  g_fav_toy = -1;
  g_fav_gif = "";
  settingsSave();
  spotifyClearAuth();
}

uint16_t settingsColorRgb() { return kColors[g_color].rgb; }

bool settingsHasMet(const char *id) {
  if (!id || !id[0]) {
    return false;
  }
  for (const String &m : g_met) {
    if (m == id) {
      return true;
    }
  }
  return false;
}

void settingsMarkMet(const char *id) {
  if (!id || !id[0] || settingsHasMet(id)) {
    return;
  }
  if (rosterMyId() == id) {
    return;
  }
  g_met.push_back(id);
  settingsSave();
}

const std::vector<String> &settingsMet() { return g_met; }

const String &settingsWifiSsid() { return g_ssid; }
const String &settingsWifiPass() { return g_psk; }

void settingsSetWifi(const String &ssid, const String &pass) {
  g_ssid = ssid;
  g_psk = pass;
  settingsSave();
}

void settingsClearWifi() {
  g_ssid = "";
  g_psk = "";
  settingsSave();
}

bool settingsHasWifi() { return g_ssid.length() > 0; }

const String &settingsNickname() { return g_nick; }

void settingsSetNickname(const String &nick) {
  g_nick = nick;
  g_nick.trim();
  if (g_nick.length() > kNickMaxLen) {
    g_nick = g_nick.substring(0, kNickMaxLen);
  }
  settingsSave();
}

void settingsClearNickname() {
  if (!g_nick.length()) {
    return;
  }
  g_nick = "";
  settingsSave();
}

bool settingsHasNickname() { return g_nick.length() > 0; }

String settingsDisplayName() {
  if (g_nick.length()) {
    String n = g_nick;
    n.toUpperCase();
    return n;
  }
  const Attendee *me = rosterMe();
  String n = (me && me->name.length()) ? me->name : "?";
  int sp = n.indexOf(' ');
  if (sp > 0) {
    n = n.substring(0, sp);
  }
  n.toUpperCase();
  return n.length() ? n : String("?");
}

