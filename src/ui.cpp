#include "ui.h"

#include "battery.h"
#include "klipper_logo.h"
#include "toy_anims.h"
#include "roster.h"
#include "settings.h"
#include "custom_gifs.h"
#include <LittleFS.h>
#include "blackjack.h"
#include "klippy_avatar.h"
#include "klippy_bird.h"
#include "outbreak.h"
#include "hideseek.h"
#include "schedule.h"
#include "social.h"
#include "spotify.h"
#include "upnp.h"
#include "fonts/Scoreboard36pt7b.h"

#include <AnimatedGIF.h>
#include <algorithm>
#include <new>
#include <ctype.h>
#include <math.h>
#include <string.h>

static constexpr uint16_t kBoard = 0x0000;
static constexpr uint16_t kSegOff = 0x18C3;
static constexpr uint16_t kRail = 0x2104;

static constexpr uint16_t SEG_A1 = 1u << 0;
static constexpr uint16_t SEG_A2 = 1u << 1;
static constexpr uint16_t SEG_B  = 1u << 2;
static constexpr uint16_t SEG_C  = 1u << 3;
static constexpr uint16_t SEG_D2 = 1u << 4;
static constexpr uint16_t SEG_D1 = 1u << 5;
static constexpr uint16_t SEG_E  = 1u << 6;
static constexpr uint16_t SEG_F  = 1u << 7;
static constexpr uint16_t SEG_G1 = 1u << 8;
static constexpr uint16_t SEG_G2 = 1u << 9;
static constexpr uint16_t SEG_H  = 1u << 10;
static constexpr uint16_t SEG_I  = 1u << 11;
static constexpr uint16_t SEG_J  = 1u << 12;
static constexpr uint16_t SEG_K  = 1u << 13;
static constexpr uint16_t SEG_L  = 1u << 14;
static constexpr uint16_t SEG_M  = 1u << 15;

static const uint16_t kSeg09[10] = {
    SEG_A1 | SEG_A2 | SEG_B | SEG_C | SEG_D1 | SEG_D2 | SEG_E | SEG_F,
    SEG_B | SEG_C,
    SEG_A1 | SEG_A2 | SEG_B | SEG_D1 | SEG_D2 | SEG_E | SEG_G1 | SEG_G2,
    SEG_A1 | SEG_A2 | SEG_B | SEG_C | SEG_D1 | SEG_D2 | SEG_G1 | SEG_G2,
    SEG_B | SEG_C | SEG_F | SEG_G1 | SEG_G2,
    SEG_A1 | SEG_A2 | SEG_C | SEG_D1 | SEG_D2 | SEG_F | SEG_G1 | SEG_G2,
    SEG_A1 | SEG_A2 | SEG_C | SEG_D1 | SEG_D2 | SEG_E | SEG_F | SEG_G1 | SEG_G2,
    SEG_A1 | SEG_A2 | SEG_B | SEG_C,
    SEG_A1 | SEG_A2 | SEG_B | SEG_C | SEG_D1 | SEG_D2 | SEG_E | SEG_F | SEG_G1 | SEG_G2,
    SEG_A1 | SEG_A2 | SEG_B | SEG_C | SEG_D1 | SEG_D2 | SEG_F | SEG_G1 | SEG_G2,
};

static const uint16_t kSegAZ[26] = {
    SEG_A1 | SEG_A2 | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G1 | SEG_G2,
    SEG_A1 | SEG_A2 | SEG_B | SEG_C | SEG_D1 | SEG_D2 | SEG_G2 | SEG_I | SEG_L,
    SEG_A1 | SEG_A2 | SEG_D1 | SEG_D2 | SEG_E | SEG_F,
    SEG_A1 | SEG_A2 | SEG_B | SEG_C | SEG_D1 | SEG_D2 | SEG_I | SEG_L,
    SEG_A1 | SEG_A2 | SEG_D1 | SEG_D2 | SEG_E | SEG_F | SEG_G1 | SEG_G2,
    SEG_A1 | SEG_A2 | SEG_E | SEG_F | SEG_G1,
    SEG_A1 | SEG_A2 | SEG_C | SEG_D1 | SEG_D2 | SEG_E | SEG_F | SEG_G2,
    SEG_B | SEG_C | SEG_E | SEG_F | SEG_G1 | SEG_G2,
    SEG_A1 | SEG_A2 | SEG_D1 | SEG_D2 | SEG_I | SEG_L,
    SEG_B | SEG_C | SEG_D1 | SEG_D2 | SEG_E,
    SEG_E | SEG_F | SEG_G1 | SEG_J | SEG_M,
    SEG_D1 | SEG_D2 | SEG_E | SEG_F,
    SEG_B | SEG_C | SEG_E | SEG_F | SEG_H | SEG_J,
    SEG_B | SEG_C | SEG_E | SEG_F | SEG_H | SEG_M,
    SEG_A1 | SEG_A2 | SEG_B | SEG_C | SEG_D1 | SEG_D2 | SEG_E | SEG_F,
    SEG_A1 | SEG_A2 | SEG_B | SEG_E | SEG_F | SEG_G1 | SEG_G2,
    SEG_A1 | SEG_A2 | SEG_B | SEG_C | SEG_D1 | SEG_D2 | SEG_E | SEG_F | SEG_M,
    SEG_A1 | SEG_A2 | SEG_B | SEG_E | SEG_F | SEG_G1 | SEG_G2 | SEG_M,
    SEG_A1 | SEG_A2 | SEG_C | SEG_D1 | SEG_D2 | SEG_F | SEG_G1 | SEG_G2,
    SEG_A1 | SEG_A2 | SEG_I | SEG_L,
    SEG_B | SEG_C | SEG_D1 | SEG_D2 | SEG_E | SEG_F,
    SEG_E | SEG_K | SEG_J | SEG_B,
    SEG_B | SEG_C | SEG_E | SEG_F | SEG_K | SEG_M,
    SEG_H | SEG_J | SEG_K | SEG_M,
    SEG_H | SEG_J | SEG_L,
    SEG_A1 | SEG_A2 | SEG_D1 | SEG_D2 | SEG_J | SEG_K,
};

static uint16_t segMask(char ch) {
  if (ch >= 'a' && ch <= 'z') {
    ch = (char)(ch - 'a' + 'A');
  }
  if (ch >= '0' && ch <= '9') {
    return kSeg09[ch - '0'];
  }
  if (ch >= 'A' && ch <= 'Z') {
    return kSegAZ[ch - 'A'];
  }
  if (ch == '-') {
    return SEG_G1 | SEG_G2;
  }
  if (ch == '_') {
    return SEG_D1 | SEG_D2;
  }
  return 0;
}

static lgfx::LovyanGFX *g_lcd = nullptr;
static lgfx::LovyanGFX *g_panel = nullptr;
static lgfx::LGFX_Sprite *g_fb = nullptr;
static Hit g_hits[40];
static uint8_t g_hit_n = 0;
static bool g_dirty = true;
static bool g_draw_hud = true;
static char g_toast[40] = "";
static uint32_t g_toast_until = 0;

static bool toastLive() {
  return g_toast[0] && (int32_t)(millis() - g_toast_until) < 0;
}

static void beginFb(bool hud = true);
static void endFb();
static void drawOutbreakOverlay();
static bool g_ob_overlay = false;

static void clearHits() { g_hit_n = 0; }

static void addHit(int id, int x, int y, int w, int h) {
  if (g_hit_n >= 40) {
    return;
  }
  g_hits[g_hit_n++] = Hit{x, y, w, h, id};
}

static void roundRect(int x, int y, int w, int h, uint16_t fill, uint16_t edge) {
  g_lcd->fillRoundRect(x, y, w, h, 6, fill);
  g_lcd->drawRoundRect(x, y, w, h, 6, edge);
}

static void useBody() {
  g_lcd->setTextSize(1.0f);
  g_lcd->setFont(&fonts::FreeSansBold9pt7b);
}

static void useTitle() {
  g_lcd->setTextSize(1.0f);
  g_lcd->setFont(&fonts::FreeSansBold12pt7b);
}

static void drawMarquee(const String &text, int x, int y, int w, int h, uint16_t fg) {
  useBody();
  g_lcd->setTextDatum(lgfx::middle_left);
  g_lcd->setTextColor(fg);
  int tw = g_lcd->textWidth(text);
  int cy = y + h / 2;
  if (tw <= w) {
    g_lcd->drawString(text, x, cy);
    return;
  }
  const int gap = 40;
  int loop = tw + gap;
  int off = (int)((millis() / 16) % (uint32_t)loop);
  g_lcd->setClipRect(x, y, w, h);
  g_lcd->drawString(text, x - off, cy);
  g_lcd->drawString(text, x - off + loop, cy);
  g_lcd->clearClipRect();
}

static void drawButton(int x, int y, int w, int h, const char *label, uint16_t fill, uint16_t edge,
                       uint16_t fg, int hit) {
  roundRect(x, y, w, h, fill, edge);
  useBody();
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->setTextColor(fg);
  g_lcd->drawString(label, x + w / 2, y + h / 2);
  addHit(hit, x, y, w, h);
}

static void segH(int x, int y, int w, int t, uint16_t c) {
  if (w < t + 2) {
    return;
  }
  int mid = y + t / 2;
  g_lcd->fillTriangle(x, mid, x + t / 2, y, x + t / 2, y + t - 1, c);
  g_lcd->fillTriangle(x + w - 1, mid, x + w - 1 - t / 2, y, x + w - 1 - t / 2, y + t - 1, c);
  int inner = w - t;
  if (inner > 0) {
    g_lcd->fillRect(x + t / 2, y, inner, t, c);
  }
}

static void segV(int x, int y, int h, int t, uint16_t c) {
  if (h < t + 2) {
    return;
  }
  int mid = x + t / 2;
  g_lcd->fillTriangle(mid, y, x, y + t / 2, x + t - 1, y + t / 2, c);
  g_lcd->fillTriangle(mid, y + h - 1, x, y + h - 1 - t / 2, x + t - 1, y + h - 1 - t / 2, c);
  int inner = h - t;
  if (inner > 0) {
    g_lcd->fillRect(x, y + t / 2, t, inner, c);
  }
}

static void drawSegChar(int x, int y, int w, int h, uint16_t mask, uint16_t on, uint16_t off) {
  int t = max(3, min(w, h) / 7);
  int p = 1;
  int xL = x + p;
  int xR = x + w - p - t;
  int yT = y + p;
  int yB = y + h - p - t;
  int xM = x + (w - t) / 2;
  int yM = y + (h - t) / 2;
  int halfW = max(t + 4, (w - 2 * p) / 2);
  int halfH = max(t + 4, (h - 2 * p - t) / 2);
  float diag_r = (float)t * 0.38f;

  auto draw = [&](uint16_t bit, uint16_t color) {
    if (bit == SEG_A1) {
      segH(xL + t / 2, yT, halfW - t / 2, t, color);
    } else if (bit == SEG_A2) {
      segH(xM, yT, halfW - t / 2, t, color);
    } else if (bit == SEG_D1) {
      segH(xL + t / 2, yB, halfW - t / 2, t, color);
    } else if (bit == SEG_D2) {
      segH(xM, yB, halfW - t / 2, t, color);
    } else if (bit == SEG_G1) {
      segH(xL + t / 2, yM, halfW - t / 2, t, color);
    } else if (bit == SEG_G2) {
      segH(xM, yM, halfW - t / 2, t, color);
    } else if (bit == SEG_F) {
      segV(xL, yT + t / 2, halfH, t, color);
    } else if (bit == SEG_B) {
      segV(xR, yT + t / 2, halfH, t, color);
    } else if (bit == SEG_E) {
      segV(xL, yM + t / 2, halfH, t, color);
    } else if (bit == SEG_C) {
      segV(xR, yM + t / 2, halfH, t, color);
    } else if (bit == SEG_I) {
      segV(xM, yT + t / 2, halfH, t, color);
    } else if (bit == SEG_L) {
      segV(xM, yM + t / 2, halfH, t, color);
    } else if (bit == SEG_H) {
      g_lcd->drawWideLine(xL + t, yT + t, xM, yM, diag_r, color);
    } else if (bit == SEG_J) {
      g_lcd->drawWideLine(xR + t / 2, yT + t, xM + t, yM, diag_r, color);
    } else if (bit == SEG_K) {
      g_lcd->drawWideLine(xL + t, yB + t / 2, xM, yM + t, diag_r, color);
    } else if (bit == SEG_M) {
      g_lcd->drawWideLine(xR + t / 2, yB + t / 2, xM + t, yM + t, diag_r, color);
    }
  };

  static const uint16_t kBits[16] = {SEG_A1, SEG_A2, SEG_B, SEG_C, SEG_D2, SEG_D1, SEG_E, SEG_F,
                                     SEG_G1, SEG_G2, SEG_H, SEG_I, SEG_J, SEG_K, SEG_L, SEG_M};
  for (uint16_t bit : kBits) {
    draw(bit, off);
  }
  for (uint16_t bit : kBits) {
    if (mask & bit) {
      draw(bit, on);
    }
  }
}

static void drawSegName(const char *name, uint16_t color, int top, int bottom,
                        uint16_t off = kSegOff) {
  String n = name ? name : "";
  n.toUpperCase();
  if (!n.length()) {
    n = "?";
  }
  const int pad = 10;
  const int count = (int)n.length();
  const int gap = count > 8 ? 3 : 5;
  int box_w = kScreenW - pad * 2;
  int box_h = bottom - top;
  int digit_w = (box_w - (count - 1) * gap) / max(1, count);
  if (digit_w > 46) {
    digit_w = 46;
  }
  if (digit_w < 14) {
    digit_w = 14;
  }
  int digit_h = min(box_h, (int)(digit_w * 1.85f));
  if (digit_h < 36) {
    digit_h = min(box_h, 36);
  }
  int total = count * digit_w + (count - 1) * gap;
  int x0 = (kScreenW - total) / 2;
  int y0 = top + (box_h - digit_h) / 2;
  for (int i = 0; i < count; i++) {
    drawSegChar(x0 + i * (digit_w + gap), y0, digit_w, digit_h, segMask(n[i]), color, off);
  }
}

static String tagName(const char *name) {
  // Display text is prepared by settingsDisplayName() (nick or first name).
  String n = (name && name[0]) ? name : "?";
  n.toUpperCase();
  return n;
}

static uint16_t inkOn(uint16_t bg) {
  int r = (bg >> 11) & 31;
  int g = (bg >> 5) & 63;
  int b = bg & 31;
  return (r * 2 + g + b) > 72 ? kBoard : kText;
}

static void drawFittedName(const String &n, uint16_t fg, int cx, int cy, int max_w) {
  g_lcd->setTextSize(1.0f);
  g_lcd->setFont(&Scoreboard36pt7b);
  if (g_lcd->textWidth(n) > max_w) {
    g_lcd->setFont(&fonts::FreeSansBold12pt7b);
  }
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->setTextColor(fg);
  g_lcd->drawString(n, cx, cy);
}

static void drawStyleDots(uint8_t style, uint16_t on, uint16_t off) {
  const int gap = 12;
  int x0 = kScreenW / 2 - ((int)kTagStyleCount - 1) * gap / 2;
  int y = kScreenH - 7;
  for (int i = 0; i < kTagStyleCount; i++) {
    g_lcd->fillCircle(x0 + i * gap, y, 3, i == style ? on : off);
  }
}

void uiBegin(LGFX_TDisplayS3 *lcd) {
  g_panel = lcd;
  g_lcd = lcd;
  g_dirty = true;
  if (!g_fb) {
    g_fb = new lgfx::LGFX_Sprite(lcd);
    g_fb->setPsram(true);
    g_fb->setColorDepth(16);
    if (!g_fb->createSprite(kScreenW, kScreenH)) {
      g_fb->setPsram(false);
      if (!g_fb->createSprite(kScreenW, kScreenH)) {
        delete g_fb;
        g_fb = nullptr;
      }
    }
  }
}

void uiNeedRedraw() { g_dirty = true; }

void uiToast(const char *msg, uint32_t ms) {
  strlcpy(g_toast, msg, sizeof(g_toast));
  g_toast_until = millis() + ms;
  g_dirty = true;
}

bool uiConsumeRedraw() {
  if (g_toast[0] && !toastLive()) {
    g_toast[0] = '\0';  // expired: one more frame without it
    g_dirty = true;
  }
  if (!g_dirty) {
    return false;
  }
  g_dirty = false;
  return true;
}

void uiDrawUnprovisioned() {
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  drawSegName("BLENNY", kText, 20, 130);
  useBody();
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->setTextColor(kMuted);
  g_lcd->drawString("Plug in at check-in", kScreenW / 2, 148);
  endFb();
}

void uiDrawDeadBattery() {
  beginFb(false);
  g_lcd->fillScreen(kBoard);
  clearHits();
  const int w = 56;
  const int h = 28;
  int x = (kScreenW - w) / 2;
  int y = 28;
  g_lcd->drawRoundRect(x, y, w, h, 4, kDanger);
  g_lcd->fillRect(x + w, y + 8, 6, h - 16, kDanger);
  useTitle();
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->setTextColor(kDanger);
  g_lcd->drawString("Battery empty", kScreenW / 2, 84);
  useBody();
  g_lcd->setTextColor(kMuted);
  g_lcd->drawString("Plug in USB to charge", kScreenW / 2, 114);
  g_lcd->drawString("Then press a button", kScreenW / 2, 136);
  endFb();
}

static int warmth(int rssi) {
  int v = rssi + 90;
  if (v < 0) v = 0;
  if (v > 60) v = 60;
  return v;
}

static int peerAngle(const char *id) {
  uint32_t h = 2166136261u;
  while (id && *id) {
    h ^= (uint8_t)*id++;
    h *= 16777619u;
  }
  return (int)(h % 360);
}

