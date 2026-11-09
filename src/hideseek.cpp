#include "hideseek.h"

#include "config.h"
#include "leds.h"
#include "radio.h"
#include "roster.h"
#include "social.h"
#include "ui.h"

#include <Preferences.h>
#include <math.h>
#include <string.h>

static constexpr uint32_t kHsMagic = 0x4B455348u;  // "HSEK"
static constexpr uint8_t kHsVersion = 1;
static constexpr uint32_t kSendEveryMs = 1000;
static constexpr uint8_t kMaxEntries = 24;

enum : uint8_t { HS_PING = 0, HS_READ = 1, HS_FOUND = 2 };

#pragma pack(push, 1)
struct HsPkt {
  uint32_t magic;
  uint8_t version;
  uint8_t kind;   // HS_PING (from it), HS_READ (a seeker's reading), HS_FOUND (id found it)
  uint8_t found;  // pings: 1 once it has been found, so late joiners learn the round is over
  int8_t rssi;    // reads: the seeker's smoothed reading of it, 0 = can't hear it
  uint32_t round;
  char id[16];
  char by[16];    // pings: who found it
};
#pragma pack(pop)

struct Entry {
  char id[16];
  int rssi;
  uint32_t last_heard;
};

// Packets land on the Wi-Fi task; park them and fold them in from the loop.
// Radar tail, appended after HsPkt (3.1.0 badges check only the header and ignore it):
// roster version, then up to kTailMax (roster index, smoothed dBm) pairs: who the sender hears.
static constexpr uint8_t kTailMax = 16;
static constexpr int kPktMax = (int)sizeof(HsPkt) + 5 + 2 * kTailMax;

struct Pending {
  HsPkt pkt;
  int rssi;
  uint8_t tail[5 + 2 * kTailMax];
  uint8_t tail_len;
};
static Pending g_pending[16];
static uint8_t g_pending_n = 0;
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

static Entry g_entries[kMaxEntries];
static bool g_open = false;
static bool g_changed = true;
static uint32_t g_sent_at = 0;
static uint8_t g_found_left = 0;  // resends of our HS_FOUND
static uint32_t g_found_at = 0;

// Seeker: my reading of it.
static int g_my_rssi = -90;  // smoothed; -90 = not heard yet
static uint32_t g_it_heard = 0;
static uint32_t g_close_since = 0;
static uint32_t g_last_close = 0;

// Persisted (NVS "hideseek").
static uint32_t g_round = 0;
static bool g_found = false;
static String g_by;
static uint32_t g_hidden_ms = 0;
static uint32_t g_tick_at = 0;
static uint32_t g_saved_at = 0;

static void save() {
  Preferences prefs;
  prefs.begin("hideseek", false);
  prefs.putUInt("round", g_round);
  prefs.putBool("found", g_found);
  prefs.putString("by", g_by);
  prefs.putUInt("hidden", g_hidden_ms / 1000);
  prefs.end();
  g_saved_at = millis();
}

static const char *itId() {
  const Attendee *it = rosterHideSeekIt();
  return it ? it->id.c_str() : "";
}

bool hideseekIAmIt() {
  const Attendee *me = rosterMe();
  return me && me->it;
}

static void syncRound() {
  uint32_t round = rosterHideSeekRound();
  if (round != g_round) {
    g_round = round;
    g_found = false;
    g_by = "";
    g_hidden_ms = 0;
    save();
  }
}

void hideseekLoad() {
  Preferences prefs;
  prefs.begin("hideseek", true);
  g_round = prefs.getUInt("round", 0);
  g_found = prefs.getBool("found", false);
  g_by = prefs.getString("by", "");
  g_hidden_ms = prefs.getUInt("hidden", 0) * 1000u;
  prefs.end();
}

