#pragma once

#include <Arduino.h>
#include <vector>

// Event agenda from /schedule.json (LittleFS), replaceable over the air like the roster.
// File: {"version":N,"tz":"POSIX TZ","days":[{"d":"2026-11-02","l":"Mon 11/2",
//        "s":[[start_min,end_min,"title"],...]}]}

struct ScheduleItem {
  uint16_t start;  // minutes after local midnight
  uint16_t end;
  String title;
};

struct ScheduleDay {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  String label;
  std::vector<ScheduleItem> items;
};

bool scheduleLoad();
uint32_t scheduleVersion();
const std::vector<ScheduleDay> &scheduleDays();
/** POSIX TZ for the event (from the file, else Mountain). */
const char *scheduleTz();
/** Validate, write atomically and reload. */
bool scheduleReplace(const uint8_t *json, size_t len);
/**
 * Where "now" falls: *day = index of today's schedule day (or -1), *minute = local minutes
 * after midnight. Returns false until the clock has been set over Wi-Fi.
 */
bool scheduleNow(int *day, int *minute);
