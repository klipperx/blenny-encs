#pragma once

#include <Arduino.h>
#include <vector>

// Blackjack over ESP-NOW. The badge that starts a table is the house: it shuffles, deals,
// plays the dealer hand and broadcasts the whole table a few times a second. Its owner also
// sits in seat 0. Other badges only render the latest table and send their own actions.
// Packets use their own magic, so firmware without blackjack drops them.

static constexpr int kBjSeats = 4;
static constexpr int kBjMaxCards = 8;
static constexpr int kBjSplitCards = 6;  // second hand after a split
// BjSeat.flags
static constexpr uint8_t kBjSplit = 1;     // seat split its pair this hand
static constexpr uint8_t kBjOnSecond = 2;  // now playing the second hand
static constexpr uint8_t kBjDone2 = 4;     // second hand finished
static constexpr uint8_t kBjHidden = 0xFF;  // face-down card
static constexpr int kBjStartChips = 100;
static constexpr int kBjMinBet = 5;

enum BjPhase : uint8_t {
  BJ_BETTING = 0,  // short countdown to adjust bets; everyone seated is dealt in
  BJ_PLAYING,      // seats take turns: Hit / Stand
  BJ_DEALER,       // house reveals and draws to 17
  BJ_RESULTS,      // payouts shown, then back to betting
  BJ_CLOSED,       // dealer ended the table
};

enum BjResult : uint8_t {
  BJR_NONE = 0,
  BJR_WIN,
  BJR_LOSE,
  BJR_PUSH,
  BJR_BLACKJACK,
  BJR_BUST,
};

#pragma pack(push, 1)
struct BjSeat {
  uint8_t used;
  char id[16];
  char name[12];
  int16_t chips;
  uint8_t bet;
  uint8_t cards[kBjMaxCards];
  uint8_t n;
  uint8_t result;   // BjResult for the last hand (first hand after a split)
  uint8_t done;     // playing: first hand stood, bust, blackjack or sitting out this hand
  uint8_t last_seq; // last action sequence the house applied for this seat
  uint8_t cards2[kBjSplitCards];  // second hand after a split
  uint8_t n2;
  uint8_t result2;
  uint8_t flags;    // kBjSplit | kBjOnSecond | kBjDone2
};

struct BjTable {
  uint32_t table_id;
  uint8_t phase;
  int8_t turn;      // seat whose turn it is, or -1
  uint8_t timer_s;  // seconds left on the current turn, or until the next deal
  uint8_t dealer[kBjMaxCards];
  uint8_t dealer_n;
  char host[12];
  BjSeat seats[kBjSeats];
};
#pragma pack(pop)

struct BjListing {
  uint32_t table_id;
  char host[12];
  uint8_t seated;
  uint32_t seen_ms;
};

enum BjRole : uint8_t { BJ_NONE = 0, BJ_HOST, BJ_PLAYER };

/** Call once the ESP-NOW radio is up (entering the Blackjack screen). */
void bjOpen();
/** Leaving the Blackjack screen: ends a hosted table or leaves a joined one. */
void bjClose();
void bjLoop();

void bjStartTable();
void bjEndTable();
void bjJoin(uint32_t table_id);
void bjLeave();

BjRole bjRole();
/** Table as everyone may see it (house hole card hidden until the reveal). */
const BjTable &bjTable();
/** This badge's seat index, or -1. */
int bjMySeat();
/** Open tables heard recently, newest first. */
std::vector<BjListing> bjListings();

void bjBetDelta(int delta);
/** Bet this badge has chosen (sent to the house while betting is open). */
int bjMyBet();
void bjHit();
void bjStand();
void bjSplit();
/** Your turn, a same-rank pair, no split yet, and chips to match the bet. */
bool bjCanSplit();

/** Best total for a hand; *soft when an ace counts as 11. Hidden cards are skipped. */
int bjTotal(const uint8_t *cards, int n, bool *soft = nullptr);
/** True once since the last call when the table or lobby changed (redraw cue). */
bool bjChanged();
/** One-shot message for a toast (e.g. "Table closed"), or nullptr. */
const char *bjTakeNotice();
