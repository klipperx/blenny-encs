#include "klippy_bird.h"

#include "config.h"
#include "radio.h"
#include "roster.h"
#include "settings.h"

#include <Preferences.h>
#include <algorithm>
#include <esp_random.h>
#include <math.h>

static constexpr uint32_t kBirdMagic = 0x4B425244u;  // "KBRD"
static constexpr uint8_t kBirdVersion = 1;
static constexpr uint32_t kShareEveryMs = 3000;
static constexpr float kGravity = 800.0f;     // px/s^2
static constexpr float kFlapV = -215.0f;      // px/s (about a 27 px hop per tap)
static constexpr float kMaxFall = 340.0f;
static constexpr float kSpeed0 = 85.0f;       // px/s, ramps to kSpeedMax
static constexpr float kSpeedMax = 150.0f;
static constexpr int kSpacing = 150;          // px between pipes
static constexpr int kGap0 = 84;              // gap height, shrinks to kGapMin
static constexpr int kGapMin = 58;
static constexpr uint32_t kRetryLockMs = 700;

#pragma pack(push, 1)
struct BirdPkt {
  uint32_t magic;
  uint8_t version;
  uint8_t pad;
  char id[16];
  uint16_t best;
};
#pragma pack(pop)

struct Heard {
  String id;
  uint16_t best;
};

static BirdState g_s = {};
static uint16_t g_best = 0;
static std::vector<Heard> g_heard;
static bool g_heard_dirty = false;
static bool g_open = false;
static bool g_changed = true;
static uint32_t g_last_ms = 0;
static uint32_t g_shared_at = 0;
static uint32_t g_rng = 1;
static char g_word[8] = "";

// Scores arrive on the Wi-Fi task; park them and fold them in from the loop.
struct Pending {
  char id[16];
  uint16_t best;
};
static Pending g_pending[16];
static uint8_t g_pending_n = 0;
static portMUX_TYPE g_pending_mux = portMUX_INITIALIZER_UNLOCKED;

static uint32_t rnd() {
  g_rng = g_rng * 1664525u + 1013904223u;
  return g_rng >> 8;
}

static void saveHeard() {
  String out;
  for (const auto &h : g_heard) {
    if (out.length()) {
      out += ';';
    }
    out += h.id + ':' + String(h.best);
  }
  Preferences prefs;
  prefs.begin("bird", false);
  prefs.putString("lb", out);
  prefs.end();
  g_heard_dirty = false;
}

static void updateWord() {
  if (g_best) {
    snprintf(g_word, sizeof(g_word), "B%u", (unsigned)g_best);
  } else {
    g_word[0] = 0;
  }
}

void birdLoad() {
  Preferences prefs;
  prefs.begin("bird", true);
  g_best = prefs.getUShort("best", 0);
  String packed = prefs.getString("lb", "");
  prefs.end();
  g_heard.clear();
  int start = 0;
  while (start < (int)packed.length()) {
    int end = packed.indexOf(';', start);
    if (end < 0) {
      end = packed.length();
    }
    String item = packed.substring(start, end);
    int colon = item.indexOf(':');
    if (colon > 0 && rosterFind(item.substring(0, colon).c_str())) {
      g_heard.push_back({item.substring(0, colon), (uint16_t)item.substring(colon + 1).toInt()});
    }
    start = end + 1;
  }
  g_s.best = g_best;
  updateWord();
}

void birdNoteScore(const char *id, uint16_t best) {
  if (!id || !id[0] || !best) {
    return;
  }
  portENTER_CRITICAL(&g_pending_mux);
  int slot = -1;
  for (int i = 0; i < g_pending_n; i++) {
    if (strncmp(g_pending[i].id, id, sizeof(g_pending[i].id)) == 0) {
      slot = i;
    }
  }
  if (slot < 0 && g_pending_n < 16) {
    slot = g_pending_n++;
    strncpy(g_pending[slot].id, id, sizeof(g_pending[slot].id) - 1);
    g_pending[slot].id[sizeof(g_pending[slot].id) - 1] = 0;
  }
  if (slot >= 0) {
    g_pending[slot].best = best;
  }
  portEXIT_CRITICAL(&g_pending_mux);
}

