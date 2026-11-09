#include "outbreak.h"

#include "config.h"
#include "leds.h"
#include "radio.h"
#include "roster.h"
#include "social.h"
#include "ui.h"

#include <Preferences.h>
#include <string.h>

static constexpr uint32_t kObMagic = 0x4B52424Fu;  // "OBRK"
static constexpr uint8_t kObVersion = 1;
static constexpr uint32_t kSendEveryMs = 1000;
static constexpr uint32_t kContactStaleMs = 5000;  // drop from "nearby" after this
static constexpr uint8_t kMaxContacts = 24;

enum : uint8_t { PKT_STATUS = 0, PKT_GOTCHA = 1 };

#pragma pack(push, 1)
struct ObPkt {
  uint32_t magic;
  uint8_t version;
  uint8_t kind;   // PKT_STATUS, or PKT_GOTCHA ("you infected me", addressed to `to`)
  uint8_t state;  // sender's OutbreakState
  uint8_t pad;
  uint32_t round;
  char id[16];
  char to[16];
};
#pragma pack(pop)

struct Contact {
  char id[16];
  uint8_t state;
  int rssi;               // smoothed
  uint32_t last_heard;
  uint32_t close_since;   // start of the current close-contact stretch (0 = none)
  uint32_t last_close;
};

// Packets land on the Wi-Fi task; park them and fold them in from the loop.
struct Pending {
  ObPkt pkt;
  int rssi;
};
static Pending g_pending[16];
static uint8_t g_pending_n = 0;
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

static Contact g_contacts[kMaxContacts];
static bool g_open = false;
static bool g_changed = true;
static uint32_t g_sent_at = 0;
static uint8_t g_gotcha_left = 0;  // resends of our "you infected me"
static uint32_t g_gotcha_at = 0;

// Persisted (NVS "outbreak").
static uint32_t g_round = 0;   // round these values belong to
static uint8_t g_state = OB_HEALTHY;
static bool g_seeded = false;  // patient zero already started this round
static String g_by;
static uint8_t g_spread = 0;
static uint32_t g_immune_until = 0;  // millis()
static uint32_t g_alive_ms = 0;      // survival time this round
static uint32_t g_tick_at = 0;       // last time the survival clock was advanced
static uint32_t g_saved_at = 0;
static char g_spread_ids[kMaxContacts][16];  // this boot: who we've already counted

static void save() {
  Preferences prefs;
  prefs.begin("outbreak", false);
  prefs.putUInt("round", g_round);
  prefs.putUChar("state", g_state == OB_MEDIC ? OB_HEALTHY : g_state);
  prefs.putBool("seeded", g_seeded);
  prefs.putString("by", g_by);
  prefs.putUChar("spread", g_spread);
  uint32_t now = millis();
  prefs.putUInt("imm_left", g_state == OB_IMMUNE && g_immune_until > now ? g_immune_until - now : 0);
  prefs.putUInt("alive", g_alive_ms / 1000);
  prefs.end();
  g_saved_at = now;
}

static bool iAmMedic() {
  const Attendee *me = rosterMe();
  return me && me->medic;
}

// Bring the saved state in line with the roster's round and this badge's role.
static void syncRound() {
  uint32_t round = rosterOutbreakRound();
  if (round != g_round) {
    g_round = round;
    g_state = OB_HEALTHY;
    g_seeded = false;
    g_by = "";
    g_spread = 0;
    g_immune_until = 0;
    g_alive_ms = 0;
    memset(g_spread_ids, 0, sizeof(g_spread_ids));
    save();
  }
  if (!round) {
    return;
  }
  if (iAmMedic()) {
    g_state = OB_MEDIC;
    return;
  }
  if (g_state == OB_MEDIC) {
    g_state = OB_HEALTHY;  // no longer a medic in this roster
  }
  const Attendee *me = rosterMe();
  if (me && me->zero && !g_seeded) {
    g_seeded = true;
    g_state = OB_INFECTED;
    g_by = "";
    save();
    socialUnlock(ACH_PATIENT_ZERO);
  }
}

void outbreakLoad() {
  Preferences prefs;
  prefs.begin("outbreak", true);
  g_round = prefs.getUInt("round", 0);
  g_state = prefs.getUChar("state", OB_HEALTHY);
  g_seeded = prefs.getBool("seeded", false);
  g_by = prefs.getString("by", "");
  g_spread = prefs.getUChar("spread", 0);
  uint32_t left = prefs.getUInt("imm_left", 0);
  g_alive_ms = prefs.getUInt("alive", 0) * 1000u;
  prefs.end();
  if (g_state > OB_IMMUNE) {
    g_state = OB_HEALTHY;
  }
  g_immune_until = left ? millis() + left : 0;
  if (g_state == OB_IMMUNE && !left) {
    g_state = OB_HEALTHY;
  }
}

