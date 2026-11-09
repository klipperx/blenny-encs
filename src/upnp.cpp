#include "upnp.h"

#include "cast.h"
#include "net.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <algorithm>
#include <vector>

static std::vector<UpnpRenderer> g_devs;
static int g_sel = 0;
static String g_err;
static WiFiUDP g_udp;
static bool g_disc_busy = false;
static bool g_disc_done = false;
static uint32_t g_disc_until = 0;
static void setErr(const char *m) { g_err = m ? m : ""; }
const char *upnpLastError() { return g_err.c_str(); }

static String xmlUnescape(String s) {
  s.replace("&lt;", "<");
  s.replace("&gt;", ">");
  s.replace("&quot;", "\"");
  s.replace("&apos;", "'");
  s.replace("&amp;", "&");
  return s;
}

static String xmlText(const String &xml, const char *tag) {
  // <tag>…</tag> or <ns:tag>…</ns:tag>
  String open_plain = String("<") + tag;
  int search = 0;
  while (search < (int)xml.length()) {
    int a = xml.indexOf(open_plain, search);
    int b = xml.indexOf(String(":") + tag, search);
    int open_at = -1;
    int name_at = -1;
    if (a >= 0 && (b < 0 || a <= b)) {
      open_at = a;
      name_at = a + 1;
    } else if (b >= 0) {
      // ensure '<' before prefix
      int lt = xml.lastIndexOf('<', b);
      if (lt >= 0 && b - lt < 16) {
        open_at = lt;
        name_at = b + 1;
      }
    }
    if (open_at < 0) {
      return "";
    }
    int gt = xml.indexOf('>', name_at);
    if (gt < 0) {
      return "";
    }
    // skip self-closing
    if (gt > open_at && xml[gt - 1] == '/') {
      search = gt + 1;
      continue;
    }
    int start = gt + 1;
    String close1 = String("</") + tag + ">";
    int c = xml.indexOf(close1, start);
    if (c < 0) {
      // </ns:tag>
      String end = String(tag) + ">";
      int p = start;
      c = -1;
      while (p < (int)xml.length()) {
        int j = xml.indexOf(end, p);
        if (j < 0) {
          break;
        }
        int lt = xml.lastIndexOf('<', j);
        if (lt >= 0 && lt < j && xml[lt + 1] == '/') {
          c = lt;
          break;
        }
        p = j + 1;
      }
    }
    if (c < start) {
      search = start;
      continue;
    }
    String val = xml.substring(start, c);
    val.trim();
    return xmlUnescape(val);
  }
  return "";
}

static String headerVal(const String &msg, const char *key) {
  String line;
  int start = 0;
  String keyc = String(key);
  keyc.toLowerCase();
  while (start < (int)msg.length()) {
    int nl = msg.indexOf('\n', start);
    if (nl < 0) {
      line = msg.substring(start);
      start = msg.length();
    } else {
      line = msg.substring(start, nl);
      start = nl + 1;
    }
    line.trim();
    if (!line.length()) {
      continue;
    }
    int colon = line.indexOf(':');
    if (colon <= 0) {
      continue;
    }
    String k = line.substring(0, colon);
    k.trim();
    k.toLowerCase();
    if (k == keyc) {
      String v = line.substring(colon + 1);
      v.trim();
      return v;
    }
  }
  return "";
}

static String urlOrigin(const String &url) {
  int scheme = url.indexOf("://");
  if (scheme < 0) {
    return "";
  }
  int slash = url.indexOf('/', scheme + 3);
  if (slash < 0) {
    return url;
  }
  return url.substring(0, slash);
}

static String urlJoin(const String &base, const String &rel) {
  if (!rel.length()) {
    return "";
  }
  if (rel.startsWith("http://") || rel.startsWith("https://")) {
    return rel;
  }
  String origin = urlOrigin(base);
  if (rel.startsWith("/")) {
    return origin + rel;
  }
  // directory of base
  int slash = base.lastIndexOf('/');
  if (slash < (int)origin.length()) {
    return origin + "/" + rel;
  }
  return base.substring(0, slash + 1) + rel;
}

