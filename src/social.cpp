#include "social.h"

#include "config.h"
#include "klippy_bird.h"
#include "roster.h"
#include "settings.h"
#include "ui.h"

#include <Preferences.h>
#include <algorithm>

struct Score {
  String id;
  uint8_t met;
};

static std::vector<Score> g_scores;
static uint32_t g_ach = 0;
static uint32_t g_faces = 0;
static uint32_t g_bj_hands = 0;  // blackjack hands dealt to this badge (NVS "bjh")
static bool g_scores_dirty = false;
static uint32_t g_scores_saved_at = 0;
static uint32_t g_last_check = 0;

// Beacons arrive on the Wi-Fi task; park them here and fold them in from loop().
struct PendingScore {
  char id[kIdLen];
  uint8_t met;
};
static PendingScore g_pending[16];
static uint8_t g_pending_n = 0;
static portMUX_TYPE g_pending_mux = portMUX_INITIALIZER_UNLOCKED;

static constexpr uint32_t kSocialCheckMs = 500;
static constexpr uint32_t kScoresSaveMs = 30000;
static constexpr uint32_t kAchToastMs = 4000;

// Names only on screen; the rules live in checkAchievements().
static const char *const kAch[ACH_COUNT] = {
    "First Contact", "Mingler",       "Social Butterfly", "Networking Legend",
    "Completionist", "Goose Whisperer", "Found klipper",  "Royalty",
    "Fashionista",   "Top 3",         "A visit to HR",    "Fashionable footwear",
    "Klippy Top 3",  "Doublemint",    "Sheep Herder",     "Timekeepers",
    "Ace in the hole", "Gambling Problem", "A friend in need",
    "Patient Zero",  "Super-spreader", "Immune",
    "Bloodhound",    "Ghost",
};

static void loadScores(const String &packed) {
  g_scores.clear();
  int start = 0;
  while (start < (int)packed.length()) {
    int end = packed.indexOf(';', start);
    if (end < 0) {
      end = packed.length();
    }
    String item = packed.substring(start, end);
    int colon = item.indexOf(':');
    if (colon > 0) {
      String id = item.substring(0, colon);
      int met = item.substring(colon + 1).toInt();
      if (rosterFind(id.c_str()) && met >= 0 && met <= 255) {
        g_scores.push_back({id, (uint8_t)met});
      }
    }
    start = end + 1;
  }
}

static String packScores() {
  String out;
  for (const auto &s : g_scores) {
    if (out.length()) {
      out += ';';
    }
    out += s.id;
    out += ':';
    out += String(s.met);
  }
  return out;
}

static void saveScores() {
  Preferences prefs;
  prefs.begin("social", false);
  prefs.putString("lb", packScores());
  prefs.end();
  g_scores_dirty = false;
  g_scores_saved_at = millis();
}

static void saveFlags() {
  Preferences prefs;
  prefs.begin("social", false);
  prefs.putUInt("ach", g_ach);
  prefs.putUInt("faces", g_faces);
  prefs.putUInt("bjh", g_bj_hands);
  prefs.end();
}

void socialLoad() {
  Preferences prefs;
  prefs.begin("social", true);
  String packed = prefs.getString("lb", "");
  g_ach = prefs.getUInt("ach", 0);
  g_faces = prefs.getUInt("faces", 0);
  g_bj_hands = prefs.getUInt("bjh", 0);
  prefs.end();
  loadScores(packed);
}

// Score shown for `id`: the roster's pinned score if it has one, else `met`.
static int scoreFor(const String &id, int met) {
  const Attendee *a = rosterFind(id.c_str());
  return (a && a->fixed_score) ? a->score : met;
}

static int metInRoster() {
  int n = 0;
  for (const auto &id : settingsMet()) {
    if (rosterFind(id.c_str())) {
      n++;
    }
  }
  return n;
}

uint8_t socialBeaconScore() {
  int met = (int)settingsMet().size();
  return (uint8_t)(std::min(met, 254) + 1);
}

