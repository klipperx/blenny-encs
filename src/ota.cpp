#include "ota.h"

#include "battery.h"
#include "config.h"
#include "net.h"
#include "ota_pubkey.h"
#include "roster.h"
#include "schedule.h"
#include "settings.h"
#include "ui.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <mbedtls/aes.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/pk.h>
#include <vector>

#ifndef BLENNY_OTA_URL
#define BLENNY_OTA_URL "https://klipperx.github.io/blenny-ota/manifest.json"
#endif

static constexpr uint32_t kOtaHealthyMs = 30000;  // new image must run this long to stick
static constexpr int kOtaMinMv = 3800;            // on battery, skip updates below this
static constexpr size_t kOtaManifestCap = 4096;
static constexpr size_t kOtaSigCap = 160;
static constexpr size_t kOtaRosterCap = 256 * 1024;  // encrypted roster download limit
static constexpr uint32_t kOtaEveryMs = 10UL * 60UL * 1000UL;  // background check cadence
static constexpr uint32_t kOtaJoinMs = 20000;                   // give up joining after this

static String g_url;
static bool g_was_connected = false;
static bool g_checked = false;
static bool g_marked = false;
static uint32_t g_wake_due = 0;    // next background check (0 = as soon as allowed)
static uint32_t g_wake_at = 0;     // when we brought Wi-Fi up ourselves; 0 = not ours

// Hold the "valid" mark until the new image has run for a while (see otaLoop), so a build
// that crashes shortly after boot rolls back instead of only one that fails to start.
extern "C" bool verifyRollbackLater() { return true; }

const char *otaVariant() {
#if defined(BLENNY_SCOREBOARD)
  return "scoreboard";
#elif defined(TOUCH_MODULES_CST_SELF)
  return "cst816";
#else
  return "cst328";
#endif
}

String otaUrl() { return g_url; }

void otaSetUrl(const String &url) {
  Preferences prefs;
  prefs.begin("blenny", false);
  if (url.length()) {
    prefs.putString("ota_url", url);
  } else {
    prefs.remove("ota_url");
  }
  prefs.end();
  g_url = url.length() ? url : String(BLENNY_OTA_URL);
  otaCheckNow();
}

void otaCheckNow() {
  g_checked = false;
  g_wake_due = millis();  // also allow a background wake right away
}

void otaBegin() {
  Preferences prefs;
  prefs.begin("blenny", true);
  g_url = prefs.isKey("ota_url") ? prefs.getString("ota_url", "") : String("");
  prefs.end();
  if (!g_url.length()) {
    g_url = BLENNY_OTA_URL;
  }
  const esp_partition_t *run = esp_ota_get_running_partition();
  esp_ota_img_states_t st;
  if (run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
    Serial.println("ota: new image pending verify");
  } else {
    g_marked = true;  // nothing to confirm
  }
  Serial.printf("ota: fw %s variant %s url %s\n", kFirmwareVersion, otaVariant(),
                g_url.length() ? g_url.c_str() : "(none)");
}

static bool parseVersion(const char *s, int v[3]) {
  v[0] = v[1] = v[2] = 0;
  return s && sscanf(s, "%d.%d.%d", &v[0], &v[1], &v[2]) >= 2;
}

static bool isNewer(const char *remote, const char *local) {
  int r[3], l[3];
  if (!parseVersion(remote, r) || !parseVersion(local, l)) {
    return false;
  }
  for (int i = 0; i < 3; i++) {
    if (r[i] != l[i]) {
      return r[i] > l[i];
    }
  }
  return false;
}

static void toHex(const uint8_t *b, size_t n, char *out) {
  static const char kHex[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out[i * 2] = kHex[b[i] >> 4];
    out[i * 2 + 1] = kHex[b[i] & 15];
  }
  out[n * 2] = 0;
}

static bool verifySignature(const std::vector<uint8_t> &msg, const std::vector<uint8_t> &sig) {
  uint8_t hash[32];
  const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!md || mbedtls_md(md, msg.data(), msg.size(), hash) != 0) {
    return false;
  }
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  bool ok = mbedtls_pk_parse_public_key(&pk, kOtaPubKeyDer, sizeof(kOtaPubKeyDer)) == 0 &&
            mbedtls_pk_can_do(&pk, MBEDTLS_PK_ECDSA) &&
            mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), sig.data(),
                              sig.size()) == 0;
  mbedtls_pk_free(&pk);
  return ok;
}