static bool parseDescription(UpnpRenderer *r, const String &xml) {
  if (!r || !xml.length()) {
    return false;
  }
  r->name = xmlText(xml, "friendlyName");
  if (!r->name.length()) {
    r->name = "Renderer";
  }
  // Find AVTransport service controlURL
  const char *svc_markers[] = {
      "urn:schemas-upnp-org:service:AVTransport:1",
      "urn:schemas-upnp-org:service:AVTransport:2",
  };
  int svc_at = -1;
  for (const char *m : svc_markers) {
    svc_at = xml.indexOf(m);
    if (svc_at >= 0) {
      break;
    }
  }
  if (svc_at < 0) {
    return false;
  }
  // controlURL after this serviceType — search forward within ~800 chars
  int window_end = svc_at + 900;
  if (window_end > (int)xml.length()) {
    window_end = xml.length();
  }
  String window = xml.substring(svc_at, window_end);
  String ctrl = xmlText(window, "controlURL");
  if (!ctrl.length()) {
    return false;
  }
  r->avt_control = urlJoin(r->location, ctrl);
  r->base_url = urlOrigin(r->location);
  return r->avt_control.length() > 0;
}

static bool addLocation(const String &loc, const String &usn) {
  if (!loc.length()) {
    return false;
  }
  for (auto &d : g_devs) {
    if (d.location == loc || (usn.length() && d.usn == usn)) {
      return false;
    }
  }
  if (g_devs.size() >= kUpnpMaxRenderers) {
    return false;
  }
  String xml = netHttpGet(loc.c_str(), 65536);
  if (!xml.length()) {
    return false;
  }
  UpnpRenderer r;
  r.location = loc;
  r.usn = usn;
  if (!parseDescription(&r, xml)) {
    Serial.printf("upnp no AVTransport %s\n", loc.c_str());
    return false;
  }
  g_devs.push_back(r);
  Serial.printf("upnp + %s (%s)\n", r.name.c_str(), r.avt_control.c_str());
  return true;
}

static void sortPreferPlaying() {
  if (g_devs.size() < 2) {
    return;
  }
  String cur_usn = (g_sel >= 0 && g_sel < (int)g_devs.size()) ? g_devs[g_sel].usn : String("");
  std::stable_sort(g_devs.begin(), g_devs.end(), [](const UpnpRenderer &a, const UpnpRenderer &b) {
    bool ap = a.transport == "PLAYING";
    bool bp = b.transport == "PLAYING";
    if (ap != bp) {
      return ap && !bp;
    }
    return a.name < b.name;
  });
  if (cur_usn.length()) {
    for (size_t i = 0; i < g_devs.size(); i++) {
      if (g_devs[i].usn == cur_usn) {
        g_sel = (int)i;
        return;
      }
    }
  }
}

void upnpReset() {
  castDisconnect();
  g_devs.clear();
  g_sel = 0;
  g_err = "";
  g_disc_busy = false;
  g_disc_done = false;
  g_disc_until = 0;
  g_udp.stop();
}

void upnpStartDiscover() {
  upnpReset();
  if (!netConnected()) {
    setErr("WiFi required");
    g_disc_done = true;
    return;
  }
  if (!g_udp.begin(0)) {
    setErr("UDP failed");
    g_disc_done = true;
    return;
  }
  g_disc_busy = true;
  g_disc_done = false;
  g_disc_until = millis() + kUpnpDiscoverMs;

  const char *sts[] = {
      "urn:schemas-upnp-org:device:MediaRenderer:1",
      "urn:schemas-upnp-org:service:AVTransport:1",
      "ssdp:all",
  };
  for (const char *st : sts) {
    String req = String("M-SEARCH * HTTP/1.1\r\n") + "HOST: 239.255.255.250:1900\r\n" +
                 "MAN: \"ssdp:discover\"\r\n" + "MX: 2\r\n" + "ST: " + st + "\r\n" + "\r\n";
    g_udp.beginPacket(IPAddress(239, 255, 255, 250), 1900);
    g_udp.write((const uint8_t *)req.c_str(), req.length());
    g_udp.endPacket();
    delay(30);
  }
  Serial.println("upnp discover start");
}

bool upnpDiscoverBusy() { return g_disc_busy; }
bool upnpDiscoverDone() { return g_disc_done; }