static uint16_t mix565(uint16_t a, uint16_t b, int t) {
  if (t <= 0) {
    return a;
  }
  if (t >= 256) {
    return b;
  }
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  int r = ar + (((br - ar) * t) >> 8);
  int g = ag + (((bg - ag) * t) >> 8);
  int bl = ab + (((bb - ab) * t) >> 8);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

static uint16_t scaleRgb(uint16_t c, int num, int den) {
  if (den <= 0) {
    den = 1;
  }
  int r = ((c >> 11) & 0x1F) * num / den;
  int g = ((c >> 5) & 0x3F) * num / den;
  int b = (c & 0x1F) * num / den;
  if (r < 0) r = 0;
  if (g < 0) g = 0;
  if (b < 0) b = 0;
  if (r > 31) r = 31;
  if (g > 63) g = 63;
  if (b > 31) b = 31;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

static const char *const kNato[] = {
    "Alpha",    "Bravo",  "Charlie", "Delta",   "Echo",  "Foxtrot", "Golf",
    "Hotel",    "India",  "Juliet",  "Kilo",    "Lima",  "Mike",    "November",
    "Oscar",    "Papa",   "Quebec",  "Romeo",   "Sierra","Tango",   "Uniform",
    "Victor",   "Whiskey","X-ray",   "Yankee",  "Zulu",
};

static int callsignIndex(const char *id) {
  uint32_t h = 2166136261u;
  while (id && *id) {
    h ^= (uint8_t)*id++;
    h *= 16777619u;
  }
  return (int)(h % kNatoCount);
}

static const char *callsign(const char *id) { return kNato[callsignIndex(id)]; }

// ---- Find Friends name hints: each callsign glitches every 3-6 s and briefly shows part of
// the person's first name; a stronger signal (closer badge) reveals more of it. ----
enum GlitchStage : uint8_t { GLITCH_NONE = 0, GLITCH_NOISE, GLITCH_HINT };

static uint32_t idHash(const char *id) {
  uint32_t h = 2166136261u;
  while (id && *id) {
    h ^= (uint8_t)*id++;
    h *= 16777619u;
  }
  return h;
}

// 0.15 s static, 0.3 s hint, 0.15 s static, then the solid callsign until the next one.
static GlitchStage glitchStage(const char *id, uint32_t now) {
  uint32_t h = idHash(id);
  uint32_t period = 3000 + (h % 3000);
  uint32_t local = (now + h * 7919u) % period;
  if (local < 150 || (local >= 450 && local < 600)) {
    return GLITCH_NOISE;
  }
  return local < 450 ? GLITCH_HINT : GLITCH_NONE;
}

// Far: first initial. Closer: first three letters. Right here: first name + last initial.
// Hidden letters stay as '_' so the length is part of the clue.
static String nameHint(const Peer *p, int warm) {
  String full = p->name;
  full.trim();
  int sp = full.indexOf(' ');
  String first = sp > 0 ? full.substring(0, sp) : full;
  String last = sp > 0 ? full.substring(sp + 1) : String("");
  first.toUpperCase();
  last.toUpperCase();
  if (!first.length()) {
    return "?";
  }
  if (warm >= 40) {
    return last.length() ? first + " " + last[0] + "." : first;
  }
  int show = warm >= 20 ? 3 : 1;
  String out;
  for (int i = 0; i < (int)first.length(); i++) {
    out += i < show ? first[i] : '_';
  }
  return out;
}

static String glitchNoise(int len, uint32_t seed) {
  static const char kJunk[] = "#%&@$*!?<>/\\=+[]{}";
  String out;
  for (int i = 0; i < len; i++) {
    seed = seed * 1664525u + 1013904223u;
    out += kJunk[(seed >> 24) % (sizeof(kJunk) - 1)];
  }
  return out;
}

// Text with a red/cyan split behind it, VHS-style.
static void drawSplitText(const String &s, int x, int y, int off, uint16_t main) {
  g_lcd->setTextColor(0xF800);
  g_lcd->drawString(s, x - off, y);
  g_lcd->setTextColor(0x07FF);
  g_lcd->drawString(s, x + off, y);
  g_lcd->setTextColor(main);
  g_lcd->drawString(s, x, y);
}

static String clipText(const String &text, int max_w) {
  if (g_lcd->textWidth(text) <= max_w) {
    return text;
  }
  String t = text;
  while (t.length() && g_lcd->textWidth(t + "...") > max_w) {
    t.remove(t.length() - 1);
  }
  return t + "...";
}

static void drawRadar(const std::vector<Peer *> &peers, int x, int y, int s, uint16_t needle) {
  int cx = x + s / 2;
  int cy = y + s / 2;
  int r = s / 2 - 3;
  uint16_t dim = scaleRgb(needle, 1, 4);
  g_lcd->fillCircle(cx, cy, r + 2, scaleRgb(needle, 1, 10));
  g_lcd->fillCircle(cx, cy, r, kBoard);
  g_lcd->drawCircle(cx, cy, r, dim);
  g_lcd->drawCircle(cx, cy, (r * 2) / 3, dim);
  g_lcd->drawCircle(cx, cy, r / 3, dim);
  g_lcd->drawFastHLine(cx - r, cy, r * 2, dim);
  g_lcd->drawFastVLine(cx, cy - r, r * 2, dim);

  float sweep = fmodf((float)millis() * 0.18f, 360.0f);
  const float deg = 0.01745329252f;
  for (int i = 18; i >= 0; i--) {
    float a = (sweep - (float)i * 3.2f) * deg;
    int x1 = cx + (int)(cosf(a) * (float)r);
    int y1 = cy + (int)(sinf(a) * (float)r);
    g_lcd->drawLine(cx, cy, x1, y1, scaleRgb(needle, 8 - i / 3, 8));
  }

  uint32_t now = millis();
  uint8_t shown = 0;
  for (Peer *p : peers) {
    if (shown >= kNearbyRows) {
      break;
    }
    int ang = peerAngle(p->id);
    int warm = warmth(p->rssi);
    int pr = r - 10 - (warm * (r - 22) / 60);
    if (pr < 10) pr = 10;
    float a = (float)ang * deg;
    int bx = cx + (int)(cosf(a) * (float)pr);
    int by = cy + (int)(sinf(a) * (float)pr);
    float d = fabsf(sweep - (float)ang);
    if (d > 180.0f) d = 360.0f - d;
    int pulse = (int)((now / 80) % 6);
    int rad = (d < 25.0f) ? 3 + pulse / 2 : 2;
    uint16_t col = needle;
    if (radioCooldownRemaining(p->id)) {
      col = kMuted;
    } else if (glitchStage(p->id, now) != GLITCH_NONE) {
      col = ((now / 40) & 1) ? kText : 0x07FF;  // flicker in step with its list row
      rad += 1;
    }
    g_lcd->fillCircle(bx, by, rad, col);
    g_lcd->drawCircle(bx, by, rad + 1, dim);
    shown++;
  }
}

static constexpr uint16_t kBatYellow = 0xFFE0;

static void drawWifiFan(int cx, int cy, float a0, float a1, uint16_t c, int scale) {
  g_lcd->fillCircle(cx, cy, 1, c);
  g_lcd->fillArc(cx, cy, 3 * scale, 2 * scale, a0, a1, c);
  g_lcd->fillArc(cx, cy, 5 * scale, 4 * scale, a0, a1, c);
  g_lcd->fillArc(cx, cy, 7 * scale, 6 * scale, a0, a1, c);
}

static void drawWifiUp(int x, int y, uint16_t c) {
  drawWifiFan(x + 7, y + 10, 225, 315, c, 1);
}

static void drawEspNow(int x, int y, uint16_t c) {
  drawWifiFan(x + 6, y + 6, -40, 40, c, 1);
  drawWifiFan(x + 20, y + 6, 140, 220, c, 1);
}

static void drawBolt(int x, int y, uint16_t c) {
  g_lcd->fillTriangle(x + 4, y, x, y + 6, x + 4, y + 6, c);
  g_lcd->fillTriangle(x + 3, y + 5, x + 8, y + 5, x + 3, y + 13, c);
  g_lcd->fillTriangle(x + 3, y + 5, x + 5, y + 5, x + 4, y + 8, 0x0000);
}

static void drawBatteryIcon(int x, int y, uint8_t segs, BatteryStatus st) {
  const int w = 24;
  const int h = 12;
  uint16_t col = kDanger;
  if (st != BatteryStatus::Discharging) {
    col = (st == BatteryStatus::Charged) ? kOk : kBatYellow;
  } else if (segs >= 4) {
    col = kOk;
  } else if (segs >= 2) {
    col = kBatYellow;
  }
  g_lcd->fillRoundRect(x, y, w, h, 2, 0x0000);
  g_lcd->drawRoundRect(x, y, w, h, 2, col);
  g_lcd->fillRect(x + w, y + 3, 3, h - 6, col);
  const int inset = 2;
  const int gap = 1;
  const int seg_w = 3;
  const int seg_h = h - inset * 2;
  int sx = x + inset + 1;
  int sy = y + inset;
  for (int i = 0; i < 5; i++) {
    uint16_t fill = (i < (int)segs) ? col : 0x2104;
    g_lcd->fillRect(sx + i * (seg_w + gap), sy, seg_w, seg_h, fill);
  }
  if (st == BatteryStatus::Charging || st == BatteryStatus::Charged) {
    drawBolt(x + 8, y - 1, st == BatteryStatus::Charged ? kText : kBatYellow);
  }
}

static void drawStatusHud() {
  uint8_t segs = batterySegments();
  BatteryStatus st = batteryStatus();
  bool now_link = radioEnabled();
  bool wifi = !now_link && netConnected();
  const int y = 3;
  const int bat_w = 27;
  const int radio_w = now_link ? 27 : (wifi ? 15 : 0);
  const int gap = radio_w ? 4 : 0;
  const int cluster = bat_w + radio_w + gap;
  const int x0 = kScreenW - 4 - cluster;
  g_lcd->fillRoundRect(x0 - 3, y - 2, cluster + 6, 16, 3, 0x0000);
  int x = x0;
  if (now_link) {
    drawEspNow(x, y, kOk);
    x += radio_w + gap;
  } else if (wifi) {
    drawWifiUp(x, y, kOk);
    x += radio_w + gap;
  }
  drawBatteryIcon(x, y, segs, st);
}

static void beginFb(bool hud) {
  g_draw_hud = hud;
  if (g_fb) {
    g_lcd = g_fb;
  }
}

// Pill along the bottom edge, drawn over whatever screen is up.
static void drawToast() {
  if (!toastLive()) {
    return;
  }
  useBody();
  const int w = g_lcd->textWidth(g_toast) + 24;
  const int h = 26;
  const int x = (kScreenW - w) / 2;
  const int y = kScreenH - h - 6;
  g_lcd->fillRoundRect(x, y, w, h, 13, kBoard);
  g_lcd->drawRoundRect(x, y, w, h, 13, settingsColorRgb());
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->setTextColor(kText);
  g_lcd->drawString(g_toast, kScreenW / 2, y + h / 2 + 1);
}

static void endFb() {
  if (g_ob_overlay) {
    drawOutbreakOverlay();
  }
  if (g_draw_hud) {
    drawStatusHud();
  }
  drawToast();
  if (g_fb) {
    g_fb->pushSprite(0, 0);
    g_lcd = g_panel ? g_panel : g_fb->getParent();
  }
}

void uiPaintHud() {
  if (!g_panel) {
    return;
  }
  lgfx::LovyanGFX *prev = g_lcd;
  g_lcd = g_panel;
  drawStatusHud();
  g_lcd = prev;
}

struct TagRng {
  uint32_t s;
  explicit TagRng(uint32_t seed) : s(seed ? seed : 1u) {}
  uint32_t next() {
    s = s * 1664525u + 1013904223u;
    return s;
  }
  int between(int lo, int hi) {
    if (hi <= lo) {
      return lo;
    }
    return lo + (int)(next() % (uint32_t)(hi - lo + 1));
  }
};

static uint32_t tagSeed(const String &n, uint32_t salt) {
  uint32_t h = 2166136261u ^ salt;
  for (size_t i = 0; i < n.length(); i++) {
    h ^= (uint8_t)n[i];
    h *= 16777619u;
  }
  return h;
}

static lgfx::LGFX_Sprite *g_ch = nullptr;

static lgfx::LGFX_Sprite *letterSprite() {
  if (g_ch) {
    return g_ch;
  }
  g_ch = new lgfx::LGFX_Sprite(g_panel ? g_panel : g_fb);
  g_ch->setPsram(true);
  g_ch->setColorDepth(16);
  if (!g_ch->createSprite(64, 72)) {
    g_ch->setPsram(false);
    if (!g_ch->createSprite(56, 64)) {
      delete g_ch;
      g_ch = nullptr;
    }
  }
  return g_ch;
}

static void setRansomFont(lgfx::LovyanGFX *d, int id) {
  d->setTextSize(1.0f);
  switch (id % 11) {
    case 0:
      d->setFont(&fonts::FreeSansBold18pt7b);
      break;
    case 1:
      d->setFont(&fonts::FreeSerifBold18pt7b);
      break;
    case 2:
      d->setFont(&fonts::FreeMonoBold18pt7b);
      break;
    case 3:
      d->setFont(&fonts::FreeSerifItalic18pt7b);
      break;
    case 4:
      d->setFont(&fonts::FreeSansBold12pt7b);
      break;
    case 5:
      d->setFont(&fonts::FreeSerifBold12pt7b);
      break;
    case 6:
      d->setFont(&fonts::Orbitron_Light_24);
      break;
    case 7:
      d->setFont(&Scoreboard36pt7b);
      break;
    case 8:
      d->setFont(&fonts::Yellowtail_32);
      break;
    case 9:
      d->setFont(&fonts::Satisfy_24);
      break;
    default:
      d->setFont(&fonts::Font4);
      d->setTextSize(2.0f);
      break;
  }
}

static void drawTornPaper(lgfx::LGFX_Sprite *spr, uint16_t paper, uint16_t chroma, TagRng &rng) {
  int w = spr->width();
  int h = spr->height();
  spr->fillScreen(chroma);
  int x0 = 6 + rng.between(-3, 4);
  int y0 = 6 + rng.between(-3, 4);
  int x1 = w - 6 + rng.between(-4, 3);
  int y1 = 6 + rng.between(-3, 4);
  int x2 = w - 6 + rng.between(-4, 3);
  int y2 = h - 6 + rng.between(-4, 3);
  int x3 = 6 + rng.between(-3, 4);
  int y3 = h - 6 + rng.between(-4, 3);
  spr->fillTriangle(x0, y0, x1, y1, x2, y2, paper);
  spr->fillTriangle(x0, y0, x2, y2, x3, y3, paper);
  for (int i = 0; i < 4; i++) {
    int n = rng.between(4, 11);
    if (i == 0) {
      spr->fillTriangle(x0, y0, x0 + n, y0, x0, y0 + n, chroma);
    } else if (i == 1) {
      spr->fillTriangle(x1, y1, x1 - n, y1, x1, y1 + n, chroma);
    } else if (i == 2) {
      spr->fillTriangle(x2, y2, x2 - n, y2, x2, y2 - n, chroma);
    } else {
      spr->fillTriangle(x3, y3, x3 + n, y3, x3, y3 - n, chroma);
    }
  }
}

static float clamp01(float t) {
  if (t < 0.0f) {
    return 0.0f;
  }
  if (t > 1.0f) {
    return 1.0f;
  }
  return t;
}

static float animT(int anim_ms) {
  if (anim_ms < 0) {
    return 1.0f;
  }
  return clamp01((float)anim_ms / (float)kTagAnimMs);
}

static float easeOutCubic(float t) {
  t = clamp01(t);
  float u = 1.0f - t;
  return 1.0f - u * u * u;
}

static char rollTo(char target, float t) {
  if (t <= 0.0f) {
    return '0';
  }
  if (t >= 1.0f) {
    return target;
  }
  int step = (int)(t * 10.0f);
  if (step >= 10) {
    return target;
  }
  return (char)('0' + step);
}

static void drawRansomName(const String &n, uint16_t accent, int anim_ms) {
  const uint16_t news = 0xC4A9;
  const uint16_t chroma = 0x07E1;
  g_lcd->fillScreen(news);
  TagRng grain(tagSeed(n, 0xC0C0u));
  for (int i = 0; i < 420; i++) {
    int x = grain.between(0, kScreenW - 1);
    int y = grain.between(0, kScreenH - 1);
    g_lcd->drawPixel(x, y, mix565(news, kBoard, grain.between(20, 90)));
  }
  for (int y = 14; y < kScreenH - 10; y += 9) {
    g_lcd->drawFastHLine(0, y, kScreenW, mix565(news, kBoard, 28));
  }

  static const uint16_t kPaper[] = {0xFFFF, 0xEF5D, 0xF6F2, 0xFFE0, 0xC618, 0x0000, 0xF800, 0x0011};
  static const uint16_t kInk[] = {0x0000, 0x0000, 0x0015, 0x8000, 0xFFFF, 0xFFE0};

  int count = (int)n.length();
  int slot = (kScreenW - 8) / max(1, count);
  lgfx::LGFX_Sprite *spr = letterSprite();
  TagRng rng(tagSeed(n, 0xA11Eu));

  for (int i = 0; i < count; i++) {
    uint16_t paper = kPaper[rng.between(0, 7)];
    if (i == count / 2) {
      paper = (inkOn(accent) == kBoard) ? 0x0000 : 0xFFFF;
    }
    uint16_t ink = kInk[rng.between(0, 5)];
    if (i == rng.between(0, max(0, count - 1)) || i == count / 3) {
      ink = accent;
    }
    if (inkOn(paper) == inkOn(ink)) {
      ink = inkOn(paper);
    }
    if (paper == chroma) {
      paper = 0xEF5D;
    }
    int cx = 4 + slot * i + slot / 2 + rng.between(-4, 4);
    int cy = kScreenH / 2 - 2 + rng.between(-16, 14);
    float angle = (float)rng.between(-18, 18);
    float zoom = count >= 8 ? 0.72f : (count <= 4 ? 1.12f : 0.92f);
    zoom += rng.between(-8, 10) * 0.01f;
    uint32_t paper_seed = rng.next();
    int font_id = rng.between(0, 10);
    float local = easeOutCubic(clamp01((animT(anim_ms) - i * 0.07f) / 0.38f));
    cy += (int)((1.0f - local) * -96.0f);
    angle += sinf((float)i + animT(anim_ms) * 14.0f) * (1.0f - local) * 28.0f;
    zoom *= 0.45f + 0.55f * local;
    if (local < 0.02f) {
      continue;
    }
    char glyph[2] = {n[i], 0};

    if (spr) {
      TagRng shadow_rng(paper_seed);
      TagRng paper_rng(paper_seed);
      drawTornPaper(spr, mix565(kBoard, paper, 90), chroma, shadow_rng);
      spr->setPivot(spr->width() / 2.0f, spr->height() / 2.0f);
      spr->pushRotateZoomWithAA(g_lcd, cx + 2, cy + 3, angle, zoom, zoom, chroma);
      drawTornPaper(spr, paper, chroma, paper_rng);
      setRansomFont(spr, font_id);
      spr->setTextDatum(lgfx::middle_center);
      spr->setTextColor(ink);
      spr->drawString(glyph, spr->width() / 2, spr->height() / 2 + 2);
      spr->pushRotateZoomWithAA(g_lcd, cx, cy, angle, zoom, zoom, chroma);
    } else {
      g_lcd->fillRect(cx - 16, cy - 22, 32, 44, paper);
      setRansomFont(g_lcd, font_id);
      g_lcd->setTextDatum(lgfx::middle_center);
      g_lcd->setTextColor(ink);
      g_lcd->drawString(glyph, cx, cy);
    }
  }

  if (spr) {
    const uint16_t tape = 0xD64C;
    for (int t = 0; t < min(4, count); t++) {
      int cx = 20 + rng.between(0, kScreenW - 40);
      int cy = 28 + rng.between(0, kScreenH - 50);
      float angle = (float)rng.between(-40, 40);
      spr->fillScreen(chroma);
      spr->fillRect(10, 30, 44, 9, tape);
      spr->fillRect(10, 30, 44, 2, mix565(tape, kText, 80));
      spr->setPivot(spr->width() / 2.0f, spr->height() / 2.0f);
      spr->pushRotateZoomWithAA(g_lcd, cx, cy, angle, 0.85f, 0.85f, chroma);
    }
  }
}

static void crayonStroke(float x0, float y0, float x1, float y1, uint16_t c, TagRng &rng) {
  float mx = (x0 + x1) * 0.5f + rng.between(-3, 3);
  float my = (y0 + y1) * 0.5f + rng.between(-3, 3);
  auto pass = [&](float ax, float ay, float bx, float by) {
    for (int p = 0; p < 4; p++) {
      uint16_t col = mix565(c, (p & 1) ? kText : kBoard, rng.between(12, 60));
      float r = p == 0 ? 3.1f : 1.7f + 0.35f * p;
      g_lcd->drawWideLine(ax + rng.between(-2, 2), ay + rng.between(-2, 2), bx + rng.between(-2, 2),
                          by + rng.between(-2, 2), r, col);
    }
  };
  pass(x0, y0, mx, my);
  pass(mx, my, x1, y1);
}

static void drawCrayonChar(int x, int y, int w, int h, uint16_t mask, uint16_t wax, TagRng &rng) {
  int t = max(4, min(w, h) / 7);
  int p = 2;
  int xL = x + p;
  int xR = x + w - p - t;
  int yT = y + p;
  int yB = y + h - p - t;
  int xM = x + (w - t) / 2;
  int yM = y + (h - t) / 2;
  int halfW = max(t + 4, (w - 2 * p) / 2);
  int halfH = max(t + 4, (h - 2 * p - t) / 2);
  auto H = [&](int sx, int sy, int sw) {
    crayonStroke(sx, sy + t / 2, sx + sw, sy + t / 2, wax, rng);
  };
  auto V = [&](int sx, int sy, int sh) {
    crayonStroke(sx + t / 2, sy, sx + t / 2, sy + sh, wax, rng);
  };
  if (mask & SEG_A1) {
    H(xL + t / 2, yT, halfW - t / 2);
  }
  if (mask & SEG_A2) {
    H(xM, yT, halfW - t / 2);
  }
  if (mask & SEG_D1) {
    H(xL + t / 2, yB, halfW - t / 2);
  }
  if (mask & SEG_D2) {
    H(xM, yB, halfW - t / 2);
  }
  if (mask & SEG_G1) {
    H(xL + t / 2, yM, halfW - t / 2);
  }
  if (mask & SEG_G2) {
    H(xM, yM, halfW - t / 2);
  }
  if (mask & SEG_F) {
    V(xL, yT + t / 2, halfH);
  }
  if (mask & SEG_B) {
    V(xR, yT + t / 2, halfH);
  }
  if (mask & SEG_E) {
    V(xL, yM + t / 2, halfH);
  }
  if (mask & SEG_C) {
    V(xR, yM + t / 2, halfH);
  }
  if (mask & SEG_I) {
    V(xM, yT + t / 2, halfH);
  }
  if (mask & SEG_L) {
    V(xM, yM + t / 2, halfH);
  }
  if (mask & SEG_H) {
    crayonStroke(xL + t, yT + t, xM, yM, wax, rng);
  }
  if (mask & SEG_J) {
    crayonStroke(xR + t / 2, yT + t, xM + t, yM, wax, rng);
  }
  if (mask & SEG_K) {
    crayonStroke(xL + t, yB + t / 2, xM, yM + t, wax, rng);
  }
  if (mask & SEG_M) {
    crayonStroke(xR + t / 2, yB + t / 2, xM + t, yM + t, wax, rng);
  }
}

static void drawCrayonName(const String &n, uint16_t color, int anim_ms) {
  const uint16_t paper = mix565(0xF5D3, color, 28);
  g_lcd->fillScreen(paper);
  TagRng rng(tagSeed(n, 0xC4A0u));
  for (int i = 0; i < 380; i++) {
    g_lcd->drawPixel(rng.between(0, kScreenW - 1), rng.between(0, kScreenH - 1),
                     mix565(paper, rng.between(0, 1) ? kText : kBoard, rng.between(12, 50)));
  }
  uint16_t wax = (inkOn(color) == kBoard) ? mix565(color, kBoard, 120) : color;
  for (int x = 8; x < kScreenW - 8; x += 14) {
    crayonStroke(x, 10 + rng.between(-2, 2), x + 14, 10 + rng.between(-2, 2), wax, rng);
    crayonStroke(x, kScreenH - 18 + rng.between(-2, 2), x + 14, kScreenH - 18 + rng.between(-2, 2), wax,
                 rng);
  }
  for (int y = 10; y < kScreenH - 18; y += 16) {
    crayonStroke(10 + rng.between(-2, 2), y, 10 + rng.between(-2, 2), y + 16, wax, rng);
    crayonStroke(kScreenW - 10 + rng.between(-2, 2), y, kScreenW - 10 + rng.between(-2, 2), y + 16, wax,
                 rng);
  }

  int count = (int)n.length();
  int gap = count > 8 ? 2 : 6;
  int box_w = kScreenW - 28;
  int digit_w = (box_w - (count - 1) * gap) / max(1, count);
  if (digit_w > 44) {
    digit_w = 44;
  }
  if (digit_w < 16) {
    digit_w = 16;
  }
  int digit_h = min(118, (int)(digit_w * 1.7f));
  int total = count * digit_w + (count - 1) * gap;
  int x0 = (kScreenW - total) / 2;
  int y0 = 22 + (kScreenH - 40 - digit_h) / 2;
  float t = animT(anim_ms);
  int revealed = (int)n.length();
  if (anim_ms >= 0) {
    revealed = (int)(t * ((int)n.length() + 0.35f));
    if (revealed > (int)n.length()) {
      revealed = (int)n.length();
    }
  }
  for (int i = 0; i < revealed; i++) {
    int jx = rng.between(-3, 3);
    int jy = rng.between(-8, 8);
    int dw = digit_w + rng.between(-3, 4);
    int dh = digit_h + rng.between(-6, 8);
    drawCrayonChar(x0 + i * (digit_w + gap) + jx, y0 + jy, dw, dh, segMask(n[i]), wax, rng);
  }
}

static void drawFullscreenSpectrum(uint32_t t_ms);

// ---- Idle-animated classics (styles 4..7): name only, fullscreen. anim_ms < 0 = still. ----

// Neon: script lettering lit like glass tube on a clear acrylic backer.
// The name is rendered once into a mask, smoothed, and turned into inside/outside distance
// fields; each frame shades pixels from those (white-hot core, saturated edge, wall glow).
static int neonLit(int anim_ms, int i, int bad) {
  if (anim_ms < 0) {
    return 256;
  }
  uint32_t slot = (uint32_t)anim_ms / 60;
  if (anim_ms < 700) {
    TagRng r(slot * 2654435761u + 7u);
    int v = r.between(0, 9);
    return v < 3 ? 0 : (v < 5 ? 140 : 256);
  }
  if (i == bad && anim_ms > 1300 && anim_ms < 2900) {
    TagRng r(slot * 40503u + 11u);
    int v = r.between(0, 9);
    return v < 6 ? 0 : (v < 8 ? 110 : 256);
  }
  return 256;
}

static constexpr int kNeonPx = kScreenW * kScreenH;
static constexpr int kNeonMaxLetters = 24;

// Neon pipeline (once per name): render each letter into a mask → smooth → find the
// stroke centreline (ridge of the inside distance) → distance from that centreline.
// Frames then shade a fixed-radius round tube around the centreline plus glow.
struct NeonField {
  String key;
  uint8_t *dist = nullptr;  // distance to tube centreline, in 1/3 px, clamped 255
  uint8_t *own = nullptr;   // letter whose centreline is nearest
  uint16_t *px = nullptr;   // shaded frame
  int count = 0;
  int radius = 9;           // tube radius, 1/3 px
};
static NeonField g_neon;

static void *neonAlloc(size_t n) {
  void *p = ps_malloc(n);
  return p ? p : malloc(n);
}

// Chamfer 3-4 distance in place; d holds 0 at seeds and 0xFFFF elsewhere (units of 1/3 px).
// When own is given, each pixel also inherits the owner of its nearest seed.
static void neonChamfer(uint16_t *d, uint8_t *own = nullptr) {
  const int W = kScreenW;
  const int H = kScreenH;
  auto relax = [&](int i, int j, uint32_t cost, uint32_t &v) {
    uint32_t c = d[j] + cost;
    if (c < v) {
      v = c;
      if (own) {
        own[i] = own[j];
      }
    }
  };
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      int i = y * W + x;
      uint32_t v = d[i];
      if (!v) {
        continue;
      }
      if (x > 0) relax(i, i - 1, 3u, v);
      if (y > 0) {
        relax(i, i - W, 3u, v);
        if (x > 0) relax(i, i - W - 1, 4u, v);
        if (x + 1 < W) relax(i, i - W + 1, 4u, v);
      }
      d[i] = (uint16_t)min<uint32_t>(v, 0xFFFFu);
    }
  }
  for (int y = H - 1; y >= 0; y--) {
    for (int x = W - 1; x >= 0; x--) {
      int i = y * W + x;
      uint32_t v = d[i];
      if (!v) {
        continue;
      }
      if (x + 1 < W) relax(i, i + 1, 3u, v);
      if (y + 1 < H) {
        relax(i, i + W, 3u, v);
        if (x + 1 < W) relax(i, i + W + 1, 4u, v);
        if (x > 0) relax(i, i + W - 1, 4u, v);
      }
      d[i] = (uint16_t)min<uint32_t>(v, 0xFFFFu);
    }
  }
}

static bool neonBuild(const String &n) {
  if (g_neon.px && g_neon.key == n) {
    return true;
  }
  if (!g_neon.dist) {
    g_neon.dist = (uint8_t *)neonAlloc(kNeonPx);
    g_neon.own = (uint8_t *)neonAlloc(kNeonPx);
    g_neon.px = (uint16_t *)neonAlloc(kNeonPx * 2);
  }
  uint16_t *tmp = (uint16_t *)neonAlloc(kNeonPx * 2);
  uint8_t *mask = (uint8_t *)neonAlloc(kNeonPx);
  uint8_t *who = (uint8_t *)neonAlloc(kNeonPx);
  auto bail = [&]() {
    free(tmp);
    free(mask);
    free(who);
    return false;
  };
  if (!g_neon.dist || !g_neon.own || !g_neon.px || !tmp || !mask || !who) {
    return bail();
  }
  const int W = kScreenW;
  const int H = kScreenH;

  // 1. Each letter drawn on its own so every stroke knows exactly which letter it is.
  lgfx::LGFX_Sprite m(g_panel ? g_panel : g_fb);
  m.setPsram(true);
  m.setColorDepth(8);
  if (!m.createSprite(W, H)) {
    return bail();
  }
  m.setFont(&fonts::Satisfy_24);
  m.setTextSize(1.0f);
  float w1 = (float)max(1, (int)m.textWidth(n));
  float h1 = (float)max(1, (int)m.fontHeight());
  float s = min((float)(W - 28) / w1, (float)(H - 44) / h1);
  if (s > 5.5f) {
    s = 5.5f;
  }
  m.setTextSize(s);
  m.setTextDatum(lgfx::middle_left);
  m.setTextColor(TFT_WHITE);
  int x0 = W / 2 - (int)m.textWidth(n) / 2;
  int count = min((int)n.length(), kNeonMaxLetters);
  memset(mask, 0, kNeonPx);
  memset(who, 0, kNeonPx);
  const uint8_t *buf = (const uint8_t *)m.getBuffer();
  char ch[2] = {0, 0};
  for (int k = 0; k < count; k++) {
    ch[0] = n[k];
    m.fillScreen(0);
    m.drawString(ch, x0 + (k ? (int)m.textWidth(n.substring(0, k)) : 0), H / 2 + 4);
    for (int i = 0; i < kNeonPx; i++) {
      if (buf[i]) {
        mask[i] = 1;
        who[i] = (uint8_t)k;
      }
    }
  }
  m.deleteSprite();

  // 2. Box-blur + threshold rounds off the stair-steps from scaling a small font up.
  int b = (int)(s * 0.5f + 0.5f);
  b = b < 1 ? 1 : (b > 3 ? 3 : b);
  for (int y = 0; y < H; y++) {
    int sum = 0;
    for (int x = -b; x <= b; x++) {
      sum += (x >= 0 && x < W) ? mask[y * W + x] : 0;
    }
    for (int x = 0; x < W; x++) {
      tmp[y * W + x] = (uint16_t)sum;
      int xo = x - b;
      int xi = x + b + 1;
      if (xo >= 0) sum -= mask[y * W + xo];
      if (xi < W) sum += mask[y * W + xi];
    }
  }
  // Owner for every pixel = nearest raw letter pixel (computed before mask is smoothed).
  {
    uint16_t *seed = (uint16_t *)neonAlloc(kNeonPx * 2);
    if (!seed) {
      return bail();
    }
    for (int i = 0; i < kNeonPx; i++) {
      seed[i] = mask[i] ? 0 : 0xFFFF;
      g_neon.own[i] = who[i];
    }
    neonChamfer(seed, g_neon.own);
    free(seed);
  }
  const int area = (2 * b + 1) * (2 * b + 1);
  for (int x = 0; x < W; x++) {
    int sum = 0;
    for (int y = -b; y <= b; y++) {
      sum += (y >= 0 && y < H) ? tmp[y * W + x] : 0;
    }
    for (int y = 0; y < H; y++) {
      mask[y * W + x] = (sum * 2 > area) ? 1 : 0;
      int yo = y - b;
      int yi = y + b + 1;
      if (yo >= 0) sum -= tmp[yo * W + x];
      if (yi < H) sum += tmp[yi * W + x];
    }
  }

  // 3. Centreline = ridge of the inside distance (local max across x or y).
  for (int i = 0; i < kNeonPx; i++) {
    tmp[i] = mask[i] ? 0xFFFF : 0;
  }
  neonChamfer(tmp);
  auto din = [&](int x, int y) -> uint16_t {
    return (x < 0 || y < 0 || x >= W || y >= H) ? 0 : tmp[y * W + x];
  };
  memset(who, 0, kNeonPx);
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      uint16_t d = din(x, y);
      if (!d) {
        continue;
      }
      bool ridge_x = d >= din(x - 1, y) && d >= din(x + 1, y);
      bool ridge_y = d >= din(x, y - 1) && d >= din(x, y + 1);
      bool ridge_d1 = d >= din(x - 1, y - 1) && d >= din(x + 1, y + 1);
      bool ridge_d2 = d >= din(x + 1, y - 1) && d >= din(x - 1, y + 1);
      if (ridge_x || ridge_y || ridge_d1 || ridge_d2) {
        who[y * W + x] = 1;
      }
    }
  }

  // 4. Distance from the centreline; owner follows the nearest centreline pixel.
  for (int i = 0; i < kNeonPx; i++) {
    tmp[i] = who[i] ? 0 : 0xFFFF;
  }
  neonChamfer(tmp, g_neon.own);
  for (int i = 0; i < kNeonPx; i++) {
    g_neon.dist[i] = (uint8_t)min<uint32_t>(tmp[i], 255u);
  }
  int r = (int)(s * 2.4f + 0.5f);  // ≈ 0.8 px of tube radius per unit of font scale
  g_neon.radius = r < 7 ? 7 : (r > 14 ? 14 : r);
  g_neon.count = max(1, count);
  free(tmp);
  free(mask);
  free(who);
  g_neon.key = n;
  return true;
}

static String neonText(const char *name) {
  String n = (name && name[0]) ? name : "?";
  n.trim();
  bool lower = false;
  for (size_t i = 0; i < n.length(); i++) {
    if (n[i] >= 'a' && n[i] <= 'z') {
      lower = true;
    }
  }
  if (!lower) {
    n.toLowerCase();
    bool start = true;
    for (size_t i = 0; i < n.length(); i++) {
      if (start && n[i] >= 'a' && n[i] <= 'z') {
        n.setCharAt(i, (char)(n[i] - 'a' + 'A'));
      }
      start = n[i] == ' ' || n[i] == '-';
    }
  }
  return n;
}


static void drawNeonName(const char *name, uint16_t color, int anim_ms) {
  const uint16_t wall = 0x0802;
  const uint16_t grid = 0x1845;
  String n = neonText(name);
  if (!neonBuild(n)) {
    g_lcd->fillScreen(wall);
    drawFittedName(n, color, kScreenW / 2, kScreenH / 2, kScreenW - 24);
    return;
  }

  const int count = g_neon.count;
  int bad = (int)(tagSeed(n, 0x4E30u) % (uint32_t)count);
  int lit[kNeonMaxLetters];
  for (int i = 0; i < count; i++) {
    lit[i] = neonLit(anim_ms, i, bad);
  }

  // Shade by distance from the tube centreline (u, 1/3 px):
  //   0..R  round glass tube, white-hot core → saturated rim
  //   >R    glow falling off across the wall
  const int R = g_neon.radius;
  const uint16_t core = mix565(color, kText, 200);
  const uint16_t rim = mix565(color, kText, 20);
  const uint16_t glass = mix565(wall, color, 60);
  uint16_t tube[16];
  for (int u = 0; u <= R && u < 16; u++) {
    float t = (float)u / (float)R;
    tube[u] = mix565(core, rim, (int)(t * t * 256.0f));
  }
  uint8_t glow[256];
  for (int u = 0; u < 256; u++) {
    float g = (float)(u - R) / 3.0f;  // px beyond the tube wall
    float a = g <= 0 ? 0 : 175.0f * expf(-g / 2.6f) + 60.0f * expf(-g / 10.0f);
    glow[u] = (uint8_t)min(230.0f, a);
  }
  const int backer = R + 3 * 12;

  const int W = kScreenW;
  for (int y = 0; y < kScreenH; y++) {
    bool grid_row = (y % 34) == 12;
    for (int x = 0; x < W; x++) {
      int i = y * W + x;
      int u = g_neon.dist[i];
      int L = lit[min((int)g_neon.own[i], count - 1)];
      uint16_t c;
      if (u <= R) {
        c = mix565(glass, tube[u], L);
      } else {
        uint16_t base = (grid_row || (x % 34) == 20) ? grid : wall;
        // Tube depth: a dim copy just behind and below the stroke.
        if (x >= 3 && y >= 3) {
          int j = i - 3 * W - 3;
          if (g_neon.dist[j] <= R) {
            int Ls = lit[min((int)g_neon.own[j], count - 1)];
            base = mix565(base, color, (70 * Ls) >> 8);
          }
        }
        // Clear acrylic backer edge tracing the word.
        if (u == backer || u == backer + 1) {
          base = mix565(base, 0x9CF3, 45);
        }
        c = mix565(base, color, (glow[u] * L) >> 8);
      }
      g_neon.px[i] = c;
    }
  }
  g_lcd->pushImage(0, 0, kScreenW, kScreenH, (const lgfx::rgb565_t *)g_neon.px);
}

// Split-flap: tiles only. Idle: everything flips to 0, holds, flips back to the name.
// Letters are drawn once at DejaVu 72's native size into a small sprite, then scaled onto
// the tile with antialiasing so edges stay smooth at any tile size.
static lgfx::LGFX_Sprite *g_flap_glyph = nullptr;

static lgfx::LGFX_Sprite *flapGlyphSprite() {
  if (g_flap_glyph) {
    return g_flap_glyph;
  }
  g_flap_glyph = new lgfx::LGFX_Sprite(g_panel ? g_panel : g_fb);
  g_flap_glyph->setPsram(true);
  g_flap_glyph->setColorDepth(16);
  g_flap_glyph->setFont(&fonts::DejaVu72);
  int gw = g_flap_glyph->textWidth("W") + 8;
  int gh = g_flap_glyph->fontHeight() + 4;
  if (!g_flap_glyph->createSprite(gw, gh)) {
    delete g_flap_glyph;
    g_flap_glyph = nullptr;
  }
  return g_flap_glyph;
}