static void onRaw(const uint8_t *mac, const uint8_t *data, int len) {
  if (len < (int)sizeof(HsPkt)) {
    return;
  }
  portENTER_CRITICAL(&g_mux);
  if (g_pending_n < 16) {
    memcpy(&g_pending[g_pending_n].pkt, data, sizeof(HsPkt));
    g_pending[g_pending_n].rssi = radioRssiForMac(mac);
    int tl = (len > kPktMax ? kPktMax : len) - (int)sizeof(HsPkt);
    g_pending[g_pending_n].tail_len = tl > 0 ? (uint8_t)tl : 0;
    if (tl > 0) {
      memcpy(g_pending[g_pending_n].tail, data + sizeof(HsPkt), tl);
    }
    g_pending_n++;
  }
  portEXIT_CRITICAL(&g_mux);
}

// ---- Radar: who hears whom, and a best-guess map ----
// Every player keeps a table of readings between players: its own (from the packets it hears)
// and everyone else's (from their radar tails). Readings become rough metres, and a small
// solver places everyone so the distances fit as well as they can. The map's shape is the
// estimate; its rotation means nothing (no compass), so players orient it by their teammates.

static constexpr uint8_t kNodes = 16;
static constexpr uint32_t kLinkFreshMs = 6000;
static constexpr uint32_t kNodeReapMs = 30000;

struct Node {
  bool used;
  uint8_t idx;  // roster index
  float x, y;   // metres
  uint32_t seen;
};
static Node g_nodes[kNodes];
static int8_t g_link[kNodes][kNodes];     // g_link[a][b]: a's reading of b (dBm), 0 = none
static uint32_t g_link_at[kNodes][kNodes];
static int g_me_slot = -1;
static uint32_t g_solved_at = 0;

// Trend: my reading of it, once a second, for the warmer/colder arrow.
static int8_t g_trend_buf[10];
static uint8_t g_trend_n = 0;
static uint32_t g_trend_at = 0;

static int rosterIndexOf(const char *id) {
  const auto &all = rosterAll();
  for (size_t i = 0; i < all.size() && i < 255; i++) {
    if (all[i].id == id) {
      return (int)i;
    }
  }
  return -1;
}

static uint32_t hash32(uint32_t n) {
  n ^= n >> 16;
  n *= 0x7feb352du;
  n ^= n >> 15;
  n *= 0x846ca68bu;
  n ^= n >> 16;
  return n;
}

float hideseekRssiToMetres(int rssi) {
  if (rssi >= 0) {
    return kHideSeekMaxMetres;
  }
  float d = powf(10.0f, (float)(kHideSeekRssiAt1m - rssi) / (10.0f * kHideSeekPathLoss));
  return d < 0.5f ? 0.5f : d > kHideSeekMaxMetres ? kHideSeekMaxMetres : d;
}

static void clearSlot(int s) {
  g_nodes[s] = Node{};
  for (int i = 0; i < kNodes; i++) {
    g_link[s][i] = g_link[i][s] = 0;
    g_link_at[s][i] = g_link_at[i][s] = 0;
  }
}

static int slotFor(int idx, uint32_t now) {
  if (idx < 0) {
    return -1;
  }
  int free_s = -1, stale_s = -1;
  for (int i = 0; i < kNodes; i++) {
    if (g_nodes[i].used && g_nodes[i].idx == idx) {
      g_nodes[i].seen = now;
      return i;
    }
    if (!g_nodes[i].used && free_s < 0) {
      free_s = i;
    } else if (g_nodes[i].used && i != g_me_slot &&
               (stale_s < 0 || g_nodes[i].seen < g_nodes[stale_s].seen)) {
      stale_s = i;
    }
  }
  int s = free_s >= 0 ? free_s : stale_s;
  if (s < 0) {
    return -1;
  }
  clearSlot(s);
  g_nodes[s].used = true;
  g_nodes[s].idx = (uint8_t)idx;
  g_nodes[s].seen = now;
  // Start somewhere stable but distinct; the solver moves it from here.
  uint32_t h = hash32((uint32_t)idx * 2654435761u + 1);
  float a = (float)(h % 360) * 0.01745329f;
  g_nodes[s].x = cosf(a) * 5.0f;
  g_nodes[s].y = sinf(a) * 5.0f;
  return s;
}

