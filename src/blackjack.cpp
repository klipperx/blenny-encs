#include "blackjack.h"

#include "radio.h"
#include "roster.h"
#include "settings.h"
#include "social.h"

#include <algorithm>
#include <esp_random.h>
#include <string.h>

static constexpr uint32_t kBjMagic = 0xB1AC4A4Bu;  // "BLACKJ"-ish; not kMsgMagic
static constexpr uint8_t kBjVersion = 2;  // 2: split hands in the seat layout
static constexpr uint32_t kStateEveryMs = 250;
static constexpr uint32_t kTurnMs = 20000;
static constexpr uint32_t kDealerStepMs = 700;
static constexpr uint32_t kResultsMs = 5000;
static constexpr uint32_t kBetMs = 6000;  // betting countdown before each auto deal
static constexpr uint32_t kSeatTimeoutMs = 10000;
static constexpr uint32_t kTableLostMs = 6000;
static constexpr uint32_t kListingMs = 3000;
static constexpr uint32_t kResendMs = 300;
static constexpr uint32_t kPingMs = 1000;
static constexpr uint32_t kJoinEveryMs = 500;
static constexpr int kMaxBet = 50;

enum Kind : uint8_t { K_STATE = 1, K_JOIN, K_LEAVE, K_ACT, K_PING };
enum Act : uint8_t { A_NONE = 0, A_BET, A_HIT, A_STAND, A_SPLIT };

#pragma pack(push, 1)
struct Header {
  uint32_t magic;
  uint8_t kind;
  uint8_t version;
};
struct StatePkt {
  Header h;
  BjTable table;
};
struct SeatPkt {  // JOIN / LEAVE / ACT / PING from a player
  Header h;
  uint32_t table_id;
  char id[16];
  char name[12];
  uint8_t act;
  uint8_t arg;
  uint8_t seq;
};
#pragma pack(pop)
static_assert(sizeof(StatePkt) <= 250, "blackjack state exceeds ESP-NOW payload");

static const uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ---- receive queue (filled on the Wi-Fi task, drained in bjLoop) ----
struct RxItem {
  uint8_t data[250];
  uint8_t len;
};
static RxItem g_rx[8];
static volatile uint8_t g_rx_head = 0;
static volatile uint8_t g_rx_tail = 0;
static portMUX_TYPE g_rx_mux = portMUX_INITIALIZER_UNLOCKED;

static void onRaw(const uint8_t * /*mac*/, const uint8_t *data, int len) {
  if (len < (int)sizeof(Header) || len > 250) {
    return;
  }
  portENTER_CRITICAL(&g_rx_mux);
  uint8_t next = (g_rx_head + 1) % 8;
  if (next != g_rx_tail) {
    memcpy(g_rx[g_rx_head].data, data, len);
    g_rx[g_rx_head].len = (uint8_t)len;
    g_rx_head = next;
  }
  portEXIT_CRITICAL(&g_rx_mux);
}

// ---- shared state ----
static BjRole g_role = BJ_NONE;
static BjTable g_view = {};  // what everyone sees
static bool g_changed = true;
static const char *g_notice = nullptr;
static std::vector<BjListing> g_listings;

// house
static BjTable g_real = {};
static uint8_t g_deck[52];
static uint8_t g_deck_pos = 0;
static uint32_t g_heard[kBjSeats] = {};
static uint32_t g_turn_at = 0;
static uint32_t g_phase_at = 0;
static uint32_t g_last_state = 0;

// player
static uint32_t g_join_id = 0;
static bool g_joined = false;
static uint32_t g_state_at = 0;
static uint32_t g_join_sent = 0;
static uint32_t g_ping_sent = 0;
static uint8_t g_seq = 0;
static uint8_t g_pend_act = A_NONE;
static uint8_t g_pend_arg = 0;
static uint8_t g_pend_seq = 0;
static uint32_t g_pend_sent = 0;

static int g_my_bet = 10;  // bet chosen before tapping Ready

static void copyName(char *dst, size_t n, const String &src) {
  strncpy(dst, src.c_str(), n - 1);
  dst[n - 1] = 0;
}

static String myName() {
  String n = settingsDisplayName();
  return n.length() ? n : rosterMyId();
}