static void drawFlapTile(int x, int y, int w, int h, char ch, uint16_t ink, float flap) {
  const uint16_t tile = 0x2124;
  const uint16_t leaf = 0x18E3;
  int half = h / 2;
  g_lcd->fillRoundRect(x, y, w, h, 6, tile);
  lgfx::LGFX_Sprite *gs = flapGlyphSprite();
  if (gs && ch != ' ') {
    gs->fillScreen(tile);
    gs->setFont(&fonts::DejaVu72);
    gs->setTextSize(1.0f);
    gs->setTextDatum(lgfx::middle_center);
    gs->setTextColor(ink);
    char buf[2] = {ch, 0};
    gs->drawString(buf, gs->width() / 2, gs->height() / 2);
    // Caps fill ~68% of the tile height (cap height ≈ 0.62 × line height), capped by width.
    float cap = (float)gs->fontHeight() * 0.62f;
    float z = min((float)h * 0.68f / cap, (float)w * 0.84f / (float)gs->textWidth("W"));
    gs->pushRotateZoomWithAA(g_lcd, (float)x + (float)w / 2.0f, (float)y + (float)half + 1.0f,
                             0.0f, z, z);
  }
  if (flap > 0.0f) {
    int fh = (int)((float)half * (1.0f - flap));
    if (fh > 0) {
      g_lcd->fillRect(x, y + half - fh, w, fh, leaf);
      g_lcd->drawFastHLine(x, y + half - fh, w, 0x39E7);
    }
  }
  g_lcd->fillRect(x, y + half - 1, w, 2, kBoard);
  g_lcd->fillRect(x - 1, y + half - 4, 3, 8, kBoard);
  g_lcd->fillRect(x + w - 2, y + half - 4, 3, 8, kBoard);
}

// Real split-flap order: each tile steps forward through this deck to reach its letter.
static const char kFlapDeck[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static constexpr int kFlapDeckLen = sizeof(kFlapDeck) - 1;

static int flapIndex(char c) {
  const char *p = strchr(kFlapDeck, toupper((unsigned char)c));
  return (p && c) ? (int)(p - kFlapDeck) : 0;
}

// One tile flipping from `from` to `to`, starting `t` ms ago; fills the shown char + flap.
static bool flapStep(int t, char from, char to, char *out, float *flap) {
  const int step_ms = 35;
  int a = flapIndex(from);
  int steps = (flapIndex(to) - a + kFlapDeckLen) % kFlapDeckLen;
  if (t < 0) {
    *out = from;
    *flap = 0.0f;
    return false;
  }
  if (t >= steps * step_ms) {
    *out = to;
    *flap = 0.0f;
    return true;
  }
  *out = kFlapDeck[(a + t / step_ms) % kFlapDeckLen];
  *flap = (float)(t % step_ms) / (float)step_ms;
  return false;
}

// Idle burst: the board flips name -> CNET, holds, then flips back to the name.
static void flapState(int anim_ms, int i, int count, char name_ch, char *out, float *flap) {
  *flap = 0.0f;
  *out = name_ch;
  if (anim_ms < 0) {
    return;
  }
  static const char kWord[] = "CNET";
  const int word_len = 4;
  const int lead = (count - word_len) / 2;
  char word_ch = (i >= lead && i < lead + word_len) ? kWord[i - lead] : ' ';
  const int stagger = 45;
  const int out_at = 100;
  const int back_at = 2250;
  if (anim_ms < back_at) {
    flapStep(anim_ms - out_at - i * stagger, name_ch, word_ch, out, flap);
  } else {
    flapStep(anim_ms - back_at - i * stagger, word_ch, name_ch, out, flap);
  }
}

static void drawFlapName(const String &n, uint16_t color, int anim_ms) {
  g_lcd->fillScreen(kBoard);
  // Always at least four tiles so CNET fits; short names sit centred among blanks.
  int len = (int)n.length();
  int count = max(4, len);
  int pad = (count - len) / 2;
  const int gap = count > 8 ? 3 : 5;
  int tw = (kScreenW - 12 - gap * (count - 1)) / count;
  if (tw > 96) {
    tw = 96;
  }
  int th = min(kScreenH - 16, (int)(tw * 1.5f));
  int x0 = (kScreenW - (count * tw + (count - 1) * gap)) / 2;
  int y0 = (kScreenH - th) / 2;

  for (int i = 0; i < count; i++) {
    int ni = i - pad;
    char target = (ni >= 0 && ni < len) ? n[ni] : ' ';
    char ch;
    float flap;
    flapState(anim_ms, i, count, target, &ch, &flap);
    drawFlapTile(x0 + i * (tw + gap), y0, tw, th, ch, color, flap);
  }
}

// CRT: just the name in phosphor, scanlines and a curved bezel. Idle: roll + retype.
static void drawCrtName(const String &n, uint16_t color, int anim_ms) {
  const uint16_t bg = mix565(kBoard, color, 18);
  const uint16_t ph = mix565(color, kText, 60);
  const uint16_t glow = mix565(bg, color, 110);
  const int len = (int)n.length();

  int typed = len;
  // Cursor always blinks at the end of the name; solid while typing.
  bool cursor = (millis() / 500) % 2 == 0;
  int roll_y = -1;
  if (anim_ms >= 0) {
    const int type_at = 600;
    const int per = 110;
    typed = anim_ms < type_at ? 0 : min(len, (anim_ms - type_at) / per + 1);
    if (typed < len) {
      cursor = true;
    }
    if (anim_ms < 900) {
      roll_y = anim_ms * kScreenH / 900;
    }
  }

  g_lcd->fillScreen(bg);
  if (roll_y >= 0) {
    g_lcd->fillRect(0, roll_y - 8, kScreenW, 16, mix565(bg, color, 50));
  }

  g_lcd->setFont(&fonts::FreeMonoBold24pt7b);
  g_lcd->setTextSize(1.0f);
  float fh = (float)g_lcd->fontHeight();
  // Size and centre the name plus one cursor cell so the cursor never runs off-screen.
  float tw = (float)g_lcd->textWidth(n + "_");
  float sz = min((float)(kScreenW - 36) / max(1.0f, tw), (float)(kScreenH - 40) / fh);
  if (sz > 2.4f) {
    sz = 2.4f;
  }
  g_lcd->setTextSize(sz);
  int full_w = g_lcd->textWidth(n + "_");
  int x = (kScreenW - full_w) / 2;
  int cy = kScreenH / 2 + 2;
  String shown = n.substring(0, typed);
  g_lcd->setTextDatum(lgfx::middle_left);
  if (typed > 0) {
    g_lcd->setTextColor(glow);
    g_lcd->drawString(shown, x + 2, cy + 1);
    g_lcd->setTextColor(ph);
    g_lcd->drawString(shown, x, cy);
  }
  if (cursor) {
    int cx = x + (typed > 0 ? g_lcd->textWidth(shown) : 0) + 3;
    int chh = (int)(fh * sz * 0.7f);
    g_lcd->fillRect(cx, cy - chh / 2, max(8, (int)(chh * 0.5f)), chh, ph);
  }
  g_lcd->setTextSize(1.0f);

  for (int y = 0; y < kScreenH; y += 3) {
    g_lcd->drawFastHLine(0, y, kScreenW, kBoard);
  }
  for (int k = 0; k < 4; k++) {
    g_lcd->drawRoundRect(k, k, kScreenW - 2 * k, kScreenH - 2 * k, 22 - k, kBoard);
  }
}

// Pixel: chunky 8-bit name as big as it fits. Idle: letters bounce like a hi-score entry.
static void drawArcadeName(const String &n, uint16_t color, int anim_ms) {
  g_lcd->fillScreen(kBoard);
  int count = max(1, (int)n.length());
  int s = (kScreenW - 12) / (6 * count);
  int s_h = (kScreenH - 14) / 8;
  if (s > s_h) {
    s = s_h;
  }
  if (s < 2) {
    s = 2;
  }
  g_lcd->setFont(&fonts::Font0);
  g_lcd->setTextSize(s);
  g_lcd->setTextDatum(lgfx::top_left);
  // Font0 cells are 6x8 with a blank trailing column; centre on the 5 lit columns.
  int x0 = (kScreenW - (count * 6 * s - s)) / 2;
  int y0 = (kScreenH - 7 * s) / 2;
  int amp = max(2, min(s * 2, y0 - 2));
  float decay = anim_ms < 0 ? 0.0f : 1.0f - clamp01((float)anim_ms / (float)kTagIdleAnimMs);
  uint16_t shadow = mix565(color, kBoard, 170);
  int off = max(1, s / 3);
  char buf[2] = {0, 0};
  for (int i = 0; i < (int)n.length(); i++) {
    buf[0] = n[i];
    int dy = 0;
    if (anim_ms >= 0) {
      dy = -(int)(fabsf(sinf((float)anim_ms * 0.011f - (float)i * 0.55f)) * (float)amp * decay);
    }
    int x = x0 + i * 6 * s;
    g_lcd->setTextColor(shadow);
    g_lcd->drawString(buf, x + off, y0 + dy + off);
    g_lcd->setTextColor(color);
    g_lcd->drawString(buf, x, y0 + dy);
  }
  g_lcd->setTextSize(1);
}

static bool g_style_locked = false;

void uiSetStyleLocked(bool locked) { g_style_locked = locked; }

// Darken the framebuffer to ~3/8 and pin the unlock hint over it.
static void drawStyleLockOverlay() {
  if (g_fb) {
    uint16_t *px = (uint16_t *)g_fb->getBuffer();
    const size_t n = (size_t)kScreenW * kScreenH;
    for (size_t i = 0; px && i < n; i++) {
      uint16_t c = (uint16_t)((px[i] << 8) | (px[i] >> 8));  // sprite stores byte-swapped 565
      c = (uint16_t)(((c >> 2) & 0x39E7) + ((c >> 3) & 0x18E3));
      px[i] = (uint16_t)((c << 8) | (c >> 8));
    }
  }
  const int w = 250;
  const int h = 56;
  const int x = (kScreenW - w) / 2;
  const int y = (kScreenH - h) / 2;
  g_lcd->fillRoundRect(x, y, w, h, 10, kBoard);
  g_lcd->drawRoundRect(x, y, w, h, 10, kMuted);
  useBody();
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->setTextColor(kText);
  g_lcd->drawString("Meet 5 people", kScreenW / 2, y + 18);
  g_lcd->setTextColor(kMuted);
  g_lcd->drawString("to unlock more styles", kScreenW / 2, y + 38);
}

void uiDrawNametag(const char *name, uint16_t color, uint8_t style, int anim_ms) {
  uint8_t s = style % kTagStyleCount;
  // Idle cinema: fullscreen spectrum on the still classic styles.
  if (anim_ms >= 0 && s < kTagStillStyleCount) {
    beginFb(false);
    clearHits();
    drawFullscreenSpectrum((uint32_t)anim_ms);
    endFb();
    return;
  }

  // Styles 4..7 are name-only (no style dots); the status HUD stays.
  beginFb();
  clearHits();
  String n = tagName(name);
  if (s == 4) {
    drawNeonName(name, color, anim_ms);
  } else if (s == 5) {
    drawFlapName(n, color, anim_ms);
  } else if (s == 6) {
    drawCrtName(n, color, anim_ms);
  } else if (s == 7) {
    drawArcadeName(n, color, anim_ms);
  } else if (s == 1) {
    drawRansomName(n, color, anim_ms);
    drawStyleDots(s, kBoard, mix565(0xC4A9, kBoard, 80));
  } else if (s == 2) {
    const uint16_t paper = 0xEF7D;
    g_lcd->fillScreen(paper);
    g_lcd->fillRect(0, 0, kScreenW, 58, color);
    g_lcd->fillRect(0, kScreenH - 10, kScreenW, 10, color);
    g_lcd->setTextSize(1.0f);
    g_lcd->setFont(&fonts::FreeSansBold18pt7b);
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(kText);
    g_lcd->drawString("HELLO", kScreenW / 2, 24);
    useBody();
    g_lcd->setTextColor(kText);
    g_lcd->drawString("my name is", kScreenW / 2, 46);
    drawFittedName(n, color, kScreenW / 2, 108, kScreenW - 24);
    drawStyleDots(s, color, mix565(paper, kBoard, 70));
  } else if (s == 3) {
    drawCrayonName(n, color, anim_ms);
    uint16_t wax = (inkOn(color) == kBoard) ? mix565(color, kBoard, 120) : color;
    drawStyleDots(s, wax, mix565(0xF5D3, kBoard, 70));
  } else {
    g_lcd->fillScreen(kBoard);
    drawSegName(n.c_str(), color, 8, kScreenH - 16);
    drawStyleDots(s, color, kSegOff);
  }
  if (g_style_locked) {
    drawStyleLockOverlay();
  }
  endFb();
}

// klipper logo, downsampled by `scale` and centred, with the boot-screen light sweep.
static void drawLogoShimmer(int scale, int y0, uint32_t t_ms, int fade) {
  const uint16_t ink = kColors[1].rgb;
  const uint16_t hot = 0xFE60;
  const uint16_t glint = 0xFFF6;
  const int dw = kLogoW / scale;
  const int dh = kLogoH / scale;
  const int x0 = (kScreenW - dw) / 2;
  // Sweep speed and glow widths are tuned at scale 2; keep the look at other sizes.
  const float k = 2.0f / (float)scale;
  float sweep = fmodf((float)t_ms * 0.22f * k, (float)dw + 80.0f * k) - 28.0f * k;
  const float core_w = 10.0f * k;
  const float halo_w = 26.0f * k;
  const float inv_core = 1.0f / (2.0f * core_w * core_w);
  const float inv_halo = 1.0f / (2.0f * halo_w * halo_w);
  const float halo_off = 14.0f * k;
  for (int y = 0; y < dh; y++) {
    for (int x = 0; x < dw; x++) {
      int a = 0;
      for (int iy = 0; iy < scale; iy++) {
        for (int ix = 0; ix < scale; ix++) {
          int sx = x * scale + ix;
          int sy = y * scale + iy;
          if (sx < kLogoW && sy < kLogoH) {
            int v = kLogoMask[sy * kLogoW + sx];
            if (v > a) {
              a = v;
            }
          }
        }
      }
      if (a < 12) {
        continue;
      }
      a = (a * fade) >> 8;
      float d = ((float)x + (float)y * 0.38f) - sweep;
      float core = expf(-d * d * inv_core);
      float halo = expf(-(d - halo_off) * (d - halo_off) * inv_halo) * 0.45f;
      float shine = core + halo;
      if (shine > 1.0f) {
        shine = 1.0f;
      }
      int mt = (int)(shine * 256.0f);
      uint16_t c = mix565(ink, hot, mt);
      if (mt > 180) {
        c = mix565(c, glint, (mt - 180) * 3);
      }
      c = scaleRgb(c, a, 255);
      g_lcd->drawPixel(x0 + x, y0 + y, c);
    }
  }
}

void uiDrawSplash(uint32_t t_ms) {
  beginFb(false);
  g_lcd->fillScreen(kBoard);

  static uint32_t cpu_mhz = 0;
  static uint32_t cores = 0;
  static uint32_t flash_k = 0;
  static uint32_t psram_k = 0;
  static uint32_t sram_k = 0;
  static bool stats = false;
  if (!stats) {
    cpu_mhz = ESP.getCpuFreqMHz();
    cores = ESP.getChipCores();
    flash_k = ESP.getFlashChipSize() / 1024;
    psram_k = ESP.getPsramSize() / 1024;
    sram_k = ESP.getHeapSize() / 1024;
    stats = true;
  }
  const uint32_t mem_k = psram_k + sram_k;

  const uint16_t vga = 0xC618;
  const uint16_t vga_dim = 0x8410;
  const int scale = 2;
  const int dh = kLogoH / scale;
  const int y0 = 2;
  int fade = t_ms < 220 ? (int)((t_ms * 256) / 220) : 256;
  drawLogoShimmer(scale, y0, t_ms, fade);

  g_lcd->setFont(&fonts::Font0);
  g_lcd->setTextSize(1);
  g_lcd->setTextDatum(lgfx::top_left);
  auto line = [&](const char *s, int x, int y, uint16_t c) {
    g_lcd->setTextColor(c);
    g_lcd->drawString(s, x, y);
  };

  char buf[48];
  const int tx = 8;
  int ty = y0 + dh + 4;
  snprintf(buf, sizeof(buf), "Firmware Revision : %s", kFirmwareVersion);
  line(buf, tx, ty, kText);
  ty += 10;
  snprintf(buf, sizeof(buf), "CPU : %s Dual-Core LX7", ESP.getChipModel());
  line(buf, tx, ty, vga);
  ty += 9;
  snprintf(buf, sizeof(buf), "      Speed : %u MHz     Cores : %u", (unsigned)cpu_mhz,
           (unsigned)cores);
  line(buf, tx, ty, vga);
  ty += 9;
  snprintf(buf, sizeof(buf), "SRAM  : %5uK     PSRAM : %5uK", (unsigned)sram_k,
           (unsigned)psram_k);
  line(buf, tx, ty, vga);
  ty += 9;
  snprintf(buf, sizeof(buf), "Flash : %5uK", (unsigned)flash_k);
  line(buf, tx, ty, vga);
  ty += 12;

  uint32_t shown = 0;
  bool done = false;
  if (t_ms >= 450) {
    uint32_t elapsed = t_ms - 450;
    const uint32_t dur = 2000;
    if (elapsed >= dur) {
      shown = mem_k;
      done = true;
    } else {
      shown = (mem_k * elapsed) / dur;
      shown = (shown / 64) * 64;
    }
  }
  snprintf(buf, sizeof(buf), "Memory Test : %7uK", (unsigned)shown);
  line(buf, tx, ty, kText);
  if (done) {
    line(" OK", tx + g_lcd->textWidth(buf), ty, kOk);
  }
  ty += 12;
  line("Press DEL to run Setup", tx, ty, vga_dim);
  endFb();
}

void uiDrawNearby(const std::vector<Peer *> &peers) {
  uint16_t mine = settingsColorRgb();
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  // Your phonetic callsign — top-left so you know what to shout.
  useTitle();
  g_lcd->setTextDatum(lgfx::top_left);
  g_lcd->setTextColor(mine);
  g_lcd->drawString(callsign(rosterMyId().c_str()), 8, 4);
  useBody();
  g_lcd->setTextColor(kMuted);
  g_lcd->drawString("Search for Friends", 8, 22);

  const int radar_s = 132;
  drawRadar(peers, 4, 36, radar_s, mine);

  const int list_x = 144;
  const int list_w = kScreenW - list_x - 6;
  int y = 36;
  uint8_t shown = 0;
  for (Peer *p : peers) {
    if (shown >= kNearbyRows) {
      break;
    }
    uint32_t cool = radioCooldownRemaining(p->id);
    uint16_t col = cool ? kMuted : kText;
    roundRect(list_x, y, list_w, 30, kPanel, col);
    useBody();
    g_lcd->setTextDatum(lgfx::middle_left);
    g_lcd->setTextColor(col);
    String label = callsign(p->id);
    if (cool) {
      label += "  " + String((cool + 999) / 1000) + "s";
    }
    int bar = warmth(p->rssi);
    int maxw = 44;
    int bw = (bar * maxw) / 60;
    int bx = list_x + list_w - 8 - maxw;
    // A heart for someone who has already met you: find them to make it mutual.
    bool knows = radioKnowsMe(p->id);
    const int text_w = bx - list_x - (knows ? 28 : 14);
    if (knows) {
      const uint16_t heart = cool ? kMuted : 0xF8B2;
      int hx = bx - 12, hy = y + 13;
      g_lcd->fillCircle(hx - 3, hy, 3, heart);
      g_lcd->fillCircle(hx + 3, hy, 3, heart);
      g_lcd->fillTriangle(hx - 6, hy + 1, hx + 6, hy + 1, hx, hy + 8, heart);
    }
    GlitchStage gs = cool ? GLITCH_NONE : glitchStage(p->id, millis());
    if (gs == GLITCH_NONE) {
      g_lcd->drawString(clipText(label, text_w), list_x + 10, y + 15);
    } else {
      g_lcd->setClipRect(list_x + 2, y + 2, text_w + 8, 26);
      if (gs == GLITCH_NOISE) {
        String junk = glitchNoise(max(4, (int)label.length()), millis() / 40 + idHash(p->id));
        drawSplitText(junk, list_x + 10 + (int)((millis() / 30) % 3) - 1, y + 15, 2, kText);
        for (int sy = y + 3; sy < y + 28; sy += 3) {
          g_lcd->drawFastHLine(list_x + 2, sy, text_w + 8, kBoard);  // scanlines
        }
      } else {
        drawSplitText(clipText(nameHint(p, bar), text_w), list_x + 10, y + 15, 1, mine);
      }
      g_lcd->clearClipRect();
    }
    g_lcd->fillRect(bx, y + 11, maxw, 8, 0x2104);
    g_lcd->fillRect(bx, y + 11, bw, 8, cool ? kMuted : mine);

    addHit(HIT_NEARBY0 + shown, list_x, y, list_w, 30);
    shown++;
    y += 33;
  }

  if (!shown) {
    useBody();
    g_lcd->setTextDatum(lgfx::top_left);
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString("No badges yet", list_x + 4, 70);
  }
  endFb();
}

void uiDrawHuntWait(const Peer *peer) {
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  useTitle();
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->setTextColor(kText);
  g_lcd->drawString("Waiting for them", kScreenW / 2, 28);
  if (peer) {
    drawSegName(callsign(peer->id), settingsColorRgb(), 42, 100);
  }
  useBody();
  g_lcd->setTextColor(kMuted);
  g_lcd->drawString("Do they want to be found?", kScreenW / 2, 118);
  drawButton(90, kScreenH - 40, 140, 32, "Cancel", kPanel, kDanger, kDanger, HIT_CANCEL);
  endFb();
}

void uiDrawHuntAsk() {
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  useTitle();
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->setTextColor(kText);
  g_lcd->drawString("Do you want to be found?", kScreenW / 2, 48);
  useBody();
  g_lcd->setTextColor(kMuted);
  g_lcd->drawString("Someone nearby picked you", kScreenW / 2, 78);
  drawButton(16, kScreenH - 48, 140, 36, "Yes", kPanel, kOk, kOk, HIT_FOUND_YES);
  drawButton(164, kScreenH - 48, 140, 36, "No", kPanel, kDanger, kDanger, HIT_FOUND_NO);
  endFb();
}

void uiDrawHunt(const Peer *peer, const char *word, uint16_t color, bool me_ready, bool them_ready) {
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  uint16_t c = color ? color : kText;
  g_lcd->fillRect(0, 0, kScreenW, 6, c);
  drawSegName(word && word[0] ? word : "?", c, 8, 72);

  int rssi = peer ? peer->rssi : -90;
  int bar = warmth(rssi);
  int maxw = 240;
  int bw = (bar * maxw) / 60;
  g_lcd->fillRoundRect(40, 76, maxw, 14, 4, kRail);
  g_lcd->fillRoundRect(40, 76, max(8, bw), 14, 4, c);

  useBody();
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->setTextColor(kText);
  g_lcd->drawString("say the word  -  match the color", kScreenW / 2, 102);

  String status = "Walk until you find each other";
  if (me_ready && !them_ready) {
    status = "Waiting for them...";
  } else if (!me_ready && them_ready) {
    status = "They see you. Tap That's you";
  }
  g_lcd->drawString(status, kScreenW / 2, 122);

  drawButton(10, kScreenH - 40, 184, 32, me_ready ? "Waiting" : "That's you", kPanel, c, kText,
             HIT_THATSYOU);
  drawButton(204, kScreenH - 40, 106, 32, "Cancel", kPanel, kDanger, kDanger, HIT_CANCEL);
  endFb();
}

void uiDrawQuiz(const Quiz &quiz) {
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  useBody();
  g_lcd->setTextDatum(lgfx::top_left);
  g_lcd->setTextColor(kText);
  g_lcd->drawString("Which fact is true?", 10, 6);

  int y = 26;
  int h = 44;
  for (int i = 0; i < 3; i++) {
    roundRect(8, y, kScreenW - 16, h, kPanel, kMuted);
    drawMarquee(quiz.options[i], 16, y + 4, kScreenW - 32, h - 8, kText);
    addHit(HIT_FACT0 + i, 8, y, kScreenW - 16, h);
    y += h + 4;
  }
  endFb();
}

void uiDrawReveal(const Quiz &quiz, int picked, uint16_t color) {
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  bool ok = picked == quiz.truth_index;
  if (ok) {
    useBody();
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(kOk);
    g_lcd->drawString("Congratulations, you met", kScreenW / 2, 16);
    drawSegName(quiz.target_name.c_str(), color ? color : kOk, 24, 78);
    roundRect(8, 82, kScreenW - 16, 44, kPanel, kOk);
    drawMarquee(quiz.options[quiz.truth_index], 18, 88, kScreenW - 36, 32, kText);
  } else {
    useTitle();
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(kDanger);
    g_lcd->drawString("Not that one", kScreenW / 2, 22);
    useBody();
    g_lcd->setTextColor(kText);
    g_lcd->drawString("The true fact was", kScreenW / 2, 46);
    roundRect(8, 58, kScreenW - 16, 64, kPanel, kDanger);
    drawMarquee(quiz.options[quiz.truth_index], 18, 70, kScreenW - 36, 40, kText);
  }

  drawButton(80, kScreenH - 40, 160, 32, "Continue", kPanel, kMuted, kText, HIT_CONTINUE);
  endFb();
}

void uiDrawMetList(const std::vector<String> &ids, int *scroll) {
  const int top = 30;
  const int row_h = 44;
  const int view_h = kScreenH - top;
  int content = (int)ids.size() * row_h;
  int max_s = content > view_h ? content - view_h : 0;
  int sc = scroll ? *scroll : 0;
  if (sc < 0) {
    sc = 0;
  }
  if (sc > max_s) {
    sc = max_s;
  }
  if (scroll) {
    *scroll = sc;
  }

  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  useTitle();
  g_lcd->setTextDatum(lgfx::top_left);
  g_lcd->setTextColor(settingsColorRgb());
  g_lcd->drawString("People you've met", 8, 6);

  if (ids.empty()) {
    useBody();
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString("Nobody yet. Go play!", kScreenW / 2, kScreenH / 2);
    endFb();
    return;
  }

  g_lcd->setClipRect(0, top, kScreenW, view_h);
  for (size_t i = 0; i < ids.size(); i++) {
    int y = top - sc + (int)i * row_h;
    if (y + row_h < top || y > kScreenH) {
      continue;
    }
    const Attendee *a = rosterFind(ids[i].c_str());
    String name = a ? a->name : ids[i];
    String fact = a ? a->truth : "";
    roundRect(8, y + 2, kScreenW - 16, row_h - 4, kPanel, kRail);
    useBody();
    g_lcd->setTextDatum(lgfx::top_left);
    g_lcd->setTextColor(kText);
    g_lcd->drawString(clipText(name, kScreenW - 40), 18, y + 8);
    if (fact.length()) {
      drawMarquee(fact, 18, y + 22, kScreenW - 40, 18, kMuted);
      g_lcd->setClipRect(0, top, kScreenW, view_h);
    }
  }
  g_lcd->clearClipRect();
  endFb();
}

static void clampScroll(int *scroll, int content, int view_h) {
  int max_s = content > view_h ? content - view_h : 0;
  int sc = scroll ? *scroll : 0;
  if (sc < 0) {
    sc = 0;
  }
  if (sc > max_s) {
    sc = max_s;
  }
  if (scroll) {
    *scroll = sc;
  }
}

static void screenTitle(const char *title) {
  useTitle();
  g_lcd->setTextDatum(lgfx::top_left);
  g_lcd->setTextColor(settingsColorRgb());
  g_lcd->drawString(title, 8, 6);
}

static void drawKey(int x, int y, int w, int h, const char *label, int hit, uint16_t edge = kMuted) {
  roundRect(x, y, w, h, kPanel, edge);
  useBody();
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->setTextColor(kText);
  g_lcd->drawString(label, x + w / 2, y + h / 2);
  addHit(hit, x, y, w, h);
}

// Menu tiles: big targets (~8 mm tall) with a bold title and a short grey hint.
static constexpr int kTileTop = 32;
static constexpr int kTileGap = 6;
static constexpr int kTileH = (kScreenH - kTileTop - 4 - kTileGap) / 2;

static void drawTile(int x, int y, int w, int h, const char *title, const char *hint, int hit,
                     uint16_t accent) {
  g_lcd->fillRoundRect(x, y, w, h, 8, kPanel);
  g_lcd->drawRoundRect(x, y, w, h, 8, accent);
  if (accent != kRail) {
    g_lcd->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 7, accent);  // primary tile: heavier edge
  }
  // Long titles step down a font before they would be clipped.
  useBody();
  if (g_lcd->textWidth(title) > w - 10) {
    g_lcd->setFont(&fonts::FreeSans9pt7b);
  }
  if (g_lcd->textWidth(title) > w - 10) {
    g_lcd->setFont(&fonts::Font2);
  }
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->setTextColor(accent == kRail ? kText : accent);
  g_lcd->drawString(clipText(title, w - 10), x + w / 2, y + h / 2 - 9);
  g_lcd->setFont(&fonts::FreeSans9pt7b);
  g_lcd->setTextColor(kMuted);
  g_lcd->drawString(clipText(hint, w - 10), x + w / 2, y + h / 2 + 11);
  // Pad the target by half the gap so taps between tiles still land somewhere.
  addHit(hit, x - kTileGap / 2, y - kTileGap / 2, w + kTileGap, h + kTileGap);
}

// "< label" back pill in the title bar; the hit area runs past the drawn pill.
// Returns the x just past the pill so a second pill can sit beside it.
static int drawBackPill(const char *label, int pad_right = 30) {
  useBody();
  String s = String("< ") + label;
  const int w = g_lcd->textWidth(s) + 20;
  roundRect(6, 4, w, 24, kPanel, kRail);
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->setTextColor(kText);
  g_lcd->drawString(s, 6 + w / 2, 16);
  addHit(HIT_MENU_BACK, 0, 0, 6 + w + pad_right, kTileTop - kTileGap / 2);
  return 6 + w;
}

