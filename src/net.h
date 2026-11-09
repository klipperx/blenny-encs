#pragma once

#include "config.h"

#include <Arduino.h>
#include <vector>

enum class NetState : uint8_t { Off, Scanning, Connecting, Connected, Failed };

struct WifiRow {
  String ssid;
  int rssi = 0;
  bool open = false;
  WifiRow() = default;
  WifiRow(const String &s, int r, bool o) : ssid(s), rssi(r), open(o) {}
};

NetState netState();
bool netConnected();
bool netBusy();
String netStatusLine();
const String &netSsid();
const std::vector<WifiRow> &netAps();

void netScan();
void netConnect(const String &ssid, const String &pass);
void netDisconnect();
bool netLoop();
String httpsGet(const char *url);
bool httpsGetBin(const char *url, std::vector<uint8_t> *out, size_t cap = 12288);
/** HTTP or HTTPS GET (LAN album art / UPnP device XML). */
String netHttpGet(const char *url, size_t cap = 49152);
bool netHttpGetBin(const char *url, std::vector<uint8_t> *out, size_t cap = 24576);