int bjTotal(const uint8_t *cards, int n, bool *soft) {
  int total = 0;
  int aces = 0;
  for (int i = 0; i < n; i++) {
    if (cards[i] == kBjHidden) {
      continue;
    }
    int r = cards[i] % 13;  // 0 = ace, 9..12 = ten-value
    if (r == 0) {
      aces++;
      total += 11;
    } else {
      total += r >= 9 ? 10 : r + 1;
    }
  }
  while (total > 21 && aces > 0) {
    total -= 10;
    aces--;
  }
  if (soft) {
    *soft = aces > 0;
  }
  return total;
}

static void sendRaw(const void *pkt, size_t len) { radioSendRaw(kBroadcastMac, (const uint8_t *)pkt, len); }

static void sendSeat(uint8_t kind, uint8_t act = A_NONE, uint8_t arg = 0, uint8_t seq = 0) {
  SeatPkt p = {};
  p.h = {kBjMagic, kind, kBjVersion};
  p.table_id = g_join_id;
  copyName(p.id, sizeof(p.id), rosterMyId());
  copyName(p.name, sizeof(p.name), myName());
  p.act = act;
  p.arg = arg;
  p.seq = seq;
  sendRaw(&p, sizeof(p));
}

// ================= house =================

static void hostPublish() {
  g_view = g_real;
  // Hole card stays face down until the house plays its hand.
  if (g_real.phase == BJ_PLAYING && g_view.dealer_n >= 2) {
    g_view.dealer[1] = kBjHidden;
  }
  StatePkt p = {};
  p.h = {kBjMagic, K_STATE, kBjVersion};
  p.table = g_view;
  sendRaw(&p, sizeof(p));
  g_last_state = millis();
  g_changed = true;
}

static uint8_t drawCard() {
  if (g_deck_pos >= 52) {
    g_deck_pos = 0;  // never reached with 4 seats, but stay in bounds
  }
  return g_deck[g_deck_pos++];
}

static void shuffle() {
  for (int i = 0; i < 52; i++) {
    g_deck[i] = (uint8_t)i;
  }
  for (int i = 51; i > 0; i--) {
    int j = (int)(esp_random() % (uint32_t)(i + 1));
    uint8_t t = g_deck[i];
    g_deck[i] = g_deck[j];
    g_deck[j] = t;
  }
  g_deck_pos = 0;
}

// A seat is finished once its first hand (and, after a split, its second) is done.
static bool seatDone(const BjSeat &s) {
  return s.done && (!(s.flags & kBjSplit) || (s.flags & kBjDone2));
}

static bool isAce(uint8_t c) { return c % 13 == 0; }

// Standard rules: any pair of the same rank splits once (split aces take one card each).
static bool canSplit(const BjSeat &s) {
  return s.n == 2 && !(s.flags & kBjSplit) && !s.done && s.cards[0] % 13 == s.cards[1] % 13 &&
         s.chips >= 2 * (int)s.bet;
}

static int nextTurn(int from) {
  for (int i = from + 1; i < kBjSeats; i++) {
    const BjSeat &s = g_real.seats[i];
    if (s.used && !seatDone(s) && s.n > 0) {
      return i;
    }
  }
  return -1;
}

static void startDealer() {
  g_real.phase = BJ_DEALER;
  g_real.turn = -1;
  g_real.timer_s = 0;
  g_phase_at = millis();
}

static void advanceTurn() {
  g_real.turn = (int8_t)nextTurn(g_real.turn);
  g_turn_at = millis();
  if (g_real.turn < 0) {
    startDealer();
  }
}

static void deal() {
  shuffle();
  g_real.dealer_n = 0;
  for (auto &s : g_real.seats) {
    s.n = 0;
    s.n2 = 0;
    s.flags = 0;
    s.result = BJR_NONE;
    s.result2 = BJR_NONE;
    s.done = s.used ? 0 : 1;
  }
  for (int round = 0; round < 2; round++) {
    for (auto &s : g_real.seats) {
      if (s.used) {
        s.cards[s.n++] = drawCard();
      }
    }
    g_real.dealer[g_real.dealer_n++] = drawCard();
  }
  for (auto &s : g_real.seats) {
    if (s.used && bjTotal(s.cards, s.n) == 21) {
      s.done = 1;  // natural: nothing to decide
    }
  }
  g_real.phase = BJ_PLAYING;
  g_real.turn = -1;
  advanceTurn();
}

