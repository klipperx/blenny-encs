#include "spotify.h"

#include "config.h"
#include "net.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <ctype.h>
#include <vector>

static String g_client_id;
static String g_refresh;
static String g_access;
static uint32_t g_access_exp_ms = 0;
static String g_last_err;
static uint32_t g_backoff_until_ms = 0;
static uint32_t g_backoff_ms = 1000;

static void setErr(const char *msg) { g_last_err = msg ? msg : ""; }

static void clearErr() { g_last_err = ""; }

static String urlEncode(const String &s) {
  String out;
  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

static void saveCreds() {
  Preferences prefs;
  prefs.begin("blenny", false);
  prefs.putString("spot_cid", g_client_id);
  prefs.putString("spot_rt", g_refresh);
  prefs.end();
}

void spotifyLoad() {
  Preferences prefs;
  prefs.begin("blenny", true);
  g_client_id = prefs.getString("spot_cid", "");
  g_refresh = prefs.getString("spot_rt", "");
  prefs.end();
  if (!g_client_id.length() && SPOTIFY_CLIENT_ID[0]) {
    g_client_id = SPOTIFY_CLIENT_ID;
  }
}

bool spotifyHasClientId() { return g_client_id.length() > 0; }
bool spotifyHasRefresh() { return g_refresh.length() > 0; }
bool spotifyAuthed() { return spotifyHasClientId() && spotifyHasRefresh(); }
const String &spotifyClientId() { return g_client_id; }
const char *spotifyLastError() { return g_last_err.c_str(); }

void spotifySetClientId(const String &id) {
  g_client_id = id;
  g_client_id.trim();
  saveCreds();
}

void spotifySetRefreshToken(const String &token) {
  g_refresh = token;
  g_refresh.trim();
  g_access = "";
  g_access_exp_ms = 0;
  g_backoff_until_ms = 0;
  g_backoff_ms = 1000;
  clearErr();
  saveCreds();
}

void spotifyClearAuth() {
  g_refresh = "";
  g_access = "";
  g_access_exp_ms = 0;
  g_backoff_until_ms = 0;
  g_backoff_ms = 1000;
  clearErr();
  saveCreds();
}

static bool inBackoff() {
  return g_backoff_until_ms && (int32_t)(millis() - g_backoff_until_ms) < 0;
}

static void noteRateLimit(HTTPClient &http) {
  String ra = http.header("Retry-After");
  uint32_t wait_ms = g_backoff_ms;
  if (ra.length()) {
    // Spotify may send seconds or an HTTP-date; treat as seconds when numeric.
    bool numeric = true;
    for (size_t i = 0; i < ra.length(); i++) {
      if (!isdigit((int)ra[i])) {
        numeric = false;
        break;
      }
    }
    if (numeric) {
      wait_ms = (uint32_t)ra.toInt() * 1000u;
      if (wait_ms < 1000) {
        wait_ms = 1000;
      }
    }
  }
  g_backoff_until_ms = millis() + wait_ms;
  if (g_backoff_ms < 60000) {
    g_backoff_ms = g_backoff_ms * 2;
    if (g_backoff_ms > 60000) {
      g_backoff_ms = 60000;
    }
  }
  setErr("Rate limited — wait");
  Serial.printf("spotify 429 backoff %u ms\n", (unsigned)wait_ms);
}

static void noteSuccess() {
  g_backoff_ms = 1000;
  g_backoff_until_ms = 0;
}

static String parseApiError(const String &body, int code) {
  JsonDocument doc;
  if (!deserializeJson(doc, body)) {
    const char *msg = doc["error"]["message"];
    if (msg && msg[0]) {
      return msg;
    }
    const char *err = doc["error"];
    if (err && err[0] && !doc["error"].is<JsonObject>()) {
      return err;
    }
  }
  return String("HTTP ") + code;
}

static String httpsForm(const char *url, const String &body, int *code_out) {
  if (code_out) {
    *code_out = 0;
  }
  WiFiClientSecure *client = new WiFiClientSecure();
  if (!client) {
    return "";
  }
  client->setInsecure();
  client->setHandshakeTimeout(12);
  HTTPClient http;
  String resp;
  if (http.begin(*client, url)) {
    http.setTimeout(15000);
    http.setUserAgent("blenny-badge/1.4");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int code = http.POST(body);
    if (code_out) {
      *code_out = code;
    }
    if (code == 429) {
      noteRateLimit(http);
    }
    if (code > 0) {
      resp = http.getString();
    } else {
      Serial.printf("spotify form fail %d %s\n", code, url);
    }
    http.end();
  }
  delete client;
  return resp;
}

struct ApiResult {
  int code = 0;
  String body;
  bool rate_limited = false;
};

static ApiResult httpsApi(const char *method, const char *url, const String *body,
                          bool json_body) {
  ApiResult r;
  if (!g_access.length()) {
    return r;
  }
  WiFiClientSecure *client = new WiFiClientSecure();
  if (!client) {
    return r;
  }
  client->setInsecure();
  client->setHandshakeTimeout(12);
  HTTPClient http;
  if (http.begin(*client, url)) {
    http.setTimeout(12000);
    http.setUserAgent("blenny-badge/1.4");
    http.addHeader("Authorization", String("Bearer ") + g_access);
    // ESP32 HTTPClient often fails PUTs with a truly empty body; send "{}" for
    // play/pause. Always set Content-Type when a JSON body is expected.
    String payload = body ? *body : String("");
    if (json_body && !payload.length()) {
      payload = "{}";
    }
    if (json_body || payload.length()) {
      http.addHeader("Content-Type", "application/json");
    }
    if (!strcmp(method, "GET")) {
      r.code = http.GET();
    } else if (!strcmp(method, "PUT")) {
      r.code = http.sendRequest("PUT", payload);
    } else if (!strcmp(method, "POST")) {
      r.code = http.POST(payload);
    }
    if (r.code == 429) {
      noteRateLimit(http);
      r.rate_limited = true;
    }
    if (r.code > 0 && r.code != 204) {
      r.body = http.getString();
    }
    if (r.code != 200 && r.code != 201 && r.code != 204 && r.code != 429) {
      Serial.printf("spotify %s %d %s\n", method, r.code, url);
      if (r.body.length()) {
        Serial.printf("spotify body %s\n", r.body.c_str());
      }
    } else {
      Serial.printf("spotify %s %d ok %s\n", method, r.code, url);
    }
    http.end();
  }
  delete client;
  return r;
}

static bool applyTokenJson(const String &json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    return false;
  }
  const char *access = doc["access_token"];
  if (!access || !access[0]) {
    return false;
  }
  g_access = access;
  int expires = doc["expires_in"] | 3600;
  if (expires < 120) {
    expires = 120;
  }
  g_access_exp_ms = millis() + (uint32_t)(expires - 60) * 1000u;
  const char *refresh = doc["refresh_token"];
  if (refresh && refresh[0]) {
    g_refresh = refresh;
    saveCreds();
  }
  return true;
}

