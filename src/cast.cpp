#include "cast.h"

#include "net.h"

#include <ArduCastControl.h>
#include <ESPmDNS.h>
#include <WiFi.h>

static ArduCastControl g_cast;
static String g_cast_host;
static String g_err;
static bool g_mdns_up = false;

const char *castLastError() { return g_err.c_str(); }

static void setErr(const char *m) { g_err = m ? m : ""; }

void castDisconnect() {
  // Library has no explicit disconnect; drop TCP by reconnecting later.
  g_cast_host = "";
  setErr("");
}

static bool ensureMdns() {
  if (g_mdns_up) {
    return true;
  }
  if (!netConnected()) {
    return false;
  }
  if (!MDNS.begin("blenny-cast")) {
    Serial.println("cast mdns begin fail");
    return false;
  }
  g_mdns_up = true;
  return true;
}

void castDiscoverInto(std::vector<UpnpRenderer> *devs, size_t max_total) {
  if (!devs || !netConnected()) {
    return;
  }
  if (!ensureMdns()) {
    setErr("mDNS failed");
    return;
  }
  int n = MDNS.queryService("googlecast", "tcp");
  Serial.printf("cast mdns %d\n", n);
  for (int i = 0; i < n && devs->size() < max_total; i++) {
    IPAddress ip = MDNS.IP(i);
    if (!ip) {
      continue;
    }
    String host = ip.toString();
    bool dup = false;
    for (const auto &d : *devs) {
      if (d.source == LanSource::Cast && d.cast_host == host) {
        dup = true;
        break;
      }
    }
    if (dup) {
      continue;
    }
    UpnpRenderer r;
    r.source = LanSource::Cast;
    r.cast_host = host;
    r.usn = String("cast:") + host;
    r.location = String("cast://") + host;
    String name = MDNS.hostname(i);
    if (name.length()) {
      r.name = name;
    } else {
      r.name = String("Chromecast ") + host;
    }
    r.transport = "";
    r.title = "Idle";
    r.ok = false;
    devs->push_back(r);
    Serial.printf("cast + %s (%s)\n", r.name.c_str(), host.c_str());
  }
}

static const char *stateName(playerState_t s) {
  switch (s) {
    case PLAYING:
      return "PLAYING";
    case PAUSED:
      return "PAUSED";
    case BUFFERING:
      return "BUFFERING";
    default:
      return "IDLE";
  }
}

static bool castEnsure(const char *host) {
  if (!host || !host[0] || !netConnected()) {
    setErr("No Cast host");
    return false;
  }
  if (g_cast_host == host) {
    connection_t st = g_cast.getConnection();
    if (st != DISCONNECTED) {
      return true;
    }
  }
  g_cast_host = "";
  int rc = g_cast.connect(host);
  if (rc != 0) {
    setErr("Cast connect failed");
    Serial.printf("cast connect %s -> %d\n", host, rc);
    return false;
  }
  g_cast_host = host;
  // Pump until we leave WAIT / get application info (bounded).
  uint32_t start = millis();
  while (millis() - start < 2500) {
    connection_t st = g_cast.loop();
    if (st == APPLICATION_RUNNING || st == CONNECTED || st == CONNECT_TO_APPLICATION) {
      // keep pumping a bit for media
    }
    if (st == DISCONNECTED) {
      setErr("Cast dropped");
      g_cast_host = "";
      return false;
    }
    delay(20);
  }
  return true;
}

bool castRefresh(UpnpRenderer *r) {
  if (!r || r->source != LanSource::Cast || !r->cast_host.length()) {
    setErr("Not a Cast device");
    return false;
  }
  if (!castEnsure(r->cast_host.c_str())) {
    r->ok = false;
    return false;
  }
  uint32_t start = millis();
  connection_t st = DISCONNECTED;
  while (millis() - start < 2000) {
    st = g_cast.loop();
    if (st == APPLICATION_RUNNING || st == CONNECTED) {
      // one more loop to catch MEDIA_STATUS
      g_cast.loop();
      break;
    }
    if (st == DISCONNECTED) {
      setErr("Cast dropped");
      r->ok = false;
      g_cast_host = "";
      return false;
    }
    delay(20);
  }
  r->transport = stateName(g_cast.playerState);
  if (g_cast.title[0]) {
    r->title = g_cast.title;
  } else if (g_cast.statusText[0]) {
    r->title = g_cast.statusText;
  } else if (g_cast.displayName[0]) {
    r->title = g_cast.displayName;
  } else {
    r->title = (r->transport == "IDLE") ? "Idle" : r->transport;
  }
  r->artist = g_cast.artist[0] ? g_cast.artist : "";
  r->album = g_cast.album[0] ? g_cast.album : (g_cast.displayName[0] ? g_cast.displayName : "");
  r->art_url = g_cast.artUrl[0] ? g_cast.artUrl : "";
  // Prefer PLAYING naming consistent with UPnP sort
  if (r->transport == "PAUSED") {
    r->transport = "PAUSED_PLAYBACK";
  }
  r->ok = true;
  setErr("");
  return true;
}

enum class CastCmd : uint8_t { Play, Pause, Next, Prev };

static bool castCtrl(UpnpRenderer *r, CastCmd cmd) {
  if (!r || !castEnsure(r->cast_host.c_str())) {
    return false;
  }
  uint32_t start = millis();
  while (millis() - start < 1500) {
    connection_t st = g_cast.loop();
    if (st == APPLICATION_RUNNING) {
      break;
    }
    if (st == DISCONNECTED) {
      setErr("Cast dropped");
      return false;
    }
    delay(20);
  }
  int rc = -1;
  switch (cmd) {
    case CastCmd::Play:
      rc = g_cast.play();
      break;
    case CastCmd::Pause:
      rc = g_cast.pause(false);
      break;
    case CastCmd::Next:
      rc = g_cast.next();
      break;
    case CastCmd::Prev:
      rc = g_cast.prev();
      break;
  }
  if (rc != 0) {
    setErr(rc == -9 ? "No media session" : "Cast control failed");
    Serial.printf("cast ctrl %d\n", rc);
    return false;
  }
  setErr("");
  delay(100);
  g_cast.loop();
  return true;
}

bool castPlay(UpnpRenderer *r) { return castCtrl(r, CastCmd::Play); }
bool castPause(UpnpRenderer *r) { return castCtrl(r, CastCmd::Pause); }
bool castNext(UpnpRenderer *r) { return castCtrl(r, CastCmd::Next); }
bool castPrevious(UpnpRenderer *r) { return castCtrl(r, CastCmd::Prev); }
