#include "roster.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>

static std::vector<Attendee> g_attendees;
static std::vector<String> g_facts;
static String g_my_id;
static bool g_ready = false;
static uint32_t g_version = 0;
static uint32_t g_outbreak = 0;
static uint32_t g_hideseek = 0;

static bool loadAttendees() {
  // Finish an OTA roster swap that lost power between remove and rename.
  if (!LittleFS.exists("/attendees.json") && LittleFS.exists("/attendees.json.new")) {
    LittleFS.rename("/attendees.json.new", "/attendees.json");
  }
  File f = LittleFS.open("/attendees.json", "r");
  if (!f) {
    Serial.println("missing /attendees.json");
    return false;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.printf("attendees.json: %s\n", err.c_str());
    return false;
  }
  JsonArray arr = doc["attendees"].as<JsonArray>();
  g_version = doc["version"] | 0u;
  g_outbreak = doc["outbreak"] | 0u;
  g_hideseek = doc["hideseek"] | 0u;
  g_attendees.clear();
  for (JsonObject o : arr) {
    Attendee a;
    a.id = o["id"] | "";
    a.name = o["name"] | "";
    a.truth = o["truth"] | "";
    a.goose = o["goose"] | false;
    a.sign = o["sign"] | "";
    a.zero = o["zero"] | false;
    a.medic = o["medic"] | false;
    a.it = o["it"] | false;
    if (o["score"].is<int>()) {
      a.fixed_score = true;
      a.score = (int16_t)(o["score"].as<int>());
    }
    if (a.id.length() && a.name.length() && a.truth.length()) {
      g_attendees.push_back(std::move(a));
    }
  }
  return !g_attendees.empty();
}

static bool loadFacts() {
  File f = LittleFS.open("/generated_facts.json", "r");
  if (!f) {
    Serial.println("missing /generated_facts.json");
    return false;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.printf("generated_facts.json: %s\n", err.c_str());
    return false;
  }
  g_facts.clear();
  for (JsonVariant v : doc.as<JsonArray>()) {
    String s = v.as<String>();
    if (s.length()) {
      g_facts.push_back(std::move(s));
    }
  }
  return !g_facts.empty();
}

bool rosterLoad() {
  g_ready = false;
  if (!LittleFS.begin(false)) {
    Serial.println("LittleFS mount failed");
    return false;
  }
  if (!loadAttendees() || !loadFacts()) {
    return false;
  }
  Preferences prefs;
  prefs.begin("blenny", true);
  g_my_id = prefs.getString("id", "");
  prefs.end();
  g_ready = true;
  Serial.printf("roster v%u: %u attendees, %u facts, me=%s\n", (unsigned)g_version,
                (unsigned)g_attendees.size(), (unsigned)g_facts.size(), g_my_id.c_str());
  return true;
}

const Attendee *rosterFind(const char *id) {
  if (!id || !id[0]) {
    return nullptr;
  }
  for (const auto &a : g_attendees) {
    if (a.id == id) {
      return &a;
    }
  }
  return nullptr;
}

const Attendee *rosterMe() { return rosterFind(g_my_id.c_str()); }

const std::vector<Attendee> &rosterAll() { return g_attendees; }
const std::vector<String> &rosterFacts() { return g_facts; }
size_t rosterCount() { return g_attendees.size(); }
size_t factsCount() { return g_facts.size(); }
const String &rosterMyId() { return g_my_id; }
bool rosterIsReady() { return g_ready; }
uint32_t rosterVersion() { return g_version; }
uint32_t rosterOutbreakRound() { return g_outbreak; }
uint32_t rosterHideSeekRound() { return g_hideseek; }
const Attendee *rosterHideSeekIt() {
  for (const auto &a : g_attendees) {
    if (a.it) {
      return &a;
    }
  }
  return nullptr;
}

bool rosterReplace(const uint8_t *json, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, (const char *)json, len)) {
    Serial.println("roster: update is not valid JSON");
    return false;
  }
  JsonArray arr = doc["attendees"].as<JsonArray>();
  if (arr.isNull() || arr.size() == 0) {
    Serial.println("roster: update has no attendees");
    return false;
  }
  if (g_my_id.length()) {
    bool found = false;
    for (JsonObject o : arr) {
      if (g_my_id == (o["id"] | "")) {
        found = true;
        break;
      }
    }
    if (!found) {
      Serial.printf("roster: update drops my id %s, refusing\n", g_my_id.c_str());
      return false;
    }
  }
  File f = LittleFS.open("/attendees.json.new", "w");
  if (!f) {
    Serial.println("roster: cannot write update");
    return false;
  }
  size_t wrote = f.write(json, len);
  f.close();
  if (wrote != len) {
    LittleFS.remove("/attendees.json.new");
    return false;
  }
  LittleFS.remove("/attendees.json");
  if (!LittleFS.rename("/attendees.json.new", "/attendees.json")) {
    Serial.println("roster: rename failed");
    return false;
  }
  return rosterLoad();
}

void rosterSetMyId(const String &id) {
  g_my_id = id;
  Preferences prefs;
  prefs.begin("blenny", false);
  prefs.putString("id", id);
  prefs.end();
}