static void resolve() {
  int d = bjTotal(g_real.dealer, g_real.dealer_n);
  bool dnat = g_real.dealer_n == 2 && d == 21;
  // One hand against the house; a split hand's 21 is never a natural.
  auto settle = [&](BjSeat &s, const uint8_t *cards, int n, bool can_natural) -> uint8_t {
    int p = bjTotal(cards, n);
    bool nat = can_natural && n == 2 && p == 21;
    if (p > 21) {
      s.chips -= s.bet;
      return BJR_BUST;
    }
    if (nat && !dnat) {
      s.chips += (s.bet * 3) / 2;
      return BJR_BLACKJACK;
    }
    if (dnat && !nat) {
      s.chips -= s.bet;
      return BJR_LOSE;
    }
    if (d > 21 || p > d) {
      s.chips += s.bet;
      return BJR_WIN;
    }
    if (p == d) {
      return BJR_PUSH;
    }
    s.chips -= s.bet;
    return BJR_LOSE;
  };
  for (auto &s : g_real.seats) {
    if (!s.used || s.n == 0) {
      continue;
    }
    bool split = s.flags & kBjSplit;
    s.result = settle(s, s.cards, s.n, !split);
    if (split) {
      s.result2 = settle(s, s.cards2, s.n2, false);
    }
  }
  g_real.phase = BJ_RESULTS;
  g_phase_at = millis();
}

static void backToBetting() {
  g_real.phase = BJ_BETTING;
  g_phase_at = millis();
  g_real.turn = -1;
  g_real.timer_s = 0;
  g_real.dealer_n = 0;
  for (auto &s : g_real.seats) {
    if (!s.used) {
      continue;
    }
    s.n = 0;
    s.n2 = 0;
    s.flags = 0;
    s.done = 0;
    if (s.chips < kBjMinBet) {
      s.chips = kBjStartChips;  // play money: broke players get a fresh stack
    }
    if (s.bet > s.chips) {
      s.bet = (uint8_t)s.chips;
    }
  }
}

static int clampBet(int bet, int chips) {
  bet = bet < kBjMinBet ? kBjMinBet : (bet > kMaxBet ? kMaxBet : bet);
  return bet > chips ? chips : bet;
}

// The hand being played is finished: move to the second hand of a split, or to the next seat.
static void finishHand(int seat) {
  BjSeat &s = g_real.seats[seat];
  if (!(s.flags & kBjOnSecond)) {
    s.done = 1;
    if (s.flags & kBjSplit) {
      s.flags |= kBjOnSecond;
      g_turn_at = millis();  // fresh timer for the second hand
      if (bjTotal(s.cards2, s.n2) == 21) {
        s.flags |= kBjDone2;
        advanceTurn();
      }
      return;
    }
  } else {
    s.flags |= kBjDone2;
  }
  advanceTurn();
}

static void hostApply(int seat, uint8_t act, uint8_t arg) {
  BjSeat &s = g_real.seats[seat];
  if (!s.used) {
    return;
  }
  if (g_real.phase == BJ_BETTING && act == A_BET) {
    s.bet = (uint8_t)clampBet(arg, s.chips);
  } else if (g_real.phase == BJ_PLAYING && g_real.turn == seat && !seatDone(s)) {
    bool second = s.flags & kBjOnSecond;
    if (act == A_SPLIT && canSplit(s)) {
      bool aces = isAce(s.cards[0]);
      s.cards2[0] = s.cards[1];
      s.cards[1] = drawCard();
      s.cards2[1] = drawCard();
      s.n2 = 2;
      s.flags |= kBjSplit;
      g_turn_at = millis();
      if (aces) {
        // Split aces: one card each, then stand.
        s.done = 1;
        s.flags |= kBjDone2;
        advanceTurn();
        return;
      }
      if (bjTotal(s.cards, s.n) == 21) {
        s.done = 1;
        s.flags |= kBjOnSecond;
      }
      if (bjTotal(s.cards2, s.n2) == 21 && s.done) {
        s.flags |= kBjDone2;
        advanceTurn();
      }
    } else if (act == A_HIT) {
      uint8_t *cards = second ? s.cards2 : s.cards;
      uint8_t &n = second ? s.n2 : s.n;
      int cap = second ? kBjSplitCards : kBjMaxCards;
      if (n < cap) {
        cards[n++] = drawCard();
      }
      if (bjTotal(cards, n) >= 21 || n >= cap) {
        finishHand(seat);
      }
    } else if (act == A_STAND) {
      finishHand(seat);
    }
  }
}