bool spotifyEnsureAccess() {
  if (!spotifyAuthed()) {
    setErr("Not linked — run tools/spotify_link.py");
    return false;
  }
  if (inBackoff()) {
    setErr("Rate limited — wait");
    return false;
  }
  if (g_access.length() && millis() < g_access_exp_ms) {
    return true;
  }
  if (!netConnected()) {
    setErr("Waiting for WiFi...");
    return false;
  }
  String body = String("grant_type=refresh_token&refresh_token=") + urlEncode(g_refresh) +
                "&client_id=" + urlEncode(g_client_id);
  int code = 0;
  String resp = httpsForm("https://accounts.spotify.com/api/token", body, &code);
  if (code == 429) {
    return false;
  }
  if (code == 200 && applyTokenJson(resp)) {
    noteSuccess();
    clearErr();
    return true;
  }
  String err = parseApiError(resp, code);
  if (code == 400 || code == 401) {
    // Refresh revoked/expired — force re-link (PKCE on laptop).
    g_refresh = "";
    g_access = "";
    g_access_exp_ms = 0;
    saveCreds();
    setErr("Session expired — re-link");
  } else {
    setErr(err.c_str());
  }
  Serial.printf("spotify refresh fail %d %s\n", code, err.c_str());
  return false;
}

static String joinArtists(JsonArray arr) {
  String out;
  for (JsonVariant v : arr) {
    const char *n = v["name"];
    if (!n) {
      continue;
    }
    if (out.length()) {
      out += ", ";
    }
    out += n;
  }
  return out;
}