static void showStep(const char *title, const char *detail, int pct) {
  uiDrawOta(title, detail, pct);
}

// Stream the image into the spare slot, hashing as it goes. Reboots on success.
static bool install(const char *url, size_t size, const char *want_sha, const char *version) {
  HTTPClient http;
  WiFiClientSecure secure;
  WiFiClient plain;
  bool https = strncmp(url, "https://", 8) == 0;
  bool begun = false;
  if (https) {
    // Integrity comes from the signed SHA-256, not the TLS session.
    secure.setInsecure();
    secure.setHandshakeTimeout(12);
    begun = http.begin(secure, url);
  } else {
    begun = http.begin(plain, url);
  }
  if (!begun) {
    return false;
  }
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  int code = http.GET();
  if (code != 200) {
    Serial.printf("ota: GET %d\n", code);
    http.end();
    return false;
  }
  int len = http.getSize();
  if (len > 0 && (size_t)len != size) {
    Serial.printf("ota: size %d != manifest %u\n", len, (unsigned)size);
    http.end();
    return false;
  }
  if (!Update.begin(size, U_FLASH)) {
    Serial.printf("ota: begin failed %s\n", Update.errorString());
    http.end();
    return false;
  }

  mbedtls_md_context_t md;
  mbedtls_md_init(&md);
  mbedtls_md_setup(&md, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
  mbedtls_md_starts(&md);

  WiFiClient *stream = http.getStreamPtr();
  static uint8_t buf[4096];
  size_t done = 0;
  uint32_t last_data = millis();
  int shown = -1;
  char detail[40];
  bool ok = true;
  while (done < size) {
    size_t avail = stream->available();
    if (!avail) {
      if (!http.connected() || millis() - last_data > 15000) {
        Serial.println("ota: stream stalled");
        ok = false;
        break;
      }
      delay(2);
      continue;
    }
    size_t want = min(avail, min(sizeof(buf), size - done));
    int n = stream->readBytes(buf, want);
    if (n <= 0) {
      continue;
    }
    last_data = millis();
    mbedtls_md_update(&md, buf, (size_t)n);
    if (Update.write(buf, (size_t)n) != (size_t)n) {
      Serial.printf("ota: write failed %s\n", Update.errorString());
      ok = false;
      break;
    }
    done += (size_t)n;
    int pct = (int)((uint64_t)done * 100 / size);
    if (pct != shown) {
      shown = pct;
      snprintf(detail, sizeof(detail), "v%s  %d%%", version, pct);
      showStep("Updating firmware", detail, pct);
    }
  }
  http.end();

  uint8_t hash[32];
  char hex[65];
  mbedtls_md_finish(&md, hash);
  mbedtls_md_free(&md);
  toHex(hash, sizeof(hash), hex);
  if (ok && strcasecmp(hex, want_sha) != 0) {
    Serial.printf("ota: sha256 mismatch %s\n", hex);
    ok = false;
  }
  if (!ok) {
    Update.abort();
    return false;
  }
  if (!Update.end(true)) {
    Serial.printf("ota: end failed %s\n", Update.errorString());
    return false;
  }
  Serial.printf("ota: installed v%s, rebooting\n", version);
  showStep("Update installed", "Restarting…", 100);
  delay(800);
  ESP.restart();
  return true;
}

// Fleet roster key (AES-256), pushed over USB at provisioning and kept only in NVS —
// never in the firmware image, which is public.
static bool rosterKey(uint8_t out[32]) {
  Preferences prefs;
  prefs.begin("blenny", true);
  size_t n = prefs.isKey("rkey") ? prefs.getBytes("rkey", out, 32) : 0;
  prefs.end();
  return n == 32;
}

bool otaHasRosterKey() {
  uint8_t k[32];
  bool ok = rosterKey(k);
  memset(k, 0, sizeof(k));
  return ok;
}

bool otaSetRosterKey(const String &hex) {
  if (hex.length() != 64) {
    return false;
  }
  uint8_t k[32];
  for (int i = 0; i < 32; i++) {
    char b[3] = {hex[i * 2], hex[i * 2 + 1], 0};
    char *end = nullptr;
    long v = strtol(b, &end, 16);
    if (!end || *end) {
      return false;
    }
    k[i] = (uint8_t)v;
  }
  Preferences prefs;
  prefs.begin("blenny", false);
  bool ok = prefs.putBytes("rkey", k, sizeof(k)) == sizeof(k);
  prefs.end();
  memset(k, 0, sizeof(k));
  return ok;
}

// roster.enc = "Salted__" || salt(8) || AES-256-CTR(attendees.json), key+IV from
// PBKDF2-HMAC-SHA256(key as lowercase hex, salt, 10000) — what `openssl enc -pbkdf2` writes.
// Integrity comes from the signed manifest's SHA-256 of roster.enc; the key only keeps
// names and facts private.
// Download a manifest entry {url,size,sha256} and decrypt it with the fleet roster key
// (openssl "Salted__" + PBKDF2-SHA256 + AES-256-CTR). Used for the roster and the schedule.
static bool fetchEncrypted(JsonObject r, const char *what, std::vector<uint8_t> *out) {
  const char *url = r["url"] | "";
  size_t size = r["size"] | 0;
  const char *sha = r["sha256"] | "";
  if (!url[0] || size <= 16 || strlen(sha) != 64) {
    return false;
  }
  uint8_t key[32];
  if (!rosterKey(key)) {
    Serial.printf("ota: %s update available but no roster key (provision over USB)\n", what);
    return false;
  }
  std::vector<uint8_t> enc;
  if (!netHttpGetBin(url, &enc, kOtaRosterCap) || enc.size() != size) {
    Serial.printf("ota: %s download failed\n", what);
    memset(key, 0, sizeof(key));
    return false;
  }
  uint8_t hash[32];
  char hex[65];
  mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), enc.data(), enc.size(), hash);
  toHex(hash, sizeof(hash), hex);
  if (strcasecmp(hex, sha) != 0) {
    Serial.printf("ota: %s sha256 mismatch\n", what);
    memset(key, 0, sizeof(key));
    return false;
  }
  if (memcmp(enc.data(), "Salted__", 8) != 0) {
    Serial.printf("ota: %s format unknown\n", what);
    memset(key, 0, sizeof(key));
    return false;
  }
  char pass[65];
  toHex(key, sizeof(key), pass);
  memset(key, 0, sizeof(key));
  uint8_t kiv[48];
  mbedtls_md_context_t md;
  mbedtls_md_init(&md);
  int rc = mbedtls_md_setup(&md, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  if (rc == 0) {
    rc = mbedtls_pkcs5_pbkdf2_hmac(&md, (const unsigned char *)pass, 64, enc.data() + 8, 8,
                                   10000, sizeof(kiv), kiv);
  }
  mbedtls_md_free(&md);
  memset(pass, 0, sizeof(pass));
  std::vector<uint8_t> &plain = *out;
  plain.assign(enc.size() - 16, 0);
  uint8_t nonce[16];
  uint8_t stream[16];
  size_t off = 0;
  memcpy(nonce, kiv + 32, 16);
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  if (rc == 0) {
    rc = mbedtls_aes_setkey_enc(&aes, kiv, 256);
  }
  if (rc == 0) {
    rc = mbedtls_aes_crypt_ctr(&aes, plain.size(), &off, nonce, stream, enc.data() + 16,
                               plain.data());
  }
  mbedtls_aes_free(&aes);
  memset(kiv, 0, sizeof(kiv));
  if (rc != 0) {
    Serial.printf("ota: %s decrypt failed\n", what);
    return false;
  }
  return true;
}