static void radarReset() {
  for (int i = 0; i < kNodes; i++) {
    clearSlot(i);
  }
  g_me_slot = slotFor(rosterIndexOf(rosterMyId().c_str()), millis());
  if (g_me_slot >= 0) {
    g_nodes[g_me_slot].x = g_nodes[g_me_slot].y = 0.0f;
  }
  g_trend_n = 0;
  g_trend_at = 0;
}

// Fresh reading between two slots in metres (both directions averaged), or 0 if none.
static float linkMetres(int a, int b, uint32_t now) {
  int sum = 0, n = 0;
  if (g_link[a][b] && now - g_link_at[a][b] <= kLinkFreshMs) {
    sum += g_link[a][b];
    n++;
  }
  if (g_link[b][a] && now - g_link_at[b][a] <= kLinkFreshMs) {
    sum += g_link[b][a];
    n++;
  }
  return n ? hideseekRssiToMetres(sum / n) : 0.0f;
}

static bool nodeLive(int s, uint32_t now) {
  if (!g_nodes[s].used) {
    return false;
  }
  if (s == g_me_slot) {
    return true;
  }
  for (int j = 0; j < kNodes; j++) {
    if (j != s && g_nodes[j].used && linkMetres(s, j, now) > 0.0f) {
      return true;
    }
  }
  return false;
}

// A few rounds of pulling each pair toward its measured distance (closer readings are
// trusted more), with a gentle push apart for unlinked players sitting on top of each other.
static void solve(uint32_t now) {
  bool live[kNodes];
  for (int i = 0; i < kNodes; i++) {
    live[i] = nodeLive(i, now);
    if (g_nodes[i].used && i != g_me_slot && now - g_nodes[i].seen > kNodeReapMs) {
      clearSlot(i);
      live[i] = false;
    }
  }
  for (int it = 0; it < 12; it++) {
    for (int a = 0; a < kNodes; a++) {
      if (!live[a]) continue;
      for (int b = a + 1; b < kNodes; b++) {
        if (!live[b]) continue;
        float dx = g_nodes[b].x - g_nodes[a].x, dy = g_nodes[b].y - g_nodes[a].y;
        float cur = sqrtf(dx * dx + dy * dy);
        if (cur < 0.01f) {
          float ang = (float)(hash32(a * 31 + b) % 360) * 0.01745329f;
          dx = cosf(ang) * 0.01f;
          dy = sinf(ang) * 0.01f;
          cur = 0.01f;
        }
        float want = linkMetres(a, b, now);
        float k;
        if (want > 0.0f) {
          float w = 1.0f / want;
          if (w > 1.0f) w = 1.0f;
          k = 0.25f * w * (cur - want) / cur;
        } else if (cur < 1.5f) {
          k = -0.05f * (1.5f - cur) / cur;
        } else {
          continue;
        }
        g_nodes[a].x += k * dx;
        g_nodes[a].y += k * dy;
        g_nodes[b].x -= k * dx;
        g_nodes[b].y -= k * dy;
      }
    }
  }
  // Keep me at the centre.
  if (g_me_slot >= 0) {
    float ox = g_nodes[g_me_slot].x, oy = g_nodes[g_me_slot].y;
    for (int i = 0; i < kNodes; i++) {
      if (g_nodes[i].used) {
        g_nodes[i].x -= ox;
        g_nodes[i].y -= oy;
        if (!isfinite(g_nodes[i].x) || !isfinite(g_nodes[i].y)) {
          g_nodes[i].x = g_nodes[i].y = 1.0f;  // never let a bad number stick
        }
      }
    }
  }
}