void upnpDiscoverLoop() {
  if (!g_disc_busy) {
    return;
  }
  int pkt = g_udp.parsePacket();
  while (pkt > 0) {
    char buf[1536];
    int n = g_udp.read(buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = 0;
      String msg(buf);
      String st = headerVal(msg, "ST");
      String nt = headerVal(msg, "NT");
      String usn = headerVal(msg, "USN");
      String loc = headerVal(msg, "LOCATION");
      bool interesting = st.indexOf("MediaRenderer") >= 0 || st.indexOf("AVTransport") >= 0 ||
                         nt.indexOf("MediaRenderer") >= 0 || nt.indexOf("AVTransport") >= 0 ||
                         usn.indexOf("MediaRenderer") >= 0 || usn.indexOf("AVTransport") >= 0 ||
                         st == "ssdp:all" || st == "upnp:rootdevice";
      // For ssdp:all / rootdevice we still try LOCATION — parseDescription filters.
      if (loc.length() && (interesting || st.length() || nt.length())) {
        addLocation(loc, usn);
      }
    }
    pkt = g_udp.parsePacket();
  }
  if ((int32_t)(millis() - g_disc_until) >= 0) {
    g_udp.stop();
    // Chromecast via mDNS (Google Cast), same swipe list.
    castDiscoverInto(&g_devs, kUpnpMaxRenderers);
    g_disc_busy = false;
    g_disc_done = true;
    // Quick transport probe so playing ones float up (UPnP is cheap; Cast is heavy).
    for (size_t i = 0; i < g_devs.size(); i++) {
      if (g_devs[i].source != LanSource::Upnp) {
        continue;
      }
      g_sel = (int)i;
      upnpRefreshSelected();
    }
    sortPreferPlaying();
    g_sel = 0;
    for (size_t i = 0; i < g_devs.size(); i++) {
      if (g_devs[i].transport == "PLAYING") {
        g_sel = (int)i;
        break;
      }
    }
    if (!g_devs.size()) {
      setErr("No LAN players found");
    } else {
      setErr("");
    }
    Serial.printf("lan discover done (%u)\n", (unsigned)g_devs.size());
  }
}

size_t upnpCount() { return g_devs.size(); }
int upnpSelected() { return g_sel; }

void upnpSelect(int idx) {
  if (!g_devs.size()) {
    g_sel = 0;
    return;
  }
  if (idx < 0) {
    idx = (int)g_devs.size() - 1;
  }
  g_sel = idx % (int)g_devs.size();
}

void upnpCycle(int dir) {
  if (!g_devs.size()) {
    return;
  }
  int n = (int)g_devs.size();
  g_sel = (g_sel + dir) % n;
  if (g_sel < 0) {
    g_sel += n;
  }
}

const UpnpRenderer *upnpCurrent() {
  if (!g_devs.size() || g_sel < 0 || g_sel >= (int)g_devs.size()) {
    return nullptr;
  }
  return &g_devs[g_sel];
}

const UpnpRenderer *upnpAt(size_t idx) {
  if (idx >= g_devs.size()) {
    return nullptr;
  }
  return &g_devs[idx];
}

static String soapEnvelope(const char *action, const String &inner) {
  return String("<?xml version=\"1.0\" encoding=\"utf-8\"?>") +
         "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
         "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
         "<s:Body>"
         "<u:" +
         String(action) + " xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">" + inner +
         "</u:" + String(action) +
         ">"
         "</s:Body></s:Envelope>";
}

static String soapCall(const String &control, const char *action, const String &inner) {
  if (!control.length() || !netConnected()) {
    return "";
  }
  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure *secure = nullptr;
  bool begun = false;
  if (control.startsWith("https://")) {
    secure = new WiFiClientSecure();
    if (!secure) {
      return "";
    }
    secure->setInsecure();
    begun = http.begin(*secure, control);
  } else {
    begun = http.begin(plain, control);
  }
  String resp;
  if (begun) {
    http.setTimeout(8000);
    http.setUserAgent("blenny-badge/1.5");
    http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
    http.addHeader("SOAPACTION",
                   String("\"urn:schemas-upnp-org:service:AVTransport:1#") + action + "\"");
    String body = soapEnvelope(action, inner);
    int code = http.POST(body);
    if (code > 0) {
      resp = http.getString();
    } else {
      Serial.printf("upnp soap fail %d %s\n", code, action);
    }
    if (code != 200) {
      Serial.printf("upnp soap %d %s\n", code, action);
    }
    http.end();
  }
  delete secure;
  return resp;
}