static int seatById(const BjTable &t, const char *id) {
  for (int i = 0; i < kBjSeats; i++) {
    if (t.seats[i].used && strncmp(t.seats[i].id, id, sizeof(t.seats[i].id)) == 0) {
      return i;
    }
  }
  return -1;
}

static void hostHandle(const uint8_t *data, int len) {
  const Header *h = (const Header *)data;
  if (h->kind == K_STATE || len < (int)sizeof(SeatPkt)) {
    return;
  }
  SeatPkt p;
  memcpy(&p, data, sizeof(p));
  if (p.table_id != g_real.table_id) {
    return;
  }
  p.id[sizeof(p.id) - 1] = 0;
  p.name[sizeof(p.name) - 1] = 0;
  int seat = seatById(g_real, p.id);
  if (h->kind == K_JOIN && seat < 0) {
    for (int i = 1; i < kBjSeats; i++) {
      BjSeat &s = g_real.seats[i];
      if (!s.used) {
        memset(&s, 0, sizeof(s));
        s.used = 1;
        strncpy(s.id, p.id, sizeof(s.id) - 1);
        strncpy(s.name, p.name, sizeof(s.name) - 1);
        s.chips = kBjStartChips;
        s.bet = 10;
        s.done = 1;  // joins mid-hand: sits out until the next deal
        seat = i;
        break;
      }
    }
  }
  if (seat < 0) {
    return;  // table full or unknown player
  }
  g_heard[seat] = millis();
  if (h->kind == K_LEAVE) {
    memset(&g_real.seats[seat], 0, sizeof(BjSeat));
    if (g_real.turn == seat) {
      advanceTurn();
    }
  } else if (h->kind == K_ACT && p.seq && p.seq != g_real.seats[seat].last_seq) {
    g_real.seats[seat].last_seq = p.seq;
    hostApply(seat, p.act, p.arg);
  }
  hostPublish();
}

static void hostLoop() {
  uint32_t now = millis();
  bool dirty = false;
  for (int i = 1; i < kBjSeats; i++) {
    if (g_real.seats[i].used && now - g_heard[i] > kSeatTimeoutMs) {
      memset(&g_real.seats[i], 0, sizeof(BjSeat));
      if (g_real.turn == i) {
        advanceTurn();
      }
      dirty = true;
    }
  }
  switch (g_real.phase) {
    case BJ_BETTING: {
      // Everyone seated is dealt in automatically when the countdown ends.
      uint32_t left = now - g_phase_at >= kBetMs ? 0 : kBetMs - (now - g_phase_at);
      uint8_t secs = (uint8_t)((left + 999) / 1000);
      if (secs != g_real.timer_s) {
        g_real.timer_s = secs;
        dirty = true;
      }
      if (left == 0) {
        deal();
        dirty = true;
      }
      break;
    }
    case BJ_PLAYING: {
      uint32_t left = now - g_turn_at >= kTurnMs ? 0 : kTurnMs - (now - g_turn_at);
      uint8_t secs = (uint8_t)((left + 999) / 1000);
      if (secs != g_real.timer_s) {
        g_real.timer_s = secs;
        dirty = true;
      }
      if (left == 0 && g_real.turn >= 0) {
        finishHand(g_real.turn);  // out of time: stand

        dirty = true;
      }
      break;
    }
    case BJ_DEALER:
      if (now - g_phase_at >= kDealerStepMs) {
        g_phase_at = now;
        bool live = false;
        for (const auto &s : g_real.seats) {
          live = live || (s.used && s.n > 0 && bjTotal(s.cards, s.n) <= 21) ||
                 (s.used && (s.flags & kBjSplit) && bjTotal(s.cards2, s.n2) <= 21);
        }
        if (live && bjTotal(g_real.dealer, g_real.dealer_n) < 17 &&
            g_real.dealer_n < kBjMaxCards) {
          g_real.dealer[g_real.dealer_n++] = drawCard();
        } else {
          resolve();
        }
        dirty = true;
      }
      break;
    case BJ_RESULTS:
      if (now - g_phase_at >= kResultsMs) {
        backToBetting();
        dirty = true;
      }
      break;
    default:
      break;
  }
  if (dirty || now - g_last_state >= kStateEveryMs) {
    hostPublish();
  }
}