static String pickArtUrl(JsonArray images) {
  String best;
  int best_h = 0;
  for (JsonVariant v : images) {
    int h = v["height"] | 0;
    const char *url = v["url"];
    if (!url || !url[0]) {
      continue;
    }
    if (h >= 64 && h <= 300 && (best_h == 0 || h < best_h)) {
      best = url;
      best_h = h;
    } else if (!best.length() && h > 0) {
      best = url;
      best_h = h;
    }
  }
  if (!best.length()) {
    for (JsonVariant v : images) {
      const char *url = v["url"];
      if (url && url[0]) {
        return url;
      }
    }
  }
  return best;
}

bool spotifyFetchNowPlaying(SpotifyNowPlaying *out) {
  if (!out) {
    return false;
  }
  *out = SpotifyNowPlaying{};
  if (!spotifyEnsureAccess()) {
    return false;
  }
  if (inBackoff()) {
    return false;
  }
  // GET /me/player/currently-playing — OpenAPI: CurrentlyPlayingObject or 204
  ApiResult r =
      httpsApi("GET", "https://api.spotify.com/v1/me/player/currently-playing", nullptr, false);
  if (r.rate_limited) {
    return false;
  }
  if (r.code == 204 || (r.code == 200 && !r.body.length())) {
    out->ok = true;
    out->active = false;
    out->playing = false;
    out->title = "Nothing playing";
    noteSuccess();
    clearErr();
    return true;
  }
  if (r.code == 401) {
    g_access = "";
    g_access_exp_ms = 0;
    setErr("Auth failed");
    return false;
  }
  if (r.code == 403) {
    setErr(parseApiError(r.body, r.code).c_str());
    return false;
  }
  if (r.code != 200) {
    setErr(parseApiError(r.body, r.code).c_str());
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, r.body)) {
    setErr("Bad response");
    return false;
  }
  out->ok = true;
  out->active = true;
  out->playing = doc["is_playing"] | false;
  out->progress_ms = doc["progress_ms"] | 0;
  JsonObject item = doc["item"].as<JsonObject>();
  if (item.isNull()) {
    out->title = "Nothing playing";
    out->active = false;
    noteSuccess();
    clearErr();
    return true;
  }
  out->title = item["name"] | "";
  out->duration_ms = item["duration_ms"] | 0;
  out->artist = joinArtists(item["artists"].as<JsonArray>());
  JsonObject album = item["album"].as<JsonObject>();
  if (!album.isNull()) {
    out->album = album["name"] | "";
    out->art_url = pickArtUrl(album["images"].as<JsonArray>());
  }
  if (!out->title.length()) {
    out->title = "Unknown track";
  }
  noteSuccess();
  clearErr();
  return true;
}

static bool playerAction(const char *method, const char *path, bool json_body) {
  if (!spotifyEnsureAccess()) {
    return false;
  }
  if (inBackoff()) {
    return false;
  }
  // /me/player/play|pause|next|previous — OpenAPI Player endpoints
  String url = String("https://api.spotify.com/v1/me/player") + path;
  ApiResult r = httpsApi(method, url.c_str(), nullptr, json_body);
  if (r.rate_limited) {
    return false;
  }
  if (r.code == 200 || r.code == 204) {
    noteSuccess();
    clearErr();
    return true;
  }
  if (r.code == 404) {
    setErr("No active device");
    return false;
  }
  if (r.code == 401) {
    g_access = "";
    g_access_exp_ms = 0;
    setErr("Auth failed");
    return false;
  }
  if (r.code == 403) {
    String msg = parseApiError(r.body, r.code);
    if (msg.indexOf("Premium") >= 0 || msg.indexOf("premium") >= 0) {
      setErr("Spotify Premium required");
    } else {
      setErr(msg.c_str());
    }
    return false;
  }
  if (r.code <= 0) {
    setErr("Network error");
    return false;
  }
  setErr(parseApiError(r.body, r.code).c_str());
  return false;
}

bool spotifyPlay() { return playerAction("PUT", "/play", true); }
bool spotifyPause() { return playerAction("PUT", "/pause", true); }
bool spotifyNext() { return playerAction("POST", "/next", false); }
bool spotifyPrevious() { return playerAction("POST", "/previous", false); }

bool spotifyFetchArt(const String &url, std::vector<uint8_t> *jpeg, size_t cap) {
  if (!jpeg || !url.length() || !netConnected()) {
    return false;
  }
  if (inBackoff()) {
    return false;
  }
  // Immediate-use only: caller replaces when track art_url changes.
  return httpsGetBin(url.c_str(), jpeg, cap);
}
