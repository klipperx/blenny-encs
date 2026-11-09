#include "schedule.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>

static std::vector<ScheduleDay> g_days;
static uint32_t g_version = 0;
static String g_tz = "MST7MDT,M3.2.0,M11.1.0";

static bool parse(const char *json, size_t len, std::vector<ScheduleDay> *out, uint32_t *version,
                  String *tz) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) {
    return false;
  }
  JsonArray days = doc["days"].as<JsonArray>();
  if (days.isNull() || days.size() == 0) {
    return false;
  }
  out->clear();
  for (JsonObject d : days) {
    ScheduleDay day;
    int y = 0, m = 0, dd = 0;
    const char *date = d["d"] | "";
    if (sscanf(date, "%d-%d-%d", &y, &m, &dd) != 3) {
      continue;
    }
    day.year = (uint16_t)y;
    day.month = (uint8_t)m;
    day.day = (uint8_t)dd;
    day.label = d["l"] | date;
    for (JsonArray s : d["s"].as<JsonArray>()) {
      if (s.size() < 3) {
        continue;
      }
      ScheduleItem it;
      it.start = s[0] | 0;
      it.end = s[1] | 0;
      it.title = s[2] | "";
      if (it.title.length()) {
        day.items.push_back(std::move(it));
      }
    }
    out->push_back(std::move(day));
  }
  *version = doc["version"] | 0u;
  const char *z = doc["tz"] | "";
  if (z[0]) {
    *tz = z;
  }
  return !out->empty();
}

static void applyTz() {
  setenv("TZ", g_tz.c_str(), 1);
  tzset();
}

bool scheduleLoad() {
  if (!LittleFS.exists("/schedule.json") && LittleFS.exists("/schedule.json.new")) {
    LittleFS.rename("/schedule.json.new", "/schedule.json");
  }
  File f = LittleFS.open("/schedule.json", "r");
  if (!f) {
    applyTz();
    return false;
  }
  String text = f.readString();
  f.close();
  bool ok = parse(text.c_str(), text.length(), &g_days, &g_version, &g_tz);
  applyTz();
  Serial.printf("schedule v%u: %u days\n", (unsigned)g_version, (unsigned)g_days.size());
  return ok;
}

uint32_t scheduleVersion() { return g_version; }
const std::vector<ScheduleDay> &scheduleDays() { return g_days; }
const char *scheduleTz() { return g_tz.c_str(); }

bool scheduleReplace(const uint8_t *json, size_t len) {
  std::vector<ScheduleDay> check;
  uint32_t v = 0;
  String tz;
  if (!parse((const char *)json, len, &check, &v, &tz)) {
    Serial.println("schedule: update is not a valid schedule");
    return false;
  }
  File f = LittleFS.open("/schedule.json.new", "w");
  if (!f) {
    return false;
  }
  size_t wrote = f.write(json, len);
  f.close();
  if (wrote != len) {
    LittleFS.remove("/schedule.json.new");
    return false;
  }
  LittleFS.remove("/schedule.json");
  if (!LittleFS.rename("/schedule.json.new", "/schedule.json")) {
    return false;
  }
  return scheduleLoad();
}

bool scheduleNow(int *day, int *minute) {
  *day = -1;
  *minute = 0;
  struct tm t;
  if (!getLocalTime(&t, 0) || t.tm_year + 1900 < 2024) {
    return false;
  }
  *minute = t.tm_hour * 60 + t.tm_min;
  for (size_t i = 0; i < g_days.size(); i++) {
    const ScheduleDay &d = g_days[i];
    if (d.year == t.tm_year + 1900 && d.month == t.tm_mon + 1 && d.day == t.tm_mday) {
      *day = (int)i;
      break;
    }
  }
  return true;
}