void socialNoteScore(const char *id, uint8_t beacon_score) {
  if (!id || !id[0] || beacon_score == 0) {
    return;
  }
  portENTER_CRITICAL(&g_pending_mux);
  int slot = -1;
  for (int i = 0; i < g_pending_n; i++) {
    if (strncmp(g_pending[i].id, id, kIdLen) == 0) {
      slot = i;
      break;
    }
  }
  if (slot < 0 && g_pending_n < (int)(sizeof(g_pending) / sizeof(g_pending[0]))) {
    slot = g_pending_n++;
    strncpy(g_pending[slot].id, id, kIdLen - 1);
    g_pending[slot].id[kIdLen - 1] = 0;
  }
  if (slot >= 0) {
    g_pending[slot].met = beacon_score - 1;
  }
  portEXIT_CRITICAL(&g_pending_mux);
}

static void drainPending() {
  PendingScore batch[16];
  uint8_t n;
  portENTER_CRITICAL(&g_pending_mux);
  n = g_pending_n;
  memcpy(batch, g_pending, sizeof(PendingScore) * n);
  g_pending_n = 0;
  portEXIT_CRITICAL(&g_pending_mux);

  const String &mine = rosterMyId();
  for (uint8_t i = 0; i < n; i++) {
    if (mine == batch[i].id || !rosterFind(batch[i].id)) {
      continue;
    }
    bool found = false;
    for (auto &s : g_scores) {
      if (s.id == batch[i].id) {
        found = true;
        // Latest beacon wins: scores can drop if that badge was reset.
        if (s.met != batch[i].met) {
          s.met = batch[i].met;
          g_scores_dirty = true;
        }
        break;
      }
    }
    if (!found) {
      g_scores.push_back({String(batch[i].id), batch[i].met});
      g_scores_dirty = true;
    }
  }
}

std::vector<LeaderRow> socialLeaders() {
  std::vector<LeaderRow> rows;
  rows.reserve(g_scores.size() + 1);
  const String &mine = rosterMyId();
  if (mine.length()) {
    rows.push_back({mine, (int16_t)scoreFor(mine, metInRoster()), true});
  }
  for (const auto &s : g_scores) {
    rows.push_back({s.id, (int16_t)scoreFor(s.id, s.met), false});
  }
  std::stable_sort(rows.begin(), rows.end(), [](const LeaderRow &a, const LeaderRow &b) {
    return a.met > b.met;
  });
  return rows;
}

int socialMyRank() {
  int mine = scoreFor(rosterMyId(), metInRoster());
  int rank = 1;
  for (const auto &s : g_scores) {
    if (scoreFor(s.id, s.met) > mine) {
      rank++;
    }
  }
  return rank;
}

int socialKnownCount() { return (int)g_scores.size() + (rosterMyId().length() ? 1 : 0); }

bool socialHasAchievement(Achievement a) { return a < ACH_COUNT && (g_ach & (1u << a)); }

int socialAchievementCount() {
  int n = 0;
  for (int i = 0; i < ACH_COUNT; i++) {
    if (g_ach & (1u << i)) {
      n++;
    }
  }
  return n;
}

const char *socialAchievementTitle(Achievement a) { return a < ACH_COUNT ? kAch[a] : ""; }

bool socialStylesUnlocked() { return socialHasAchievement(ACH_MINGLER); }

static void announce(uint32_t fresh) {
  if (!fresh) {
    return;
  }
  int n = 0;
  int last = 0;
  for (int i = 0; i < ACH_COUNT; i++) {
    if (fresh & (1u << i)) {
      n++;
      last = i;
    }
  }
  char msg[40];
  if (n == 1 && last == ACH_MINGLER) {
    snprintf(msg, sizeof(msg), "Mingler: new styles unlocked");
  } else if (n == 1) {
    snprintf(msg, sizeof(msg), "Unlocked: %s", kAch[last]);
  } else {
    snprintf(msg, sizeof(msg), "%d achievements unlocked", n);
  }
  uiToast(msg, kAchToastMs);
}

void socialUnlock(Achievement a) {
  if (a >= ACH_COUNT || (g_ach & (1u << a))) {
    return;
  }
  g_ach |= 1u << a;
  saveFlags();
  announce(1u << a);
}