void bjStartTable() {
  memset(&g_real, 0, sizeof(g_real));
  g_real.table_id = esp_random() | 1u;
  g_real.phase = BJ_BETTING;
  g_real.turn = -1;
  g_phase_at = millis();
  copyName(g_real.host, sizeof(g_real.host), myName());
  BjSeat &me = g_real.seats[0];
  me.used = 1;
  copyName(me.id, sizeof(me.id), rosterMyId());
  copyName(me.name, sizeof(me.name), myName());
  me.chips = kBjStartChips;
  me.bet = 10;
  g_my_bet = 10;
  g_role = BJ_HOST;
  hostPublish();
}

void bjEndTable() {
  if (g_role != BJ_HOST) {
    return;
  }
  g_real.phase = BJ_CLOSED;
  for (int i = 0; i < 4; i++) {
    hostPublish();
    delay(15);
  }
  g_role = BJ_NONE;
  memset(&g_view, 0, sizeof(g_view));
  g_changed = true;
}

// ================= player =================

void bjJoin(uint32_t table_id) {
  if (g_role != BJ_NONE) {
    return;
  }
  memset(&g_view, 0, sizeof(g_view));
  g_join_id = table_id;
  g_joined = false;
  g_role = BJ_PLAYER;
  g_state_at = millis();
  g_join_sent = 0;
  g_pend_act = A_NONE;
  g_my_bet = 10;
  g_changed = true;
}

void bjLeave() {
  if (g_role != BJ_PLAYER) {
    return;
  }
  for (int i = 0; i < 3; i++) {
    sendSeat(K_LEAVE);
    delay(15);
  }
  g_role = BJ_NONE;
  memset(&g_view, 0, sizeof(g_view));
  g_changed = true;
}

static void playerHandleState(const BjTable &t) {
  if (t.table_id != g_join_id) {
    return;
  }
  g_view = t;
  g_state_at = millis();
  g_changed = true;
  if (t.phase == BJ_CLOSED) {
    g_notice = "Table closed";
    g_role = BJ_NONE;
    return;
  }
  int seat = seatById(t, rosterMyId().c_str());
  if (seat >= 0) {
    g_joined = true;
    if (g_pend_act != A_NONE && t.seats[seat].last_seq == g_pend_seq) {
      g_pend_act = A_NONE;  // the house applied it
    }
  } else if (g_joined) {
    g_notice = "Removed from table";
    g_role = BJ_NONE;
  }
}

static void sendAction(uint8_t act, uint8_t arg) {
  if (g_role == BJ_HOST) {
    hostApply(0, act, arg);
    hostPublish();
    return;
  }
  if (g_role != BJ_PLAYER) {
    return;
  }
  if (++g_seq == 0) {
    g_seq = 1;
  }
  g_pend_act = act;
  g_pend_arg = arg;
  g_pend_seq = g_seq;
  g_pend_sent = millis();
  sendSeat(K_ACT, act, arg, g_seq);
}

static void playerLoop() {
  uint32_t now = millis();
  if (now - g_state_at > kTableLostMs) {
    g_notice = g_joined ? "Lost the table" : "Couldn't reach that table";
    g_role = BJ_NONE;
    g_changed = true;
    return;
  }
  if (!g_joined) {
    if (now - g_join_sent >= kJoinEveryMs) {
      g_join_sent = now;
      sendSeat(K_JOIN);
    }
    return;
  }
  if (g_pend_act != A_NONE && now - g_pend_sent >= kResendMs) {
    g_pend_sent = now;
    sendSeat(K_ACT, g_pend_act, g_pend_arg, g_pend_seq);
  } else if (now - g_ping_sent >= kPingMs) {
    g_ping_sent = now;
    sendSeat(K_PING);
  }
}

// ================= common =================

static void noteListing(const BjTable &t) {
  uint32_t now = millis();
  if (t.phase == BJ_CLOSED) {
    for (size_t i = 0; i < g_listings.size(); i++) {
      if (g_listings[i].table_id == t.table_id) {
        g_listings.erase(g_listings.begin() + i);
        g_changed = true;
        break;
      }
    }
    return;
  }
  uint8_t seated = 0;
  for (const auto &s : t.seats) {
    seated += s.used ? 1 : 0;
  }
  for (auto &l : g_listings) {
    if (l.table_id == t.table_id) {
      if (l.seated != seated) {
        g_changed = true;
      }
      l.seated = seated;
      l.seen_ms = now;
      return;
    }
  }
  BjListing l = {};
  l.table_id = t.table_id;
  strncpy(l.host, t.host, sizeof(l.host) - 1);
  l.seated = seated;
  l.seen_ms = now;
  g_listings.push_back(l);
  g_changed = true;
}

