#include "radio.h"

#include "roster.h"
#include "settings.h"
#include "klippy_bird.h"
#include "social.h"

#include <WiFi.h>
#include <algorithm>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

static Peer g_peers[kMaxPeers];
static bool g_enabled = false;
static bool g_ready = false;
static uint32_t g_last_beacon = 0;
static void (*g_incoming)(const uint8_t *, const BadgeMsg &, int) = nullptr;
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

static uint32_t g_cooldowns[kMaxPeers];
static char g_cooldown_ids[kMaxPeers][kIdLen];

static uint8_t g_rssi_mac[kMaxPeers][6];
static int8_t g_rssi_val[kMaxPeers];
static uint8_t g_rssi_used[kMaxPeers];
static uint32_t g_rssi_at[kMaxPeers];

// Sticky nearby list: once the screen is full, don't shuffle in new badges
// until a sticky slot frees (timeout / already-met).
static char g_sticky_ids[kNearbyRows][kIdLen];
static uint8_t g_sticky_n = 0;

static void clearSticky() {
  g_sticky_n = 0;
  for (uint8_t i = 0; i < kNearbyRows; i++) {
    g_sticky_ids[i][0] = 0;
  }
}

static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// "I know you": a badge that has met you says so when it hears you, so Find Friends can mark
// people who already know you. Broadcast with the target's id, so no ESP-NOW peer slot is
// needed; older firmware drops the unknown magic.
static constexpr uint32_t kKnowMagic = 0x574F4E4B;  // "KNOW"
static constexpr uint32_t kKnowEveryMs = 20000;
struct __attribute__((packed)) KnowMsg {
  uint32_t magic;
  char from[kIdLen];
  char to[kIdLen];
};
static char g_knows[kMaxPeers][kIdLen];  // ids that told us they've met us
static uint8_t g_knows_next = 0;

static bool macEq(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6) == 0; }

static void copyCstr(char *dst, size_t n, const char *s) {
  strncpy(dst, s ? s : "", n - 1);
  dst[n - 1] = 0;
}

static int8_t rssiFromMac(const uint8_t *mac) {
  for (uint8_t i = 0; i < kMaxPeers; i++) {
    if (g_rssi_used[i] && macEq(g_rssi_mac[i], mac)) {
      return g_rssi_val[i];
    }
  }
  return -70;
}

static Peer *allocPeer(const uint8_t *mac) {
  Peer *oldest = nullptr;
  for (auto &p : g_peers) {
    if (p.used && macEq(p.mac, mac)) {
      return &p;
    }
  }
  // Always track a new badge (it may be asking us to hunt even if it isn't on our list);
  // radioNearby() decides which four to show. A full table drops the stalest entry.
  for (auto &p : g_peers) {
    if (!p.used) {
      p = Peer{};
      p.used = true;
      memcpy(p.mac, mac, 6);
      return &p;
    }
    if (!oldest || p.last_seen < oldest->last_seen) {
      oldest = &p;
    }
  }
  if (oldest) {
    *oldest = Peer{};
    oldest->used = true;
    memcpy(oldest->mac, mac, 6);
    return oldest;
  }
  return nullptr;
}

static void applyRssi(Peer *p, int rssi) {
  if (p->rssi == -90) {
    p->rssi = rssi;
  } else {
    p->rssi = (p->rssi * 3 + rssi) / 4;
  }
}

// Signal strength per sender, for ESP-NOW frames only: a vendor action frame (0xD0, category
// 127) with Espressif's OUI. Without the filter, access points and phones on the channel filled
// every slot within seconds and badges that showed up later read as a flat -70 dBm.
static void promiscuousRx(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) {
    return;
  }
  const auto *pkt = (wifi_promiscuous_pkt_t *)buf;
  const uint8_t *f = pkt->payload;
  if (pkt->rx_ctrl.sig_len < 28 || f[0] != 0xD0 || f[24] != 0x7F || f[25] != 0x18 ||
      f[26] != 0xFE || f[27] != 0x34) {
    return;
  }
  const uint8_t *mac = f + 10;
  int8_t rssi = pkt->rx_ctrl.rssi;
  uint32_t now = millis();
  int slot = -1;
  for (uint8_t i = 0; i < kMaxPeers; i++) {
    if (g_rssi_used[i] && macEq(g_rssi_mac[i], mac)) {
      g_rssi_val[i] = rssi;
      g_rssi_at[i] = now;
      return;
    }
    if (!g_rssi_used[i]) {
      if (slot < 0 || g_rssi_used[slot]) {
        slot = i;
      }
    } else if (slot < 0 || (g_rssi_used[slot] && g_rssi_at[i] < g_rssi_at[slot])) {
      slot = i;  // stalest so far, unless a free slot turns up
    }
  }
  if (slot >= 0) {
    g_rssi_used[slot] = 1;
    memcpy(g_rssi_mac[slot], mac, 6);
    g_rssi_val[slot] = rssi;
    g_rssi_at[slot] = now;
  }
}