// A packet from `sender_idx`: note my own reading of it, then fold in its radar tail.
static void radarHear(int sender_idx, int rssi, const uint8_t *tail, uint8_t tail_len,
                      uint32_t now) {
  if (g_me_slot < 0) {
    return;
  }
  int s = slotFor(sender_idx, now);
  if (s < 0 || s == g_me_slot) {
    return;
  }
  int8_t prev = g_link[g_me_slot][s];
  int r = rssi < -120 ? -120 : rssi > -1 ? -1 : rssi;
  bool fresh = prev && now - g_link_at[g_me_slot][s] <= kLinkFreshMs;
  g_link[g_me_slot][s] = (int8_t)(fresh ? (prev * 3 + r) / 4 : r);
  g_link_at[g_me_slot][s] = now;
  if (tail_len < 5) {
    return;
  }
  uint32_t rv;
  memcpy(&rv, tail, 4);
  if (rv != rosterVersion()) {
    return;  // roster indexes only line up on the same roster
  }
  uint8_t n = tail[4];
  if (n > kTailMax || 5 + 2 * n > tail_len) {
    return;
  }
  for (uint8_t i = 0; i < n; i++) {
    int idx = tail[5 + 2 * i];
    int8_t v = (int8_t)tail[6 + 2 * i];
    if (idx >= (int)rosterCount() || v >= 0 || v < -120) {
      continue;
    }
    int b = slotFor(idx, now);
    if (b < 0 || b == s) {
      continue;
    }
    g_link[s][b] = v;
    g_link_at[s][b] = now;
  }
}

// My radar tail: who I hear right now.
static int radarTail(uint8_t *out, uint32_t now) {
  uint32_t rv = rosterVersion();
  memcpy(out, &rv, 4);
  uint8_t n = 0;
  if (g_me_slot >= 0) {
    for (int b = 0; b < kNodes && n < kTailMax; b++) {
      if (b == g_me_slot || !g_nodes[b].used || !g_link[g_me_slot][b] ||
          now - g_link_at[g_me_slot][b] > kLinkFreshMs) {
        continue;
      }
      out[5 + 2 * n] = g_nodes[b].idx;
      out[6 + 2 * n] = (uint8_t)g_link[g_me_slot][b];
      n++;
    }
  }
  out[4] = n;
  return 5 + 2 * n;
}

static void trendTick(uint32_t now) {
  if (g_trend_at && now - g_trend_at < 1000) {
    return;
  }
  g_trend_at = now;
  int r = hideseekMyRssi();
  if (!r) {
    return;
  }
  if (g_trend_n == sizeof(g_trend_buf)) {
    memmove(g_trend_buf, g_trend_buf + 1, sizeof(g_trend_buf) - 1);
    g_trend_n--;
  }
  g_trend_buf[g_trend_n++] = (int8_t)r;
}

int hideseekTrend() {
  if (g_trend_n < 6 || !hideseekMyRssi()) {
    return 2;
  }
  int first = (g_trend_buf[0] + g_trend_buf[1] + g_trend_buf[2]) / 3;
  int last = (g_trend_buf[g_trend_n - 1] + g_trend_buf[g_trend_n - 2] + g_trend_buf[g_trend_n - 3]) / 3;
  int d = last - first;
  return d >= 3 ? 1 : d <= -3 ? -1 : 0;
}