void birdDrain() {
  Pending batch[16];
  uint8_t n;
  portENTER_CRITICAL(&g_pending_mux);
  n = g_pending_n;
  memcpy(batch, g_pending, sizeof(Pending) * n);
  g_pending_n = 0;
  portEXIT_CRITICAL(&g_pending_mux);
  const String &mine = rosterMyId();
  for (uint8_t i = 0; i < n; i++) {
    if (mine == batch[i].id || !rosterFind(batch[i].id)) {
      continue;
    }
    bool found = false;
    for (auto &h : g_heard) {
      if (h.id == batch[i].id) {
        found = true;
        if (batch[i].best > h.best) {  // bests only go up
          h.best = batch[i].best;
          g_heard_dirty = g_changed = true;
        }
      }
    }
    if (!found) {
      g_heard.push_back({String(batch[i].id), batch[i].best});
      g_heard_dirty = g_changed = true;
    }
  }
  if (g_heard_dirty && !g_open) {
    saveHeard();
  }
}

static void onRaw(const uint8_t * /*mac*/, const uint8_t *data, int len) {
  if (len < (int)sizeof(BirdPkt)) {
    return;
  }
  BirdPkt p;
  memcpy(&p, data, sizeof(p));
  if (p.magic != kBirdMagic || p.version != kBirdVersion) {
    return;
  }
  p.id[sizeof(p.id) - 1] = 0;
  birdNoteScore(p.id, p.best);
}