static bool addPeer(const uint8_t *mac);

static uint32_t g_raw_magic = 0;
static void (*g_raw_cb)(const uint8_t *mac, const uint8_t *data, int len) = nullptr;

void radioOnRaw(uint32_t magic, void (*cb)(const uint8_t *mac, const uint8_t *data, int len)) {
  g_raw_magic = magic;
  g_raw_cb = cb;
}

int radioRssiForMac(const uint8_t *mac) { return mac ? rssiFromMac(mac) : -70; }

void radioSendRaw(const uint8_t *mac, const uint8_t *data, size_t len) {
  if (!g_ready || !mac || !data || len == 0 || len > 250) {
    return;
  }
  addPeer(mac);
  esp_now_send(mac, data, len);
}

static void noteKnowsMe(const char *id) {
  for (auto &k : g_knows) {
    if (strncmp(k, id, kIdLen) == 0) {
      return;
    }
  }
  strncpy(g_knows[g_knows_next], id, kIdLen - 1);
  g_knows[g_knows_next][kIdLen - 1] = 0;
  g_knows_next = (g_knows_next + 1) % kMaxPeers;
}

static void onRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (!mac || !data) {
    return;
  }
  if (len >= (int)sizeof(KnowMsg)) {
    uint32_t magic;
    memcpy(&magic, data, 4);
    if (magic == kKnowMagic) {
      KnowMsg k;
      memcpy(&k, data, sizeof(k));
      k.from[kIdLen - 1] = 0;
      k.to[kIdLen - 1] = 0;
      const String &mine = rosterMyId();
      if (k.from[0] && mine.length() && strncmp(mine.c_str(), k.to, kIdLen) == 0 &&
          strncmp(mine.c_str(), k.from, kIdLen) != 0) {
        portENTER_CRITICAL(&g_mux);
        noteKnowsMe(k.from);
        portEXIT_CRITICAL(&g_mux);
      }
      return;
    }
  }
  if (g_raw_cb && len >= 4) {
    uint32_t magic;
    memcpy(&magic, data, 4);
    if (magic == g_raw_magic) {
      g_raw_cb(mac, data, len);
      return;
    }
  }
  if (len < (int)sizeof(BadgeMsg)) {
    return;
  }
  BadgeMsg msg;
  memcpy(&msg, data, sizeof(msg));
  if (msg.magic != kMsgMagic || msg.version != kMsgVersion) {
    return;
  }
  msg.id[kIdLen - 1] = 0;
  msg.name[kNameLen - 1] = 0;
  msg.word[kWordLen - 1] = 0;
  if (!msg.id[0]) {
    return;
  }
  const String &mine = rosterMyId();
  if (mine.length() && strncmp(mine.c_str(), msg.id, kIdLen) == 0) {
    return;
  }
  socialNoteScore(msg.id, msg.pad);
  if (msg.type == MSG_BEACON && msg.word[0] == 'B') {
    birdNoteScore(msg.id, (uint16_t)atoi(msg.word + 1));
  }

  int rssi = -70;
  portENTER_CRITICAL(&g_mux);
  Peer *p = allocPeer(mac);
  if (p) {
    copyCstr(p->id, kIdLen, msg.id);
    copyCstr(p->name, kNameLen, msg.name);
    if (msg.type == MSG_BEACON) {
      p->color = msg.color % kColorCount;
    }
    p->last_seen = millis();
    applyRssi(p, rssiFromMac(mac));
    rssi = p->rssi;
  }
  portEXIT_CRITICAL(&g_mux);

  if (g_incoming && msg.type != MSG_BEACON) {
    g_incoming(mac, msg, rssi);
  }
}

static bool addPeer(const uint8_t *mac) {
  esp_now_peer_info_t info = {};
  memcpy(info.peer_addr, mac, 6);
  info.channel = 0;
  info.encrypt = false;
  info.ifidx = WIFI_IF_STA;
  if (esp_now_is_peer_exist(mac)) {
    return true;
  }
  return esp_now_add_peer(&info) == ESP_OK;
}