void uiDrawMenu() {
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  int bx = drawBackPill("Nametag", 4);
  {
    useBody();
    const char *label = "Deep sleep";
    const int w = g_lcd->textWidth(label) + 20;
    const int x = bx + 10;
    roundRect(x, 4, w, 24, kPanel, kRail);
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString(label, x + w / 2, 16);
    addHit(HIT_SLEEP_ASK, x - 4, 0, w + 8, kTileTop - kTileGap / 2);
  }
  const int w = (kScreenW - 12 - 2 * kTileGap) / 3;
  const int y0 = kTileTop;
  const int y1 = y0 + kTileH + kTileGap;
  auto col = [&](int i) { return 6 + i * (w + kTileGap); };
  const Attendee *me = rosterMe();
  char met[24];
  snprintf(met, sizeof(met), "%u of %u", (unsigned)settingsMet().size(),
           (unsigned)(rosterCount() - (me ? 1 : 0)));
  char rank[24];
  snprintf(rank, sizeof(rank), "#%d of %d", socialMyRank(), socialKnownCount());
  char awards[24];
  snprintf(awards, sizeof(awards), "%d of %d", socialAchievementCount(), (int)ACH_COUNT);
  drawTile(col(0), y0, w, kTileH, "Play", "find friends", HIT_MENU0 + MENU_FRIENDS,
           settingsColorRgb());
  drawTile(col(1), y0, w, kTileH, "Met", met, HIT_MENU0 + MENU_REGISTRY, kRail);
  drawTile(col(2), y0, w, kTileH, "Leaders", rank, HIT_MENU0 + MENU_LEADERS, kRail);
  drawTile(col(0), y1, w, kTileH, "Achievements", awards, HIT_MENU0 + MENU_AWARDS, kRail);
  drawTile(col(1), y1, w, kTileH, "Schedule", "agenda", HIT_MENU0 + MENU_SCHEDULE, kRail);
  drawTile(col(2), y1, w, kTileH, "More", "settings", HIT_MENU0 + MENU_MORE, kRail);
  endFb();
}

// "Ada Lovelace" -> "Ada L."; single names pass through.
static String shortName(const String &full) {
  int sp = full.indexOf(' ');
  if (sp < 0 || sp + 1 >= (int)full.length()) {
    return full;
  }
  return full.substring(0, sp + 2) + ".";
}

void uiDrawLeaderboard() {
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  screenTitle("Leaderboard");
  std::vector<LeaderRow> rows = socialLeaders();
  const int top = 32;
  const int row_h = 22;
  const int shown = 5;
  int my_index = -1;
  for (size_t i = 0; i < rows.size(); i++) {
    if (rows[i].me) {
      my_index = (int)i;
      break;
    }
  }
  auto drawRow = [&](int rank, const LeaderRow &r, int y) {
    const Attendee *a = rosterFind(r.id.c_str());
    uint16_t ink = r.me ? settingsColorRgb() : kText;
    roundRect(8, y, kScreenW - 16, row_h - 2, r.me ? scaleRgb(ink, 1, 5) : kPanel,
              r.me ? ink : kRail);
    useBody();
    g_lcd->setTextColor(ink);
    g_lcd->setTextDatum(lgfx::middle_left);
    char num[8];
    snprintf(num, sizeof(num), "%d", rank);
    g_lcd->drawString(num, 16, y + row_h / 2 - 1);
    String name = r.me ? String("You") : (a ? shortName(a->name) : r.id);
    g_lcd->drawString(clipText(name, kScreenW - 120), 46, y + row_h / 2 - 1);
    g_lcd->setTextDatum(lgfx::middle_right);
    snprintf(num, sizeof(num), "%d met", (int)r.met);
    g_lcd->drawString(num, kScreenW - 16, y + row_h / 2 - 1);
  };
  int y = top;
  for (int i = 0; i < (int)rows.size() && i < shown; i++) {
    drawRow(i + 1, rows[i], y);
    y += row_h;
  }
  if (my_index >= shown) {
    // Keep your own line visible below the top 5.
    g_lcd->setTextColor(kMuted);
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->drawString("...", kScreenW / 2, y + 2);
    drawRow(socialMyRank(), rows[my_index], y + 8);
  } else if (rows.size() <= 1) {
    useBody();
    g_lcd->setTextColor(kMuted);
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->drawString("Play to hear other badges' scores", kScreenW / 2, y + 30);
  }
  endFb();
}

void uiDrawAchievements(int *scroll) {
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  char title[32];
  snprintf(title, sizeof(title), "Achievements %d/%d", socialAchievementCount(), (int)ACH_COUNT);
  screenTitle(title);
  const int top = 30;
  const int view_h = kScreenH - top;
  const int col_w = (kScreenW - 12 - 6 - 8) / 2;  // leave room for the scroll bar
  const int row_h = 22;
  const int rows = (ACH_COUNT + 1) / 2;
  const int content = rows * row_h;
  const int max_s = content > view_h ? content - view_h : 0;
  int sc = scroll ? *scroll : 0;
  sc = sc < 0 ? 0 : (sc > max_s ? max_s : sc);
  if (scroll) {
    *scroll = sc;
  }
  g_lcd->setClipRect(0, top, kScreenW, view_h);
  for (int i = 0; i < ACH_COUNT; i++) {
    Achievement a = (Achievement)i;
    bool have = socialHasAchievement(a);
    int x = 6 + (i % 2) * (col_w + 6);
    int y = top - sc + (i / 2) * row_h;
    if (y + row_h < top || y > kScreenH) {
      continue;
    }
    roundRect(x, y, col_w, row_h - 2, have ? kPanel : kBoard, have ? settingsColorRgb() : kRail);
    const int cy = y + (row_h - 2) / 2;
    if (have) {
      g_lcd->fillCircle(x + 10, cy, 4, settingsColorRgb());
    } else {
      g_lcd->drawCircle(x + 10, cy, 4, kMuted);
    }
    g_lcd->setFont(&fonts::FreeSans9pt7b);
    g_lcd->setTextSize(1);
    g_lcd->setTextDatum(lgfx::middle_left);
    g_lcd->setTextColor(have ? kText : kMuted);
    g_lcd->drawString(clipText(socialAchievementTitle(a), col_w - 24), x + 20, cy);
  }
  g_lcd->clearClipRect();
  if (max_s > 0) {
    const int track_x = kScreenW - 5;
    g_lcd->drawFastVLine(track_x, top, view_h - 2, kRail);
    int thumb_h = max(14, view_h * view_h / content);
    int thumb_y = top + (view_h - 2 - thumb_h) * sc / max_s;
    g_lcd->fillRoundRect(track_x - 2, thumb_y, 5, thumb_h, 2, kMuted);
  }
  endFb();
}

static void drawSettingsDots(int page) {
  const int gap = 14;
  int x0 = kScreenW / 2 - (kSettingsPageCount - 1) * gap / 2;
  int y = kScreenH - 8;
  for (int i = 0; i < kSettingsPageCount; i++) {
    g_lcd->fillCircle(x0 + i * gap, y, 3, i == page ? settingsColorRgb() : kMuted);
  }
}

// Heart centred at (cx, cy), about 2r wide (Queen egg and About page).
static void drawHeart(int cx, int cy, int r, uint16_t c) {
  g_lcd->fillCircle(cx - r / 2, cy - r / 4, r / 2, c);
  g_lcd->fillCircle(cx + r / 2, cy - r / 4, r / 2, c);
  g_lcd->fillTriangle(cx - r, cy, cx + r, cy, cx, cy + r + r / 3, c);
}

// About (under More): shimmering logo, version, game progress, own fact, credit line.
static void drawAboutPage() {
  const int scale = 3;
  const int logo_y = 6;
  drawLogoShimmer(scale, logo_y, millis(), 256);
  int y = logo_y + kLogoH / scale + 8;

  useBody();
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->setTextColor(kMuted);
  char buf[48];
  snprintf(buf, sizeof(buf), "v%s", kFirmwareVersion);
  g_lcd->drawString(buf, kScreenW / 2, y);
  y += 20;

  const Attendee *me = rosterMe();
  size_t others = rosterCount() - (me ? 1 : 0);
  snprintf(buf, sizeof(buf), "Met %u of %u", (unsigned)settingsMet().size(), (unsigned)others);
  g_lcd->setTextColor(settingsColorRgb());
  g_lcd->drawString(buf, kScreenW / 2, y);
  y += 20;

  // Own fact, word-wrapped onto at most two lines; the second is clipped with "...".
  g_lcd->setFont(&fonts::FreeSans9pt7b);
  g_lcd->setTextColor(kText);
  const int max_w = kScreenW - 20;
  String rest = me ? me->truth : String("");
  for (int line = 0; line < 2 && rest.length(); line++) {
    String out = rest;
    if (line == 0 && g_lcd->textWidth(rest) > max_w) {
      int cut = -1;
      for (int i = 0; i < (int)rest.length(); i++) {
        if (rest[i] == ' ' && g_lcd->textWidth(rest.substring(0, i)) <= max_w) {
          cut = i;
        }
      }
      if (cut > 0) {
        out = rest.substring(0, cut);
        rest = rest.substring(cut + 1);
      } else {
        rest = "";
      }
    } else {
      rest = "";
    }
    g_lcd->drawString(clipText(out, max_w), kScreenW / 2, y);
    y += 18;
  }

  // "made with <heart> in SLC by klipper", centred as one run.
  g_lcd->setFont(&fonts::FreeSans9pt7b);
  g_lcd->setTextSize(1);
  g_lcd->setTextDatum(lgfx::middle_left);
  g_lcd->setTextColor(kMuted);
  const char *lead = "made with ";
  const char *tail = " in SLC by klipper";
  const int heart_r = 6;
  int w = g_lcd->textWidth(lead) + heart_r * 2 + g_lcd->textWidth(tail);
  int x = (kScreenW - w) / 2;
  const int fy = kScreenH - 23;
  g_lcd->drawString(lead, x, fy);
  x += g_lcd->textWidth(lead);
  drawHeart(x + heart_r, fy - 1, heart_r, 0xF8A0);
  x += heart_r * 2;
  g_lcd->drawString(tail, x, fy);
}

// Greedy word wrap in the current font; a single over-long word is clipped.
static std::vector<String> wrapText(const String &text, int max_w) {
  std::vector<String> lines;
  String line;
  int i = 0;
  const int n = (int)text.length();
  while (i < n) {
    int j = text.indexOf(' ', i);
    if (j < 0) {
      j = n;
    }
    String word = text.substring(i, j);
    String trial = line.length() ? line + " " + word : word;
    if (line.length() && g_lcd->textWidth(trial) > max_w) {
      lines.push_back(line);
      line = word;
    } else {
      line = trial;
    }
    i = j + 1;
  }
  if (line.length()) {
    lines.push_back(clipText(line, max_w));
  }
  return lines;
}

void uiDrawSleepConfirm(int *scroll) {
  static const char *const kParas[] = {
      "Deep sleep turns off the screen, Wi-Fi and the badge radio so the battery lasts "
      "days instead of hours.",
      "To wake it, press the top button. The badge restarts in a few seconds.",
      "Kept: your name, friends met, achievements, leaderboard and settings.",
      "Paused while asleep: Find Friends, updates and the clock. Other badges can't see you.",
      "Using the badge as a desk display on USB power? Take the battery out so it isn't kept "
      "charging all day.",
  };
  const int top = 30;
  const int btn_y = kScreenH - 40;
  const int view_h = btn_y - 6 - top;
  const int line_h = 17;
  const int para_gap = 6;
  const int text_w = kScreenW - 28;

  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  screenTitle("Deep sleep");
  g_lcd->setFont(&fonts::FreeSans9pt7b);
  g_lcd->setTextSize(1);

  // Lay out once per draw (cheap: five short paragraphs).
  struct Row {
    String text;
    bool accent;
  };
  std::vector<Row> rows;
  int content = 0;
  const int last = (int)(sizeof(kParas) / sizeof(kParas[0])) - 1;
  for (int p = 0; p <= last; p++) {
    for (const String &l : wrapText(kParas[p], text_w)) {
      rows.push_back({l, p == last});
      content += line_h;
    }
    content += para_gap;
    rows.push_back({"", false});  // paragraph gap marker
  }
  int max_s = content > view_h ? content - view_h : 0;
  int sc = scroll ? *scroll : 0;
  sc = sc < 0 ? 0 : (sc > max_s ? max_s : sc);
  if (scroll) {
    *scroll = sc;
  }

  g_lcd->setClipRect(0, top, kScreenW, view_h);
  g_lcd->setTextDatum(lgfx::top_left);
  int y = top - sc;
  for (const Row &r : rows) {
    if (!r.text.length()) {
      y += para_gap;
      continue;
    }
    if (y + line_h >= top && y <= top + view_h) {
      g_lcd->setTextColor(r.accent ? settingsColorRgb() : kText);
      g_lcd->drawString(r.text, 10, y);
    }
    y += line_h;
  }
  g_lcd->clearClipRect();

  // Scroll cue: a thin track on the right while there's more to read.
  if (max_s > 0) {
    const int track_x = kScreenW - 8;
    g_lcd->drawFastVLine(track_x, top, view_h, kRail);
    int thumb_h = max(14, view_h * view_h / content);
    int thumb_y = top + (view_h - thumb_h) * sc / max_s;
    g_lcd->fillRoundRect(track_x - 2, thumb_y, 5, thumb_h, 2, kMuted);
  }

  drawButton(16, btn_y, 136, 32, "Cancel", kPanel, kMuted, kText, HIT_SLEEP_NO);
  drawButton(168, btn_y, 136, 32, "Sleep", kPanel, kOk, kOk, HIT_SLEEP_YES);
  endFb();
}

void uiDrawSleeping() {
  beginFb(false);
  g_lcd->fillScreen(kBoard);
  clearHits();
  useTitle();
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->setTextColor(kText);
  g_lcd->drawString("Sleeping...", kScreenW / 2, kScreenH / 2 - 14);
  useBody();
  g_lcd->setTextColor(kMuted);
  g_lcd->drawString("Press the top button to wake", kScreenW / 2, kScreenH / 2 + 16);
  endFb();
}

void uiDrawAbout() {
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  drawAboutPage();
  endFb();
}

void uiDrawSettings(int page) {
  if (page < 0) {
    page = 0;
  }
  if (page >= kSettingsPageCount) {
    page = kSettingsPageCount - 1;
  }
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();

  if (page == 0) {
    screenTitle("Display name");
    useBody();
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString("Shown on your nametag", kScreenW / 2, 40);
    useTitle();
    g_lcd->setTextColor(kText);
    g_lcd->drawString(clipText(settingsDisplayName(), kScreenW - 24), kScreenW / 2, 78);
    useBody();
    if (settingsHasNickname()) {
      g_lcd->setTextColor(kMuted);
      g_lcd->drawString("Custom nickname", kScreenW / 2, 104);
      drawButton(16, 120, 140, 32, "Edit", kPanel, kMuted, kText, HIT_NICK_EDIT);
      drawButton(164, 120, 140, 32, "Clear", kPanel, kDanger, kDanger, HIT_NICK_CLEAR);
    } else {
      g_lcd->setTextColor(kMuted);
      g_lcd->drawString("Using first name from roster", kScreenW / 2, 104);
      drawButton(60, 120, 200, 32, "Set nickname", kPanel, kMuted, kText, HIT_NICK_EDIT);
    }
  } else if (page == 1) {
    screenTitle("WiFi");
    useBody();
    g_lcd->setTextDatum(lgfx::top_left);
    g_lcd->setTextColor(kText);
    g_lcd->drawString(clipText(netStatusLine(), kScreenW - 20), 10, 36);
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString("WiFi and Search for Friends", 10, 60);
    g_lcd->drawString("cannot run at the same time.", 10, 78);
    drawButton(8, 110, 148, 32, "Scan WiFi", kPanel, kMuted, kText, HIT_WIFI_SCAN);
    if (netConnected()) {
      drawButton(164, 110, 148, 32, "Disconnect", kPanel, kDanger, kDanger, HIT_WIFI_DISCONNECT);
    } else if (settingsHasWifi() && netState() != NetState::Connecting) {
      drawButton(164, 110, 148, 32, "Connect", kPanel, kOk, kOk, HIT_WIFI_CONNECT);
    }
  }

  if (page == 2) {
    screenTitle("Auto mode");
    const bool on = settingsAutoMode();
    const Attendee *me = rosterMe();
    const uint16_t accent = settingsColorRgb();
    g_lcd->setFont(&fonts::FreeSans9pt7b);
    g_lcd->setTextSize(1);
    g_lcd->setTextDatum(lgfx::top_left);
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString(me && me->goose ? "Nametag, goose, then your favourite"
                                      : "Nametag 10 s, then your favourite 6 s",
                      10, 34);
    drawButton(8, 58, 148, 32, on ? "Auto: On" : "Auto: Off", kPanel, on ? accent : kRail,
               on ? accent : kText, HIT_AUTO_TOGGLE);
    drawButton(164, 58, 148, 32, "Preview", kPanel, kRail, kText, HIT_FAV_PREVIEW);
    // Favourite: built-ins first, then this badge's uploaded GIFs.
    String fav = "None";
    if (settingsFavToy() >= 0) {
      fav = uiToyName(settingsFavToy());
    } else if (settingsFavGif().length()) {
      fav = settingsFavGif();
      bool there = false;
      for (int i = 0; i < gifsCount(); i++) {
        there = there || gifsName(i) == fav;
      }
      if (!there) {
        fav += " (deleted)";
      }
    }
    drawButton(8, 100, 44, 32, "<", kPanel, kRail, kText, HIT_FAV_PREV);
    roundRect(58, 100, kScreenW - 116, 32, kBoard, kRail);
    useBody();
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(kText);
    g_lcd->drawString(clipText(fav, kScreenW - 132), kScreenW / 2, 116);
    drawButton(kScreenW - 52, 100, 44, 32, ">", kPanel, kRail, kText, HIT_FAV_NEXT);
  }

  drawSettingsDots(page);
  endFb();
}

const char *uiToyName(int toy) {
  static const char *const kNames[] = {"Spectrum", "Matrix", "Winamp", "VU meter",
                                       "Glow 1",   "Glow 2", "Glow 3"};
  return (toy >= 0 && toy < (int)(sizeof(kNames) / sizeof(kNames[0]))) ? kNames[toy] : "";
}

void uiDrawWifiWarn() {
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  useTitle();
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->setTextColor(kText);
  g_lcd->drawString("WiFi will disconnect", kScreenW / 2, 36);
  useBody();
  g_lcd->setTextColor(kMuted);
  g_lcd->drawString("Search for Friends and WiFi", kScreenW / 2, 68);
  g_lcd->drawString("can't run at the same time.", kScreenW / 2, 88);
  drawButton(16, kScreenH - 44, 136, 32, "Cancel", kPanel, kMuted, kText, HIT_WARN_NO);
  drawButton(168, kScreenH - 44, 136, 32, "Continue", kPanel, kDanger, kDanger, HIT_WARN_YES);
  endFb();
}

void uiDrawWifiScan(int *scroll, bool scanning) {
  const auto &aps = netAps();
  const int top = 30;
  const int row_h = 28;
  const int view_h = kScreenH - top;
  clampScroll(scroll, (int)aps.size() * row_h, view_h);
  int sc = scroll ? *scroll : 0;

  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  screenTitle("Networks");
  if (scanning) {
    useBody();
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString("Scanning...", kScreenW / 2, kScreenH / 2);
    endFb();
    return;
  }
  if (aps.empty()) {
    useBody();
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString("No networks found", kScreenW / 2, kScreenH / 2);
    endFb();
    return;
  }
  g_lcd->setClipRect(0, top, kScreenW, view_h);
  for (size_t i = 0; i < aps.size(); i++) {
    int y = top - sc + (int)i * row_h;
    if (y + row_h < top || y > kScreenH) {
      continue;
    }
    roundRect(8, y + 1, kScreenW - 16, row_h - 2, kPanel, kRail);
    useBody();
    g_lcd->setTextDatum(lgfx::middle_left);
    g_lcd->setTextColor(kText);
    g_lcd->drawString(clipText(aps[i].ssid, kScreenW - 90), 18, y + row_h / 2);
    g_lcd->setTextDatum(lgfx::middle_right);
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString(aps[i].open ? "open" : "lock", kScreenW - 16, y + row_h / 2);
    addHit(HIT_WIFI0 + (int)i, 8, y, kScreenW - 16, row_h);
  }
  g_lcd->clearClipRect();
  endFb();
}

void uiDrawKeyboard(const char *ssid, const String &pass, bool shift, bool num) {
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  useBody();
  g_lcd->setTextDatum(lgfx::top_left);
  g_lcd->setTextColor(kMuted);
  g_lcd->drawString(clipText(ssid ? ssid : "", kScreenW - 16), 8, 2);

  roundRect(8, 18, kScreenW - 16, 22, kPanel, kRail);
  g_lcd->setTextDatum(lgfx::middle_left);
  g_lcd->setTextColor(kText);
  String shown = pass;
  while (shown.length() && g_lcd->textWidth(shown) > kScreenW - 36) {
    shown.remove(0, 1);
  }
  g_lcd->drawString(shown.length() ? shown : String("_"), 16, 29);

  const char *rows[3];
  if (num) {
    rows[0] = "1234567890";
    rows[1] = "@$%&-_+=()";
    rows[2] = ".,?!#*'";
  } else if (shift) {
    rows[0] = "QWERTYUIOP";
    rows[1] = "ASDFGHJKL";
    rows[2] = "ZXCVBNM";
  } else {
    rows[0] = "qwertyuiop";
    rows[1] = "asdfghjkl";
    rows[2] = "zxcvbnm";
  }

  int y = 44;
  const int kh = 28;
  for (int r = 0; r < 3; r++) {
    int n = (int)strlen(rows[r]);
    int kw = kScreenW / max(n, 1);
    int x0 = (kScreenW - kw * n) / 2;
    for (int i = 0; i < n; i++) {
      char buf[2] = {rows[r][i], 0};
      drawKey(x0 + i * kw + 1, y, kw - 2, kh - 2, buf, HIT_CHAR0 + (uint8_t)rows[r][i]);
    }
    y += kh;
  }
  drawKey(4, y, 70, kh - 2, "SHFT", HIT_KB_SHIFT);
  drawKey(76, y, 70, kh - 2, num ? "ABC" : "123", HIT_KB_NUM);
  drawKey(148, y, 90, kh - 2, "space", HIT_KB_SPACE);
  drawKey(240, y, 36, kh - 2, "del", HIT_KB_BKSP);
  drawKey(278, y, 38, kh - 2, "go", HIT_KB_JOIN, kOk);
  endFb();
}

void uiDrawLeagues() {
  static const char *kNames[kLeagueCount] = {"MLB", "NHL", "NBA", "NFL"};
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  screenTitle("Scoreboard");
  const int gap = 10;
  const int w = (kScreenW - 24 - gap) / 2;
  const int h = 52;
  const int y0 = 40;
  for (int i = 0; i < kLeagueCount; i++) {
    int x = 12 + (i % 2) * (w + gap);
    int y = y0 + (i / 2) * (h + gap);
    roundRect(x, y, w, h, kPanel, kRail);
    useTitle();
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(kText);
    g_lcd->drawString(kNames[i], x + w / 2, y + h / 2);
    addHit(HIT_LEAGUE0 + i, x, y, w, h);
  }
  endFb();
}

void uiDrawMlbNeedWifi(const char *detail) {
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  useTitle();
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->setTextColor(kDanger);
  g_lcd->drawString("WiFi required", kScreenW / 2, 40);
  useBody();
  g_lcd->setTextColor(kText);
  g_lcd->drawString("Connect to WiFi first", kScreenW / 2, 78);
  g_lcd->setTextColor(kMuted);
  g_lcd->drawString(clipText(detail ? detail : "This feature needs a network.", kScreenW - 24),
                    kScreenW / 2, 100);
  drawButton(80, kScreenH - 44, 160, 32, "Settings", kPanel, kMuted, kText, HIT_GOTO_SETTINGS);
  endFb();
}

void uiDrawWifiWake(const char *ssid) {
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  useTitle();
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->setTextColor(kText);
  g_lcd->drawString("Waking WiFi radio", kScreenW / 2, 56);
  useBody();
  g_lcd->setTextColor(kMuted);
  if (ssid && ssid[0]) {
    g_lcd->drawString(clipText(String("Joining ") + ssid, kScreenW - 24), kScreenW / 2, 92);
  } else {
    g_lcd->drawString("Connecting...", kScreenW / 2, 92);
  }
  endFb();
}

// "More": Diversions, Settings, About on top; the post-con music apps below.
void uiDrawPostConMenu() {
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  drawBackPill("Menu");
  const int y0 = kTileTop;
  const int y1 = y0 + kTileH + kTileGap;
  const int w = (kScreenW - 12 - 2 * kTileGap) / 3;
  auto col = [&](int i) { return 6 + i * (w + kTileGap); };
  drawTile(col(0), y0, w, kTileH, "Diversions", "games & fun", HIT_POSTCON0 + POSTCON_DIVERSIONS,
           settingsColorRgb());
  drawTile(col(1), y0, w, kTileH, "Settings", "name, wifi", HIT_POSTCON0 + POSTCON_SETTINGS,
           kRail);
  drawTile(col(2), y0, w, kTileH, "About", "version", HIT_POSTCON0 + POSTCON_ABOUT, kRail);
  const int w2 = (kScreenW - 12 - kTileGap) / 2;
  drawTile(6, y1, w2, kTileH, "Spotify", "remote", HIT_POSTCON0 + POSTCON_SPOTIFY, kRail);
  drawTile(6 + w2 + kTileGap, y1, w2, kTileH, "LAN Music", "now playing",
           HIT_POSTCON0 + POSTCON_UPNP, kRail);
  endFb();
}

void uiDrawDiversions() {
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  drawBackPill("More");
  const int w = (kScreenW - 12 - kTileGap) / 2;
  const int x1 = 6 + w + kTileGap, y1 = kTileTop + kTileH + kTileGap;
  const char *hs = !hideseekActive() ? "not running"
                   : hideseekFound()  ? "found!"
                   : hideseekIAmIt()  ? "you're it"
                                      : "find them";
  drawTile(6, kTileTop, w, kTileH, "Games", "blackjack", HIT_DIV0 + DIV_GAMES, settingsColorRgb());
  drawTile(x1, kTileTop, w, kTileH, "Hide & Seek", hs, HIT_DIV0 + DIV_HIDESEEK,
           hideseekActive() && !hideseekFound() ? 0xFD20 : kRail);
  drawTile(6, y1, w, kTileH, "Silly", "animations", HIT_DIV0 + DIV_SILLY, kRail);
  drawTile(x1, y1, w, kTileH, "Scores", "live games", HIT_DIV0 + DIV_SCORES, kRail);
  endFb();
}

// ---- Hide and Seek ----
// Seekers: a big hot/cold meter for my own reading of it, and the team's best readings.
// It: a danger meter for the closest seeker, and who's closing in.

static uint16_t heatColor(int rssi) {
  if (!rssi) return 0x7BEF;
  if (rssi >= kHideSeekCloseRssi) return 0xF81F;
  if (rssi >= -55) return 0xF800;
  if (rssi >= -65) return 0xFD20;
  if (rssi >= -75) return 0x07FF;
  return 0x5DDF;
}

// 0..1 across the useful range: -95 dBm (nothing) to -40 dBm (touching).
static float heatFrac(int rssi) {
  if (!rssi) return 0.0f;
  float f = (float)(rssi + 95) / 55.0f;
  return f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}

// Segmented meter, cold blue on the left to hot magenta on the right.
static void drawHeatMeter(int x, int y, int w, int h, int rssi) {
  const int segs = 22, gap = 2;
  const int sw = (w - (segs - 1) * gap) / segs;
  int lit = (int)(heatFrac(rssi) * segs + 0.5f);
  bool pulse = rssi && rssi >= -55 && (millis() / 180) % 2;
  for (int i = 0; i < segs; i++) {
    int r = -95 + (int)((i + 0.5f) * 55.0f / segs);
    uint16_t c = heatColor(r);
    int sx = x + i * (sw + gap);
    if (i < lit) {
      g_lcd->fillRoundRect(sx, y, sw, h, 2, (pulse && i == lit - 1) ? 0xFFFF : c);
    } else {
      g_lcd->drawRoundRect(sx, y, sw, h, 2, 0x2104);
    }
  }
}

static String hsFirst(const char *id) {
  const Attendee *a = rosterFind(id);
  String n = a ? a->name : String(id);
  int sp = n.indexOf(' ');
  return sp > 0 ? n.substring(0, sp) : n;
}

static bool g_hs_radar = false;
void uiHideSeekToggleView() { g_hs_radar = !g_hs_radar; }

