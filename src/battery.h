#pragma once

#include <Arduino.h>

enum class BatteryStatus : uint8_t { Discharging, Charging, Charged };

void batteryBegin();
void batteryPoll();
uint8_t batterySegments();  // 0..5
int batteryMillivolts();
BatteryStatus batteryStatus();
bool batteryUsb();
bool batteryEmpty();
void batteryArmCutoff();
const char *batteryStatusName();
