#pragma once

#include <Arduino.h>

enum class LedShow : uint8_t {
  Idle,
  Breathe,
  Unison,
  Scanner,
};

// Optional carrier LEDs (four lights on GPIO 11, 12, 13, 43). ENCS badges have none wired, so
// they're off unless a badge is switched on over USB (SETLEDS 1); the choice and brightness
// live in NVS ("leds", "led"), so one firmware serves every badge. Every call is a no-op while
// disabled.

/** Read the saved on/off + brightness (call before ledsBegin). */
void ledsLoad();
bool ledsEnabled();
/** Save on/off + brightness; turning off parks the pins low. */
void ledsSetEnabled(bool on, uint8_t level);

// 0 = low, 1 = medium, 2 = high (peak PWM scale).
void ledsSetBrightness(uint8_t level);
uint8_t ledsBrightness();

void ledsBegin();
void ledsOff();
void ledsCelebrateScore();
void ledsBlink(uint8_t times, uint16_t on_ms, uint16_t off_ms);
void ledsTick(LedShow show);
