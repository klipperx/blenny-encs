#pragma once

#include <Arduino.h>

#define PIN_LCD_BL 38
#define PIN_POWER_ON 15
#define PIN_BUTTON_USER 14
#define PIN_BUTTON_BOOT 0
#define PIN_BAT_VOLT 4
#define PIN_IIC_SCL 17
#define PIN_IIC_SDA 18
#define PIN_TOUCH_INT 16
#define PIN_TOUCH_RES 21

static constexpr uint16_t kScreenW = 320;
static constexpr uint16_t kScreenH = 170;

static constexpr uint8_t kWifiChannel = 1;
static constexpr uint32_t kBeaconMs = 400;
static constexpr uint32_t kPeerTimeoutMs = 2500;
static constexpr uint32_t kCooldownMs = 60000;
static constexpr uint32_t kUiRefreshMs = 250;
static constexpr uint32_t kAboutRefreshMs = 40;  // About page logo shimmer
static constexpr uint32_t kNearbyRefreshMs = 40;
static constexpr uint32_t kNearbyRepickMs = 5000;  // Find Friends: re-pick the strongest rows
static constexpr uint32_t kBtnBounceMs = 80;
static constexpr uint32_t kBtnDoubleMs = 400;

static constexpr size_t kIdLen = 16;
static constexpr size_t kNameLen = 32;
static constexpr size_t kWordLen = 12;
static constexpr uint8_t kMaxPeers = 32;
static constexpr uint8_t kNearbyRows = 4;
static constexpr uint8_t kNatoCount = 26;
static constexpr uint32_t kHuntOfferMs = 20000;
static constexpr uint8_t kTagStyleCount = 8;  // classic HELLO styles only
static constexpr uint8_t kTagStillStyleCount = 4;  // 0..3 stay put; 4..7 get a short idle anim
static constexpr uint8_t kClassicFaceCount = 8;
static constexpr uint8_t kToyFaceCount = 7;   // Silly Animations: spectrum, matrix, winamp, vu, 3 clips
static constexpr uint32_t kSplashMs = 3800;
static constexpr const char *kFirmwareVersion = "3.2.0";
#define PIN_LED1 11  // optional carrier LEDs (see leds.h)
#define PIN_LED2 12
#define PIN_LED3 13
#define PIN_LED4 43
static constexpr uint32_t kTagAnimMs = 12000;  // idle spectrum cinema — linger, don't fly by
static constexpr uint32_t kTagIdleMinMs = 20000;
static constexpr uint32_t kTagIdleMaxMs = 45000;
static constexpr uint32_t kTagIdleAnimMs = 4200;  // neon / flap / crt / arcade idle burst
static constexpr uint32_t kQueenHoldMs = 10000;  // USER hold: Queen easter egg
static constexpr uint32_t kRotateTagMs = 10000;  // rotation: nametag dwell
static constexpr uint32_t kRotateToyMs = 6000;   // Auto mode: favourite animation dwell
static constexpr uint32_t kGooseTagMs = 30000;   // goose badges: nametag dwell
static constexpr uint32_t kGooseShameMs = 10000;  // goose badges: shame goose dwell
static constexpr uint32_t kRosterToastMs = 5000;
static constexpr uint32_t kLockedPreviewMs = 5000;
// Power: backlight drops to kBrightDim after kDimAfterMs without a touch or button press.
static constexpr uint8_t kBrightFull = 200;
static constexpr uint8_t kBrightDim = 48;
static constexpr uint32_t kDimAfterMs = 60000;
static constexpr uint32_t kIdleCpuMhz = 80;   // still screens
static constexpr uint32_t kBusyCpuMhz = 240;  // animations, games, Wi-Fi, USB work  // locked nametag style preview before snap-back  // "Roster updated" pill after an OTA roster
static constexpr uint32_t kScreenLongPressMs = 650;  // touch hold: name overlay on/off
static constexpr uint32_t kSpotifyPollMs = 3000;
static constexpr uint32_t kUpnpUiPollMs = 2500;
// Public PKCE client id (optional build flag). Also settable via serial SETSPOTIFY_CLIENT.
// Link once on a laptop: tools/spotify_link.py (redirect http://127.0.0.1:8888/callback).
#ifndef SPOTIFY_CLIENT_ID
#define SPOTIFY_CLIENT_ID ""
#endif

static constexpr uint32_t kMsgMagic = 0x424C4E59;  // BLNY
static constexpr uint8_t kMsgVersion = 2;

enum MsgType : uint8_t {
  MSG_BEACON = 1,
  MSG_HUNT_REQ = 2,
  MSG_HUNT_ACK = 3,
  MSG_HUNT_CANCEL = 4,
  MSG_MEET_READY = 5,
  MSG_HUNT_DECLINE = 6,
};

enum class Mode : uint8_t {
  Unprovisioned,
  Nametag,
  Menu,
  MetList,
  Nearby,
  HuntWait,
  HuntAsk,
  Hunt,
  Quiz,
  Reveal,
  Settings,
  WifiScan,
  WifiKeyboard,
  NickKeyboard,
  WifiWarn,
  MlbNeedWifi,
  WifiWake,
  LeaguePick,
  MlbGames,
  MlbBoard,
  Egg,
  Silly,
  Queen,
  PostConMenu,
  Spotify,
  Upnp,
  Leaderboard,
  Achievements,
  Schedule,
  About,
  SleepConfirm,
  Diversions,
  Blackjack,
  Games,
  KlippyBird,
  Outbreak,
  HideSeek,
};

static constexpr uint8_t kMaxWifiAps = 16;
static constexpr uint8_t kMaxMlbGames = 16;
static constexpr uint32_t kMlbRefreshMs = 20000;

struct Color {
  uint16_t rgb;
  const char *name;
};

// High-contrast nametag / hunt colors (RGB565).
static constexpr Color kColors[] = {
    {0xF800, "red"},     {0xFD20, "orange"}, {0xFFE0, "yellow"},
    {0x07E0, "green"},   {0x07FF, "cyan"},   {0x05FF, "blue"},
    {0xF81F, "magenta"}, {0xFFFF, "white"},  {0xFDB8, "pink"},
};
static constexpr uint8_t kColorCount = sizeof(kColors) / sizeof(kColors[0]);

static constexpr uint16_t kBg = 0x0841;
static constexpr uint16_t kPanel = 0x18C3;
static constexpr uint16_t kMuted = 0x8410;
static constexpr uint16_t kText = 0xFFFF;
static constexpr uint16_t kDanger = 0xF800;
static constexpr uint16_t kOk = 0x07E0;

#pragma pack(push, 1)
struct BadgeMsg {
  uint32_t magic;
  uint8_t version;
  uint8_t type;
  uint8_t color;
  uint8_t pad;
  char id[kIdLen];
  char name[kNameLen];
  char word[kWordLen];
};
#pragma pack(pop)

static_assert(sizeof(BadgeMsg) <= 250, "ESP-NOW payload too large");
