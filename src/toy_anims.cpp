#include "toy_anims.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>

// LovyanGFX's miniz inflater (already linked for PNG decoding).
extern "C" size_t lgfx_tinfl_decompress_mem_to_mem(void *out, size_t out_len, const void *src,
                                                   size_t src_len, int flags);
static constexpr int kTinflParseZlibHeader = 1;

// Embedded by PlatformIO board_build.embed_files (see platformio.ini).
extern const uint8_t _binary_src_toys_movie1_z_start[] asm("_binary_src_toys_movie1_z_start");
extern const uint8_t _binary_src_toys_movie2_z_start[] asm("_binary_src_toys_movie2_z_start");
extern const uint8_t _binary_src_toys_movie8_z_start[] asm("_binary_src_toys_movie8_z_start");

const ToyAnim kToy_movie1 = {60, 60, _binary_src_toys_movie1_z_start};
const ToyAnim kToy_movie2 = {60, 60, _binary_src_toys_movie2_z_start};
const ToyAnim kToy_movie8 = {60, 60, _binary_src_toys_movie8_z_start};

const ToyAnim *const kToyAnims[] = {&kToy_movie1, &kToy_movie2, &kToy_movie8};

static uint8_t *g_frame = nullptr;
static int g_frame_clip = -1;
static int g_frame_index = -1;

static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

const uint8_t *toyAnimFrame(int clip, int fi) {
  if (clip < 0 || clip >= kToyAnimCount) {
    return nullptr;
  }
  const ToyAnim &a = *kToyAnims[clip];
  if (!a.data || memcmp(a.data, "TZ01", 4) != 0 || fi < 0 || fi >= a.frames) {
    return nullptr;
  }
  if (clip == g_frame_clip && fi == g_frame_index && g_frame) {
    return g_frame;  // same frame redrawn: skip the inflate
  }
  if (!g_frame) {
    g_frame = (uint8_t *)heap_caps_malloc(kToyAnimFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_frame) {
      g_frame = (uint8_t *)malloc(kToyAnimFrameBytes);
    }
    if (!g_frame) {
      return nullptr;
    }
  }
  const uint8_t *offs = a.data + 8;
  uint32_t start = rd32(offs + 4 * fi);
  uint32_t end = rd32(offs + 4 * (fi + 1));
  size_t got = lgfx_tinfl_decompress_mem_to_mem(g_frame, kToyAnimFrameBytes, a.data + start,
                                                end - start, kTinflParseZlibHeader);
  if (got != (size_t)kToyAnimFrameBytes) {
    g_frame_clip = -1;
    return nullptr;
  }
  g_frame_clip = clip;
  g_frame_index = fi;
  return g_frame;
}
