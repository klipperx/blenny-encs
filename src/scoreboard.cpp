#include "scoreboard.h"

#include "config.h"

#ifdef BLENNY_SCOREBOARD

#include <Arduino.h>

static constexpr uint8_t kHomeClk = 1;
static constexpr uint8_t kHomeDio = 2;
static constexpr uint8_t kVisClk = 3;
static constexpr uint8_t kVisDio = 10;

static const uint8_t kSeg[10] = {
    0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f,
};

static void tmDelay() { delayMicroseconds(50); }

static void tmStart(uint8_t clk, uint8_t dio) {
  pinMode(dio, OUTPUT);
  pinMode(clk, OUTPUT);
  digitalWrite(dio, HIGH);
  digitalWrite(clk, HIGH);
  tmDelay();
  digitalWrite(dio, LOW);
  tmDelay();
  digitalWrite(clk, LOW);
}

static void tmStop(uint8_t clk, uint8_t dio) {
  pinMode(dio, OUTPUT);
  digitalWrite(clk, LOW);
  digitalWrite(dio, LOW);
  tmDelay();
  digitalWrite(clk, HIGH);
  tmDelay();
  digitalWrite(dio, HIGH);
}

static void tmWrite(uint8_t clk, uint8_t dio, uint8_t data) {
  for (uint8_t i = 0; i < 8; i++) {
    digitalWrite(clk, LOW);
    digitalWrite(dio, data & 1 ? HIGH : LOW);
    tmDelay();
    digitalWrite(clk, HIGH);
    tmDelay();
    data >>= 1;
  }
  digitalWrite(clk, LOW);
  pinMode(dio, INPUT_PULLUP);
  tmDelay();
  digitalWrite(clk, HIGH);
  tmDelay();
  pinMode(dio, OUTPUT);
}

static void tmShow(uint8_t clk, uint8_t dio, uint8_t value) {
  uint8_t tens = (value / 10) % 10;
  uint8_t ones = value % 10;
  tmStart(clk, dio);
  tmWrite(clk, dio, 0x40);
  tmStop(clk, dio);
  tmStart(clk, dio);
  tmWrite(clk, dio, 0xC0);
  tmWrite(clk, dio, kSeg[tens]);
  tmWrite(clk, dio, kSeg[ones]);
  tmStop(clk, dio);
  tmStart(clk, dio);
  tmWrite(clk, dio, 0x88 | 4);
  tmStop(clk, dio);
}

void scoreboardBegin() {
  pinMode(kHomeClk, OUTPUT);
  pinMode(kHomeDio, OUTPUT);
  pinMode(kVisClk, OUTPUT);
  pinMode(kVisDio, OUTPUT);
  scoreboardSet(0, 0);
}

void scoreboardSet(uint8_t home, uint8_t visitor) {
  tmShow(kHomeClk, kHomeDio, home);
  tmShow(kVisClk, kVisDio, visitor);
}

void scoreboardOff() {
  tmStart(kHomeClk, kHomeDio);
  tmWrite(kHomeClk, kHomeDio, 0x80);
  tmStop(kHomeClk, kHomeDio);
  tmStart(kVisClk, kVisDio);
  tmWrite(kVisClk, kVisDio, 0x80);
  tmStop(kVisClk, kVisDio);
}

#else

void scoreboardBegin() {}
void scoreboardSet(uint8_t, uint8_t) {}
void scoreboardOff() {}

#endif