// Radar: me in the centre, teammates as named blips, it as a fuzzy blob (position) or a ring
// (distance only). The layout is a best fit of everyone's readings; its rotation is arbitrary.
static void drawHideSeekRadar(const String &it_first) {
  HideSeekRadar rd;
  hideseekRadar(&rd);
  const int cx = 82, cy = 101, r = 64;
  const float deg = 0.01745329f;
  uint32_t now = millis();
  float far = 3.0f;
  for (int i = 0; i < rd.n; i++) {
    float d = sqrtf(rd.blips[i].x * rd.blips[i].x + rd.blips[i].y * rd.blips[i].y);
    if (d > far) far = d;
  }
  if (rd.it_fix == 2) {
    float d = sqrtf(rd.it_x * rd.it_x + rd.it_y * rd.it_y) + rd.it_r * 0.5f;
    if (d > far) far = d;
  }
  if (rd.it_fix >= 1 && rd.it_ring > far) far = rd.it_ring;
  far *= 1.15f;
  if (far > kHideSeekMaxMetres) far = kHideSeekMaxMetres;
  float ppm = (float)(r - 4) / far;

  const uint16_t grid = 0x0320, glow = 0x07E0;
  g_lcd->fillCircle(cx, cy, r + 2, 0x0100);
  g_lcd->fillCircle(cx, cy, r, kBoard);
  for (int k = 1; k <= 3; k++) {
    g_lcd->drawCircle(cx, cy, r * k / 3, grid);
  }
  g_lcd->drawFastHLine(cx - r, cy, r * 2, grid);
  g_lcd->drawFastVLine(cx, cy - r, r * 2, grid);
  float sweep = fmodf((float)now * 0.18f, 360.0f);
  for (int i = 12; i >= 0; i--) {
    float a = (sweep - (float)i * 3.5f) * deg;
    g_lcd->drawLine(cx, cy, cx + (int)(cosf(a) * r), cy + (int)(sinf(a) * r),
                    scaleRgb(glow, 8 - i / 2, 10));
  }
  g_lcd->setFont(&fonts::Font0);
  g_lcd->setTextSize(1);
  char lab[12];
  snprintf(lab, sizeof(lab), "~%dm", (int)(far + 0.5f));
  g_lcd->setTextDatum(lgfx::bottom_right);
  g_lcd->setTextColor(0x4A69);
  g_lcd->drawString(lab, cx + r, cy + r);

  auto clampIn = [&](float x, float y, int &px, int &py) {
    float len = sqrtf(x * x + y * y) * ppm;
    float k = len > (float)(r - 5) ? (float)(r - 5) / len : 1.0f;
    px = cx + (int)(x * ppm * k);
    py = cy - (int)(y * ppm * k);
  };

  // It.
  bool pulse = (now / 300) % 2;
  if (rd.it_fix == 2) {
    int px, py;
    clampIn(rd.it_x, rd.it_y, px, py);
    int rr = (int)(rd.it_r * ppm);
    if (rr < 6) rr = 6;
    if (rr > r) rr = r;
    for (int a = 0; a < 360; a += 12) {
      g_lcd->drawPixel(px + (int)(cosf(a * deg) * rr), py + (int)(sinf(a * deg) * rr), 0xF81F);
    }
    g_lcd->fillCircle(px, py, pulse ? 5 : 4, 0xF81F);
    g_lcd->drawCircle(px, py, pulse ? 8 : 7, 0xF81F);
    g_lcd->setTextDatum(lgfx::bottom_center);
    g_lcd->setTextColor(0xF81F);
    g_lcd->drawString((it_first + "?").c_str(), px, py - 9);
  } else if (rd.it_fix == 1) {
    int rr = (int)(rd.it_ring * ppm);
    if (rr > r - 2) rr = r - 2;
    for (int a = 0; a < 360; a += 8) {
      int x = cx + (int)(cosf(a * deg) * rr), y = cy + (int)(sinf(a * deg) * rr);
      g_lcd->fillRect(x, y, 2, 2, pulse ? 0xF81F : 0x780F);
    }
  }

  // Teammates (or, on it's badge, the seekers).
  for (int i = 0; i < rd.n; i++) {
    int px, py;
    clampIn(rd.blips[i].x, rd.blips[i].y, px, py);
    g_lcd->fillCircle(px, py, 3, 0x07FF);
    g_lcd->drawCircle(px, py, 4, 0x0210);
    String nm = hsFirst(rd.blips[i].id);
    if (nm.length() > 7) nm = nm.substring(0, 7);
    g_lcd->setTextDatum(px > cx ? lgfx::middle_right : lgfx::middle_left);
    g_lcd->setTextColor(0xBDF7);
    g_lcd->drawString(nm, px > cx ? px - 6 : px + 6, py);
  }
  // Me.
  g_lcd->fillCircle(cx, cy, 4, kText);

  // Right panel.
  const int x0 = 158;
  g_lcd->setFont(&fonts::FreeSans9pt7b);
  g_lcd->setTextDatum(lgfx::top_left);
  if (hideseekIAmIt()) {
    g_lcd->setTextColor(kText);
    g_lcd->drawString("You're it", x0, 38);
    g_lcd->setTextColor(kMuted);
    char t[28];
    snprintf(t, sizeof(t), "%d seeker%s on map", rd.n, rd.n == 1 ? "" : "s");
    g_lcd->drawString(t, x0, 60);
    float best = 0;
    for (int i = 0; i < rd.n; i++) {
      if (rd.blips[i].metres > 0 && (best == 0 || rd.blips[i].metres < best)) best = rd.blips[i].metres;
    }
    if (best > 0) {
      snprintf(t, sizeof(t), "closest ~%dm", (int)(best + 0.5f));
      g_lcd->setTextColor(best < 4 ? 0xF800 : best < 10 ? 0xFD20 : 0x07FF);
      g_lcd->drawString(t, x0, 82);
    }
  } else {
    int mine = hideseekMyRssi();
    g_lcd->setTextColor(heatColor(mine));
    g_lcd->drawString(clipText(mine ? hideseekHeatWord(mine) : (hideseekDark() ? "Gone dark" : "No signal"),
                               kScreenW - x0 - 4),
                      x0, 36);
    int tr = hideseekTrend();
    const char *tw = tr == 1 ? "warmer" : tr == -1 ? "colder" : tr == 0 ? "steady" : "walk a bit";
    uint16_t tc = tr == 1 ? 0xF800 : tr == -1 ? 0x5DDF : kMuted;
    // Arrow: up = warmer, down = colder.
    int ax = x0 + 8, ay = 68;
    if (tr == 1) {
      g_lcd->fillTriangle(ax, ay - 8, ax - 7, ay + 4, ax + 7, ay + 4, tc);
    } else if (tr == -1) {
      g_lcd->fillTriangle(ax, ay + 8, ax - 7, ay - 4, ax + 7, ay - 4, tc);
    } else {
      g_lcd->fillRect(ax - 7, ay - 2, 14, 4, tc);
    }
    g_lcd->setTextColor(tc);
    g_lcd->drawString(tw, x0 + 22, 60);
    g_lcd->setTextColor(kMuted);
    char t[32];
    if (rd.it_fix == 2) {
      snprintf(t, sizeof(t), "fix: %d players", rd.it_anchors);
      g_lcd->setTextColor(0xF81F);
    } else if (rd.it_fix == 1) {
      snprintf(t, sizeof(t), "~%dm, no fix yet", (int)(rd.it_ring + 0.5f));
    } else {
      snprintf(t, sizeof(t), "no fix yet");
    }
    g_lcd->drawString(t, x0, 84);
    g_lcd->setTextColor(0x632C);
    g_lcd->drawString("3+ players = fix", x0, 106);
  }
  g_lcd->setFont(&fonts::Font0);
  g_lcd->setTextColor(0x632C);
  g_lcd->setTextDatum(lgfx::bottom_left);
  g_lcd->drawString("turn the map to match", x0, 148);
  g_lcd->drawString("your team. tap: board", x0, 160);
}

