#include "net.h"
#include "schedule.h"

#include "radio.h"
#include "settings.h"

#include <algorithm>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

static NetState g_state = NetState::Off;
static String g_ssid;
static std::vector<WifiRow> g_aps;
static uint32_t g_conn_at = 0;
static bool g_ntp = false;

static void prepareSta() {
  radioStop();
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
}

NetState netState() { return g_state; }
bool netConnected() { return g_state == NetState::Connected && WiFi.status() == WL_CONNECTED; }
bool netBusy() { return g_state == NetState::Connected || g_state == NetState::Connecting; }
const String &netSsid() { return g_ssid; }
const std::vector<WifiRow> &netAps() { return g_aps; }

String netStatusLine() {
  switch (g_state) {
    case NetState::Connected:
      return String("Connected  ") + (g_ssid.length() ? g_ssid : WiFi.SSID());
    case NetState::Connecting:
      return String("Connecting to ") + g_ssid + "...";
    case NetState::Failed:
      return g_ssid.length() ? (String("Couldn't join ") + g_ssid) : String("WiFi failed");
    case NetState::Scanning:
      return "Scanning...";
    default:
      if (settingsHasWifi()) {
        return String("Saved  ") + settingsWifiSsid();
      }
      return "WiFi off";
  }
}

void netScan() {
  prepareSta();
  g_state = NetState::Scanning;
  g_aps.clear();
  int n = WiFi.scanNetworks(false, true);
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    ssid.trim();
    if (!ssid.length()) {
      continue;
    }
    int rssi = WiFi.RSSI(i);
    bool open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    bool found = false;
    for (auto &row : g_aps) {
      if (row.ssid == ssid) {
        found = true;
        if (rssi > row.rssi) {
          row.rssi = rssi;
          row.open = open;
        }
        break;
      }
    }
    if (!found && g_aps.size() < kMaxWifiAps) {
      g_aps.push_back(WifiRow{ssid, rssi, open});
    }
  }
  WiFi.scanDelete();
  std::sort(g_aps.begin(), g_aps.end(),
            [](const WifiRow &a, const WifiRow &b) { return a.rssi > b.rssi; });
  // Scan done — don't leave STA idling until the user joins an AP.
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_OFF);
  g_state = NetState::Off;
}

void netConnect(const String &ssid, const String &pass) {
  prepareSta();
  g_ssid = ssid;
  g_state = NetState::Connecting;
  g_conn_at = millis();
  g_ntp = false;
  WiFi.setAutoReconnect(true);
  if (pass.length()) {
    WiFi.begin(ssid.c_str(), pass.c_str());
  } else {
    WiFi.begin(ssid.c_str());
  }
}

void netDisconnect() {
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  g_state = NetState::Off;
  g_ntp = false;
}

bool netLoop() {
  wl_status_t s = WiFi.status();
  if (g_state == NetState::Connected && s != WL_CONNECTED) {
    // Auto-reconnect is already on. Watch it, instead of latching Failed
    // while the stack is still joining.
    g_state = NetState::Connecting;
    g_conn_at = millis();
    g_ntp = false;
    Serial.println("wifi reconnecting");
    return true;
  }
  if (g_state == NetState::Failed && s == WL_CONNECTED) {
    g_state = NetState::Connecting;
    g_conn_at = millis();
  }
  if (g_state != NetState::Connecting) {
    return false;
  }
  if (s == WL_CONNECTED) {
    g_state = NetState::Connected;
    g_ssid = WiFi.SSID();
    if (!g_ntp) {
      configTzTime(scheduleTz(), "pool.ntp.org", "time.nist.gov");
      g_ntp = true;
    }
    Serial.printf("wifi %s %s\n", g_ssid.c_str(), WiFi.localIP().toString().c_str());
    return true;
  }
  if (s == WL_CONNECT_FAILED || s == WL_NO_SSID_AVAIL || millis() - g_conn_at > 20000) {
    g_state = NetState::Failed;
    Serial.println("wifi failed");
    return true;
  }
  return false;
}