void radioStop() {
  g_enabled = false;
  if (g_ready) {
    esp_wifi_set_promiscuous(false);
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    g_ready = false;
  }
  // Drop the radio stack entirely so nametag/menu don't keep the modem warm.
  // Callers that need STA (scoreboard / WiFi scan) bring WiFi back up next.
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_OFF);
  portENTER_CRITICAL(&g_mux);
  for (auto &p : g_peers) {
    p.used = false;
  }
  for (uint8_t i = 0; i < kMaxPeers; i++) {
    g_rssi_used[i] = 0;
  }
  clearSticky();
  portEXIT_CRITICAL(&g_mux);
}

bool radioStart() {
  if (g_ready) {
    esp_wifi_set_channel(kWifiChannel, WIFI_SECOND_CHAN_NONE);
    return true;
  }
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  WiFi.setSleep(false);
  delay(50);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_channel(kWifiChannel, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println("esp-now init failed");
    return false;
  }
  esp_wifi_set_channel(kWifiChannel, WIFI_SECOND_CHAN_NONE);
  esp_now_register_recv_cb(onRecv);
  addPeer(kBroadcast);
  esp_wifi_set_promiscuous_rx_cb(promiscuousRx);
  esp_wifi_set_promiscuous(true);
  g_ready = true;
  uint8_t ch = 0;
  wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
  esp_wifi_get_channel(&ch, &second);
  Serial.printf("mac %s ch %u\n", WiFi.macAddress().c_str(), (unsigned)ch);
  return true;
}

bool radioBegin() { return radioStart(); }
bool radioReady() { return g_ready; }

void radioSetEnabled(bool on) { g_enabled = on && g_ready; }
bool radioEnabled() { return g_enabled; }

void radioSend(const uint8_t *mac, MsgType type, uint8_t color, const char *word) {
  const Attendee *me = rosterMe();
  if (!me) {
    return;
  }
  BadgeMsg msg = {};
  msg.magic = kMsgMagic;
  msg.version = kMsgVersion;
  msg.type = type;
  msg.color = (color == 255) ? settingsColor() : (color % kColorCount);
  msg.pad = socialBeaconScore();  // met count + 1; older firmware leaves this 0
  copyCstr(msg.id, kIdLen, me->id.c_str());
  copyCstr(msg.name, kNameLen, me->name.c_str());
  copyCstr(msg.word, kWordLen, word);
  addPeer(mac);
  esp_now_send(mac, reinterpret_cast<uint8_t *>(&msg), sizeof(msg));
}

// Beacons also carry this badge's Klippy Bird best in the (otherwise empty) word field.
void radioBroadcastBeacon() { radioSend(kBroadcast, MSG_BEACON, 255, birdBeaconWord()); }

void radioLoop() {
  uint32_t now = millis();
  if (g_enabled && now - g_last_beacon >= kBeaconMs) {
    g_last_beacon = now;
    radioBroadcastBeacon();
  }
  // Every kKnowEveryMs, tell each badge we can hear and have met that we know them.
  char due[kMaxPeers][kIdLen];
  uint8_t n_due = 0;
  portENTER_CRITICAL(&g_mux);
  for (auto &p : g_peers) {
    if (p.used && now - p.last_seen > kPeerTimeoutMs) {
      p.used = false;
    }
    if (g_ready && p.used && p.id[0] && (p.know_sent == 0 || now - p.know_sent >= kKnowEveryMs)) {
      p.know_sent = now | 1;
      memcpy(due[n_due++], p.id, kIdLen);
    }
  }
  portEXIT_CRITICAL(&g_mux);
  if (n_due && rosterMe()) {
    KnowMsg k = {};
    k.magic = kKnowMagic;
    copyCstr(k.from, kIdLen, rosterMyId().c_str());
    for (uint8_t i = 0; i < n_due; i++) {
      if (settingsHasMet(due[i])) {
        copyCstr(k.to, kIdLen, due[i]);
        radioSendRaw(kBroadcast, reinterpret_cast<const uint8_t *>(&k), sizeof(k));
      }
    }
  }
}

bool radioKnowsMe(const char *id) {
  if (!id || !id[0]) {
    return false;
  }
  bool hit = false;
  portENTER_CRITICAL(&g_mux);
  for (auto &k : g_knows) {
    if (strncmp(k, id, kIdLen) == 0) {
      hit = true;
      break;
    }
  }
  portEXIT_CRITICAL(&g_mux);
  return hit;
}