void hideseekRadar(HideSeekRadar *out) {
  memset(out, 0, sizeof(*out));
  uint32_t now = millis();
  if (g_me_slot < 0) {
    return;
  }
  int it_idx = rosterIndexOf(itId());
  int it_slot = -1;
  for (int i = 0; i < kNodes; i++) {
    if (g_nodes[i].used && g_nodes[i].idx == it_idx) {
      it_slot = i;
    }
  }
  const auto &all = rosterAll();
  for (int i = 0; i < kNodes && out->n < kHideSeekBlips; i++) {
    if (i == g_me_slot || i == it_slot || !nodeLive(i, now) || g_nodes[i].idx >= all.size()) {
      continue;
    }
    HideSeekBlip &b = out->blips[out->n++];
    strncpy(b.id, all[g_nodes[i].idx].id.c_str(), sizeof(b.id) - 1);
    b.x = g_nodes[i].x;
    b.y = g_nodes[i].y;
    b.metres = linkMetres(g_me_slot, i, now);
  }
  if (it_slot < 0 || it_slot == g_me_slot) {
    return;
  }
  // How many players have a fresh reading of it: three or more pin it down (up to a mirror).
  int anchors = 0;
  float err = 0.0f, sum_d = 0.0f;
  for (int j = 0; j < kNodes; j++) {
    if (j == it_slot || !g_nodes[j].used) continue;
    float d = linkMetres(it_slot, j, now);
    if (d <= 0.0f) continue;
    anchors++;
    float dx = g_nodes[j].x - g_nodes[it_slot].x, dy = g_nodes[j].y - g_nodes[it_slot].y;
    float e = sqrtf(dx * dx + dy * dy) - d;
    err += e * e;
    sum_d += d;
  }
  out->it_anchors = anchors;
  out->it_ring = linkMetres(g_me_slot, it_slot, now);
  if (anchors >= 3) {
    out->it_fix = 2;
    out->it_x = g_nodes[it_slot].x;
    out->it_y = g_nodes[it_slot].y;
    float r = sqrtf(err / anchors) + 0.25f * (sum_d / anchors);
    out->it_r = r < 1.0f ? 1.0f : r;
  } else if (out->it_ring > 0.0f) {
    out->it_fix = 1;
  }
}

static void send(uint8_t kind) {
  const String &me = rosterMyId();
  if (!me.length() || !g_round) {
    return;
  }
  HsPkt p = {};
  p.magic = kHsMagic;
  p.version = kHsVersion;
  p.kind = kind;
  p.round = g_round;
  p.found = g_found ? 1 : 0;
  int r = hideseekMyRssi();
  p.rssi = (int8_t)(r < -127 ? -127 : r);
  strncpy(p.id, me.c_str(), sizeof(p.id) - 1);
  strncpy(p.by, g_by.c_str(), sizeof(p.by) - 1);
  static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t buf[kPktMax];
  memcpy(buf, &p, sizeof(p));
  int tl = kind == HS_FOUND ? 0 : radarTail(buf + sizeof(p), millis());
  radioSendRaw(bcast, buf, sizeof(p) + tl);
}

static Entry *entryFor(const char *id, uint32_t now) {
  Entry *stalest = nullptr;
  for (auto &e : g_entries) {
    if (e.id[0] && strncmp(e.id, id, 16) == 0) {
      return &e;
    }
  }
  for (auto &e : g_entries) {
    if (!e.id[0]) {
      stalest = &e;
      break;
    }
    if (!stalest || e.last_heard < stalest->last_heard) {
      stalest = &e;
    }
  }
  memset(stalest, 0, sizeof(*stalest));
  strncpy(stalest->id, id, 15);
  stalest->rssi = -90;
  stalest->last_heard = now;
  return stalest;
}

static String firstName(const char *id) {
  const Attendee *a = rosterFind(id);
  String n = a ? a->name : String(id);
  int sp = n.indexOf(' ');
  return sp > 0 ? n.substring(0, sp) : n;
}

static void markFound(const char *by) {
  if (g_found) {
    return;
  }
  g_found = true;
  g_by = by;
  save();
  const String &me = rosterMyId();
  if (me == by) {
    socialUnlock(ACH_BLOODHOUND);
    uiToast((String("You found ") + firstName(itId()) + "!").c_str(), 5000);
    ledsBlink(6, 80, 80);
  } else if (hideseekIAmIt()) {
    uiToast((String("Found by ") + firstName(by) + "!").c_str(), 5000);
    ledsBlink(3, 300, 150);
  } else {
    uiToast((String(firstName(itId())) + " found by " + firstName(by)).c_str(), 5000);
    ledsBlink(2, 150, 150);
  }
  g_changed = true;
}