static void share() {
  if (!g_best || !rosterMe()) {
    return;
  }
  BirdPkt p = {};
  p.magic = kBirdMagic;
  p.version = kBirdVersion;
  strncpy(p.id, rosterMyId().c_str(), sizeof(p.id) - 1);
  p.best = g_best;
  static const uint8_t kAll[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  radioSendRaw(kAll, (const uint8_t *)&p, sizeof(p));
}

static String shortName(const String &id) {
  const Attendee *a = rosterFind(id.c_str());
  String n = a ? a->name : id;
  int sp = n.indexOf(' ');
  if (sp > 0) {
    n = n.substring(0, sp);
  }
  n.toUpperCase();
  return n;
}

// Up to three leaders worth chasing: a flag at the pipe where each crashed.
static void pickGhosts() {
  std::vector<Heard> top = g_heard;
  std::sort(top.begin(), top.end(), [](const Heard &a, const Heard &b) { return a.best > b.best; });
  g_s.ghosts.clear();
  for (const auto &h : top) {
    if (g_s.ghosts.size() >= 3) {
      break;
    }
    g_s.ghosts.push_back({h.best, shortName(h.id)});
  }
}

static int gapFor(uint16_t index) {
  int g = kGap0 - (int)index;  // one pixel narrower per pipe
  return g < kGapMin ? kGapMin : g;
}

static void addPipe(float x, uint16_t index, int prev_y) {
  int gap = gapFor(index);
  int lo = 14 + gap / 2;
  int hi = (int)kScreenH - kBirdGround - 14 - gap / 2;
  int y = lo + (int)(rnd() % (uint32_t)(hi - lo + 1));
  if (prev_y > 0) {  // keep consecutive gaps reachable
    y = std::max(prev_y - 60, std::min(prev_y + 60, y));
  }
  g_s.pipes.push_back({x, (int16_t)y, (int16_t)gap, index});
}

static void startRun() {
  g_rng = esp_random() | 1u;
  g_s.phase = BIRD_PLAY;
  g_s.y = kScreenH / 2.0f - 10;
  g_s.vy = kFlapV;
  g_s.score = 0;
  g_s.new_best = false;
  g_s.pipes.clear();
  for (int i = 0; i < 3; i++) {
    addPipe((float)(kScreenW + 40 + i * kSpacing), (uint16_t)i,
            g_s.pipes.empty() ? 0 : g_s.pipes.back().gap_y);
  }
  pickGhosts();
  g_last_ms = millis();
  g_changed = true;
}

static void endRun() {
  g_s.phase = BIRD_OVER;
  g_s.over_at = millis();
  if (g_s.score > g_best) {
    g_best = g_s.score;
    g_s.best = g_best;
    g_s.new_best = true;
    Preferences prefs;
    prefs.begin("bird", false);
    prefs.putUShort("best", g_best);
    prefs.end();
    updateWord();
    share();
  }
  g_changed = true;
}

void birdOpen() {
  radioOnRaw(kBirdMagic, onRaw);
  g_open = true;
  g_s.phase = BIRD_TITLE;
  g_s.y = kScreenH / 2.0f - 10;
  g_s.vy = 0;
  g_s.pipes.clear();
  g_s.best = g_best;
  g_shared_at = 0;
  g_last_ms = millis();
  g_changed = true;
}

void birdClose() {
  radioOnRaw(kBirdMagic, nullptr);
  g_open = false;
  if (g_heard_dirty) {
    saveHeard();
  }
}

void birdTap() {
  if (g_s.phase == BIRD_PLAY) {
    g_s.vy = kFlapV;
  } else if (g_s.phase == BIRD_TITLE ||
             (g_s.phase == BIRD_OVER && millis() - g_s.over_at >= kRetryLockMs)) {
    startRun();
  }
}

void birdLoop() {
  birdDrain();
  uint32_t now = millis();
  if (now - g_shared_at >= kShareEveryMs) {
    g_shared_at = now;
    share();
  }
  float dt = (float)(now - g_last_ms) / 1000.0f;
  g_last_ms = now;
  if (dt > 0.05f) {
    dt = 0.05f;  // a slow frame shouldn't teleport the bird through a pipe
  }
  if (g_s.phase == BIRD_TITLE) {
    g_s.y = kScreenH / 2.0f - 10 + sinf((float)now / 300.0f) * 6.0f;  // idle bob
    g_s.world += 40.0f * dt;
    return;
  }
  if (g_s.phase != BIRD_PLAY) {
    return;
  }
  float speed = std::min(kSpeedMax, kSpeed0 + g_s.score * 1.5f);
  g_s.world += speed * dt;
  g_s.vy = std::min(kMaxFall, g_s.vy + kGravity * dt);
  g_s.y += g_s.vy * dt;
  if (g_s.y < kBirdR) {
    g_s.y = kBirdR;
    g_s.vy = 0;
  }
  for (auto &p : g_s.pipes) {
    float before = p.x + kBirdPipeW;
    p.x -= speed * dt;
    if (before >= kBirdX - kBirdR && p.x + kBirdPipeW < kBirdX - kBirdR) {
      g_s.score++;  // passed it
    }
  }
  while (!g_s.pipes.empty() && g_s.pipes.front().x + kBirdPipeW < -4) {
    g_s.pipes.erase(g_s.pipes.begin());
  }
  while (g_s.pipes.size() < 4) {
    const BirdPipe &last = g_s.pipes.back();
    addPipe(last.x + kSpacing, (uint16_t)(last.index + 1), last.gap_y);
  }
  // Ground, or a pipe edge inside the bird's circle (clamped-rectangle test).
  bool dead = g_s.y + kBirdR >= kScreenH - kBirdGround;
  for (const auto &p : g_s.pipes) {
    if (dead) {
      break;
    }
    float top = p.gap_y - p.gap_h / 2.0f;
    float bot = p.gap_y + p.gap_h / 2.0f;
    float cx = std::max(p.x, std::min((float)kBirdX, p.x + kBirdPipeW));
    for (int part = 0; part < 2 && !dead; part++) {
      float y0 = part == 0 ? 0.0f : bot;
      float y1 = part == 0 ? top : (float)kScreenH;
      float cy = std::max(y0, std::min(g_s.y, y1));
      float dx = kBirdX - cx, dy = g_s.y - cy;
      dead = dx * dx + dy * dy < (float)(kBirdR * kBirdR);
    }
  }
  if (dead) {
    endRun();
  }
}

const BirdState &birdState() { return g_s; }
uint16_t birdBest() { return g_best; }
const char *birdBeaconWord() { return g_word; }

std::vector<BirdScore> birdLeaders() {
  std::vector<BirdScore> rows;
  if (g_best && rosterMe()) {
    rows.push_back({rosterMyId(), g_best, true});
  }
  for (const auto &h : g_heard) {
    rows.push_back({h.id, h.best, false});
  }
  std::stable_sort(rows.begin(), rows.end(),
                   [](const BirdScore &a, const BirdScore &b) { return a.best > b.best; });
  return rows;
}

int birdMyRank() {
  int rank = 1;
  for (const auto &h : g_heard) {
    if (h.best > g_best) {
      rank++;
    }
  }
  return rank;
}

int birdKnownCount() { return (int)g_heard.size() + (g_best ? 1 : 0); }

bool birdChanged() {
  bool c = g_changed;
  g_changed = false;
  return c;
}
