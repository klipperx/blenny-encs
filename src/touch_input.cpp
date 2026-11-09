#include "touch_input.h"

#include "config.h"

#include <Wire.h>
#include <TouchLib.h>

#if defined(TOUCH_MODULES_CST_MUTUAL)
TouchLib g_touch(Wire, PIN_IIC_SDA, PIN_IIC_SCL, CTS328_SLAVE_ADDRESS, PIN_TOUCH_RES);
#else
TouchLib g_touch(Wire, PIN_IIC_SDA, PIN_IIC_SCL, CTS820_SLAVE_ADDRESS, PIN_TOUCH_RES);
#endif

static bool g_ok = false;
static bool g_down = false;
static uint8_t g_hits = 0;
static uint8_t g_miss = 0;
static int g_lx = 0;
static int g_ly = 0;
static char g_i2c[64] = "";

// The controller pulls INT low when it has a touch report. Once we've seen it fire, skip the
// I2C read while no finger is down and nothing new arrived (saves the bus and the battery).
static volatile uint32_t g_irq_at = 0;
static volatile bool g_irq_seen = false;
static uint32_t g_last_read = 0;

static void IRAM_ATTR touchIrq() {
  g_irq_at = millis();
  g_irq_seen = true;
}

const char *touchI2c() { return g_i2c; }

bool touchBegin() {
  pinMode(PIN_TOUCH_RES, OUTPUT);
  digitalWrite(PIN_TOUCH_RES, LOW);
  delay(200);
  digitalWrite(PIN_TOUCH_RES, HIGH);
  delay(50);

  Wire.begin(PIN_IIC_SDA, PIN_IIC_SCL);
  g_i2c[0] = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      char bit[8];
      snprintf(bit, sizeof(bit), " %02x", addr);
      strlcat(g_i2c, bit, sizeof(g_i2c));
    }
  }

  g_ok = g_touch.init();
  if (g_ok) {
    g_touch.setRotation(1);
    pinMode(PIN_TOUCH_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_TOUCH_INT), touchIrq, FALLING);
  }
  Serial.printf("touch %s (%s) i2c%s\n", g_ok ? "ok" : "missing",
#if defined(TOUCH_MODULES_CST_MUTUAL)
                "cst328",
#else
                "cst816",
#endif
                g_i2c[0] ? g_i2c : " none");
  return g_ok;
}

bool touchPoll(int *x, int *y, bool *pressed) {
  *pressed = false;
  if (!g_ok) {
    g_down = false;
    g_hits = 0;
    g_miss = 0;
    return false;
  }

  uint32_t now = millis();
  bool recent = now - g_irq_at < 150;
  if (g_irq_seen && !g_down && !recent && now - g_last_read < 1000) {
    g_hits = 0;
    return false;  // no finger down and no new report: nothing to read
  }
  g_last_read = now;
  bool sample = g_touch.read();
  uint8_t n = sample ? (g_touch.getPointNum() & 0x0f) : 0;
  bool valid = false;
  int sx = 0;
  int sy = 0;
  if (sample && n == 1) {
    TP_Point p = g_touch.getPoint(0);
    // 0=down 1=up 2=contact. A lift event is not a press.
    if ((p.state & 0x03) != 1) {
      sx = p.x;
      sy = (kScreenH - 1) - p.y;
      if (sx < 0) sx = 0;
      if (sy < 0) sy = 0;
      if (sx > kScreenW - 1) sx = kScreenW - 1;
      if (sy > kScreenH - 1) sy = kScreenH - 1;
      if (!(sx == 0 && sy == 0) && !(sx == kScreenW - 1 && sy == kScreenH - 1)) {
        valid = true;
      }
    }
  }

  if (valid) {
    g_miss = 0;
    if (g_hits < 8) {
      g_hits++;
    }
    g_lx = sx;
    g_ly = sy;
    *x = sx;
    *y = sy;
    if (!g_down && g_hits >= 3) {
      g_down = true;
      *pressed = true;
    }
    return g_down;
  }

  g_hits = 0;
  if (g_down && g_miss < 4) {
    g_miss++;
    *x = g_lx;
    *y = g_ly;
    return true;
  }
  g_down = false;
  g_miss = 0;
  return false;
}

bool touchOk() { return g_ok; }