static void checkRoster(JsonObject r) {
  uint32_t version = r["version"] | 0u;
  if (!version) {
    return;
  }
  if (version <= rosterVersion()) {
    Serial.printf("ota: roster up to date (v%u)\n", (unsigned)rosterVersion());
    return;
  }
  std::vector<uint8_t> plain;
  if (!fetchEncrypted(r, "roster", &plain)) {
    return;
  }
  uint32_t was = rosterVersion();
  if (rosterReplace(plain.data(), plain.size())) {
    Serial.printf("ota: roster v%u -> v%u (%u people)\n", (unsigned)was,
                  (unsigned)rosterVersion(), (unsigned)rosterCount());
    char msg[40];
    snprintf(msg, sizeof(msg), "Roster updated: %u people", (unsigned)rosterCount());
    uiToast(msg, kRosterToastMs);
  }
}

static void checkSchedule(JsonObject r) {
  uint32_t version = r["version"] | 0u;
  if (!version || version <= scheduleVersion()) {
    return;
  }
  std::vector<uint8_t> plain;
  if (!fetchEncrypted(r, "schedule", &plain)) {
    return;
  }
  uint32_t was = scheduleVersion();
  if (scheduleReplace(plain.data(), plain.size())) {
    Serial.printf("ota: schedule v%u -> v%u\n", (unsigned)was, (unsigned)scheduleVersion());
    uiToast("Schedule updated", kRosterToastMs);
  }
}

