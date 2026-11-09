#include "leds.h"

#include "config.h"

#include <Preferences.h>
#include <esp_random.h>
#include <math.h>

static const int kPins[] = {PIN_LED1, PIN_LED2, PIN_LED3, PIN_LED4};
static constexpr int kCount = 4;
static constexpr uint32_t kScoreFlashMs = 10000;
static constexpr uint8_t kBrightScale[3] = {48, 140, 255};  // low / medium / high

enum class LedFx : uint8_t { None, Crazy, Blink };

static LedShow g_show = LedShow::Idle;
static uint32_t g_show_at = 0;
static LedFx g_fx = LedFx::None;
static uint32_t g_fx_until = 0;
static uint32_t g_fx_step_at = 0;
static uint8_t g_bright = 2;  // high by default (legacy behaviour)
static bool g_enabled = false;
static bool g_attached = false;

static uint8_t g_blink_left = 0;
static uint16_t g_blink_on = 80;
static uint16_t g_blink_off = 80;
static bool g_blink_lit = false;

static int g_breath_order[kCount] = {0, 1, 2, 3};
static int g_breath_i = 0;
static uint32_t g_breath_at = 0;

void ledsSetBrightness(uint8_t level) { g_bright = level % 3; }
uint8_t ledsBrightness() { return g_bright; }

static uint8_t scaleBright(uint8_t v) {
  return (uint8_t)(((uint16_t)v * kBrightScale[g_bright]) / 255);
}

static void writeLed(int i, uint8_t v) {
  ledcWrite((uint8_t)i, scaleBright(v));
}

static void writeAll(uint8_t v) {
  for (int i = 0; i < kCount; i++) {
    writeLed(i, v);
  }
}

static void shuffleBreath() {
  int last = g_breath_order[kCount - 1];
  for (int i = kCount - 1; i > 0; i--) {
    int j = (int)(esp_random() % (uint32_t)(i + 1));
    int tmp = g_breath_order[i];
    g_breath_order[i] = g_breath_order[j];
    g_breath_order[j] = tmp;
  }
  if (g_breath_order[0] == last && kCount > 1) {
    int tmp = g_breath_order[0];
    g_breath_order[0] = g_breath_order[1];
    g_breath_order[1] = tmp;
  }
  g_breath_i = 0;
}

static void tickBreathe(uint32_t now) {
  constexpr uint32_t kPeriod = 720;
  if (!g_breath_at) {
    shuffleBreath();
    g_breath_at = now;
  }
  uint32_t dt = now - g_breath_at;
  if (dt >= kPeriod) {
    g_breath_i++;
    if (g_breath_i >= kCount) {
      shuffleBreath();
    }
    g_breath_at = now;
    dt = 0;
  }
  float s = sinf((float)dt / (float)kPeriod * 3.14159265f);
  uint8_t peak = (uint8_t)(s * s * 255.0f);
  int on = g_breath_order[g_breath_i];
  for (int i = 0; i < kCount; i++) {
    writeLed(i, i == on ? peak : 12);
  }
}

static void tickUnison(uint32_t now) {
  constexpr uint32_t kPeriod = 1800;
  float t = (float)((now - g_show_at) % kPeriod) / (float)kPeriod;
  float s = 0.5f * (1.0f + sinf(t * 6.2831853f - 1.5707963f));
  writeAll((uint8_t)(s * s * 255.0f));
}

static void tickScanner(uint32_t now) {
  constexpr uint32_t kStepMs = 85;
  int step = (int)(((now - g_show_at) / kStepMs) % 6);
  int pos = step < 4 ? step : 6 - step;
  for (int i = 0; i < kCount; i++) {
    int d = abs(i - pos);
    uint8_t v = 0;
    if (d == 0) {
      v = 255;
    } else if (d == 1) {
      v = 36;
    }
    writeLed(i, v);
  }
}