static void onRaw(const uint8_t *mac, const uint8_t *data, int len) {
  if (len < (int)sizeof(ObPkt)) {
    return;
  }
  portENTER_CRITICAL(&g_mux);
  if (g_pending_n < 16) {
    memcpy(&g_pending[g_pending_n].pkt, data, sizeof(ObPkt));
    g_pending[g_pending_n].rssi = radioRssiForMac(mac);
    g_pending_n++;
  }
  portEXIT_CRITICAL(&g_mux);
}

static void send(uint8_t kind, const char *to) {
  const String &me = rosterMyId();
  if (!me.length() || !g_round) {
    return;
  }
  ObPkt p = {};
  p.magic = kObMagic;
  p.version = kObVersion;
  p.kind = kind;
  p.state = g_state;
  p.round = g_round;
  strncpy(p.id, me.c_str(), sizeof(p.id) - 1);
  if (to) {
    strncpy(p.to, to, sizeof(p.to) - 1);
  }
  static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  radioSendRaw(bcast, reinterpret_cast<const uint8_t *>(&p), sizeof(p));
}

static Contact *contactFor(const char *id, uint32_t now) {
  Contact *stalest = nullptr;
  for (auto &c : g_contacts) {
    if (c.id[0] && strncmp(c.id, id, 16) == 0) {
      return &c;
    }
  }
  for (auto &c : g_contacts) {
    if (!c.id[0]) {
      stalest = &c;
      break;
    }
    if (!stalest || c.last_heard < stalest->last_heard) {
      stalest = &c;
    }
  }
  memset(stalest, 0, sizeof(*stalest));
  strncpy(stalest->id, id, 15);
  stalest->rssi = -90;
  stalest->last_heard = now;
  return stalest;
}

