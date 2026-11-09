#pragma once

#include <Arduino.h>

// Attendee GIFs in LittleFS /gifs/, shown after the built-in Silly Animations. They arrive
// over USB serial from the browser uploader (klipperx.github.io/blenny-ota/gif/):
//   GIFBEGIN <name> <bytes> <crc32 hex>   -> OK GIFBEGIN | ERR <why>
//   GIFDATA <offset> <base64 chunk>       -> OK GIFDATA <bytes so far> | ERR <why>
//     (a chunk resent with the same offset is acknowledged again, not written twice;
//      a garbled line gets RESEND <offset> and the upload stays open)
//   GIFEND                                -> OK GIFEND <name> | ERR <why>
//   GIFLIST                               -> GIF <name> <bytes> ... then OK GIFLIST <n> <max> <free>
//   GIFDEL <name>                         -> OK GIFDEL <name> | ERR <why>
// One upload at a time; a new GIFBEGIN abandons a half-finished one.

static constexpr int kGifMaxCount = 8;
static constexpr size_t kGifMaxBytes = 1024 * 1024;
static constexpr const char *kGifUploadUrl = "klipperx.github.io/blenny-ota/gif";

void gifsLoad();
int gifsCount();
/** Display name (file name without .gif). */
const String &gifsName(int i);
/** LittleFS path ("/gifs/<name>.gif"). */
String gifsPath(int i);

/** Handle one serial command if it's a GIF* verb; returns false for anything else. */
bool gifsHandleCommand(const String &verb, const String &arg);
/** True while an upload is in progress; *pct gets 0..100. */
bool gifsReceiving(int *pct, String *name);
/** Name of a GIF that just finished uploading (once), or empty. */
String gifsTakeAdded();
