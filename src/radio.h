#pragma once

#include "config.h"

#include <vector>

struct Peer {
  bool used = false;
  uint8_t mac[6] = {};
  char id[kIdLen] = {};
  char name[kNameLen] = {};
  uint8_t color = 0;
  int rssi = -90;
  uint32_t last_seen = 0;
  uint32_t know_sent = 0;  // last "I know you" sent to this badge (0 = never)
};

bool radioBegin();
bool radioStart();
void radioStop();
bool radioReady();
void radioLoop();
void radioSetEnabled(bool on);
bool radioEnabled();
void radioSend(const uint8_t *mac, MsgType type, uint8_t color = 255, const char *word = nullptr);
void radioBroadcastBeacon();
void radioNoteCooldown(const char *id, uint32_t until_ms);
uint32_t radioCooldownRemaining(const char *id);
/**
 * Up to kNearbyRows unmet badges, strongest first. Rows are sticky between re-picks; with
 * `repick`, every kNearbyRepickMs the set is re-chosen as the current strongest (rows that
 * stay in the top set keep their position). Taps pass false so the drawn rows don't shift.
 */
std::vector<Peer *> radioNearby(bool repick = false);
Peer *radioFindById(const char *id);
/** True once the badge with this id has told us it has met us (kept until reboot). */
bool radioKnowsMe(const char *id);
Peer *radioFindByMac(const uint8_t *mac);
void radioOnIncoming(void (*cb)(const uint8_t *mac, const BadgeMsg &msg, int rssi));
/** Latest signal strength heard from this MAC (dBm; -70 if unknown). */
int radioRssiForMac(const uint8_t *mac);
/** Send a raw ESP-NOW frame (radio must be up). */
void radioSendRaw(const uint8_t *mac, const uint8_t *data, size_t len);
/** Frames starting with this 32-bit magic go to `cb` instead of the badge protocol. */
void radioOnRaw(uint32_t magic, void (*cb)(const uint8_t *mac, const uint8_t *data, int len));