static void noteGotcha(const char *from) {
  for (auto &s : g_spread_ids) {
    if (strncmp(s, from, 16) == 0) {
      return;  // already counted this boot (it resends a few times)
    }
  }
  for (auto &s : g_spread_ids) {
    if (!s[0]) {
      strncpy(s, from, 15);
      break;
    }
  }
  if (g_spread < 255) {
    g_spread++;
  }
  save();
  if (g_spread >= kOutbreakSpreader) {
    socialUnlock(ACH_SPREADER);
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
  for (uint8_t i = 0; i < n; i++) {
    ObPkt &p = local[i].pkt;
    p.id[15] = 0;
    p.to[15] = 0;
    if (p.magic != kObMagic || p.version != kObVersion || p.round != g_round || !p.id[0] ||
        !rosterFind(p.id) || me == p.id) {
      continue;
    }
    if (p.kind == PKT_GOTCHA) {
      if (me == p.to) {
        noteGotcha(p.id);
      }
      continue;
    }
    Contact *c = contactFor(p.id, now);
    c->state = p.state;
    c->rssi = c->rssi == -90 ? local[i].rssi : (c->rssi * 3 + local[i].rssi) / 4;
    c->last_heard = now;
    if (c->rssi >= kOutbreakCloseRssi) {
      if (!c->close_since || now - c->last_close > kOutbreakGapMs) {
        c->close_since = now;
      }
      c->last_close = now;
    }
  }
}

// Longest current close-contact stretch with someone in `state`, and who.
static uint32_t exposureTo(uint8_t state, uint32_t now, const char **who) {
  uint32_t best = 0;
  for (auto &c : g_contacts) {
    if (!c.id[0] || c.state != state || !c.close_since || now - c.last_close > kOutbreakGapMs) {
      continue;
    }
    uint32_t e = c.last_close - c.close_since;
    if (e >= best) {
      best = e;
      if (who) {
        *who = c.id;
      }
    }
  }
  return best;
}

static void step(uint32_t now) {
  if (g_state == OB_IMMUNE && (int32_t)(now - g_immune_until) >= 0) {
    g_state = OB_HEALTHY;
    save();
    g_changed = true;
  }
  const char *who = nullptr;
  if (g_state == OB_HEALTHY && exposureTo(OB_INFECTED, now, &who) >= kOutbreakExposeMs && who) {
    g_state = OB_INFECTED;
    g_by = who;
    save();
    g_gotcha_left = 3;
    g_gotcha_at = 0;
    const Attendee *a = rosterFind(who);
    String first = a ? a->name : String(who);
    int sp = first.indexOf(' ');
    if (sp > 0) {
      first = first.substring(0, sp);
    }
    uiToast((String("Infected by ") + first + "!").c_str(), 4000);
    ledsBlink(6, 60, 60);
    g_changed = true;
  } else if (g_state == OB_INFECTED && exposureTo(OB_MEDIC, now, nullptr) >= kOutbreakExposeMs) {
    g_state = OB_IMMUNE;
    g_immune_until = now + kOutbreakImmuneMs;
    save();
    socialUnlock(ACH_IMMUNE);
    uiToast("Cured! Immune for an hour", 4000);
    ledsBlink(3, 200, 120);
    g_changed = true;
  }
}

void outbreakOpen() {
  syncRound();
  memset(g_contacts, 0, sizeof(g_contacts));
  portENTER_CRITICAL(&g_mux);
  g_pending_n = 0;
  portEXIT_CRITICAL(&g_mux);
  radioOnRaw(kObMagic, onRaw);
  g_open = true;
  g_sent_at = 0;
  g_tick_at = millis();
  g_changed = true;
}

void outbreakClose() {
  radioOnRaw(kObMagic, nullptr);
  g_open = false;
  save();  // keeps the immunity countdown
}

void outbreakLoop() {
  if (!g_open) {
    return;
  }
  uint32_t now = millis();
  drain(now);
  if (!g_round) {
    return;
  }
  step(now);
  // Survival clock: only while playing, healthy or immune, and not a medic.
  if (g_state == OB_HEALTHY || g_state == OB_IMMUNE) {
    uint32_t prev = g_alive_ms / 1000;
    g_alive_ms += now - g_tick_at;
    if (g_alive_ms / 1000 != prev) {
      g_changed = true;
    }
  }
  g_tick_at = now;
  if (now - g_saved_at >= 30000) {
    save();
  }
  if (!g_sent_at || now - g_sent_at >= kSendEveryMs) {
    g_sent_at = now;
    send(PKT_STATUS, nullptr);
  }
  if (g_gotcha_left && (!g_gotcha_at || now - g_gotcha_at >= 700)) {
    g_gotcha_at = now;
    g_gotcha_left--;
    send(PKT_GOTCHA, g_by.c_str());
  }
}

bool outbreakChanged() {
  bool c = g_changed;
  g_changed = false;
  return c;
}

bool outbreakActive() { return rosterOutbreakRound() != 0; }
OutbreakState outbreakState() { return (OutbreakState)g_state; }

float outbreakExposure() {
  uint32_t now = millis();
  uint8_t threat = g_state == OB_HEALTHY ? OB_INFECTED : g_state == OB_INFECTED ? OB_MEDIC : 255;
  if (threat == 255) {
    return 0.0f;
  }
  uint32_t e = exposureTo(threat, now, nullptr);
  float f = (float)e / (float)kOutbreakExposeMs;
  return f > 1.0f ? 1.0f : f;
}

const String &outbreakInfectedBy() { return g_by; }
uint8_t outbreakSpreadCount() { return g_spread; }

uint32_t outbreakAliveSecs() { return g_alive_ms / 1000; }

int outbreakBestRssi() {
  uint32_t now = millis();
  int best = 0;
  for (auto &c : g_contacts) {
    if (c.id[0] && now - c.last_heard <= kContactStaleMs && (best == 0 || c.rssi > best)) {
      best = c.rssi;
    }
  }
  return best;
}

String outbreakDescribe() {
  const Attendee *me = rosterMe();
  int best = -127;
  const char *best_id = "-";
  for (auto &c : g_contacts) {
    if (c.id[0] && c.rssi > best) {
      best = c.rssi;
      best_id = c.id;
    }
  }
  char buf[180];
  snprintf(buf, sizeof(buf),
           "OB roster %u saved %u state %u zero %d medic %d seeded %d spread %u by %s open %d"
           " heard %d best %s %d dBm expose %d%%",
           (unsigned)rosterOutbreakRound(), (unsigned)g_round, (unsigned)g_state,
           me && me->zero ? 1 : 0, me && me->medic ? 1 : 0, g_seeded ? 1 : 0, (unsigned)g_spread,
           g_by.length() ? g_by.c_str() : "-", g_open ? 1 : 0, outbreakNearbyCount(), best_id, best,
           (int)(outbreakExposure() * 100));
  return String(buf);
}

int outbreakNearbyCount() {
  uint32_t now = millis();
  int n = 0;
  for (auto &c : g_contacts) {
    if (c.id[0] && now - c.last_heard <= kContactStaleMs) {
      n++;
    }
  }
  return n;
}