static void parseDidl(UpnpRenderer *r, const String &didl_raw) {
  String didl = xmlUnescape(didl_raw);
  r->title = xmlText(didl, "dc:title");
  if (!r->title.length()) {
    r->title = xmlText(didl, "title");
  }
  r->artist = xmlText(didl, "upnp:artist");
  if (!r->artist.length()) {
    r->artist = xmlText(didl, "dc:creator");
  }
  r->album = xmlText(didl, "upnp:album");
  r->art_url = xmlText(didl, "upnp:albumArtURI");
  if (!r->art_url.length()) {
    r->art_url = xmlText(didl, "albumArtURI");
  }
  if (r->art_url.length() && !r->art_url.startsWith("http")) {
    r->art_url = urlJoin(r->base_url, r->art_url);
  }
}

bool upnpRefreshSelected() {
  UpnpRenderer *r = nullptr;
  if (g_sel >= 0 && g_sel < (int)g_devs.size()) {
    r = &g_devs[g_sel];
  }
  if (!r) {
    setErr("No renderer");
    return false;
  }
  if (r->source == LanSource::Cast) {
    bool ok = castRefresh(r);
    if (!ok) {
      setErr(castLastError());
    } else {
      setErr("");
    }
    return ok;
  }
  // Leaving a Cast session frees the TLS client for the next device.
  castDisconnect();
  String inner = "<InstanceID>0</InstanceID>";
  String ti = soapCall(r->avt_control, "GetTransportInfo", inner);
  if (!ti.length()) {
    setErr("Transport failed");
    r->ok = false;
    return false;
  }
  r->transport = xmlText(ti, "CurrentTransportState");
  String pi = soapCall(r->avt_control, "GetPositionInfo", inner);
  String meta;
  if (pi.length()) {
    meta = xmlText(pi, "TrackMetaData");
    String track = xmlText(pi, "TrackURI");
    (void)track;
  }
  if (!meta.length() || meta == "NOT_IMPLEMENTED" || meta == "") {
    String mi = soapCall(r->avt_control, "GetMediaInfo", inner);
    if (mi.length()) {
      meta = xmlText(mi, "CurrentURIMetaData");
    }
  }
  r->title = "";
  r->artist = "";
  r->album = "";
  r->art_url = "";
  if (meta.length() && meta != "NOT_IMPLEMENTED") {
    parseDidl(r, meta);
  }
  if (!r->title.length()) {
    if (r->transport == "PLAYING") {
      r->title = "Playing";
    } else if (r->transport == "PAUSED_PLAYBACK") {
      r->title = "Paused";
    } else if (r->transport == "STOPPED") {
      r->title = "Stopped";
    } else {
      r->title = r->transport.length() ? r->transport : "Idle";
    }
  }
  r->ok = true;
  setErr("");
  return true;
}

static bool avtAction(const char *action, const String &extra) {
  UpnpRenderer *r = nullptr;
  if (g_sel >= 0 && g_sel < (int)g_devs.size()) {
    r = &g_devs[g_sel];
  }
  if (!r) {
    setErr("No renderer");
    return false;
  }
  if (r->source == LanSource::Cast) {
    bool ok = false;
    if (!strcmp(action, "Play")) {
      ok = castPlay(r);
    } else if (!strcmp(action, "Pause")) {
      ok = castPause(r);
    } else if (!strcmp(action, "Next")) {
      ok = castNext(r);
    } else if (!strcmp(action, "Previous")) {
      ok = castPrevious(r);
    }
    if (!ok) {
      setErr(castLastError());
    } else {
      setErr("");
    }
    return ok;
  }
  String inner = String("<InstanceID>0</InstanceID>") + extra;
  String resp = soapCall(r->avt_control, action, inner);
  if (!resp.length()) {
    setErr("Control failed");
    return false;
  }
  if (resp.indexOf("fault") >= 0 || resp.indexOf("Fault") >= 0 || resp.indexOf("UPnPError") >= 0) {
    String code = xmlText(resp, "errorCode");
    String desc = xmlText(resp, "errorDescription");
    if (desc.length()) {
      setErr(desc.c_str());
    } else if (code.length()) {
      setErr((String("UPnP ") + code).c_str());
    } else {
      setErr("UPnP fault");
    }
    return false;
  }
  setErr("");
  return true;
}

bool upnpPlay() { return avtAction("Play", "<Speed>1</Speed>"); }
bool upnpPause() { return avtAction("Pause", ""); }
bool upnpNext() { return avtAction("Next", ""); }
bool upnpPrevious() { return avtAction("Previous", ""); }

bool upnpFetchArt(const String &url, std::vector<uint8_t> *out, size_t cap) {
  if (!url.length() || !out) {
    return false;
  }
  return netHttpGetBin(url.c_str(), out, cap);
}
