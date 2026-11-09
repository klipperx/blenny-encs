#include "custom_gifs.h"

#include "ui.h"

#include <LittleFS.h>
#include <algorithm>
#include <mbedtls/base64.h>
#include <vector>

static constexpr const char *kDir = "/gifs";
static constexpr const char *kTmp = "/gifs/.upload.tmp";
static constexpr size_t kKeepFree = 256 * 1024;  // headroom for roster/schedule updates

static std::vector<String> g_names;
static File g_up;
static String g_up_name;
static size_t g_up_size = 0;
static size_t g_up_got = 0;
static uint32_t g_up_crc_want = 0;
static uint32_t g_up_crc = 0xFFFFFFFFu;
static String g_added;

// Standard CRC-32 (zlib / JS / Python binascii), updated incrementally.
static uint32_t crcUpdate(uint32_t crc, const uint8_t *p, size_t n) {
  while (n--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return crc;
}

// Letters, digits, '-' and '_' only, at most 24; never empty.
static String safeName(const String &in) {
  String out;
  for (size_t i = 0; i < in.length() && out.length() < 24; i++) {
    char c = in[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_') {
      out += c;
    } else if (c == ' ' || c == '.') {
      out += '_';
    }
  }
  return out.length() ? out : String("gif");
}

void gifsLoad() {
  g_names.clear();
  if (!LittleFS.exists(kDir)) {
    LittleFS.mkdir(kDir);
  }
  File dir = LittleFS.open(kDir);
  if (!dir || !dir.isDirectory()) {
    return;
  }
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String n = f.name();
    int slash = n.lastIndexOf('/');
    if (slash >= 0) {
      n = n.substring(slash + 1);
    }
    if (n.endsWith(".gif") && !n.startsWith(".")) {
      g_names.push_back(n.substring(0, n.length() - 4));
    }
    f.close();
  }
  std::sort(g_names.begin(), g_names.end());
}

int gifsCount() { return (int)g_names.size(); }
const String &gifsName(int i) { return g_names[i]; }
String gifsPath(int i) { return String(kDir) + "/" + g_names[i] + ".gif"; }

static void abortUpload() {
  if (g_up) {
    g_up.close();
  }
  LittleFS.remove(kTmp);
  g_up_name = "";
  g_up_size = g_up_got = 0;
}

static size_t fsFree() {
  size_t total = LittleFS.totalBytes();
  size_t used = LittleFS.usedBytes();
  return total > used ? total - used : 0;
}