void radioNoteCooldown(const char *id, uint32_t until_ms) {
  if (!id) {
    return;
  }
  for (uint8_t i = 0; i < kMaxPeers; i++) {
    if (g_cooldown_ids[i][0] && strncmp(g_cooldown_ids[i], id, kIdLen) == 0) {
      g_cooldowns[i] = until_ms;
      return;
    }
  }
  for (uint8_t i = 0; i < kMaxPeers; i++) {
    if (!g_cooldown_ids[i][0]) {
      strncpy(g_cooldown_ids[i], id, kIdLen - 1);
      g_cooldowns[i] = until_ms;
      return;
    }
  }
}

uint32_t radioCooldownRemaining(const char *id) {
  uint32_t now = millis();
  for (uint8_t i = 0; i < kMaxPeers; i++) {
    if (g_cooldown_ids[i][0] && strncmp(g_cooldown_ids[i], id, kIdLen) == 0) {
      if (g_cooldowns[i] > now) {
        return g_cooldowns[i] - now;
      }
      g_cooldown_ids[i][0] = 0;
      return 0;
    }
  }
  return 0;
}

std::vector<Peer *> radioNearby(bool repick) {
  Peer *tmp[kMaxPeers];
  uint8_t n = 0;
  uint32_t now = millis();
  portENTER_CRITICAL(&g_mux);
  for (auto &p : g_peers) {
    if (p.used && now - p.last_seen <= kPeerTimeoutMs && p.id[0] && n < kMaxPeers) {
      tmp[n++] = &p;
    }
  }
  portEXIT_CRITICAL(&g_mux);

  std::vector<Peer *> eligible;
  eligible.reserve(n);
  for (uint8_t i = 0; i < n; i++) {
    if (!settingsHasMet(tmp[i]->id)) {
      eligible.push_back(tmp[i]);
    }
  }
  std::sort(eligible.begin(), eligible.end(), [](Peer *a, Peer *b) { return a->rssi > b->rssi; });

  // Every few seconds, drop sticky seats that fell out of the strongest set.
  static uint32_t last_repick = 0;
  if (repick && now - last_repick >= kNearbyRepickMs) {
    last_repick = now;
    uint8_t kept = 0;
    for (uint8_t i = 0; i < g_sticky_n; i++) {
      bool strong = false;
      for (size_t j = 0; j < eligible.size() && j < kNearbyRows; j++) {
        if (strncmp(eligible[j]->id, g_sticky_ids[i], kIdLen) == 0) {
          strong = true;
          break;
        }
      }
      if (strong) {
        if (kept != i) {
          memcpy(g_sticky_ids[kept], g_sticky_ids[i], kIdLen);
        }
        kept++;
      }
    }
    g_sticky_n = kept;
  }

  // Keep sticky badges that are still around; only fill empty seats.
  std::vector<Peer *> out;
  out.reserve(kNearbyRows);
  auto findEligible = [&](const char *id) -> Peer * {
    for (Peer *p : eligible) {
      if (strncmp(p->id, id, kIdLen) == 0) {
        return p;
      }
    }
    return nullptr;
  };

  for (uint8_t i = 0; i < g_sticky_n && out.size() < kNearbyRows; i++) {
    Peer *p = findEligible(g_sticky_ids[i]);
    if (p) {
      out.push_back(p);
    }
  }
  for (Peer *p : eligible) {
    if (out.size() >= kNearbyRows) {
      break;
    }
    bool already = false;
    for (Peer *o : out) {
      if (o == p) {
        already = true;
        break;
      }
    }
    if (!already) {
      out.push_back(p);
    }
  }

  g_sticky_n = 0;
  for (Peer *p : out) {
    if (g_sticky_n >= kNearbyRows) {
      break;
    }
    strncpy(g_sticky_ids[g_sticky_n], p->id, kIdLen - 1);
    g_sticky_ids[g_sticky_n][kIdLen - 1] = 0;
    g_sticky_n++;
  }
  return out;
}

Peer *radioFindById(const char *id) {
  for (auto &p : g_peers) {
    if (p.used && strncmp(p.id, id, kIdLen) == 0) {
      return &p;
    }
  }
  return nullptr;
}

Peer *radioFindByMac(const uint8_t *mac) {
  for (auto &p : g_peers) {
    if (p.used && macEq(p.mac, mac)) {
      return &p;
    }
  }
  return nullptr;
}

void radioOnIncoming(void (*cb)(const uint8_t *mac, const BadgeMsg &msg, int rssi)) {
  g_incoming = cb;
}