void uiDrawHideSeek() {
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  int px = drawBackPill("Diversions");
  useBody();
  g_lcd->setTextDatum(lgfx::middle_left);
  g_lcd->setTextColor(0xFD20);
  g_lcd->drawString("Hide & Seek", px + 6, 16);

  String it = hideseekItName();
  int sp = it.indexOf(' ');
  String it_first = sp > 0 ? it.substring(0, sp) : it;
  g_lcd->setTextDatum(lgfx::middle_center);

  if (!hideseekActive()) {
    g_lcd->setTextColor(kText);
    g_lcd->drawString("No round running", kScreenW / 2, 80);
    g_lcd->setFont(&fonts::FreeSans9pt7b);
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString("A roster update starts one", kScreenW / 2, 106);
    endFb();
    return;
  }

  if (hideseekFound()) {
    g_lcd->setFont(&fonts::FreeSansBold18pt7b);
    g_lcd->setTextColor(0xF81F);
    g_lcd->drawString("FOUND!", kScreenW / 2, 70);
    useBody();
    g_lcd->setTextColor(kText);
    String by = hideseekFoundBy();
    String line = hideseekIAmIt() ? String("Found by ") + hsFirst(by.c_str())
                  : by == rosterMyId() ? String("You found ") + it_first + "!"
                                       : it_first + " found by " + hsFirst(by.c_str());
    g_lcd->drawString(clipText(line, kScreenW - 16), kScreenW / 2, 106);
    g_lcd->setFont(&fonts::FreeSans9pt7b);
    g_lcd->setTextColor(kMuted);
    if (hideseekIAmIt()) {
      uint32_t s = hideseekHiddenSecs();
      char t[40];
      snprintf(t, sizeof(t), "Hidden for %u:%02u. Next round soon.", (unsigned)(s / 60),
               (unsigned)(s % 60));
      g_lcd->drawString(t, kScreenW / 2, 136);
    } else {
      g_lcd->drawString("Next round comes with a roster update", kScreenW / 2, 136);
    }
    endFb();
    return;
  }

  addHit(HIT_HS_VIEW, 0, 32, kScreenW, kScreenH - 32);
  if (g_hs_radar) {
    drawHideSeekRadar(it_first);
    endFb();
    return;
  }
  {
    g_lcd->setFont(&fonts::Font0);
    g_lcd->setTextSize(1);
    g_lcd->setTextDatum(lgfx::top_left);
    g_lcd->setTextColor(0x632C);
    g_lcd->drawString("tap: radar", px + 8, 25);
    useBody();
    g_lcd->setTextDatum(lgfx::middle_center);
  }

  HideSeekReading board[3];
  int n = hideseekBoard(board, 3);

  if (hideseekIAmIt()) {
    // It: how close the nearest seeker is, and who.
    int best = n ? board[0].rssi : 0;
    uint32_t s = hideseekHiddenSecs();
    char t[32];
    snprintf(t, sizeof(t), "You're it!  %u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    g_lcd->setTextDatum(lgfx::middle_left);
    g_lcd->setTextColor(kText);
    g_lcd->drawString(t, 8, 44);
    g_lcd->setTextDatum(lgfx::middle_right);
    g_lcd->setTextColor(heatColor(best));
    g_lcd->drawString(n ? (best >= -55 ? "DANGER" : best >= -65 ? "Careful" : "Safe")
                        : "Nobody near",
                      kScreenW - 8, 44);
    drawHeatMeter(8, 58, kScreenW - 16, 16, best);
    g_lcd->setFont(&fonts::FreeSans9pt7b);
    for (int i = 0; i < n; i++) {
      int y = 92 + i * 24;
      g_lcd->setTextDatum(lgfx::middle_left);
      g_lcd->setTextColor(kText);
      g_lcd->drawString(clipText(hsFirst(board[i].id), 120), 10, y);
      g_lcd->setTextColor(heatColor(board[i].rssi));
      g_lcd->drawString(hideseekHeatWord(board[i].rssi), 140, y);
      char d[12];
      snprintf(d, sizeof(d), "%d", board[i].rssi);
      g_lcd->setTextDatum(lgfx::middle_right);
      g_lcd->setTextColor(kMuted);
      g_lcd->drawString(d, kScreenW - 10, y);
    }
    if (!n) {
      g_lcd->setTextDatum(lgfx::middle_center);
      g_lcd->setTextColor(kMuted);
      g_lcd->drawString("Keep this screen open while you hide", kScreenW / 2, 110);
    }
    endFb();
    return;
  }

  // Seeker: my reading, big, then the team's best readings.
  int mine = hideseekMyRssi();
  String head;
  if (mine) {
    head = hideseekHeatWord(mine);
  } else if (hideseekDark()) {
    head = it_first + " has gone dark";
  } else {
    head = String("Listening for ") + it_first + "...";
  }
  g_lcd->setTextDatum(lgfx::middle_left);
  g_lcd->setTextColor(heatColor(mine));
  g_lcd->drawString(clipText(head, kScreenW - 80), 8, 44);
  if (mine) {
    char d[16];
    snprintf(d, sizeof(d), "%d dBm", mine);
    g_lcd->setTextDatum(lgfx::middle_right);
    g_lcd->setTextColor(kText);
    g_lcd->drawString(d, kScreenW - 8, 44);
  }
  drawHeatMeter(8, 58, kScreenW - 16, 16, mine);
  float prog = hideseekFoundProgress();
  int list_y = 92;
  if (prog > 0.0f) {
    g_lcd->fillRect(8, 80, kScreenW - 16, 5, 0x2104);
    g_lcd->fillRect(8, 80, (int)(prog * (kScreenW - 16)), 5, 0xF81F);
    list_y = 98;
  }
  g_lcd->setFont(&fonts::FreeSans9pt7b);
  g_lcd->setTextDatum(lgfx::middle_left);
  g_lcd->setTextColor(kMuted);
  g_lcd->drawString(prog > 0.0f ? "Hold close to find them!" : (String("Team on ") + it_first).c_str(),
                    8, list_y);
  for (int i = 0; i < n; i++) {
    int y = list_y + 20 + i * 20;
    g_lcd->setTextDatum(lgfx::middle_left);
    g_lcd->setTextColor(kText);
    g_lcd->drawString(clipText(hsFirst(board[i].id), 100), 10, y);
    int bx = 118, bw = kScreenW - bx - 52;
    g_lcd->fillRect(bx, y - 4, bw, 8, 0x2104);
    g_lcd->fillRect(bx, y - 4, (int)(heatFrac(board[i].rssi) * bw), 8, heatColor(board[i].rssi));
    char d[12];
    snprintf(d, sizeof(d), "%d", board[i].rssi);
    g_lcd->setTextDatum(lgfx::middle_right);
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString(d, kScreenW - 10, y);
  }
  if (!n) {
    g_lcd->setTextColor(0x4208);
    g_lcd->drawString("no teammates in range", 10, list_y + 20);
  }
  endFb();
}

void uiDrawGames() {
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  drawBackPill("Diversions");
  const int w = (kScreenW - 12 - 2 * kTileGap) / 3;
  const int h = kTileH * 2 + kTileGap;
  auto col = [&](int i) { return 6 + i * (w + kTileGap); };
  char best[24];
  snprintf(best, sizeof(best), birdBest() ? "best %u" : "tap to flap", (unsigned)birdBest());
  char ob[24];
  if (!outbreakActive()) {
    snprintf(ob, sizeof(ob), "not running");
  } else if (outbreakState() == OB_MEDIC) {
    snprintf(ob, sizeof(ob), "medic");
  } else if (outbreakState() == OB_INFECTED) {
    snprintf(ob, sizeof(ob), "infected!");
  } else if (outbreakAliveSecs()) {
    uint32_t s = outbreakAliveSecs();
    snprintf(ob, sizeof(ob), "alive %uh%02um", (unsigned)(s / 3600), (unsigned)(s / 60 % 60));
  } else {
    snprintf(ob, sizeof(ob), "stay healthy");
  }
  drawTile(col(0), kTileTop, w, h, "Blackjack", "up to 4", HIT_GAME_BJ, kRail);
  drawTile(col(1), kTileTop, w, h, "Klippy Bird", best, HIT_GAME_BIRD, settingsColorRgb());
  drawTile(col(2), kTileTop, w, h, "Outbreak", ob, HIT_GAME_OUTBREAK,
           outbreakState() == OB_INFECTED ? 0x5FE0 : kRail);
  endFb();
}

// ---- Outbreak ----
// Drawn over the nametag (from endFb) while the Outbreak game is open: a heartbeat strip along
// the bottom, the player's status, and the sickly green look while infected.

void uiSetOutbreakOverlay(bool on) { g_ob_overlay = on; }

// One heartbeat, u in 0..1: P wave, QRS spike, T wave. Returns -0.35..1.
static float ecgShape(float u) {
  auto bump = [](float u, float c, float w) {
    float d = (u - c) / w;
    return d * d < 1.0f ? (1.0f - d * d) : 0.0f;
  };
  float v = 0.12f * bump(u, 0.14f, 0.05f);
  if (u > 0.28f && u < 0.31f) v -= (u - 0.28f) / 0.03f * 0.2f;
  else if (u >= 0.31f && u < 0.34f) v += -0.2f + (u - 0.31f) / 0.03f * 1.2f;
  else if (u >= 0.34f && u < 0.38f) v += 1.0f - (u - 0.34f) / 0.04f * 1.35f;
  else if (u >= 0.38f && u < 0.41f) v += -0.35f + (u - 0.38f) / 0.03f * 0.35f;
  v += 0.25f * bump(u, 0.6f, 0.08f);
  return v;
}

static void drawBiohazard(int cx, int cy, int r, uint16_t c, uint16_t bg) {
  for (int i = 0; i < 3; i++) {
    float a = (-90.0f + i * 120.0f) * (float)M_PI / 180.0f;
    int x = cx + (int)(cosf(a) * r * 0.55f), y = cy + (int)(sinf(a) * r * 0.55f);
    g_lcd->fillCircle(x, y, r / 2, c);
    int x2 = cx + (int)(cosf(a) * r * 0.75f), y2 = cy + (int)(sinf(a) * r * 0.75f);
    g_lcd->fillCircle(x2, y2, r / 4, bg);
  }
  g_lcd->fillCircle(cx, cy, r / 4 + 1, bg);
  g_lcd->drawCircle(cx, cy, r * 0.62f, c);
  g_lcd->fillCircle(cx, cy, r / 7 + 1, c);
}

static void drawOutbreakOverlay() {
  uint32_t now = millis();
  OutbreakState st = outbreakState();
  bool active = outbreakActive();
  bool sick = active && st == OB_INFECTED;
  const uint16_t green = 0x5FE0, dk = 0x0000;

  if (sick) {
    // Sickly look: faint green scanlines over everything, and a glitch burst every few seconds.
    for (int y = 30; y < kScreenH - 36; y += 4) {
      for (int x = (y / 4) % 2 ? 0 : 3; x < kScreenW; x += 6) {
        g_lcd->drawFastHLine(x, y, 2, 0x2A04);
      }
    }
    uint32_t ph = now % 2600;
    if (ph < 380) {
      uint32_t seed = now / 60 + 7;
      for (int i = 0; i < 7; i++) {
        seed = seed * 1103515245u + 12345u;
        int y = 30 + (int)((seed >> 8) % (kScreenH - 70));
        int h = 2 + (int)((seed >> 4) % 5);
        int x = (int)((seed >> 16) % 80);
        g_lcd->fillRect(x, y, kScreenW - x - (int)((seed >> 20) % 60), h,
                        (seed & 1) ? green : 0x0320);
      }
    }
    drawBiohazard(kScreenW - 24, 46, 16, green, dk);
  }

  // Heartbeat strip.
  const int top = kScreenH - 34, h = 30, mid = top + 19;
  g_lcd->fillRect(0, top, kScreenW, h + 4, dk);
  uint16_t trace = !active ? 0x7BEF : sick ? 0xBFE0 : st == OB_MEDIC ? 0x07FF
                   : st == OB_IMMUNE ? 0x5DDF : 0xF800;  // healthy = red, a living heartbeat
  uint32_t period = sick ? 640 : 950;
  int prev_y = mid;
  for (int x = 0; x < kScreenW - 26; x += 2) {
    int y = mid;
    if (active) {
      uint32_t t = now - (uint32_t)(kScreenW - 26 - x) * 5;
      float u = (float)(t % period) / (float)period;
      if (sick && ((t / period) % 3) == 2) {
        u = fmodf(u * 1.4f, 1.0f);  // skipped beat
      }
      y = mid - (int)(ecgShape(u) * 15.0f);
    }
    if (x) {
      g_lcd->drawLine(x - 2, prev_y, x, y, trace);
    }
    prev_y = y;
  }
  float u = (float)(now % period) / (float)period;
  int r = (active && u > 0.3f && u < 0.45f) ? 9 : 7;
  drawHeart(kScreenW - 13, mid - 3, r, active ? (sick ? green : st == OB_HEALTHY ? 0xF800 : trace) : 0x7BEF);

  // Status chip, and a bar showing progress toward catching it (or toward a cure).
  useBody();
  String label;
  if (!active) {
    label = "No outbreak running";
  } else if (st == OB_INFECTED) {
    label = "INFECTED";
  } else if (st == OB_MEDIC) {
    label = "MEDIC";
  } else if (st == OB_IMMUNE) {
    label = "IMMUNE";
  } else {
    label = "HEALTHY";
  }
  if (active && st != OB_MEDIC) {
    // Survival time: runs while you're playing and not infected.
    uint32_t s = outbreakAliveSecs();
    char t[16];
    if (s >= 3600) {
      snprintf(t, sizeof(t), "  %uh%02um", (unsigned)(s / 3600), (unsigned)(s / 60 % 60));
    } else {
      snprintf(t, sizeof(t), "  %u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    }
    label += t;
  }
  int lw = g_lcd->textWidth(label) + 12;
  g_lcd->fillRoundRect(4, top - 10, lw, 18, 4, dk);
  g_lcd->drawRoundRect(4, top - 10, lw, 18, 4, trace);
  g_lcd->setTextDatum(lgfx::middle_left);
  g_lcd->setTextColor(trace);
  g_lcd->drawString(label, 10, top);
  float e = outbreakExposure();
  if (e > 0.0f) {
    int bw = (int)(e * (kScreenW - lw - 20));
    g_lcd->fillRect(lw + 10, top - 2, kScreenW - lw - 20, 4, 0x2104);
    g_lcd->fillRect(lw + 10, top - 2, bw, 4, st == OB_INFECTED ? 0x07FF : green);
  }
  if (active) {
    // Who's in range, and how strong: "close" needs kOutbreakCloseRssi or better.
    int n = outbreakNearbyCount();
    char nb[40];
    if (n) {
      snprintf(nb, sizeof(nb), "%d nearby  %d dBm", n, outbreakBestRssi());
    } else {
      snprintf(nb, sizeof(nb), "nobody nearby");
    }
    g_lcd->setFont(&fonts::Font0);
    g_lcd->setTextSize(1);
    int tw = g_lcd->textWidth(nb) + 8;
    g_lcd->fillRoundRect(kScreenW - tw - 4, top - 24, tw, 12, 3, dk);
    g_lcd->setTextDatum(lgfx::middle_right);
    g_lcd->setTextColor(n && outbreakBestRssi() >= kOutbreakCloseRssi ? 0xFFE0 : 0xBDF7);
    g_lcd->drawString(nb, kScreenW - 8, top - 18);
  }
  drawBackPill("Games");
}

// ---- Klippy Bird ----

static lgfx::LGFX_Sprite *g_klippy = nullptr;

static lgfx::LGFX_Sprite *klippySprite() {
  if (!g_klippy) {
    g_klippy = new lgfx::LGFX_Sprite(g_panel ? g_panel : g_fb);
    g_klippy->setColorDepth(16);
    if (g_klippy->createSprite(kKlippyW, kKlippyH)) {
      g_klippy->pushImage(0, 0, kKlippyW, kKlippyH, (const lgfx::rgb565_t *)kKlippyPixels);
    }
  }
  return g_klippy;
}

static uint32_t birdHash(uint32_t n) {
  n ^= n >> 16;
  n *= 0x7feb352du;
  n ^= n >> 15;
  n *= 0x846ca68bu;
  return n ^ (n >> 16);
}

// Sky, a parallax skyline and clouds, all generated from the distance flown.
static void drawBirdWorld(float world) {
  const int ground_y = kScreenH - kBirdGround;
  for (int y = 0; y < ground_y; y += 6) {
    g_lcd->fillRect(0, y, kScreenW, 6, mix565(0x4D9F, 0xC71F, y * 255 / ground_y));
  }
  // Clouds at 1/8 speed.
  float cw = world * 0.125f;
  for (int i = (int)(cw / 90) - 1; i <= (int)(cw / 90) + 5; i++) {
    uint32_t h = birdHash((uint32_t)i * 2654435761u + 7);
    int x = i * 90 - (int)cw + (int)(h % 40);
    int y = 10 + (int)((h >> 8) % 40);
    int r = 8 + (int)((h >> 16) % 6);
    g_lcd->fillCircle(x, y, r, 0xEF7D);
    g_lcd->fillCircle(x + r, y + 2, r - 2, 0xEF7D);
    g_lcd->fillCircle(x - r, y + 3, r - 3, 0xEF7D);
  }
  // Skyline at 1/4 speed, with a few lit windows.
  float sw = world * 0.25f;
  int bx = -((int)sw % 26) - 26;
  for (int i = (int)(sw / 26) - 1; bx < kScreenW + 26; i++, bx += 26) {
    uint32_t h = birdHash((uint32_t)i + 99);
    int bh = 22 + (int)(h % 44);
    int bw = 18 + (int)((h >> 8) % 8);
    g_lcd->fillRect(bx, ground_y - bh, bw, bh, 0x3A6F);
    for (int wy = ground_y - bh + 5; wy < ground_y - 4; wy += 7) {
      for (int wx = bx + 3; wx < bx + bw - 3; wx += 5) {
        if ((birdHash((uint32_t)(wx * 31 + wy)) & 3) == 0) {
          g_lcd->fillRect(wx, wy, 2, 3, 0xFF2C);
        }
      }
    }
  }
}

static void drawBirdPipe(const BirdPipe &p) {
  const uint16_t body = 0x2E68, edge = 0x1B44, lip = 0x3F6A;
  int x = (int)p.x;
  int top = p.gap_y - p.gap_h / 2;
  int bot = p.gap_y + p.gap_h / 2;
  int ground_y = kScreenH - kBirdGround;
  g_lcd->fillRect(x, 0, kBirdPipeW, top, body);
  g_lcd->fillRect(x, bot, kBirdPipeW, ground_y - bot, body);
  g_lcd->fillRect(x + 4, 0, 4, top, lip);
  g_lcd->fillRect(x + 4, bot, 4, ground_y - bot, lip);
  g_lcd->fillRect(x - 3, top - 9, kBirdPipeW + 6, 9, body);
  g_lcd->drawRect(x - 3, top - 9, kBirdPipeW + 6, 9, edge);
  g_lcd->fillRect(x - 3, bot, kBirdPipeW + 6, 9, body);
  g_lcd->drawRect(x - 3, bot, kBirdPipeW + 6, 9, edge);
  g_lcd->drawFastVLine(x, 0, top - 9, edge);
  g_lcd->drawFastVLine(x + kBirdPipeW - 1, 0, top - 9, edge);
  g_lcd->drawFastVLine(x, bot + 9, ground_y - bot - 9, edge);
  g_lcd->drawFastVLine(x + kBirdPipeW - 1, bot + 9, ground_y - bot - 9, edge);
}

static void drawBirdGround(float world) {
  const int gy = kScreenH - kBirdGround;
  g_lcd->fillRect(0, gy, kScreenW, kBirdGround, 0xDEB2);
  g_lcd->fillRect(0, gy, kScreenW, 3, 0x5D06);
  int off = (int)world % 12;
  for (int x = -off; x < kScreenW; x += 12) {
    g_lcd->drawLine(x, gy + 4, x + 6, kScreenH, 0xC5ED);
  }
}

static void drawBirdPanel(int y, int h) {
  g_lcd->fillRoundRect(40, y, kScreenW - 80, h, 10, 0x18E3);
  g_lcd->drawRoundRect(40, y, kScreenW - 80, h, 10, settingsColorRgb());
}

void uiDrawKlippyBird() {
  const BirdState &s = birdState();
  beginFb(false);
  clearHits();
  drawBirdWorld(s.world);
  for (const auto &p : s.pipes) {
    drawBirdPipe(p);
    // Ghost flag where a leader crashed: the leader's first name in bold white on a dark tag
    // just above the ground, with a red pennant on a short pole.
    int stack = 0;
    for (const auto &g : s.ghosts) {
      if (g.score == p.index) {
        const uint16_t tag = 0x0841, rim = 0xFFFF, pennant = 0xF800;
        int fx = (int)p.x + kBirdPipeW / 2;
        int fy = kScreenH - kBirdGround - 17 - stack * 28;  // tag centre
        g_lcd->setFont(&fonts::FreeSansBold9pt7b);
        g_lcd->setTextSize(1);
        int tw = g_lcd->textWidth(g.label) + 14;
        int top = fy - 11;
        int left = fx - tw / 2;
        g_lcd->fillRect(left + 2, top - 16, 2, 16, rim);
        g_lcd->fillTriangle(left + 4, top - 16, left + 4, top - 6, left + 17, top - 11, pennant);
        g_lcd->fillRoundRect(left, top, tw, 22, 4, tag);
        g_lcd->drawRoundRect(left, top, tw, 22, 4, rim);
        g_lcd->drawRoundRect(left + 1, top + 1, tw - 2, 20, 3, rim);
        g_lcd->setTextDatum(lgfx::middle_center);
        g_lcd->setTextColor(0xFFFF);
        g_lcd->drawString(g.label, fx, fy + 1);
        stack++;
      }
    }
  }
  drawBirdGround(s.world);
  // Klippy tilts with speed: nose up flapping, nose down falling.
  float angle = s.phase == BIRD_PLAY ? std::max(-25.0f, std::min(70.0f, s.vy * 0.16f)) : 0.0f;
  lgfx::LGFX_Sprite *k = klippySprite();
  if (k) {
    k->pushRotateZoom(g_lcd, (float)kBirdX, s.y, angle, 1.0f, 1.0f, lgfx::rgb565_t(kKlippyKey));
  }

  char buf[48];
  if (s.phase == BIRD_PLAY) {
    g_lcd->setFont(&fonts::FreeSansBold18pt7b);
    g_lcd->setTextSize(1);
    g_lcd->setTextDatum(lgfx::top_center);
    snprintf(buf, sizeof(buf), "%u", (unsigned)s.score);
    g_lcd->setTextColor(0x0000);
    g_lcd->drawString(buf, kScreenW / 2 + 2, 6);
    g_lcd->setTextColor(0xFFFF);
    g_lcd->drawString(buf, kScreenW / 2, 4);
  } else if (s.phase == BIRD_TITLE) {
    drawBackPill("Games");
    // Title + top 5 on the right.
    useTitle();
    g_lcd->setTextDatum(lgfx::top_left);
    g_lcd->setTextColor(0x0000);
    g_lcd->drawString("KLIPPY BIRD", 132, 6);
    g_lcd->setTextColor(0xFFE0);
    g_lcd->drawString("KLIPPY BIRD", 130, 4);
    std::vector<BirdScore> top = birdLeaders();
    g_lcd->setFont(&fonts::Font0);
    g_lcd->setTextSize(1);
    int y = 32;
    if (top.empty()) {
      g_lcd->setTextColor(0xFFFF);
      g_lcd->drawString("No scores yet. Be first!", 132, y);
    }
    for (int i = 0; i < (int)top.size() && i < 5; i++) {
      const Attendee *a = rosterFind(top[i].id.c_str());
      String n = a ? a->name : top[i].id;
      int sp = n.indexOf(' ');
      n = sp > 0 ? n.substring(0, sp) : n;
      n.toUpperCase();
      snprintf(buf, sizeof(buf), "%d. %-10s %4u", i + 1, n.c_str(), (unsigned)top[i].best);
      g_lcd->fillRect(128, y - 2, 186, 12, top[i].me ? settingsColorRgb() : 0x18E3);
      g_lcd->setTextColor(top[i].me ? 0x0000 : 0xFFFF);
      g_lcd->drawString(buf, 132, y);
      y += 14;
    }
    useBody();
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(0xFFFF);
    g_lcd->drawString("Tap to flap", kBirdX, kScreenH - kBirdGround - 14);
  } else {
    drawBirdPanel(30, 96);
    useTitle();
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(s.new_best ? 0xFFE0 : 0xFFFF);
    g_lcd->drawString(s.new_best ? "NEW BEST!" : "Game over", kScreenW / 2, 48);
    useBody();
    g_lcd->setTextColor(0xFFFF);
    snprintf(buf, sizeof(buf), "Score %u   Best %u", (unsigned)s.score, (unsigned)s.best);
    g_lcd->drawString(buf, kScreenW / 2, 76);
    snprintf(buf, sizeof(buf), "#%d of %d nearby", birdMyRank(), birdKnownCount());
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString(buf, kScreenW / 2, 96);
    g_lcd->setTextColor(settingsColorRgb());
    g_lcd->drawString("Tap to fly again", kScreenW / 2, 114);
  }
  endFb();
}

// ---- Blackjack ----

// Suit glyph centred at (cx, cy), roughly 2r across. Suits: 0 spades, 1 hearts, 2 diamonds,
// 3 clubs (card / 13).
static void drawSuit(int cx, int cy, int r, int suit, uint16_t c) {
  if (suit == 1) {
    drawHeart(cx, cy - r / 3, r, c);
  } else if (suit == 2) {
    g_lcd->fillTriangle(cx, cy - r - 1, cx - r + 1, cy, cx + r - 1, cy, c);
    g_lcd->fillTriangle(cx, cy + r + 1, cx - r + 1, cy, cx + r - 1, cy, c);
  } else if (suit == 0) {
    g_lcd->fillCircle(cx - r / 2, cy + r / 4, r / 2, c);
    g_lcd->fillCircle(cx + r / 2, cy + r / 4, r / 2, c);
    g_lcd->fillTriangle(cx - r, cy + r / 4, cx + r, cy + r / 4, cx, cy - r - 1, c);
    g_lcd->fillTriangle(cx, cy + r / 4, cx - r / 2, cy + r + 1, cx + r / 2, cy + r + 1, c);
  } else {
    int rr = r / 2 + 1;
    g_lcd->fillCircle(cx, cy - r / 2, rr, c);
    g_lcd->fillCircle(cx - r / 2 - 1, cy + r / 4, rr, c);
    g_lcd->fillCircle(cx + r / 2 + 1, cy + r / 4, rr, c);
    g_lcd->fillTriangle(cx, cy, cx - r / 2, cy + r + 1, cx + r / 2, cy + r + 1, c);
  }
}

static void drawCard(int x, int y, int w, int h, uint8_t card) {
  if (card == kBjHidden) {
    g_lcd->fillRoundRect(x, y, w, h, 4, 0x18B5);
    g_lcd->drawRoundRect(x, y, w, h, 4, kText);
    g_lcd->drawRoundRect(x + 3, y + 3, w - 6, h - 6, 3, 0x5AFF);
    return;
  }
  static const char *const kRanks[13] = {"A", "2", "3", "4", "5", "6", "7",
                                         "8", "9", "10", "J", "Q", "K"};
  int suit = card / 13;
  uint16_t ink = (suit == 1 || suit == 2) ? 0xD800 : 0x0000;
  g_lcd->fillRoundRect(x, y, w, h, 4, 0xFFFF);
  g_lcd->drawRoundRect(x, y, w, h, 4, 0x8410);
  g_lcd->setFont(&fonts::FreeSansBold9pt7b);
  g_lcd->setTextSize(1);
  g_lcd->setTextDatum(lgfx::top_left);
  g_lcd->setTextColor(ink);
  g_lcd->drawString(kRanks[card % 13], x + 3, y + 2);
  int r = max(4, w / 5);
  drawSuit(x + w / 2 + 2, y + h - r - 5, r, suit, ink);
}

// Cards left to right from x, squeezing the overlap to fit max_w.
static void drawHand(int x, int y, int w, int h, const uint8_t *cards, int n, int max_w) {
  if (n <= 0) {
    return;
  }
  int step = n > 1 ? min(w - 4, (max_w - w) / (n - 1)) : 0;
  step = max(step, 8);
  for (int i = 0; i < n; i++) {
    drawCard(x + i * step, y, w, h, cards[i]);
  }
}

static String handLabel(const uint8_t *cards, int n, bool split = false) {
  if (n <= 0) {
    return "";
  }
  bool soft = false;
  int t = bjTotal(cards, n, &soft);
  if (n == 2 && t == 21 && !split) {
    return "BJ";
  }
  if (t > 21) {
    return "Bust";
  }
  return soft && t < 21 ? String("soft ") + t : String(t);
}

static void drawBjLobby() {
  screenTitle("Blackjack");
  drawButton(8, 32, kScreenW - 16, 38, "Start table", kPanel, settingsColorRgb(), kText,
             HIT_BJ_START);
  std::vector<BjListing> tables = bjListings();
  useBody();
  g_lcd->setTextDatum(lgfx::middle_left);
  if (tables.empty()) {
    g_lcd->setTextColor(kMuted);
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->drawString("No tables nearby yet", kScreenW / 2, 112);
    g_lcd->setFont(&fonts::FreeSans9pt7b);
    g_lcd->drawString("Start one, or wait for someone who has", kScreenW / 2, 136);
    return;
  }
  int y = 78;
  for (int i = 0; i < (int)tables.size() && i < kBjListShown; i++) {
    const BjListing &t = tables[i];
    bool full = t.seated >= kBjSeats;
    roundRect(8, y, kScreenW - 16, 26, kPanel, full ? kRail : kMuted);
    useBody();
    g_lcd->setTextDatum(lgfx::middle_left);
    g_lcd->setTextColor(full ? kMuted : kText);
    g_lcd->drawString(clipText(String(t.host) + "'s table", kScreenW - 90), 16, y + 13);
    g_lcd->setTextDatum(lgfx::middle_right);
    char seats[12];
    snprintf(seats, sizeof(seats), full ? "full" : "%u/%d", (unsigned)t.seated, kBjSeats);
    g_lcd->drawString(seats, kScreenW - 16, y + 13);
    if (!full) {
      addHit(HIT_BJ_TABLE0 + i, 0, y - 1, kScreenW, 28);
    }
    y += 30;
  }
}

static void drawBjTable() {
  const BjTable &t = bjTable();
  const int me = bjMySeat();
  const BjRole role = bjRole();
  const uint16_t accent = settingsColorRgb();

  // Top: house hand, and End table / Leave on the right.
  useBody();
  g_lcd->setTextDatum(lgfx::middle_left);
  g_lcd->setTextColor(kMuted);
  String house = "House";
  bool hidden = t.dealer_n >= 2 && t.dealer[1] == kBjHidden;
  if (t.dealer_n > 0 && !hidden) {
    house += "  " + handLabel(t.dealer, t.dealer_n);
  }
  g_lcd->drawString(house, 8, 12);
  drawButton(kScreenW - 92, 2, 86, 22, role == BJ_HOST ? "End table" : "Leave", kPanel, kRail,
             kMuted, HIT_BJ_END);
  drawHand(8, 28, 26, 38, t.dealer, t.dealer_n, 196);

  // Right: every seat with its total; the seat whose turn it is is outlined.
  int y = 30;
  for (int i = 0; i < kBjSeats; i++) {
    const BjSeat &s = t.seats[i];
    if (!s.used) {
      continue;
    }
    bool turn = t.phase == BJ_PLAYING && t.turn == i;
    roundRect(212, y, kScreenW - 216, 22, turn ? scaleRgb(accent, 1, 5) : kPanel,
              turn ? accent : kRail);
    g_lcd->setFont(&fonts::FreeSans9pt7b);
    g_lcd->setTextDatum(lgfx::middle_left);
    g_lcd->setTextColor(i == me ? accent : kText);
    g_lcd->drawString(clipText(i == me ? String("You") : String(s.name), 58), 217, y + 11);
    g_lcd->setTextDatum(lgfx::middle_right);
    String right;
    if (t.phase == BJ_BETTING) {
      right = String("bet ") + (i == me ? bjMyBet() : s.bet);
    } else {
      if (!s.n) {
        right = "out";
      } else if (s.flags & kBjSplit) {
        right = String(bjTotal(s.cards, s.n)) + " | " + bjTotal(s.cards2, s.n2);
      } else {
        right = handLabel(s.cards, s.n);
      }
    }
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString(right, kScreenW - 8, y + 11);
    y += 24;
  }

  // Middle: your hand, big.
  if (me >= 0) {
    const BjSeat &s = t.seats[me];
    const bool split = s.flags & kBjSplit;
    g_lcd->setFont(&fonts::Font0);
    g_lcd->setTextSize(1);
    if (split) {
      // Two hands side by side; the one being played is outlined.
      const bool second = s.flags & kBjOnSecond;
      const bool live = t.phase == BJ_PLAYING && t.turn == me;
      for (int h = 0; h < 2; h++) {
        const int hx = 6 + h * 102;
        if (live && (h == 1) == second) {
          g_lcd->drawRoundRect(hx - 3, 69, 100, 54, 6, accent);
        }
        drawHand(hx, 72, 28, 40, h ? s.cards2 : s.cards, h ? s.n2 : s.n, 94);
        g_lcd->setTextDatum(lgfx::top_left);
        g_lcd->setTextColor(kText);
        g_lcd->drawString(handLabel(h ? s.cards2 : s.cards, h ? s.n2 : s.n, true), hx, 114);
      }
    } else {
      drawHand(8, 72, 34, 48, s.cards, s.n, 196);
      if (s.n > 0) {
        g_lcd->setTextDatum(lgfx::top_right);
        g_lcd->setTextColor(kText);
        g_lcd->drawString(handLabel(s.cards, s.n), 204, 124);
      }
    }
    g_lcd->setTextDatum(lgfx::top_left);
    g_lcd->setTextColor(kMuted);
    char line[40];
    int bet = t.phase == BJ_BETTING ? bjMyBet() : s.bet;
    snprintf(line, sizeof(line), split ? "%d chips   bet %d x2" : "%d chips   bet %d", s.chips, bet);
    g_lcd->drawString(line, 10, 124);
  }

  // Bottom: what you can do right now.
  const int by = kScreenH - 34;
  auto status = [&](const String &msg, uint16_t c) {
    useBody();
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(c);
    g_lcd->drawString(clipText(msg, kScreenW - 16), kScreenW / 2, by + 15);
  };
  if (me < 0) {
    status("Joining table...", kMuted);
    return;
  }
  const BjSeat &s = t.seats[me];
  if (t.phase == BJ_BETTING) {
    // You're dealt in automatically; this is just the window to change the bet.
    drawButton(8, by, 44, 30, "-", kPanel, kRail, kText, HIT_BJ_MINUS);
    char b[16];
    snprintf(b, sizeof(b), "Bet %d", bjMyBet());
    useBody();
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(kText);
    g_lcd->drawString(b, 104, by + 15);
    drawButton(156, by, 44, 30, "+", kPanel, kRail, kText, HIT_BJ_PLUS);
    char d[24];
    snprintf(d, sizeof(d), "Deal in %us", (unsigned)t.timer_s);
    g_lcd->setTextColor(accent);
    g_lcd->drawString(d, 260, by + 15);
  } else if (t.phase == BJ_PLAYING) {
    if (t.turn == me) {
      char st[16];
      snprintf(st, sizeof(st), "Stand  %us", (unsigned)t.timer_s);
      if (bjCanSplit()) {
        drawButton(8, by, 96, 30, "Hit", kPanel, kOk, kOk, HIT_BJ_HIT);
        drawButton(112, by, 96, 30, "Split", kPanel, 0xFE60, 0xFE60, HIT_BJ_SPLIT);
        drawButton(216, by, kScreenW - 224, 30, st, kPanel, accent, accent, HIT_BJ_STAND);
      } else {
        drawButton(8, by, 148, 30, "Hit", kPanel, kOk, kOk, HIT_BJ_HIT);
        drawButton(164, by, kScreenW - 172, 30, st, kPanel, accent, accent, HIT_BJ_STAND);
      }
    } else if (s.n == 0) {
      status("Sitting out - next hand", kMuted);
    } else {
      String who = (t.turn >= 0 && t.seats[t.turn].used) ? String(t.seats[t.turn].name) : "";
      status(who + "'s turn  " + t.timer_s + "s", kMuted);
    }
  } else if (t.phase == BJ_DEALER) {
    status("House plays...", kMuted);
  } else if (t.phase == BJ_RESULTS && (s.flags & kBjSplit)) {
    auto delta = [&](uint8_t r) {
      return r == BJR_WIN ? (int)s.bet : (r == BJR_BUST || r == BJR_LOSE) ? -(int)s.bet : 0;
    };
    int a = delta(s.result), b = delta(s.result2);
    char msg[40];
    snprintf(msg, sizeof(msg), "Hand 1 %+d  Hand 2 %+d  = %+d", a, b, a + b);
    status(msg, a + b > 0 ? kOk : (a + b < 0 ? kDanger : kMuted));
  } else if (t.phase == BJ_RESULTS) {
    char msg[32];
    uint16_t c = kMuted;
    switch (s.result) {
      case BJR_BLACKJACK:
        snprintf(msg, sizeof(msg), "Blackjack!  +%d", (s.bet * 3) / 2);
        c = kOk;
        break;
      case BJR_WIN:
        snprintf(msg, sizeof(msg), "You win  +%d", s.bet);
        c = kOk;
        break;
      case BJR_PUSH:
        snprintf(msg, sizeof(msg), "Push");
        break;
      case BJR_BUST:
        snprintf(msg, sizeof(msg), "Bust  -%d", s.bet);
        c = kDanger;
        break;
      case BJR_LOSE:
        snprintf(msg, sizeof(msg), "House wins  -%d", s.bet);
        c = kDanger;
        break;
      default:
        snprintf(msg, sizeof(msg), "Next hand soon");
        break;
    }
    status(msg, c);
  }
}

void uiDrawBlackjack() {
  beginFb(false);  // full screen: the table needs the top bar
  g_lcd->fillScreen(kBoard);
  clearHits();
  if (bjRole() == BJ_NONE) {
    drawBjLobby();
  } else {
    drawBjTable();
  }
  endFb();
}

// "8:30a" / "1:00p": compact enough for the schedule's time column.
static void fmtClock(uint16_t minutes, char *out, size_t n) {
  int h = minutes / 60;
  int m = minutes % 60;
  char ap = h >= 12 ? 'p' : 'a';
  h %= 12;
  if (h == 0) {
    h = 12;
  }
  snprintf(out, n, "%d:%02d%c", h, m, ap);
}

void uiDrawSchedule(int day, int *scroll) {
  const std::vector<ScheduleDay> &days = scheduleDays();
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  if (days.empty()) {
    screenTitle("Schedule");
    useBody();
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString("No schedule yet", kScreenW / 2, kScreenH / 2);
    endFb();
    return;
  }
  if (day < 0) {
    day = 0;
  }
  if (day >= (int)days.size()) {
    day = (int)days.size() - 1;
  }
  const ScheduleDay &d = days[day];

  // Title bar: "<"  Wed 11/4  ">"
  useTitle();
  const int lw = g_lcd->textWidth(d.label);
  roundRect(6, 4, 28, 24, kPanel, day > 0 ? kRail : kBoard);
  roundRect(44 + lw, 4, 28, 24, kPanel, day + 1 < (int)days.size() ? kRail : kBoard);
  g_lcd->setTextDatum(lgfx::middle_left);
  g_lcd->setTextColor(settingsColorRgb());
  g_lcd->drawString(d.label, 39, 16);
  useBody();
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->setTextColor(day > 0 ? kText : kRail);
  g_lcd->drawString("<", 20, 16);
  g_lcd->setTextColor(day + 1 < (int)days.size() ? kText : kRail);
  g_lcd->drawString(">", 58 + lw, 16);
  addHit(HIT_SCHED_PREV, 0, 0, 38, 30);
  addHit(HIT_SCHED_NEXT, 40 + lw, 0, 40, 30);

  int now_day = -1;
  int now_min = 0;
  bool clock = scheduleNow(&now_day, &now_min);
  const bool today = clock && now_day == day;

  const int top = 32;
  const int row_h = 26;
  const int view_h = kScreenH - top;
  int content = (int)d.items.size() * row_h;
  int max_s = content > view_h ? content - view_h : 0;
  int sc = scroll ? *scroll : 0;
  sc = sc < 0 ? 0 : (sc > max_s ? max_s : sc);
  if (scroll) {
    *scroll = sc;
  }
  g_lcd->setClipRect(0, top, kScreenW, view_h);
  for (size_t i = 0; i < d.items.size(); i++) {
    const ScheduleItem &it = d.items[i];
    int y = top - sc + (int)i * row_h;
    if (y + row_h < top || y > kScreenH) {
      continue;
    }
    bool now = today && now_min >= it.start && now_min < it.end;
    bool past = (clock && now_day > day) || (today && now_min >= it.end);
    uint16_t ink = settingsColorRgb();
    if (now) {
      g_lcd->fillRoundRect(4, y + 1, kScreenW - 8, row_h - 2, 6, scaleRgb(ink, 1, 5));
      g_lcd->drawRoundRect(4, y + 1, kScreenW - 8, row_h - 2, 6, ink);
    }
    char t[8];
    fmtClock(it.start, t, sizeof(t));
    g_lcd->setFont(&fonts::FreeSans9pt7b);
    g_lcd->setTextSize(1);
    g_lcd->setTextDatum(lgfx::middle_left);
    g_lcd->setTextColor(now ? ink : kMuted);
    g_lcd->drawString(t, 10, y + row_h / 2);
    g_lcd->setTextColor(now ? kText : (past ? kMuted : kText));
    g_lcd->drawString(clipText(it.title, kScreenW - 74), 66, y + row_h / 2);
  }
  g_lcd->clearClipRect();
  endFb();
}

void uiDrawSpotify(const SpotifyNowPlaying &np, const std::vector<uint8_t> *art_jpeg, bool loading,
                   const char *err, bool authed) {
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  screenTitle("Spotify");

  if (!authed) {
    useBody();
    g_lcd->setTextDatum(lgfx::top_left);
    g_lcd->setTextColor(kText);
    g_lcd->drawString("Link once on a computer:", 12, 34);
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString("python3 tools/spotify_link.py", 12, 56);
    g_lcd->drawString("Then USB serial:", 12, 84);
    g_lcd->drawString("SETSPOTIFY_CLIENT / _REFRESH", 12, 106);
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString("Content from Spotify", 12, 138);
    endFb();
    return;
  }

  const int art = 72;
  int ax = 10;
  int ay = 28;
  bool drew = false;
  if (art_jpeg && !art_jpeg->empty()) {
    drew = (bool)g_lcd->drawJpg(art_jpeg->data(), art_jpeg->size(), ax, ay, art, art);
  }
  if (!drew) {
    roundRect(ax, ay, art, art, kPanel, kRail);
    useBody();
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString("♪", ax + art / 2, ay + art / 2);
  }

  int tx = ax + art + 10;
  int tw = kScreenW - tx - 8;
  useBody();
  g_lcd->setTextDatum(lgfx::top_left);
  g_lcd->setTextColor(kText);
  if (loading && !np.ok) {
    g_lcd->drawString("Loading...", tx, ay + 8);
  } else if (err && err[0]) {
    g_lcd->setTextColor(kDanger);
    g_lcd->drawString(clipText(err, tw), tx, ay + 8);
  } else {
    g_lcd->drawString(clipText(np.title.length() ? np.title : "-", tw), tx, ay);
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString(clipText(np.artist.length() ? np.artist : " ", tw), tx, ay + 22);
    g_lcd->drawString(clipText(np.album.length() ? np.album : " ", tw), tx, ay + 42);
  }

  useBody();
  g_lcd->setTextDatum(lgfx::bottom_right);
  g_lcd->setTextColor(kMuted);
  g_lcd->drawString("Spotify", kScreenW - 8, kScreenH - 44);

  drawButton(10, kScreenH - 40, 70, 32, "<<", kPanel, kMuted, kText, HIT_SPOTIFY_PREV);
  drawButton(90, kScreenH - 40, 100, 32, np.playing ? "Pause" : "Play", kPanel, kOk, kText,
             HIT_SPOTIFY_PLAY);
  drawButton(200, kScreenH - 40, 70, 32, ">>", kPanel, kMuted, kText, HIT_SPOTIFY_NEXT);
  drawButton(278, 4, 38, 20, "x", kPanel, kDanger, kDanger, HIT_SPOTIFY_LOGOUT);
  endFb();
}

void uiDrawUpnp(const UpnpRenderer *r, int index, int total, const std::vector<uint8_t> *art_jpeg,
                bool discovering, bool loading, const char *err) {
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  screenTitle("LAN Now Playing");

  if (discovering) {
    useBody();
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(kText);
    g_lcd->drawString("Searching LAN...", kScreenW / 2, 80);
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString("UPnP + Chromecast", kScreenW / 2, 110);
    endFb();
    return;
  }

  if (!r || total <= 0) {
    useBody();
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(kDanger);
    g_lcd->drawString(err && err[0] ? err : "No renderers found", kScreenW / 2, 70);
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString("Swipe or Rescan", kScreenW / 2, 100);
    drawButton(100, kScreenH - 40, 120, 32, "Rescan", kPanel, kMuted, kText, HIT_UPNP_REFRESH);
    endFb();
    return;
  }

  const int art = 64;
  int ax = 8;
  int ay = 26;
  bool drew = false;
  if (art_jpeg && !art_jpeg->empty()) {
    drew = (bool)g_lcd->drawJpg(art_jpeg->data(), art_jpeg->size(), ax, ay, art, art);
    if (!drew) {
      drew = (bool)g_lcd->drawPng(art_jpeg->data(), art_jpeg->size(), ax, ay, art, art);
    }
  }
  if (!drew) {
    roundRect(ax, ay, art, art, kPanel, kRail);
    useBody();
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString("♪", ax + art / 2, ay + art / 2);
  }

  int tx = ax + art + 8;
  int tw = kScreenW - tx - 8;
  useBody();
  g_lcd->setTextDatum(lgfx::top_left);
  g_lcd->setTextColor(kMuted);
  char idxbuf[32];
  const char *kind = (r->source == LanSource::Cast) ? "Cast" : "DLNA";
  snprintf(idxbuf, sizeof(idxbuf), "%d/%d %s", index + 1, total, kind);
  String head = String(idxbuf) + "  " + r->name;
  g_lcd->drawString(clipText(head, tw), tx, ay);

  bool playing = r->transport == "PLAYING";
  g_lcd->setTextColor(playing ? kOk : kText);
  String state = r->transport.length() ? r->transport : "-";
  state.replace("_PLAYBACK", "");
  g_lcd->drawString(clipText(state, tw), tx, ay + 16);

  g_lcd->setTextColor(kText);
  if (loading && !r->ok) {
    g_lcd->drawString("Loading...", tx, ay + 34);
  } else if (err && err[0]) {
    g_lcd->setTextColor(kDanger);
    g_lcd->drawString(clipText(err, tw), tx, ay + 34);
  } else {
    g_lcd->drawString(clipText(r->title.length() ? r->title : "-", tw), tx, ay + 34);
    g_lcd->setTextColor(kMuted);
    String sub = r->artist;
    if (r->album.length()) {
      if (sub.length()) {
        sub += " · ";
      }
      sub += r->album;
    }
    g_lcd->drawString(clipText(sub.length() ? sub : " ", tw), tx, ay + 50);
  }

  useBody();
  g_lcd->setTextDatum(lgfx::bottom_left);
  g_lcd->setTextColor(kMuted);
  g_lcd->drawString("swipe devices", 8, kScreenH - 42);

  drawButton(8, kScreenH - 36, 56, 28, "<<", kPanel, kMuted, kText, HIT_UPNP_PREV);
  drawButton(72, kScreenH - 36, 88, 28, playing ? "Pause" : "Play", kPanel, kOk, kText,
             HIT_UPNP_PLAY);
  drawButton(168, kScreenH - 36, 56, 28, ">>", kPanel, kMuted, kText, HIT_UPNP_NEXT);
  drawButton(232, kScreenH - 36, 80, 28, "Scan", kPanel, kMuted, kText, HIT_UPNP_REFRESH);
  endFb();
}

void uiDrawMlbGames(const std::vector<MlbGame> &games, League league, int *scroll, bool loading,
                    const char *err) {
  const int top = 30;
  const int row_h = 36;
  const int view_h = kScreenH - top;
  clampScroll(scroll, (int)games.size() * row_h, view_h);
  int sc = scroll ? *scroll : 0;
  const char *title = leagueName(league);

  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  screenTitle(title);
  if (loading) {
    useBody();
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString("Loading games...", kScreenW / 2, kScreenH / 2);
    endFb();
    return;
  }
  if (err && err[0]) {
    useBody();
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(kDanger);
    g_lcd->drawString(err, kScreenW / 2, kScreenH / 2);
    endFb();
    return;
  }
  if (games.empty()) {
    useBody();
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(kMuted);
    char empty[32];
    snprintf(empty, sizeof(empty), "No %s games today", title);
    g_lcd->drawString(empty, kScreenW / 2, kScreenH / 2);
    endFb();
    return;
  }
  g_lcd->setClipRect(0, top, kScreenW, view_h);
  for (size_t i = 0; i < games.size(); i++) {
    int y = top - sc + (int)i * row_h;
    if (y + row_h < top || y > kScreenH) {
      continue;
    }
    const MlbGame &g = games[i];
    uint16_t edge = g.live ? kOk : kRail;
    roundRect(8, y + 1, kScreenW - 16, row_h - 2, kPanel, edge);
    useBody();
    g_lcd->setTextDatum(lgfx::middle_left);
    g_lcd->setTextColor(kText);
    char line[48];
    snprintf(line, sizeof(line), "%s %d  @  %s %d",
             g.away_name.length() ? g.away_name.c_str() : g.away.c_str(), g.away_score,
             g.home_name.length() ? g.home_name.c_str() : g.home.c_str(), g.home_score);
    g_lcd->drawString(clipText(line, kScreenW - 110), 18, y + 12);
    g_lcd->setTextColor(g.live ? kOk : kMuted);
    g_lcd->drawString(clipText(g.detail.length() ? g.detail : g.state, kScreenW - 40), 18, y + 26);
    addHit(HIT_MLB0 + (int)i, 8, y, kScreenW - 16, row_h);
  }
  g_lcd->clearClipRect();
  endFb();
}

void uiDrawMlbBoard(const MlbBoard &board) {
  beginFb();
  g_lcd->fillScreen(kBoard);
  clearHits();
  const uint16_t amber = 0xFE60;
  useBody();
  g_lcd->setTextDatum(lgfx::top_left);
  g_lcd->setTextColor(board.live ? kOk : kMuted);
  String header = board.status.length() ? board.status : leagueName(board.league);
  if (board.live && board.league == League::Mlb) {
    char cnt[24];
    snprintf(cnt, sizeof(cnt), "  B%d S%d O%d", board.balls, board.strikes, board.outs);
    header += cnt;
  }
  g_lcd->drawString(clipText(header, kScreenW - 16), 8, 4);

  auto mark = [&](int id, const std::vector<uint8_t> &png, const String &abbr, int x, int y) {
    int s = 36;
    if (!png.empty() && g_lcd->drawPng(png.data(), png.size(), x, y, s, s)) {
      return;
    }
    uint16_t col = sportsMarkColor(board, id == board.home_id);
    g_lcd->fillRoundRect(x, y, s, s, 8, col);
    useBody();
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(kText);
    g_lcd->drawString(clipText(abbr.length() ? abbr : "?", s - 4), x + s / 2, y + s / 2);
  };

  auto drawTeam = [&](int id, const std::vector<uint8_t> &png, const String &abbr,
                      const String &name, int runs, int y) {
    mark(id, png, abbr, 8, y);
    useTitle();
    g_lcd->setTextDatum(lgfx::middle_left);
    g_lcd->setTextColor(kText);
    String label = name.length() ? name : abbr;
    g_lcd->drawString(clipText(label, 170), 52, y + 18);
    g_lcd->setFont(&Scoreboard36pt7b);
    g_lcd->setTextDatum(lgfx::middle_right);
    g_lcd->setTextColor(amber);
    g_lcd->drawString(String(runs), kScreenW - 8, y + 18);
  };

  if (!board.ok) {
    useBody();
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString("Loading score...", kScreenW / 2, 100);
  } else {
    drawTeam(board.away_id, board.away_logo, board.away, board.away_name, board.away_runs, 24);
    drawTeam(board.home_id, board.home_logo, board.home, board.home_name, board.home_runs, 66);
    useBody();
    int cols = board.inning_count > 0 ? board.inning_count : leagueRegulationPeriods(board.league);
    if (cols > 9) {
      cols = 9;
    }
    int box = (kScreenW - 16) / (cols + 1);
    int y = 114;
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(kMuted);
    for (int i = 0; i < cols; i++) {
      char lab[6];
      leaguePeriodLabel(board.league, i, cols, lab, sizeof(lab));
      g_lcd->drawString(lab, 8 + box * i + box / 2, y);
    }
    g_lcd->drawString(board.league == League::Mlb ? "R" : "T", 8 + box * cols + box / 2, y);
    auto row = [&](int which, int yy, int total) {
      g_lcd->setTextColor(kText);
      for (int i = 0; i < cols; i++) {
        int v = board.innings[i][which];
        g_lcd->drawString(v < 0 ? "-" : String(v), 8 + box * i + box / 2, yy);
      }
      g_lcd->setTextColor(amber);
      g_lcd->drawString(String(total), 8 + box * cols + box / 2, yy);
    };
    row(0, y + 16, board.away_runs);
    row(1, y + 32, board.home_runs);
  }
  endFb();
}

static constexpr int kEggCw = 6;
static constexpr int kEggCh = 8;
static constexpr int kEggCols = kScreenW / kEggCw;

struct EggDrop {
  int16_t y;
  uint8_t speed;
  uint8_t len;
  uint8_t seed;
};

static EggDrop g_rain[kEggCols];

static char eggGlyph(uint32_t n) {
  static const char kGlyphs[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ#$%&*+<>";
  return kGlyphs[n % (sizeof(kGlyphs) - 1)];
}

static uint16_t eggGreen(int k, int len) {
  if (k == 0) {
    return 0xDFFF;
  }
  if (k == 1) {
    return 0x07E0;
  }
  int t = (len - k) * 255 / (len ? len : 1);
  int g = 6 + (t * 50) / 255;
  return (uint16_t)(g << 5);
}

static int g_silly_total = kEggToyCount;

void uiSetSillyTotal(int total) { g_silly_total = max(1, total); }

static void drawEggDots(int toy) {
  const int gap = g_silly_total > 12 ? 10 : 14;
  int x0 = kScreenW / 2 - (g_silly_total - 1) * gap / 2;
  int y = kScreenH - 7;
  for (int i = 0; i < g_silly_total; i++) {
    g_lcd->fillCircle(x0 + i * gap, y, 3, i == toy ? (uint16_t)0x07E0 : (uint16_t)0x4208);
  }
}

static float eggBeat(uint32_t t_ms) {
  // Fake ~150 BPM kick + offbeat snare energy in 0..1 (sped up)
  float beat = (float)(t_ms % 400) / 400.0f;
  float kick = 1.0f - beat;
  kick = kick * kick;
  float snare = 0.0f;
  if (beat > 0.48f && beat < 0.72f) {
    float u = (beat - 0.48f) / 0.24f;
    snare = (1.0f - u) * (1.0f - u) * 0.85f;
  }
  return kick * 0.9f + snare * 0.7f;
}

static void drawMatrixEgg(uint32_t t_ms) {
  static uint32_t last_t = 0xFFFFFFFFu;
  if (t_ms < last_t) {
    TagRng rng(0x4D415458u ^ millis());
    for (int i = 0; i < kEggCols; i++) {
      g_rain[i].y = (int16_t)rng.between(-120, kScreenH);
      g_rain[i].speed = (uint8_t)rng.between(2, 6);
      g_rain[i].len = (uint8_t)rng.between(8, 18);
      g_rain[i].seed = (uint8_t)rng.next();
    }
  }
  last_t = t_ms;

  g_lcd->fillScreen(0x0000);
  g_lcd->setFont(&fonts::Font0);
  g_lcd->setTextSize(1);
  g_lcd->setTextDatum(lgfx::top_left);

  uint32_t tick = t_ms / 70;
  for (int c = 0; c < kEggCols; c++) {
    EggDrop &col = g_rain[c];
    col.y = (int16_t)(col.y + col.speed);
    int tail = col.y - col.len * kEggCh;
    if (tail > kScreenH) {
      TagRng rng(0xC0FFu + col.seed + tick + (uint32_t)c * 17u);
      col.y = (int16_t)(-rng.between(8, 80));
      col.speed = (uint8_t)rng.between(2, 6);
      col.len = (uint8_t)rng.between(8, 18);
      col.seed = (uint8_t)rng.next();
    }
    for (int k = 0; k < col.len; k++) {
      int py = col.y - k * kEggCh;
      if (py < -kEggCh || py >= kScreenH) {
        continue;
      }
      char s[2] = {eggGlyph(col.seed + (uint32_t)k * 13u + tick + (uint32_t)c * 7u), 0};
      if (k == 0) {
        s[0] = eggGlyph(col.seed + tick * 3u + (uint32_t)c);
      }
      g_lcd->setTextColor(eggGreen(k, col.len));
      g_lcd->drawString(s, c * kEggCw, py);
    }
  }
}

// Classic Winamp 2.x base-skin main window is 275×116 (Nullsoft / Webamp).
static constexpr int kWaW = 275;
static constexpr int kWaH = 116;

static uint16_t eggRgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Base-skin VISCOLOR.TXT rows 2..17 (top/peak → foot), transcribed from Webamp.
static uint16_t eggVisColor(int row_from_top) {
  static const uint8_t kVis[16][3] = {
      {239, 49, 16},  {206, 41, 16},  {214, 90, 0},   {214, 102, 0},
      {214, 115, 0},  {198, 123, 8},  {222, 165, 24}, {214, 181, 33},
      {189, 222, 41}, {148, 222, 33}, {41, 206, 16},  {50, 190, 16},
      {57, 181, 16},  {49, 156, 8},   {41, 148, 0},   {24, 132, 8},
  };
  if (row_from_top < 0) {
    row_from_top = 0;
  }
  if (row_from_top > 15) {
    row_from_top = 15;
  }
  const uint8_t *c = kVis[row_from_top];
  return eggRgb(c[0], c[1], c[2]);
}

static void eggBevelRect(int x, int y, int w, int h, uint16_t fill, uint16_t hi, uint16_t lo) {
  g_lcd->fillRect(x, y, w, h, fill);
  g_lcd->drawFastHLine(x, y, w, hi);
  g_lcd->drawFastVLine(x, y, h, hi);
  g_lcd->drawFastHLine(x, y + h - 1, w, lo);
  g_lcd->drawFastVLine(x + w - 1, y, h, lo);
}

static void eggLcdDigit(int x, int y, char ch, uint16_t on, uint16_t off) {
  // Compact 7-seg-ish LCD digit matching classic Numbers.bmp footprint (9×13).
  const int w = 9;
  const int h = 13;
  g_lcd->fillRect(x, y, w, h, 0x0000);
  if (ch == ' ') {
    return;
  }
  uint16_t m = segMask(ch);
  auto segH = [&](int sx, int sy, int sw, bool lit) {
    g_lcd->fillRect(sx, sy, sw, 2, lit ? on : off);
  };
  auto segV = [&](int sx, int sy, int sh, bool lit) {
    g_lcd->fillRect(sx, sy, 2, sh, lit ? on : off);
  };
  segH(x + 2, y + 1, 5, m & (SEG_A1 | SEG_A2));
  segV(x + 7, y + 2, 4, m & SEG_B);
  segV(x + 7, y + 7, 4, m & SEG_C);
  segH(x + 2, y + 11, 5, m & (SEG_D1 | SEG_D2));
  segV(x + 1, y + 7, 4, m & SEG_E);
  segV(x + 1, y + 2, 4, m & SEG_F);
  segH(x + 2, y + 6, 5, m & (SEG_G1 | SEG_G2));
}

static void drawWinampEgg(uint32_t t_ms) {
  // Classic base-skin chrome colours (approximate of Main.bmp / Titlebar.bmp).
  const uint16_t body = eggRgb(58, 58, 58);
  const uint16_t face = eggRgb(41, 41, 41);
  const uint16_t hi = eggRgb(99, 99, 99);
  const uint16_t lo = eggRgb(20, 20, 20);
  const uint16_t title_gold = eggRgb(214, 189, 74);
  const uint16_t lcd_green = eggRgb(0, 239, 0);
  const uint16_t lcd_dim = eggRgb(0, 90, 0);
  const uint16_t vis_bg = eggRgb(0, 0, 0);
  const uint16_t vis_dot = eggRgb(24, 33, 41);
  const uint16_t peak = eggRgb(150, 150, 150);
  const uint16_t btn = eggRgb(74, 74, 74);
  const uint16_t btn_hi = eggRgb(132, 132, 132);

  const int ox = (kScreenW - kWaW) / 2;
  const int oy = (kScreenH - 10 - kWaH) / 2;
  g_lcd->fillScreen(0x0000);
  eggBevelRect(ox, oy, kWaW, kWaH, body, hi, lo);

  // Titlebar
  eggBevelRect(ox, oy, kWaW, 14, eggRgb(48, 48, 48), hi, lo);
  g_lcd->setFont(&fonts::Font0);
  g_lcd->setTextSize(1);
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->setTextColor(title_gold);
  g_lcd->drawString("WINAMP", ox + kWaW / 2, oy + 7);
  // Window controls (menu / shade / close)
  eggBevelRect(ox + 6, oy + 3, 9, 9, btn, btn_hi, lo);
  eggBevelRect(ox + kWaW - 46, oy + 3, 9, 9, btn, btn_hi, lo);
  eggBevelRect(ox + kWaW - 34, oy + 3, 9, 9, btn, btn_hi, lo);
  eggBevelRect(ox + kWaW - 18, oy + 3, 9, 9, eggRgb(120, 40, 40), eggRgb(180, 70, 70), lo);

  // Inner face plate
  g_lcd->fillRect(ox + 6, oy + 16, kWaW - 12, kWaH - 22, face);

  // Clutterbar left of vis
  for (int i = 0; i < 5; i++) {
    eggBevelRect(ox + 10, oy + 22 + i * 8, 11, 7, btn, btn_hi, lo);
  }

  // Play/pause indicator + time LCD digits (classic Numbers.bmp slots)
  g_lcd->fillTriangle(ox + 26, oy + 28, ox + 26, oy + 36, ox + 33, oy + 32, lcd_green);
  uint32_t sec = (t_ms / 1000) % 215;
  char digs[5];
  snprintf(digs, sizeof(digs), "%1d%1d%02d", (int)(sec / 600), (int)((sec / 60) % 10), (int)(sec % 60));
  eggLcdDigit(ox + 48, oy + 26, digs[0], lcd_green, lcd_dim);
  eggLcdDigit(ox + 60, oy + 26, digs[1], lcd_green, lcd_dim);
  // colon
  g_lcd->fillRect(ox + 72, oy + 30, 2, 2, lcd_green);
  g_lcd->fillRect(ox + 72, oy + 35, 2, 2, lcd_green);
  eggLcdDigit(ox + 78, oy + 26, digs[2], lcd_green, lcd_dim);
  eggLcdDigit(ox + 90, oy + 26, digs[3], lcd_green, lcd_dim);

  // Song title marquee at classic (111,27), clipped to the LCD slot
  static const char kTrack[] = "1. BLENNY - KLIPPER AND L34N  ***  ";
  const int title_x = ox + 111;
  const int title_y = oy + 24;
  const int title_w = 154;
  const int title_h = 12;
  g_lcd->fillRect(title_x, title_y, title_w, title_h, 0x0000);
  g_lcd->setTextDatum(lgfx::middle_left);
  g_lcd->setTextColor(lcd_green);
  int tw = g_lcd->textWidth(kTrack);
  const int gap = 24;
  int loop = tw + gap;
  int off = loop > 0 ? (int)((t_ms / 40) % (uint32_t)loop) : 0;
  g_lcd->setClipRect(title_x, title_y, title_w, title_h);
  int cy = title_y + title_h / 2;
  g_lcd->drawString(kTrack, title_x - off, cy);
  g_lcd->drawString(kTrack, title_x - off + loop, cy);
  g_lcd->clearClipRect();

  // Bitrate / sample rate (110,43) / (156,43)
  g_lcd->setTextColor(lcd_green);
  g_lcd->drawString("192", ox + 110, oy + 46);
  g_lcd->drawString("44", ox + 156, oy + 46);
  g_lcd->setTextColor(lcd_dim);
  g_lcd->drawString("kbps", ox + 128, oy + 46);
  g_lcd->drawString("kHz", ox + 170, oy + 46);

  // Mono / Stereo indicators
  g_lcd->setTextColor(lcd_dim);
  g_lcd->drawString("mono", ox + 212, oy + 47);
  g_lcd->setTextColor(lcd_green);
  g_lcd->drawString("stereo", ox + 239, oy + 47);

  // Spectrum analyzer — classic 76×16 at (24,43), 19 bands × 16 levels
  const int vx = ox + 24;
  const int vy = oy + 43;
  const int vw = 76;
  const int vh = 16;
  g_lcd->fillRect(vx, vy, vw, vh, vis_bg);
  for (int y = 0; y < vh; y += 2) {
    for (int x = 0; x < vw; x += 2) {
      g_lcd->drawPixel(vx + x, vy + y, vis_dot);
    }
  }
  static float peaks[19];
  float beat = eggBeat(t_ms);
  for (int i = 0; i < 19; i++) {
    float phase = (float)i * 0.62f;
    float wobble = 0.45f + 0.55f * fabsf(sinf((float)t_ms * 0.014f + phase));
    float bass = beat * (1.0f - (float)i / 22.0f);
    float mid = 0.30f + 0.70f * fabsf(sinf((float)t_ms * 0.021f + phase * 1.4f));
    float level = clamp01(wobble * (0.18f + bass * 0.95f + mid * 0.55f));
    int h = (int)(level * 16.0f + 0.5f);
    if (h < 1) {
      h = 1;
    }
    if (h > 16) {
      h = 16;
    }
    if (level > peaks[i]) {
      peaks[i] = level;
    } else {
      peaks[i] -= 0.02f;
      if (peaks[i] < 0) {
        peaks[i] = 0;
      }
    }
    int px = vx + i * 4;
    for (int s = 0; s < h; s++) {
      // row_from_top: tip of bar uses redder colors
      int from_foot = s;
      int from_top = 15 - from_foot;
      g_lcd->fillRect(px, vy + vh - 1 - s, 3, 1, eggVisColor(from_top));
    }
    int peak_y = vy + vh - 1 - (int)(peaks[i] * 15.0f);
    g_lcd->fillRect(px, peak_y, 3, 1, peak);
  }

  // Volume (static ~75%) + balance sliders
  eggBevelRect(ox + 107, oy + 57, 68, 13, lo, hi, body);
  const int vol_w = 48;  // ~75% of the 64px fill track
  for (int x = 0; x < vol_w; x++) {
    uint16_t c = (x < 40) ? eggRgb(0, 200, 0) : eggRgb(220, 200, 0);
    g_lcd->drawFastVLine(ox + 109 + x, oy + 59, 9, c);
  }
  eggBevelRect(ox + 107 + vol_w - 4, oy + 57, 10, 13, btn, btn_hi, lo);
  eggBevelRect(ox + 177, oy + 57, 38, 13, lo, hi, body);
  g_lcd->fillRect(ox + 192, oy + 59, 8, 9, eggRgb(0, 180, 0));
  eggBevelRect(ox + 190, oy + 57, 10, 13, btn, btn_hi, lo);

  // EQ / PL
  eggBevelRect(ox + 219, oy + 58, 23, 12, btn, btn_hi, lo);
  eggBevelRect(ox + 244, oy + 58, 23, 12, btn, btn_hi, lo);
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->setTextColor(title_gold);
  g_lcd->drawString("EQ", ox + 230, oy + 64);
  g_lcd->drawString("PL", ox + 255, oy + 64);

  // Position bar
  eggBevelRect(ox + 16, oy + 72, 248, 10, lo, hi, body);
  int pos = (int)((t_ms / 40) % 220);
  eggBevelRect(ox + 16 + pos, oy + 72, 29, 10, btn, btn_hi, lo);

  // Transport: prev / play / pause / stop / next / eject
  const int bx[] = {16, 39, 62, 85, 108, 136};
  const int bw[] = {23, 23, 23, 23, 22, 22};
  for (int i = 0; i < 6; i++) {
    eggBevelRect(ox + bx[i], oy + (i == 5 ? 89 : 88), bw[i], i == 5 ? 16 : 18, btn, btn_hi, lo);
  }
  g_lcd->setTextColor(0xFFFF);
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->drawString("|<", ox + 27, oy + 97);
  g_lcd->drawString(">", ox + 50, oy + 97);
  g_lcd->drawString("||", ox + 73, oy + 97);
  g_lcd->drawString("#", ox + 96, oy + 97);
  g_lcd->drawString(">|", ox + 119, oy + 97);
  g_lcd->drawString("^", ox + 147, oy + 97);

  // Shuffle / Repeat
  eggBevelRect(ox + 164, oy + 89, 47, 15, btn, btn_hi, lo);
  eggBevelRect(ox + 212, oy + 89, 28, 15, btn, btn_hi, lo);
  g_lcd->setTextColor(title_gold);
  g_lcd->drawString("SHUF", ox + 187, oy + 96);
  g_lcd->drawString("REP", ox + 226, oy + 96);
}

// Classic ANSI C16.5 VU scale positions along the arc (0=−20 … 1=+3).
static float vuScaleU(float vu) {
  // Piecewise to match the familiar non-linear face.
  static const float marks_vu[] = {-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3};
  static const float marks_u[] = {0.00f, 0.30f, 0.42f, 0.52f, 0.62f, 0.70f,
                                  0.78f, 0.86f, 0.91f, 0.96f, 1.00f};
  if (vu <= marks_vu[0]) {
    return marks_u[0];
  }
  if (vu >= marks_vu[10]) {
    return marks_u[10];
  }
  for (int i = 0; i < 10; i++) {
    if (vu <= marks_vu[i + 1]) {
      float t = (vu - marks_vu[i]) / (marks_vu[i + 1] - marks_vu[i]);
      return marks_u[i] + t * (marks_u[i + 1] - marks_u[i]);
    }
  }
  return 1.0f;
}

static void vuPolar(int cx, int cy, float u, float r, int *x, int *y) {
  // Sweep ~−50° … +50° from vertical-up, left = −20, right = +3.
  float a = -2.443f + u * 1.745f;  // radians
  *x = cx + (int)(cosf(a) * r);
  *y = cy + (int)(sinf(a) * r);
}

static void drawVuMeter(int cx, int cy, float vu, const char *ch) {
  const uint16_t bezel = eggRgb(18, 18, 18);
  const uint16_t rim = eggRgb(70, 70, 70);
  const uint16_t face = eggRgb(12, 10, 6);
  const uint16_t glow = eggRgb(48, 40, 8);
  const uint16_t scale = eggRgb(230, 230, 220);
  const uint16_t red = eggRgb(220, 40, 30);
  const uint16_t needle = eggRgb(255, 170, 40);
  const uint16_t hub = eggRgb(200, 120, 30);

  const int mw = 148;
  const int mh = 118;
  const int x0 = cx - mw / 2;
  const int y0 = cy - mh / 2;
  g_lcd->fillRoundRect(x0 - 3, y0 - 3, mw + 6, mh + 6, 4, rim);
  g_lcd->fillRoundRect(x0, y0, mw, mh, 3, bezel);
  g_lcd->fillRoundRect(x0 + 6, y0 + 8, mw - 12, mh - 22, 2, face);
  // warm backlight wash
  g_lcd->fillRoundRect(x0 + 10, y0 + 14, mw - 20, 50, 2, glow);

  const int pivot_x = cx;
  const int pivot_y = y0 + mh - 28;
  const float r_outer = 54.0f;
  const float r_inner = 46.0f;
  const float r_num = 40.0f;

  // Arc + ticks
  static const float ticks[] = {-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3};
  for (int i = 0; i < 11; i++) {
    float u = vuScaleU(ticks[i]);
    bool overload = ticks[i] >= 0;
    int x1, y1, x2, y2;
    vuPolar(pivot_x, pivot_y, u, r_inner, &x1, &y1);
    vuPolar(pivot_x, pivot_y, u, r_outer + (overload || ticks[i] == -20 || ticks[i] == -10 ? 2 : 0),
            &x2, &y2);
    g_lcd->drawLine(x1, y1, x2, y2, overload ? red : scale);
    // Fine marks between major ticks
    if (i < 10) {
      for (int k = 1; k < 4; k++) {
        float vu_m = ticks[i] + (ticks[i + 1] - ticks[i]) * (float)k / 4.0f;
        float um = vuScaleU(vu_m);
        int xa, ya, xb, yb;
        vuPolar(pivot_x, pivot_y, um, r_inner + 2, &xa, &ya);
        vuPolar(pivot_x, pivot_y, um, r_outer - 2, &xb, &yb);
        g_lcd->drawLine(xa, ya, xb, yb, (vu_m >= 0) ? red : eggRgb(160, 160, 150));
      }
    }
  }
  // Thick red overload arc 0…+3
  for (int s = 0; s <= 20; s++) {
    float u = vuScaleU(0.0f + 3.0f * (float)s / 20.0f);
    int x, y;
    vuPolar(pivot_x, pivot_y, u, r_outer + 1, &x, &y);
    g_lcd->fillCircle(x, y, 1, red);
  }

  g_lcd->setFont(&fonts::Font0);
  g_lcd->setTextSize(1);
  g_lcd->setTextDatum(lgfx::middle_center);
  struct Label {
    float vu;
    const char *txt;
  };
  static const Label labs[] = {{-20, "-20"}, {-10, "-10"}, {-7, "-7"}, {-5, "-5"},
                               {-3, "-3"},   {-1, "-1"},  {0, "0"},   {1, "+1"},
                               {2, "+2"},    {3, "+3"}};
  for (const Label &L : labs) {
    int x, y;
    vuPolar(pivot_x, pivot_y, vuScaleU(L.vu), r_num, &x, &y);
    g_lcd->setTextColor(L.vu >= 0 ? red : scale);
    g_lcd->drawString(L.txt, x, y - 1);
  }

  // Percent scale under arc
  g_lcd->setTextColor(eggRgb(140, 140, 130));
  g_lcd->drawString("0", cx - 48, pivot_y - 8);
  g_lcd->drawString("50", cx - 10, pivot_y - 18);
  g_lcd->drawString("100", cx + 28, pivot_y - 14);
  g_lcd->setTextColor(scale);
  g_lcd->drawString("VU", cx, y0 + mh - 14);
  g_lcd->setTextColor(eggRgb(180, 180, 40));
  g_lcd->drawString(ch, cx - 58, y0 + 18);

  // Peak LED
  bool peak_on = vu >= 0.0f;
  g_lcd->fillCircle(cx + 58, y0 + 18, 4, peak_on ? eggRgb(255, 40, 30) : eggRgb(60, 20, 20));

  // Needle
  float u = vuScaleU(vu);
  int nx, ny;
  vuPolar(pivot_x, pivot_y, u, r_outer - 4, &nx, &ny);
  g_lcd->drawLine(pivot_x, pivot_y, nx, ny, needle);
  g_lcd->drawLine(pivot_x + 1, pivot_y, nx + 1, ny, eggRgb(180, 100, 20));
  g_lcd->fillCircle(pivot_x, pivot_y, 4, hub);
  g_lcd->fillCircle(pivot_x, pivot_y, 2, eggRgb(40, 30, 10));
}

static void drawVuEgg(uint32_t t_ms) {
  g_lcd->fillScreen(eggRgb(28, 28, 30));
  float beat = eggBeat(t_ms);
  float l_raw = beat * 0.95f + 0.30f * fabsf(sinf((float)t_ms * 0.021f));
  float r_raw = beat * 0.80f + 0.40f * fabsf(sinf((float)t_ms * 0.019f + 1.3f));
  float lag = 0.12f * fabsf(sinf((float)t_ms * 0.009f));
  l_raw = clamp01(l_raw + lag * 0.2f);
  r_raw = clamp01(r_raw - lag * 0.15f);

  // Soft ballistics toward target (VU-ish).
  static float l_vu = -20.0f;
  static float r_vu = -20.0f;
  float l_tgt = -20.0f + l_raw * 23.0f;  // −20 … +3
  float r_tgt = -20.0f + r_raw * 23.0f;
  auto slew = [](float cur, float tgt) {
    float a = (tgt > cur) ? 2.8f : 1.4f;  // faster attack
    float n = cur + (tgt - cur) * a * 0.08f;
    if (n < -20.0f) {
      n = -20.0f;
    }
    if (n > 3.0f) {
      n = 3.0f;
    }
    return n;
  };
  l_vu = slew(l_vu, l_tgt);
  r_vu = slew(r_vu, r_tgt);

  drawVuMeter(80, 78, l_vu, "L");
  drawVuMeter(240, 78, r_vu, "R");
}

static void drawFullscreenSpectrum(uint32_t t_ms) {
  // Classic analyzer: LED bricks, green→yellow→red, bass-heavy bounce. No chrome.
  const uint16_t vis_bg = eggRgb(0, 0, 0);
  const uint16_t vis_dot = eggRgb(18, 24, 30);
  const uint16_t peak_c = eggRgb(220, 220, 220);
  g_lcd->fillScreen(vis_bg);
  const int bands = 32;
  const int levels = 20;
  const int brick_h = 6;
  const int brick_gap = 1;
  const int gap = 2;
  const int bw = (kScreenW - (bands - 1) * gap) / bands;
  const int vh = levels * (brick_h + brick_gap) - brick_gap;
  const int vx = (kScreenW - (bands * bw + (bands - 1) * gap)) / 2;
  const int vy = (kScreenH - vh) / 2;
  for (int y = 0; y < vh; y += brick_h + brick_gap) {
    for (int x = 0; x < bands * (bw + gap) - gap; x += 2) {
      g_lcd->drawPixel(vx + x, vy + y + brick_h / 2, vis_dot);
    }
  }
  static float peaks[32];
  static float levels_s[32];
  static float spike_s[32];
  float beat = eggBeat(t_ms);
  float bass_pulse = beat * beat;
  for (int i = 0; i < bands; i++) {
    // Per-band motion only — no shared traveling phase (that reads as scroll).
    float seed = (float)((i * 73 + 17) % 97) / 97.0f;
    float t = (float)i / (float)(bands - 1);  // 0 bass .. 1 treble
    float bass_w = 1.0f - t / 0.50f;
    if (bass_w < 0) {
      bass_w = 0;
    }
    bass_w = bass_w * bass_w;
    float mid_w = 1.0f - fabsf(t - 0.42f) * 2.4f;
    if (mid_w < 0) {
      mid_w = 0;
    }
    float treb_w = (t - 0.62f) / 0.38f;
    if (treb_w < 0) {
      treb_w = 0;
    }
    treb_w = treb_w * treb_w * (3.0f - 2.0f * treb_w);
    float bass = bass_pulse * (0.45f + 1.05f * bass_w);
    float bob = 0.28f + 0.55f * fabsf(sinf((float)t_ms * (0.014f + seed * 0.012f) + seed * 6.28f));
    float variety = 0.35f + 0.65f * fabsf(sinf((float)t_ms * (0.0035f + seed * 0.0028f) + seed * 9.1f));
    float mid = mid_w * (0.10f + 0.22f * bob) * variety;
    float treb = treb_w * (0.10f + 0.32f * fabsf(sinf((float)t_ms * (0.022f + seed * 0.015f) + seed * 2.7f))) *
                 variety;
    float chatter = 0.04f * fabsf(sinf((float)t_ms * (0.040f + seed * 0.025f) + seed * 4.1f));
    // Sharp intermittent spikes so peak caps light up often.
    // Occasional spikes only — valleys should read clearly between them.
    float spike_gate = fabsf(sinf((float)t_ms * (0.009f + seed * 0.016f) + seed * 12.0f));
    if (spike_gate > 0.96f) {
      spike_s[i] = 0.55f + 0.40f * variety * (0.35f + 0.65f * (bass_w + treb_w));
    } else {
      spike_s[i] *= 0.78f;
    }
    // Breath curve spends more time near the floor (pronounced valleys).
    float breath = fabsf(sinf((float)t_ms * (0.006f + seed * 0.004f) + seed * 5.5f));
    breath = breath * breath * breath;  // bias toward quiet
    float body =
        (0.015f + bass * bob * 0.70f + mid * 0.55f + treb * 0.50f + chatter * 0.7f) *
        (0.45f + 0.55f * variety);
    float target = clamp01((body + spike_s[i]) * (0.10f + 0.90f * breath));
    if (target > 0.96f) {
      target = 0.96f;
    }
    float cur = levels_s[i];
    if (target > cur) {
      cur += (target - cur) * 0.55f;
    } else {
      cur += (target - cur) * 0.22f;  // fall into valleys faster
    }
    levels_s[i] = cur;
    float level = cur;
    int h = (int)(level * (float)levels + 0.5f);
    if (h < 1) {
      h = 1;
    }
    if (h > levels) {
      h = levels;
    }
    if (level > peaks[i]) {
      peaks[i] = level;
    } else {
      peaks[i] -= 0.006f;  // linger peak holds longer
      if (peaks[i] < 0) {
        peaks[i] = 0;
      }
    }
    int px = vx + i * (bw + gap);
    for (int s = 0; s < h; s++) {
      int from_top = 15 - (s * 16) / levels;
      if (from_top < 0) {
        from_top = 0;
      }
      int by = vy + vh - (s + 1) * (brick_h + brick_gap) + brick_gap;
      g_lcd->fillRect(px, by, bw, brick_h, eggVisColor(from_top));
    }
    int peak_lvl = (int)(peaks[i] * (float)(levels - 1) + 0.5f);
    if (peak_lvl < 0) {
      peak_lvl = 0;
    }
    if (peak_lvl > levels - 1) {
      peak_lvl = levels - 1;
    }
    int peak_y = vy + vh - (peak_lvl + 1) * (brick_h + brick_gap) + brick_gap;
    g_lcd->fillRect(px, peak_y, bw, 2, peak_c);
  }
}

static void dimFrame(uint8_t alpha) {
  // Pull the living bg toward black so overlays sit clearly in front.
#if defined(LGFX_USE_V1) || 1
  g_lcd->fillRectAlpha(0, 0, kScreenW, kScreenH, 0x0000, alpha);
#else
  (void)alpha;
  for (int y = 0; y < (int)kScreenH; y += 2) {
    g_lcd->drawFastHLine(0, y, kScreenW, 0x0000);
  }
#endif
}

static void drawNameOverlayImpl(const String &n, uint16_t color) {
  if (!n.length()) {
    return;
  }
  String label = n;
  label.toUpperCase();
  const int count = (int)label.length();
  // Size to feel like the clock digits — shrink only when the name is long.
  int digit_w = 34;
  int digit_h = 58;
  int gap = 6;
  if (count > 6) {
    digit_w = 28;
    digit_h = 50;
    gap = 5;
  }
  if (count > 8) {
    digit_w = 22;
    digit_h = 42;
    gap = 4;
  }
  if (count > 10) {
    digit_w = 18;
    digit_h = 36;
    gap = 3;
  }
  const int total = count * digit_w + (count - 1) * gap;
  const int x0 = (kScreenW - total) / 2;
  const int y0 = (kScreenH - digit_h) / 2;
  // Dim the living bg, then a solid plate so filled 7-seg reads cleanly.
  dimFrame(140);
  const int pad_x = 18;
  const int pad_y = 16;
  g_lcd->fillRect(x0 - pad_x, y0 - pad_y, total + pad_x * 2, digit_h + pad_y * 2, 0x0000);
  for (int i = 0; i < count; i++) {
    // kSegOff near-black so "off" segments disappear into the solid plate (filled look).
    drawSegChar(x0 + i * (digit_w + gap), y0, digit_w, digit_h, segMask(label[i]), color, 0x0000);
  }
}

static void drawClockOverlayImpl() {
  dimFrame(170);
  struct tm t {};
  bool ok = getLocalTime(&t, 0);
  char timebuf[16];
  char datebuf[32];
  if (ok) {
    strftime(timebuf, sizeof(timebuf), "%H:%M", &t);
    strftime(datebuf, sizeof(datebuf), "%a %b %d", &t);
  } else {
    snprintf(timebuf, sizeof(timebuf), "--:--");
    snprintf(datebuf, sizeof(datebuf), "SET WIFI TIME");
  }
  String label(timebuf);
  const int count = (int)label.length();
  const int digit_w = 36;
  const int digit_h = 64;
  const int gap = 8;
  const int total = count * digit_w + (count - 1) * gap;
  const int x0 = (kScreenW - total) / 2;
  const int y0 = (kScreenH - digit_h) / 2 - 10;
  const uint16_t on = eggRgb(0, 230, 90);
  g_lcd->fillRectAlpha(x0 - 12, y0 - 12, total + 24, digit_h + 48, 0x0000, 110);
  for (int i = 0; i < count; i++) {
    char ch = label[i];
    if (ch == ':') {
      int cx = x0 + i * (digit_w + gap) + digit_w / 2 - 3;
      g_lcd->fillRect(cx, y0 + 18, 6, 6, on);
      g_lcd->fillRect(cx, y0 + 40, 6, 6, on);
      continue;
    }
    drawSegChar(x0 + i * (digit_w + gap), y0, digit_w, digit_h, segMask(ch), on, kSegOff);
  }
  g_lcd->setFont(&fonts::Font2);
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->setTextColor(eggRgb(200, 200, 200));
  g_lcd->drawString(datebuf, kScreenW / 2, y0 + digit_h + 18);
}

// --- Toys: spectrum, matrix, winamp, VU, glow GIF clips ---

static uint16_t glowLevelColor(int lvl) {
  // Piecewise cyan ramp: deep floor, quiet mids, brighter upper shoulder (not white).
  if (lvl <= 0) {
    return 0x0000;
  }
  if (lvl > 13) {
    lvl = 13;
  }
  float n = (float)lvl / 13.0f;
  uint8_t r, g, b;
  if (n < 0.40f) {
    // Structure / dim scanlines — stay dark
    float t = n / 0.40f;
    t = t * t;
    r = (uint8_t)(t * 6.0f);
    g = (uint8_t)(10.0f + t * 55.0f);
    b = (uint8_t)(12.0f + t * 65.0f);
  } else if (n < 0.72f) {
    // Mids — controlled
    float t = (n - 0.40f) / 0.32f;
    r = (uint8_t)(6.0f + t * 8.0f);
    g = (uint8_t)(65.0f + t * 70.0f);
    b = (uint8_t)(77.0f + t * 75.0f);
  } else {
    // Upper cyan glow — hotter hue, soft-capped brightness
    float t = (n - 0.72f) / 0.28f;
    t = t * (2.0f - t);  // ease out before blowout
    r = (uint8_t)(14.0f + t * 10.0f);
    g = (uint8_t)(135.0f + t * 85.0f);   // ~220 max
    b = (uint8_t)(152.0f + t * 88.0f);   // ~240 max
  }
  return eggRgb(r, g, b);
}

static void drawGlowClip(uint32_t t_ms, int clip) {
  if (clip < 0 || clip >= kToyAnimCount) {
    clip = 0;
  }
  const ToyAnim &anim = *kToyAnims[clip];
  g_lcd->fillScreen(0x0000);
  if (!anim.data || anim.frames == 0) {
    return;
  }
  uint32_t delay = anim.delay_ms ? anim.delay_ms : 120;
  int fi = (int)((t_ms / delay) % anim.frames);
  const uint8_t *frame = toyAnimFrame(clip, fi);
  if (!frame) {
    return;
  }
  for (int y = 0; y < kToyAnimH && y < (int)kScreenH; y++) {
    int x = 0;
    while (x < kToyAnimW && x < (int)kScreenW) {
      int idx = y * kToyAnimW + x;
      uint8_t byte = frame[idx >> 1];
      int lvl = ((idx & 1) == 0) ? (byte >> 4) : (byte & 0x0F);
      if (lvl == 0) {
        x++;
        continue;
      }
      int x0 = x;
      int run = lvl;
      while (x < kToyAnimW && x < (int)kScreenW) {
        idx = y * kToyAnimW + x;
        byte = frame[idx >> 1];
        int l2 = ((idx & 1) == 0) ? (byte >> 4) : (byte & 0x0F);
        if (l2 != run) {
          break;
        }
        x++;
      }
      g_lcd->drawFastHLine(x0, y, x - x0, glowLevelColor(run));
    }
  }
}

void uiDrawEgg(uint32_t t_ms, int toy, bool show_name, uint16_t name_color, bool clock_overlay,
               bool dots) {
  if (toy < 0) {
    toy = 0;
  }
  if (toy >= kEggToyCount) {
    toy = kEggToyCount - 1;
  }
  beginFb(false);
  clearHits();
  // 0 spectrum, 1 matrix, 2 winamp, 3 vu, 4..6 glow clips
  if (toy == 0) {
    drawFullscreenSpectrum(t_ms);
  } else if (toy == 1) {
    drawMatrixEgg(t_ms);
  } else if (toy == 2) {
    drawWinampEgg(t_ms);
  } else if (toy == 3) {
    drawVuEgg(t_ms);
  } else {
    drawGlowClip(t_ms, toy - 4);
  }
  if (clock_overlay) {
    drawClockOverlayImpl();
  } else if (show_name) {
    drawNameOverlayImpl(settingsDisplayName(), name_color);
  } else if (dots) {
    drawEggDots(toy);
  }
  endFb();
}

void uiDrawNameOverlay(const char *name, uint16_t color) {
  drawNameOverlayImpl(tagName(name), color);
}

void uiDrawClock() {
  beginFb(false);
  clearHits();
  g_lcd->fillScreen(eggRgb(0, 0, 0));
  drawClockOverlayImpl();
  endFb();
}

int uiHit(int x, int y) {
  for (uint8_t i = 0; i < g_hit_n; i++) {
    if (g_hits[i].contains(x, y)) {
      return g_hits[i].id;
    }
  }
  return HIT_NONE;
}

// ---- Queen easter egg: embedded julia2.gif, floating hearts, caption ----
extern const uint8_t kQueenGif[] asm("_binary_src_queen_julia2_gif_start");
extern const uint8_t kQueenGifEnd[] asm("_binary_src_queen_julia2_gif_end");

static AnimatedGIF *g_qgif = nullptr;
static uint16_t *g_qrgb = nullptr;  // composited canvas, big-endian RGB565 (sprite byte order)
static bool g_qok = false;
static uint32_t g_qnext = 0;

// RAW line → persistent canvas. Transparent pixels keep the prior frame (no corner junk).
static void queenDraw(GIFDRAW *pDraw) {
  int w = pDraw->iWidth;
  if (pDraw->iX + w > (int)kScreenW) {
    w = (int)kScreenW - pDraw->iX;
  }
  int y = pDraw->iY + pDraw->y;
  if (!g_qrgb || w <= 0 || y < 0 || y >= (int)kScreenH || pDraw->iX >= (int)kScreenW) {
    return;
  }
  uint8_t *s = pDraw->pPixels;
  uint16_t *pal = pDraw->pPalette;
  if (pDraw->ucDisposalMethod == 2) {
    for (int x = 0; x < w; x++) {
      if (s[x] == pDraw->ucTransparent) {
        s[x] = pDraw->ucBackground;
      }
    }
    pDraw->ucHasTransparency = 0;
  }
  uint16_t *d = &g_qrgb[y * (int)kScreenW + pDraw->iX];
  if (pDraw->ucHasTransparency) {
    uint8_t tr = pDraw->ucTransparent;
    for (int x = 0; x < w; x++) {
      if (s[x] != tr) {
        d[x] = pal[s[x]];
      }
    }
  } else {
    for (int x = 0; x < w; x++) {
      d[x] = pal[s[x]];
    }
  }
}

// ---- Custom GIFs (uploaded by attendees, stored in LittleFS) ----
static AnimatedGIF *g_cgif = nullptr;
static uint16_t *g_crgb = nullptr;  // composited canvas, big-endian RGB565
static bool g_cok = false;
static uint32_t g_cnext = 0;
static String g_cpath;
static int g_cox = 0;
static int g_coy = 0;

static void *cgifOpen(const char *name, int32_t *size) {
  File *f = new File(LittleFS.open(name, "r"));
  if (!*f) {
    delete f;
    return nullptr;
  }
  *size = (int32_t)f->size();
  return f;
}

static void cgifClose(void *handle) {
  File *f = (File *)handle;
  if (f) {
    f->close();
    delete f;
  }
}

static int32_t cgifRead(GIFFILE *g, uint8_t *buf, int32_t len) {
  File *f = (File *)g->fHandle;
  int32_t n = min(len, g->iSize - g->iPos);
  if (n <= 0) {
    return 0;
  }
  n = (int32_t)f->read(buf, (size_t)n);
  g->iPos = (int32_t)f->position();
  return n;
}

static int32_t cgifSeek(GIFFILE *g, int32_t pos) {
  File *f = (File *)g->fHandle;
  f->seek((uint32_t)pos);
  g->iPos = (int32_t)f->position();
  return g->iPos;
}

// Same compositing as the Queen, offset so the GIF sits centred on the screen.
static void customDraw(GIFDRAW *pDraw) {
  int x0 = pDraw->iX + g_cox;
  int y = pDraw->iY + pDraw->y + g_coy;
  int w = pDraw->iWidth;
  if (!g_crgb || y < 0 || y >= (int)kScreenH || x0 >= (int)kScreenW) {
    return;
  }
  int skip = x0 < 0 ? -x0 : 0;
  if (x0 + w > (int)kScreenW) {
    w = (int)kScreenW - x0;
  }
  uint8_t *s = pDraw->pPixels;
  uint16_t *pal = pDraw->pPalette;
  if (pDraw->ucDisposalMethod == 2) {
    for (int x = 0; x < pDraw->iWidth; x++) {
      if (s[x] == pDraw->ucTransparent) {
        s[x] = pDraw->ucBackground;
      }
    }
    pDraw->ucHasTransparency = 0;
  }
  uint16_t *d = &g_crgb[y * (int)kScreenW + x0];
  for (int x = skip; x < w; x++) {
    if (!pDraw->ucHasTransparency || s[x] != pDraw->ucTransparent) {
      d[x] = pal[s[x]];
    }
  }
}

void uiCustomGifStop() {
  if (g_cgif && g_cok) {
    g_cgif->close();
  }
  g_cok = false;
  g_cnext = 0;
  g_cpath = "";
}

static bool customOpen(const char *path);

// Advance the open GIF if its frame time is up and paint the canvas.
static void playCustomFrame() {
  if (!g_cok) {
    g_lcd->fillScreen(kBoard);
    return;
  }
  uint32_t now = millis();
  if (!g_cnext || (int32_t)(now - g_cnext) >= 0) {
    int delay_ms = 0;
    int rc = g_cgif->playFrame(false, &delay_ms);
    if (rc == 0) {
      g_cgif->reset();
      rc = g_cgif->playFrame(false, &delay_ms);
    }
    if (rc < 0) {
      g_cok = false;
    }
    g_cnext = now + (uint32_t)max(20, delay_ms);
  }
  g_lcd->pushImage(0, 0, kScreenW, kScreenH, (const lgfx::swap565_t *)g_crgb);
}

static bool customOpen(const char *path) {
  uiCustomGifStop();
  if (!g_crgb) {
    g_crgb = (uint16_t *)ps_malloc(kScreenW * kScreenH * 2);
  }
  if (!g_cgif) {
    void *mem = ps_malloc(sizeof(AnimatedGIF));
    if (mem) {
      g_cgif = new (mem) AnimatedGIF();
    }
  }
  if (!g_cgif || !g_crgb) {
    return false;
  }
  memset(g_crgb, 0, kScreenW * kScreenH * 2);
  g_cgif->begin(GIF_PALETTE_RGB565_BE);
  if (!path) {
    return false;  // caller opens an in-memory GIF itself
  }
  g_cok = g_cgif->open(path, cgifOpen, cgifClose, cgifRead, cgifSeek, customDraw) != 0;
  if (g_cok) {
    g_cox = ((int)kScreenW - g_cgif->getCanvasWidth()) / 2;
    g_coy = ((int)kScreenH - g_cgif->getCanvasHeight()) / 2;
  } else {
    Serial.printf("gif: open %s failed err=%d\n", path, g_cgif->getLastError());
  }
  g_cpath = path;
  return g_cok;
}

void uiDrawCustomGif(const char *path, int page) {
  if (g_cpath != path) {
    customOpen(path);
  }
  beginFb(false);
  clearHits();
  if (g_cok) {
    uint32_t now = millis();
    if (!g_cnext || (int32_t)(now - g_cnext) >= 0) {
      int delay_ms = 0;
      int rc = g_cgif->playFrame(false, &delay_ms);
      if (rc == 0) {
        g_cgif->reset();
        rc = g_cgif->playFrame(false, &delay_ms);
      }
      if (rc < 0) {
        g_cok = false;
      }
      g_cnext = now + (uint32_t)max(20, delay_ms);
    }
    g_lcd->pushImage(0, 0, kScreenW, kScreenH, (const lgfx::swap565_t *)g_crgb);
  } else {
    g_lcd->fillScreen(kBoard);
    useBody();
    g_lcd->setTextDatum(lgfx::middle_center);
    g_lcd->setTextColor(kMuted);
    g_lcd->drawString("Couldn't play this GIF", kScreenW / 2, kScreenH / 2);
  }
  if (page >= 0) {
    drawEggDots(page);
  }
  endFb();
}

void uiDrawGifHelp(int page, int count, int max) {
  beginFb(false);
  clearHits();
  g_lcd->fillScreen(kBoard);
  const String url = String("https://") + kGifUploadUrl;
  // QR on the left (dark modules on white), steps on the right.
  g_lcd->fillRoundRect(8, 10, 140, 140, 8, 0xFFFF);
  g_lcd->qrcode(url.c_str(), 14, 16, 128, 3);
  useTitle();
  g_lcd->setTextDatum(lgfx::top_left);
  g_lcd->setTextColor(settingsColorRgb());
  g_lcd->drawString("Add a GIF", 160, 10);
  g_lcd->setFont(&fonts::FreeSans9pt7b);
  g_lcd->setTextSize(1);
  g_lcd->setTextColor(kText);
  g_lcd->drawString("1  Plug into a laptop", 160, 44);
  g_lcd->drawString("2  Scan, or open in", 160, 64);
  g_lcd->drawString("    Chrome or Edge:", 160, 82);
  g_lcd->setFont(&fonts::Font0);
  g_lcd->setTextColor(settingsColorRgb());
  g_lcd->drawString(kGifUploadUrl, 160, 104);
  g_lcd->setFont(&fonts::FreeSans9pt7b);
  g_lcd->setTextColor(kText);
  g_lcd->drawString("3  Drop in a GIF", 160, 118);
  g_lcd->setTextColor(kMuted);
  char slots[24];
  snprintf(slots, sizeof(slots), "%d of %d used", count, max);
  g_lcd->drawString(slots, 160, 140);
  drawEggDots(page);
  endFb();
}

void uiQueenStop() {
  if (g_qgif && g_qok) {
    g_qgif->close();
  }
  g_qok = false;
  g_qnext = 0;
}

bool uiQueenStart() {
  uiQueenStop();
  if (!g_qrgb) {
    g_qrgb = (uint16_t *)ps_malloc(kScreenW * kScreenH * 2);
  }
  if (!g_qgif) {
    void *mem = ps_malloc(sizeof(AnimatedGIF));
    if (mem) {
      g_qgif = new (mem) AnimatedGIF();
    }
  }
  if (!g_qgif || !g_qrgb) {
    return false;
  }
  memset(g_qrgb, 0, kScreenW * kScreenH * 2);
  g_qgif->begin(GIF_PALETTE_RGB565_BE);
  int size = (int)(kQueenGifEnd - kQueenGif);
  g_qok = g_qgif->open((uint8_t *)kQueenGif, size, queenDraw) != 0;
  if (!g_qok) {
    Serial.printf("queen: gif open failed err=%d\n", g_qgif->getLastError());
  }
  return g_qok;
}

void uiDrawQueen(uint32_t t_ms) {
  beginFb(false);
  clearHits();
  if (g_qok) {
    uint32_t now = millis();
    if (!g_qnext || (int32_t)(now - g_qnext) >= 0) {
      int delay_ms = 0;
      int rc = g_qgif->playFrame(false, &delay_ms);
      if (rc == 0) {
        g_qgif->reset();
        rc = g_qgif->playFrame(false, &delay_ms);
      }
      if (rc < 0) {
        Serial.printf("queen: playFrame err=%d\n", g_qgif->getLastError());
        g_qok = false;
      }
      g_qnext = now + (uint32_t)max(20, delay_ms);
    }
    g_lcd->pushImage(0, 0, kScreenW, kScreenH, (const lgfx::swap565_t *)g_qrgb);
  } else {
    g_lcd->fillScreen(kBoard);
  }

  static const int kXs[] = {18, 62, 108, 156, 204, 252, 294};
  for (int i = 0; i < 7; i++) {
    int bob = (int)((t_ms / 90 + (uint32_t)i * 13) % 9) - 4;
    int span = (int)kScreenH + 40;
    int travel = (int)((t_ms / (16 + (i % 4) * 4) + (uint32_t)i * 31) % (uint32_t)span);
    int y = (int)kScreenH + 10 - travel;
    int r = 5 + (i % 3) * 2;
    drawHeart(kXs[i] + bob, y, r, (i & 1) ? (uint16_t)0xF81F : (uint16_t)0xF800);
  }

  const uint32_t period = 700;
  if ((t_ms % period) < 520) {
    const char *line = ((t_ms / period) & 1) ? "Our Lady of I-80" : "Our Queen of I-15";
    useTitle();
    g_lcd->setTextDatum(lgfx::middle_center);
    int tw = g_lcd->textWidth(line);
    if (tw > (int)kScreenW - 20) {
      useBody();
      tw = g_lcd->textWidth(line);
    }
    int y = (int)kScreenH - 18;
    int x = max(2, ((int)kScreenW - tw) / 2 - 8);
    g_lcd->fillRoundRect(x, y - 14, tw + 16, 28, 6, kBoard);
    g_lcd->setTextColor(kText);
    g_lcd->drawString(line, kScreenW / 2, y);
  }
  endFb();
}

void uiDrawOta(const char *title, const char *detail, int pct) {
  beginFb();
  clearHits();
  g_lcd->fillScreen(kBoard);
  screenTitle(title);
  useBody();
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->setTextColor(kText);
  g_lcd->drawString(detail, kScreenW / 2, 62);
  if (pct >= 0) {
    const int x = 20;
    const int y = 86;
    const int w = kScreenW - 40;
    const int h = 18;
    g_lcd->drawRoundRect(x, y, w, h, 5, kMuted);
    int fill = (w - 4) * min(100, pct) / 100;
    if (fill > 0) {
      g_lcd->fillRoundRect(x + 2, y + 2, fill, h - 4, 3, settingsColorRgb());
    }
  }
  g_lcd->setTextColor(kMuted);
  g_lcd->drawString("Keep the badge powered", kScreenW / 2, 132);
  endFb();
}

// ---- Shame goose: waddles back and forth holding a sign (goose-flagged badges) ----
void uiDrawGoose(uint32_t t_ms, const char *sign) {
  beginFb();
  clearHits();
  const uint16_t sky = 0x867D;
  const uint16_t grass = 0x3D27;
  const uint16_t grass_dk = 0x2C64;
  const uint16_t white = 0xFFFF;
  const uint16_t shade = 0xBDF7;
  const uint16_t wing = 0xE73C;
  const uint16_t orange = 0xFD20;
  const uint16_t ground_y = 132;
  g_lcd->fillScreen(sky);
  g_lcd->fillRect(0, ground_y, kScreenW, kScreenH - ground_y, grass);
  for (int x = 6; x < (int)kScreenW; x += 23) {
    g_lcd->drawLine(x, ground_y + 10, x - 3, ground_y + 3, grass_dk);
    g_lcd->drawLine(x + 2, ground_y + 10, x + 4, ground_y + 2, grass_dk);
  }

  // Back-and-forth walk; face the way we're heading.
  const float period = 5200.0f;
  float ph = 6.2831853f * (float)(t_ms % (uint32_t)period) / period;
  int cx = 160 + (int)(sinf(ph) * 78.0f);
  int f = cosf(ph) >= 0.0f ? 1 : -1;
  int step = (int)(t_ms / 170);
  int bob = (step & 1) ? 3 : 0;
  int swing = (step & 1) ? 5 : -5;
  int by = 108 - bob;  // body centre

  // Legs + feet.
  for (int leg = -1; leg <= 1; leg += 2) {
    int hip_x = cx + leg * 8;
    int foot_x = hip_x + leg * swing;
    g_lcd->drawWideLine(hip_x, by + 14, foot_x, ground_y + 4, 1.6f, orange);
    g_lcd->fillTriangle(foot_x - 5, ground_y + 6, foot_x + 7 * f, ground_y + 6, foot_x, ground_y + 1,
                        orange);
  }
  // Tail, body, wing.
  g_lcd->fillTriangle(cx - 26 * f, by - 4, cx - 42 * f, by - 14, cx - 30 * f, by + 8, white);
  g_lcd->fillEllipse(cx, by, 31, 18, white);
  g_lcd->drawEllipse(cx, by, 31, 18, shade);
  g_lcd->fillEllipse(cx - 5 * f, by - 2, 18, 10, wing);
  // Neck + head.
  int hx = cx + 27 * f;
  int hy = 62 - bob;
  g_lcd->drawWideLine(cx + 18 * f, by - 6, hx - 2 * f, hy + 6, 6.0f, white);
  g_lcd->fillCircle(hx, hy, 10, white);
  g_lcd->fillCircle(hx + 3 * f, hy - 2, 2, kBoard);
  g_lcd->fillCircle(hx - 2 * f, hy + 4, 2, 0xFBB6);  // little blush
  g_lcd->fillTriangle(hx + 8 * f, hy - 3, hx + 8 * f, hy + 4, hx + 20 * f, hy + 1, orange);

  // Sign on a stick, held up by the wing; kept on screen at the ends of the walk.
  // Text splits into two balanced lines and the board grows to fit.
  useBody();
  String text = (sign && sign[0]) ? String(sign) : String("I didn't follow instructions");
  String l1 = text;
  String l2 = "";
  if (g_lcd->textWidth(text) > 140) {
    int best = INT32_MAX;
    for (int i = 0; i < (int)text.length(); i++) {
      if (text[i] != ' ') {
        continue;
      }
      int w = max(g_lcd->textWidth(text.substring(0, i)), g_lcd->textWidth(text.substring(i + 1)));
      if (w < best) {
        best = w;
        l1 = text.substring(0, i);
        l2 = text.substring(i + 1);
      }
    }
  }
  const int max_w = kScreenW - 20;
  l1 = clipText(l1, max_w);
  l2 = clipText(l2, max_w);
  const int sw = max(156, (int)max(g_lcd->textWidth(l1), g_lcd->textWidth(l2)) + 22);
  const int sh = l2.length() ? 42 : 26;
  int stick_x = cx - 8 * f;
  int sx = stick_x - sw / 2;
  sx = max(4, min((int)kScreenW - sw - 4, sx));
  int sy = 8 - bob / 2;
  g_lcd->drawWideLine(stick_x, by - 4, stick_x, sy + sh, 2.0f, 0x8A22);
  g_lcd->fillRoundRect(sx, sy, sw, sh, 5, 0xFFDB);
  g_lcd->drawRoundRect(sx, sy, sw, sh, 5, 0x8A22);
  g_lcd->drawRoundRect(sx + 1, sy + 1, sw - 2, sh - 2, 4, 0x8A22);
  useBody();
  g_lcd->setTextDatum(lgfx::middle_center);
  g_lcd->setTextColor(kBoard);
  if (l2.length()) {
    g_lcd->drawString(l1, sx + sw / 2, sy + 13);
    g_lcd->drawString(l2, sx + sw / 2, sy + 29);
  } else {
    g_lcd->drawString(l1, sx + sw / 2, sy + 13);
  }
  endFb();
}
