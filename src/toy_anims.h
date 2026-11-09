#pragma once
#include <stdint.h>

static constexpr int kToyAnimW = 320;
static constexpr int kToyAnimH = 170;
// 4 bits per pixel, 2 pixels per byte (high nibble = even index).
static constexpr int kToyAnimFrameBytes = (kToyAnimW * kToyAnimH + 1) / 2;

struct ToyAnim {
  uint16_t frames;
  uint16_t delay_ms;
  const uint8_t *data;  // packed clip from tools/pack_toys.py ("TZ01", offsets, zlib frames)
};

extern const ToyAnim kToy_movie1;
extern const ToyAnim kToy_movie2;
extern const ToyAnim kToy_movie8;
extern const ToyAnim *const kToyAnims[];
static constexpr int kToyAnimCount = 3;

/** Inflate frame `fi` of clip `clip`; returns kToyAnimFrameBytes, or nullptr on failure. */
const uint8_t *toyAnimFrame(int clip, int fi);