static void tickCrazy(uint32_t now) {
  if (now - g_fx_step_at < 35) {
    return;
  }
  g_fx_step_at = now;
  uint32_t r = esp_random();
  if ((r & 7) == 0) {
    writeAll(255);
    return;
  }
  if ((r & 7) == 1) {
    writeAll(0);
    return;
  }
  for (int i = 0; i < kCount; i++) {
    writeLed(i, (r >> (i * 3)) & 1 ? 255 : 0);
  }
}

static void tickBlink(uint32_t now) {
  uint32_t wait = g_blink_lit ? g_blink_on : g_blink_off;
  if (!g_fx_step_at) {
    g_fx_step_at = now;
    writeAll(g_blink_lit ? 255 : 0);
    return;
  }
  if (now - g_fx_step_at < wait) {
    return;
  }
  g_fx_step_at = now;
  if (g_blink_lit) {
    g_blink_lit = false;
    writeAll(0);
    g_blink_left--;
    if (g_blink_left == 0) {
      g_fx = LedFx::None;
    }
  } else {
    g_blink_lit = true;
    writeAll(255);
  }
}

void ledsLoad() {
  Preferences prefs;
  prefs.begin("blenny", true);
  g_enabled = prefs.getBool("leds", false);
  int level = prefs.getInt("led", 2);  // same key the pre-ENCS LED firmware used
  prefs.end();
  g_bright = (level >= 0 && level <= 2) ? (uint8_t)level : 2;
}

bool ledsEnabled() { return g_enabled; }

void ledsSetEnabled(bool on, uint8_t level) {
  g_bright = level % 3;
  Preferences prefs;
  prefs.begin("blenny", false);
  prefs.putBool("leds", on);
  prefs.putInt("led", g_bright);
  prefs.end();
  if (on && !g_enabled) {
    g_enabled = true;
    ledsBegin();
  } else if (!on && g_enabled) {
    ledsOff();
    g_enabled = false;
  }
}

void ledsBegin() {
  if (!g_enabled) {
    return;
  }
  g_attached = true;
  for (int i = 0; i < kCount; i++) {
    ledcSetup((uint8_t)i, 5000, 8);
    ledcAttachPin(kPins[i], (uint8_t)i);
    writeLed(i, 0);
  }
  g_show = LedShow::Idle;
  g_show_at = millis();
  g_fx = LedFx::None;
}

void ledsOff() {
  if (!g_attached) {
    return;
  }
  g_attached = false;
  g_fx = LedFx::None;
  for (int i = 0; i < kCount; i++) {
    writeLed(i, 0);
    ledcDetachPin(kPins[i]);
    pinMode(kPins[i], OUTPUT);
    digitalWrite(kPins[i], LOW);
  }
}

void ledsCelebrateScore() {
  if (!g_attached) {
    return;
  }
  g_fx = LedFx::Crazy;
  g_fx_until = millis() + kScoreFlashMs;
  g_fx_step_at = 0;
}

void ledsBlink(uint8_t times, uint16_t on_ms, uint16_t off_ms) {
  if (!times || !g_attached) {
    return;
  }
  g_fx = LedFx::Blink;
  g_blink_left = times;
  g_blink_on = on_ms ? on_ms : 80;
  g_blink_off = off_ms ? off_ms : 80;
  g_blink_lit = true;
  g_fx_step_at = 0;
}

void ledsTick(LedShow show) {
  if (!g_attached) {
    return;
  }
  uint32_t now = millis();
  if (show != g_show) {
    g_show = show;
    g_show_at = now;
    g_breath_at = 0;
  }
  if (g_fx == LedFx::Crazy) {
    if (now < g_fx_until) {
      tickCrazy(now);
      return;
    }
    g_fx = LedFx::None;
  } else if (g_fx == LedFx::Blink) {
    tickBlink(now);
    if (g_fx == LedFx::Blink) {
      return;
    }
  }
  switch (g_show) {
    case LedShow::Breathe:
      tickBreathe(now);
      break;
    case LedShow::Unison:
      tickUnison(now);
      break;
    case LedShow::Scanner:
      tickScanner(now);
      break;
    case LedShow::Idle:
    default:
      writeAll(0);
      break;
  }
}