static void drain(uint32_t now) {
  Pending local[16];
  uint8_t n;
  portENTER_CRITICAL(&g_mux);
  n = g_pending_n;
  memcpy(local, g_pending, sizeof(Pending) * n);
  g_pending_n = 0;
  portEXIT_CRITICAL(&g_mux);
  const String &me = rosterMyId();
  const char *it = itId();
  bool i_am_it = hideseekIAmIt();
  for (uint8_t i = 0; i < n; i++) {
    HsPkt &p = local[i].pkt;
    p.id[15] = 0;
    p.by[15] = 0;
    if (p.magic != kHsMagic || p.version != kHsVersion || p.round != g_round || !p.id[0] ||
        !rosterFind(p.id) || me == p.id) {
      continue;
    }
    if (p.kind != HS_FOUND) {
      radarHear(rosterIndexOf(p.id), local[i].rssi, local[i].tail, local[i].tail_len, now);
    }
    if (p.kind == HS_FOUND) {
      if (strncmp(p.id, it, 16) != 0) {
        markFound(p.id);
      }
      continue;
    }
    if (p.kind == HS_PING) {
      if (i_am_it || strncmp(p.id, it, 16) != 0) {
        continue;  // only the roster's "it" pings
      }
      if (p.found && p.by[0]) {
        markFound(p.by);
      }
      g_my_rssi = g_my_rssi == -90 ? local[i].rssi : (g_my_rssi * 3 + local[i].rssi) / 4;
      g_it_heard = now;
      if (g_my_rssi >= kHideSeekCloseRssi) {
        if (!g_close_since || now - g_last_close > kHideSeekGapMs) {
          g_close_since = now;
        }
        g_last_close = now;
      }
      g_changed = true;
      continue;
    }
    if (p.kind == HS_READ && strncmp(p.id, it, 16) != 0) {
      Entry *e = entryFor(p.id, now);
      if (i_am_it) {
        e->rssi = e->rssi == -90 ? local[i].rssi : (e->rssi * 3 + local[i].rssi) / 4;
      } else {
        e->rssi = p.rssi;
      }
      e->last_heard = now;
      g_changed = true;
    }
  }
}

void hideseekOpen() {
  syncRound();
  memset(g_entries, 0, sizeof(g_entries));
  portENTER_CRITICAL(&g_mux);
  g_pending_n = 0;
  portEXIT_CRITICAL(&g_mux);
  g_my_rssi = -90;
  g_it_heard = 0;
  g_close_since = 0;
  g_last_close = 0;
  g_found_left = 0;
  radarReset();
  g_solved_at = 0;
  radioOnRaw(kHsMagic, onRaw);
  g_open = true;
  g_sent_at = 0;
  g_tick_at = millis();
  g_changed = true;
}

void hideseekClose() {
  radioOnRaw(kHsMagic, nullptr);
  g_open = false;
  save();
}

void hideseekLoop() {
  if (!g_open) {
    return;
  }
  uint32_t now = millis();
  drain(now);
  if (!hideseekActive()) {
    return;
  }
  bool i_am_it = hideseekIAmIt();
  if (i_am_it && !g_found) {
    uint32_t prev = g_hidden_ms / 1000;
    g_hidden_ms += now - g_tick_at;
    if (g_hidden_ms / 1000 != prev) {
      g_changed = true;
      if (g_hidden_ms / 1000 >= kHideSeekGhostSecs) {
        socialUnlock(ACH_GHOST);
      }
    }
  }
  g_tick_at = now;
  trendTick(now);
  if (now - g_solved_at >= 250) {
    g_solved_at = now;
    solve(now);
  }
  if (now - g_saved_at >= 30000) {
    save();
  }
  if (!i_am_it && !g_found && hideseekFoundProgress() >= 1.0f) {
    markFound(rosterMyId().c_str());
    g_found_left = 3;
    g_found_at = 0;
  }
  if (!g_sent_at || now - g_sent_at >= kSendEveryMs) {
    g_sent_at = now;
    send(i_am_it ? HS_PING : HS_READ);
  }
  if (g_found_left && (!g_found_at || now - g_found_at >= 700)) {
    g_found_at = now;
    g_found_left--;
    send(HS_FOUND);
  }
}