static void checkAchievements() {
  if (!rosterMe()) {
    return;
  }
  const int met = metInRoster();
  int geese = 0;
  for (const auto &id : settingsMet()) {
    const Attendee *a = rosterFind(id.c_str());
    if (a && a->goose) {
      geese++;
    }
  }
  const int others = (int)rosterCount() - 1;

  uint32_t want = 0;
  if (met >= 1) want |= 1u << ACH_FIRST;
  if (met >= 5) want |= 1u << ACH_MINGLER;
  if (met >= 25) want |= 1u << ACH_BUTTERFLY;
  if (met >= 50) want |= 1u << ACH_LEGEND;
  if (others > 0 && met >= others) want |= 1u << ACH_COMPLETIONIST;
  if (geese >= 5) want |= 1u << ACH_GOOSE;
  if (rosterMyId() == "klipper" || settingsHasMet("klipper")) want |= 1u << ACH_KLIPPER;
  // A top-3 spot only counts once there are a few scores to beat.
  if (met > 0 && socialKnownCount() >= 5 && socialMyRank() <= 3) want |= 1u << ACH_TOP3;
  // Inside jokes: point these at people at your own event (roster ids).
  if (settingsHasMet("hr-person")) want |= 1u << ACH_HR;
  if (settingsHasMet("sneaker-fan")) want |= 1u << ACH_FOOTWEAR;
  if (settingsHasMet("shepherd")) want |= 1u << ACH_SHEEP;
  if (g_bj_hands > 30) want |= 1u << ACH_GAMBLER;
  if (settingsHasMet("timekeeper1") && settingsHasMet("timekeeper2") && settingsHasMet("timekeeper3")) {
    want |= 1u << ACH_TIMEKEEPERS;
  }
  // Klippy Bird: a top-3 best once there are a few scores to beat.
  if (birdBest() > 0 && birdKnownCount() >= 5 && birdMyRank() <= 3) want |= 1u << ACH_BIRD_TOP3;
  // Doublemint: met two people who share a first name.
  {
    std::vector<String> firsts;
    for (const auto &id : settingsMet()) {
      const Attendee *a = rosterFind(id.c_str());
      if (!a) {
        continue;
      }
      String f = a->name;
      int sp = f.indexOf(' ');
      f = sp > 0 ? f.substring(0, sp) : f;
      f.toLowerCase();
      if (std::find(firsts.begin(), firsts.end(), f) != firsts.end()) {
        want |= 1u << ACH_DOUBLEMINT;
        break;
      }
      firsts.push_back(f);
    }
  }

  int page = settingsFacePage();
  if (page >= 0 && page < 32 && (page == 0 || socialStylesUnlocked()) &&
      !(g_faces & (1u << page))) {
    g_faces |= 1u << page;
    saveFlags();
  }
  const int faces = settingsFaceCount();
  uint32_t all_faces = faces >= 32 ? 0xFFFFFFFFu : ((1u << faces) - 1u);
  if (faces > 0 && (g_faces & all_faces) == all_faces) want |= 1u << ACH_FASHION;

  // House rules: the klipper badge (by roster ID, which only the USB station can set) gets
  // every achievement, and with Mingler, every nametag style.
  if (rosterMyId() == "klipper") {
    want = (1u << ACH_COUNT) - 1u;
  }

  uint32_t fresh = want & ~g_ach;
  if (fresh) {
    g_ach |= fresh;
    saveFlags();
    announce(fresh);
  }
}

void socialLoop() {
  uint32_t now = millis();
  if (now - g_last_check < kSocialCheckMs) {
    return;
  }
  g_last_check = now;
  drainPending();
  checkAchievements();
  if (g_scores_dirty && now - g_scores_saved_at >= kScoresSaveMs) {
    saveScores();
  }
}

void socialFlush() {
  drainPending();
  if (g_scores_dirty) {
    saveScores();
  }
}

void socialNoteBjHand(bool natural, bool full_table) {
  g_bj_hands++;
  saveFlags();
  if (natural) {
    socialUnlock(ACH_ACE_HOLE);
  }
  if (full_table) {
    socialUnlock(ACH_FULL_TABLE);
  }
}

void socialReset() {
  g_scores.clear();
  g_ach = 0;
  g_faces = 0;
  g_bj_hands = 0;
  Preferences prefs;
  prefs.begin("social", false);
  prefs.clear();
  prefs.end();
  g_scores_dirty = false;
}