static void configureHttp(HTTPClient &http, bool json) {
  http.setTimeout(15000);
  http.setUserAgent("Mozilla/5.0 (compatible; blenny-badge/1.0)");
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setRedirectLimit(4);
  if (json) {
    http.addHeader("Accept", "application/json");
  }
}

String httpsGet(const char *url) {
  if (!netConnected() || !url || !url[0]) {
    return "";
  }
  WiFiClientSecure *client = new WiFiClientSecure();
  if (!client) {
    return "";
  }
  client->setInsecure();
  client->setHandshakeTimeout(12);
  HTTPClient http;
  String body;
  if (http.begin(*client, url)) {
    configureHttp(http, true);
    int code = http.GET();
    if (code == 200) {
      body = http.getString();
    } else {
      Serial.printf("http %d %s\n", code, url);
    }
    http.end();
  }
  delete client;
  return body;
}

bool httpsGetBin(const char *url, std::vector<uint8_t> *out, size_t cap) {
  return netHttpGetBin(url, out, cap);
}

static bool urlIsHttps(const char *url) {
  return url && (strncmp(url, "https://", 8) == 0 || strncmp(url, "HTTPS://", 8) == 0);
}

String netHttpGet(const char *url, size_t cap) {
  if (!netConnected() || !url || !url[0]) {
    return "";
  }
  HTTPClient http;
  String body;
  bool begun = false;
  WiFiClientSecure *secure = nullptr;
  WiFiClient plain;
  if (urlIsHttps(url)) {
    secure = new WiFiClientSecure();
    if (!secure) {
      return "";
    }
    secure->setInsecure();
    secure->setHandshakeTimeout(12);
    begun = http.begin(*secure, url);
  } else {
    begun = http.begin(plain, url);
  }
  if (begun) {
    configureHttp(http, false);
    http.setTimeout(10000);
    int code = http.GET();
    if (code == 200) {
      body = http.getString();
      if (body.length() > cap) {
        body = body.substring(0, cap);
      }
    } else {
      Serial.printf("http %d %s\n", code, url);
    }
    http.end();
  }
  delete secure;
  return body;
}

bool netHttpGetBin(const char *url, std::vector<uint8_t> *out, size_t cap) {
  if (!out || !netConnected() || !url || !url[0]) {
    return false;
  }
  out->clear();
  HTTPClient http;
  bool begun = false;
  WiFiClientSecure *secure = nullptr;
  WiFiClient plain;
  if (urlIsHttps(url)) {
    secure = new WiFiClientSecure();
    if (!secure) {
      return false;
    }
    secure->setInsecure();
    secure->setHandshakeTimeout(12);
    begun = http.begin(*secure, url);
  } else {
    begun = http.begin(plain, url);
  }
  bool ok = false;
  if (begun) {
    configureHttp(http, false);
    http.setTimeout(10000);
    int code = http.GET();
    if (code == 200) {
      WiFiClient *stream = http.getStreamPtr();
      int remain = http.getSize();
      uint8_t tmp[256];
      uint32_t start = millis();
      while (http.connected() && (remain > 0 || remain == -1) && millis() - start < 8000) {
        size_t avail = stream->available();
        if (!avail) {
          delay(1);
          continue;
        }
        size_t chunk = avail < sizeof(tmp) ? avail : sizeof(tmp);
        int n = stream->readBytes(tmp, chunk);
        if (n <= 0) {
          break;
        }
        if (out->size() + (size_t)n > cap) {
          out->clear();
          break;
        }
        out->insert(out->end(), tmp, tmp + n);
        if (remain > 0) {
          remain -= n;
        }
      }
      ok = !out->empty();
    } else {
      Serial.printf("http %d %s\n", code, url);
    }
    http.end();
  }
  delete secure;
  return ok;
}