bool hideseekChanged() {
  bool c = g_changed;
  g_changed = false;
  return c;
}

bool hideseekActive() { return rosterHideSeekRound() != 0 && rosterHideSeekIt() != nullptr; }

String hideseekItName() {
  const Attendee *it = rosterHideSeekIt();
  return it ? it->name : String("");
}

int hideseekMyRssi() {
  if (!g_it_heard || millis() - g_it_heard > kHideSeekDarkMs) {
    return 0;
  }
  return g_my_rssi;
}

bool hideseekDark() { return g_it_heard && millis() - g_it_heard > kHideSeekDarkMs; }

float hideseekFoundProgress() {
  uint32_t now = millis();
  if (!g_close_since || now - g_last_close > kHideSeekGapMs) {
    return 0.0f;
  }
  float f = (float)(g_last_close - g_close_since) / (float)kHideSeekFoundMs;
  return f > 1.0f ? 1.0f : f;
}

// The saved result belongs to g_round; a new roster round reads as not found until it syncs.
bool hideseekFound() { return g_found && g_round == rosterHideSeekRound(); }
const String &hideseekFoundBy() { return g_by; }
uint32_t hideseekHiddenSecs() { return g_round == rosterHideSeekRound() ? g_hidden_ms / 1000 : 0; }

int hideseekBoard(HideSeekReading *out, int max) {
  uint32_t now = millis();
  int n = 0;
  for (auto &e : g_entries) {
    if (!e.id[0] || now - e.last_heard > kHideSeekDarkMs || e.rssi == 0 || e.rssi <= -90) {
      continue;
    }
    // Insert in order, strongest first; drop the weakest when full.
    int at = n;
    while (at > 0 && out[at - 1].rssi < e.rssi) {
      at--;
    }
    if (at >= max) {
      continue;
    }
    int last = n < max ? n : max - 1;
    for (int j = last; j > at; j--) {
      out[j] = out[j - 1];
    }
    memcpy(out[at].id, e.id, 16);
    out[at].rssi = e.rssi;
    if (n < max) {
      n++;
    }
  }
  return n;
}

const char *hideseekHeatWord(int rssi) {
  if (!rssi) return "No signal";
  if (rssi >= kHideSeekCloseRssi) return "ON TOP OF THEM";
  if (rssi >= -55) return "Hot";
  if (rssi >= -65) return "Warm";
  if (rssi >= -75) return "Cool";
  return "Cold";
}

String hideseekDescribe() {
  HideSeekReading top[1];
  int n = hideseekBoard(top, 1);
  char buf[180];
  snprintf(buf, sizeof(buf),
           "HS roster %u saved %u it %s me_it %d open %d found %d by %s my %d dBm prog %d%%"
           " hidden %us best %s %d",
           (unsigned)rosterHideSeekRound(), (unsigned)g_round, itId(), hideseekIAmIt() ? 1 : 0,
           g_open ? 1 : 0, g_found ? 1 : 0, g_by.length() ? g_by.c_str() : "-", hideseekMyRssi(),
           (int)(hideseekFoundProgress() * 100), (unsigned)hideseekHiddenSecs(),
           n ? top[0].id : "-", n ? top[0].rssi : 0);
  return String(buf);
}