static bool batteryOk() { return batteryUsb() || batteryMillivolts() >= kOtaMinMv; }

static void runCheck() {
  if (!g_url.length()) {
    return;
  }
  if (!batteryOk()) {
    Serial.printf("ota: battery %dmV, skipping check\n", batteryMillivolts());
    return;
  }
  std::vector<uint8_t> manifest;
  std::vector<uint8_t> sig;
  String sig_url = g_url + ".sig";
  if (!netHttpGetBin(g_url.c_str(), &manifest, kOtaManifestCap) ||
      !netHttpGetBin(sig_url.c_str(), &sig, kOtaSigCap)) {
    Serial.println("ota: manifest fetch failed");
    return;
  }
  if (!verifySignature(manifest, sig)) {
    Serial.println("ota: manifest signature INVALID, ignoring");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, (const char *)manifest.data(), manifest.size())) {
    Serial.println("ota: manifest json bad");
    return;
  }
  // Roster first: small, no reboot, and still applies when the firmware is current.
  if (doc["roster"].is<JsonObject>()) {
    checkRoster(doc["roster"].as<JsonObject>());
  }
  if (doc["schedule"].is<JsonObject>()) {
    checkSchedule(doc["schedule"].as<JsonObject>());
  }
  const char *version = doc["version"] | "";
  if (!isNewer(version, kFirmwareVersion)) {
    Serial.printf("ota: up to date (%s, latest %s)\n", kFirmwareVersion, version);
    return;
  }
  JsonObject b = doc["builds"][otaVariant()];
  const char *url = b["url"] | "";
  size_t size = b["size"] | 0;
  const char *sha = b["sha256"] | "";
  if (!url[0] || !size || strlen(sha) != 64) {
    Serial.printf("ota: no %s build in manifest\n", otaVariant());
    return;
  }
  Serial.printf("ota: %s -> %s (%u bytes)\n", kFirmwareVersion, version, (unsigned)size);
  char detail[40];
  snprintf(detail, sizeof(detail), "v%s  0%%", version);
  showStep("Updating firmware", detail, 0);
  if (!install(url, size, sha, version)) {
    showStep("Update failed", "Keeping current firmware", -1);
    delay(2000);
    uiNeedRedraw();
  }
}

static void releaseWifi(uint32_t now, bool disconnect) {
  if (disconnect) {
    netDisconnect();
  }
  g_wake_at = 0;
  g_wake_due = now + kOtaEveryMs;
}

void otaLoop(bool may_wake) {
  uint32_t now = millis();
  if (!g_marked && now >= kOtaHealthyMs) {
    g_marked = true;
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
      Serial.println("ota: new image marked healthy");
    }
  }

  // Background wake: bring Wi-Fi up just long enough to check, then drop it again.
  // Don't stack an update on top of a first boot that hasn't proven itself yet.
  if (g_wake_at) {
    if (!may_wake) {
      // A game or Wi-Fi screen took over; leave the radio to it.
      g_wake_at = 0;
      g_wake_due = now + kOtaEveryMs;
    } else if (netConnected()) {
      g_checked = true;
      runCheck();  // reboots if it installs
      releaseWifi(millis(), true);
      return;
    } else if (now - g_wake_at >= kOtaJoinMs) {
      Serial.println("ota: wifi join timed out");
      releaseWifi(now, true);
      return;
    }
  } else if (may_wake && g_marked && g_url.length() && settingsHasWifi() && !netConnected() &&
             (int32_t)(now - g_wake_due) >= 0) {
    if (batteryOk()) {
      Serial.println("ota: waking wifi to check for updates");
      g_wake_at = now ? now : 1;
      netConnect(settingsWifiSsid(), settingsWifiPass());
    } else {
      g_wake_due = now + kOtaEveryMs;
    }
    return;
  }

  // Wi-Fi brought up by a screen (Scoreboard, Spotify…): check once per connection.
  bool up = netConnected();
  if (up && !g_was_connected) {
    g_checked = false;
  }
  g_was_connected = up;
  if (up && !g_checked && g_marked && !g_wake_at) {
    g_checked = true;
    runCheck();
    g_wake_due = millis() + kOtaEveryMs;
  }
}