void bjOpen() {
  radioOnRaw(kBjMagic, onRaw);
  g_listings.clear();
  g_changed = true;
}

void bjClose() {
  if (g_role == BJ_HOST) {
    bjEndTable();
  } else if (g_role == BJ_PLAYER) {
    bjLeave();
  }
  radioOnRaw(kBjMagic, nullptr);
  g_listings.clear();
}

void bjLoop() {
  // Drain packets.
  while (true) {
    RxItem item;
    bool got = false;
    portENTER_CRITICAL(&g_rx_mux);
    if (g_rx_tail != g_rx_head) {
      item = g_rx[g_rx_tail];
      g_rx_tail = (g_rx_tail + 1) % 8;
      got = true;
    }
    portEXIT_CRITICAL(&g_rx_mux);
    if (!got) {
      break;
    }
    const Header *h = (const Header *)item.data;
    if (h->magic != kBjMagic || h->version != kBjVersion) {
      continue;
    }
    if (h->kind == K_STATE && item.len >= sizeof(StatePkt)) {
      BjTable t;
      memcpy(&t, item.data + sizeof(Header), sizeof(t));
      t.host[sizeof(t.host) - 1] = 0;
      if (!(g_role == BJ_HOST && t.table_id == g_real.table_id)) {
        noteListing(t);
      }
      if (g_role == BJ_PLAYER) {
        playerHandleState(t);
      }
    } else if (g_role == BJ_HOST) {
      hostHandle(item.data, item.len);
    }
  }

  // Each hand dealt to this badge counts once (achievements): armed while betting, counted
  // when our seat first shows cards.
  static bool armed = true;
  if (g_role == BJ_NONE || g_view.phase == BJ_BETTING) {
    armed = true;
  } else if (armed) {
    int me = bjMySeat();
    if (me >= 0 && g_view.seats[me].n >= 2) {
      armed = false;
      const BjSeat &s = g_view.seats[me];
      int seated = 0;
      for (const auto &x : g_view.seats) {
        seated += x.used ? 1 : 0;
      }
      bool natural = !(s.flags & kBjSplit) && bjTotal(s.cards, 2) == 21;
      socialNoteBjHand(natural, seated >= kBjSeats);
    }
  }

  uint32_t now = millis();
  for (size_t i = 0; i < g_listings.size();) {
    if (now - g_listings[i].seen_ms > kListingMs) {
      g_listings.erase(g_listings.begin() + i);
      g_changed = true;
    } else {
      i++;
    }
  }

  if (g_role == BJ_HOST) {
    hostLoop();
  } else if (g_role == BJ_PLAYER) {
    playerLoop();
  }
}

BjRole bjRole() { return g_role; }
const BjTable &bjTable() { return g_view; }

int bjMySeat() {
  if (g_role == BJ_NONE) {
    return -1;
  }
  return seatById(g_view, rosterMyId().c_str());
}

std::vector<BjListing> bjListings() {
  std::vector<BjListing> out = g_listings;
  std::sort(out.begin(), out.end(),
            [](const BjListing &a, const BjListing &b) { return a.seen_ms > b.seen_ms; });
  return out;
}

void bjBetDelta(int delta) {
  int seat = bjMySeat();
  int chips = seat >= 0 ? g_view.seats[seat].chips : kBjStartChips;
  g_my_bet = clampBet(g_my_bet + delta, chips);
  g_changed = true;
  if (g_view.phase == BJ_BETTING) {
    sendAction(A_BET, (uint8_t)g_my_bet);  // absolute amount: the latest one wins
  }
}

int bjMyBet() {
  int seat = bjMySeat();
  if (seat >= 0 && g_my_bet > g_view.seats[seat].chips) {
    g_my_bet = clampBet(g_my_bet, g_view.seats[seat].chips);  // stack shrank below the bet
  }
  return g_my_bet;
}

void bjHit() { sendAction(A_HIT, 0); }
void bjStand() { sendAction(A_STAND, 0); }
void bjSplit() { sendAction(A_SPLIT, 0); }

bool bjCanSplit() {
  int me = bjMySeat();
  return me >= 0 && g_view.phase == BJ_PLAYING && g_view.turn == me && canSplit(g_view.seats[me]);
}

bool bjChanged() {
  bool c = g_changed;
  g_changed = false;
  return c;
}

const char *bjTakeNotice() {
  const char *n = g_notice;
  g_notice = nullptr;
  return n;
}