bool gifsHandleCommand(const String &verb, const String &arg) {
  if (verb == "GIFBEGIN") {
    abortUpload();
    // <name> <bytes> <crc32 hex>
    int a = arg.indexOf(' ');
    int b = a < 0 ? -1 : arg.indexOf(' ', a + 1);
    if (a < 0 || b < 0) {
      Serial.println("ERR usage GIFBEGIN <name> <bytes> <crc32>");
      return true;
    }
    String name = safeName(arg.substring(0, a));
    long size = arg.substring(a + 1, b).toInt();
    uint32_t crc = (uint32_t)strtoul(arg.substring(b + 1).c_str(), nullptr, 16);
    bool replacing = std::find(g_names.begin(), g_names.end(), name) != g_names.end();

    if (size <= 6 || (size_t)size > kGifMaxBytes) {
      Serial.printf("ERR size must be 1 byte to %u KB\n", (unsigned)(kGifMaxBytes / 1024));
    } else if (!replacing && gifsCount() >= kGifMaxCount) {
      Serial.printf("ERR full: %d GIFs already, delete one first\n", kGifMaxCount);
    } else if ((size_t)size + kKeepFree > fsFree()) {
      Serial.printf("ERR not enough space (%u KB free)\n", (unsigned)(fsFree() / 1024));
    } else if (!(g_up = LittleFS.open(kTmp, "w"))) {
      Serial.println("ERR cannot write");
    } else {
      g_up_name = name;
      g_up_size = (size_t)size;
      g_up_got = 0;
      g_up_crc_want = crc;
      g_up_crc = 0xFFFFFFFFu;
      Serial.printf("OK GIFBEGIN %s\n", name.c_str());
    }
    return true;
  }
  if (verb == "GIFDATA") {
    if (!g_up) {
      Serial.println("ERR no upload in progress");
      return true;
    }
    // GIFDATA <offset> <base64>: the offset makes a resent chunk harmless.
    int sp = arg.indexOf(' ');
    if (sp < 0) {
      Serial.printf("RESEND %u\n", (unsigned)g_up_got);
      return true;
    }
    size_t off = (size_t)arg.substring(0, sp).toInt();
    static uint8_t buf[3072];
    size_t out = 0;
    if (mbedtls_base64_decode(buf, sizeof(buf), &out, (const unsigned char *)arg.c_str() + sp + 1,
                              arg.length() - sp - 1) != 0) {
      Serial.printf("RESEND %u\n", (unsigned)g_up_got);
      return true;
    }
    if (off + out == g_up_got && out > 0) {
      Serial.printf("OK GIFDATA %u\n", (unsigned)g_up_got);  // already have it: re-acknowledge
      return true;
    }
    if (off != g_up_got || g_up_got + out > g_up_size) {
      // Usually a garbled or merged line (a byte lost on USB): ask for the right chunk again.
      Serial.printf("RESEND %u\n", (unsigned)g_up_got);
      return true;
    }
    if (g_up.write(buf, out) != out) {
      Serial.println("ERR write failed");
      abortUpload();
      return true;
    }
    g_up_crc = crcUpdate(g_up_crc, buf, out);
    g_up_got += out;
    Serial.printf("OK GIFDATA %u\n", (unsigned)g_up_got);
    return true;
  }
  if (verb == "GIFEND") {
    if (!g_up) {
      Serial.println("ERR no upload in progress");
      return true;
    }
    g_up.close();
    uint32_t crc = ~g_up_crc;
    if (g_up_got != g_up_size || crc != g_up_crc_want) {
      Serial.printf("ERR checksum (got %u of %u bytes)\n", (unsigned)g_up_got, (unsigned)g_up_size);
      abortUpload();
      return true;
    }
    // Must actually be a GIF.
    File f = LittleFS.open(kTmp, "r");
    char magic[4] = {};
    if (f) {
      f.readBytes(magic, 3);
      f.close();
    }
    if (strncmp(magic, "GIF", 3) != 0) {
      Serial.println("ERR not a GIF file");
      abortUpload();
      return true;
    }
    String path = String(kDir) + "/" + g_up_name + ".gif";
    uiCustomGifStop();  // the player may have this file open (replacing a GIF on screen)
    LittleFS.remove(path);
    if (!LittleFS.rename(kTmp, path)) {
      Serial.println("ERR save failed");
      abortUpload();
      return true;
    }
    g_added = g_up_name;
    Serial.printf("OK GIFEND %s\n", g_up_name.c_str());
    g_up_name = "";
    g_up_size = g_up_got = 0;
    gifsLoad();
    return true;
  }
  if (verb == "GIFLIST") {
    for (int i = 0; i < gifsCount(); i++) {
      File f = LittleFS.open(gifsPath(i), "r");
      Serial.printf("GIF %s %u\n", g_names[i].c_str(), f ? (unsigned)f.size() : 0u);
      if (f) {
        f.close();
      }
    }
    Serial.printf("OK GIFLIST %d %d %u\n", gifsCount(), kGifMaxCount, (unsigned)fsFree());
    return true;
  }
  if (verb == "GIFDEL") {
    String name = safeName(arg);
    String path = String(kDir) + "/" + name + ".gif";
    uiCustomGifStop();
    if (LittleFS.exists(path) && LittleFS.remove(path)) {
      gifsLoad();
      Serial.printf("OK GIFDEL %s\n", name.c_str());
    } else {
      Serial.printf("ERR no GIF named %s\n", name.c_str());
    }
    return true;
  }
  return false;
}

bool gifsReceiving(int *pct, String *name) {
  if (!g_up || !g_up_size) {
    return false;
  }
  if (pct) {
    *pct = (int)((g_up_got * 100) / g_up_size);
  }
  if (name) {
    *name = g_up_name;
  }
  return true;
}

String gifsTakeAdded() {
  String a = g_added;
  g_added = "";
  return a;
}
