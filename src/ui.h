#pragma once

#include "config.h"
#include "game.h"
#include "lgfx_tdisplay.h"
#include "mlb.h"
#include "net.h"
#include "radio.h"

#include <vector>

struct Hit {
  int x;
  int y;
  int w;
  int h;
  int id;
  bool contains(int px, int py) const {
    return px >= x && py >= y && px < x + w && py < y + h;
  }
};

void uiBegin(LGFX_TDisplayS3 *lcd);
void uiDrawSplash(uint32_t t_ms);
void uiDrawUnprovisioned();
void uiDrawNametag(const char *name, uint16_t color, uint8_t style, int anim_ms = -1);
void uiDrawNearby(const std::vector<Peer *> &peers);
void uiDrawHuntWait(const Peer *peer);
void uiDrawHuntAsk();
void uiDrawHunt(const Peer *peer, const char *word, uint16_t color, bool me_ready, bool them_ready);
void uiDrawQuiz(const Quiz &quiz);
void uiDrawReveal(const Quiz &quiz, int picked, uint16_t color);
void uiDrawMetList(const std::vector<String> &ids, int *scroll);
void uiDrawMenu();
static constexpr int kSettingsPageCount = 3;  // name, wifi, auto mode
void uiDrawSettings(int page);
/** Built-in Silly Animation names (kToyFaceCount of them), for the Auto mode picker. */
const char *uiToyName(int toy);
void uiDrawWifiWarn();
void uiDrawWifiScan(int *scroll, bool scanning);
void uiDrawKeyboard(const char *ssid, const String &pass, bool shift, bool num);
void uiDrawLeagues();
void uiDrawMlbNeedWifi(const char *detail = "This feature needs a network.");
void uiDrawWifiWake(const char *ssid = nullptr);
void uiDrawMlbGames(const std::vector<MlbGame> &games, League league, int *scroll, bool loading,
                    const char *err);
void uiDrawMlbBoard(const MlbBoard &board);
void uiDrawPostConMenu();
void uiDrawSpotify(const struct SpotifyNowPlaying &np, const std::vector<uint8_t> *art_jpeg,
                   bool loading, const char *err, bool authed);
void uiDrawUpnp(const struct UpnpRenderer *r, int index, int total,
                const std::vector<uint8_t> *art_jpeg, bool discovering, bool loading,
                const char *err);
// Toy faces: spectrum, matrix, winamp, vu, 3 glow clips
static constexpr int kEggToyCount = 7;
void uiDrawEgg(uint32_t t_ms, int toy, bool show_name = false, uint16_t name_color = 0,
               bool clock_overlay = false, bool dots = true);
/** Firmware update screen; pct < 0 hides the progress bar. Draws immediately. */
void uiDrawOta(const char *title, const char *detail, int pct);
/** Shame goose for goose-flagged badges: waddles holding "I didn't follow instructions". */
/** Shame goose; `sign` overrides the default "I didn't follow instructions" (nullptr/empty = default). */
void uiDrawGoose(uint32_t t_ms, const char *sign = nullptr);
/** Queen easter egg: embedded GIF with floating hearts and caption. */
bool uiQueenStart();
void uiQueenStop();
void uiDrawQueen(uint32_t t_ms);
/** Plain clock (classic nametag pages — no live bg). */
void uiDrawClock();
void uiDrawNameOverlay(const char *name, uint16_t color);
void uiDrawDeadBattery();
void uiPaintHud();
int uiHit(int x, int y);
void uiNeedRedraw();
void uiDrawLeaderboard();
/** How many pages Silly Animations has (built-ins + custom GIFs + the "Add your own" page). */
void uiSetSillyTotal(int total);
/** A custom GIF from LittleFS, centred; `page` is its Silly page for the dots (-1 = no dots). */
void uiDrawCustomGif(const char *path, int page);
void uiCustomGifStop();
/** Last Silly page: QR code + steps for adding your own GIF. */
void uiDrawGifHelp(int page, int count, int max);
void uiDrawDiversions();
/** Games picker: Blackjack, Klippy Bird. */
void uiDrawGames();
void uiDrawKlippyBird();
/** Draw the Outbreak heartbeat / infected overlay on every frame while the game is open. */
void uiSetOutbreakOverlay(bool on);
/** Hide and Seek screen: hot/cold meter and team board (or, for it, who's closing in). */
void uiDrawHideSeek();
/** Hide and Seek: switch between the board and the radar. */
void uiHideSeekToggleView();
/** Blackjack lobby (no table) or the table you're at. */
void uiDrawBlackjack();
/** Deep sleep: what it does and doesn't do (word-wrapped, drag to scroll), Cancel / Sleep. */
void uiDrawSleepConfirm(int *scroll);
/** Last frame before deep sleep. */
void uiDrawSleeping();
/** Standalone About screen (logo shimmer, version, stats, own fact, credit). */
void uiDrawAbout();
/** Agenda for schedule day `day`; highlights the session running now. */
void uiDrawSchedule(int day, int *scroll);
/** Next uiDrawNametag renders dimmed with the "meet 5 people" lock note. */
void uiSetStyleLocked(bool locked);
/** Achievements, two columns; drag to scroll. */
void uiDrawAchievements(int *scroll);
/** Brief pill over the current screen (e.g. after an OTA roster update). */
void uiToast(const char *msg, uint32_t ms);
bool uiConsumeRedraw();

enum UiHitId {
  HIT_NONE = -1,
  HIT_PLAY = 1,
  HIT_NAMETAG_BACK = 2,
  HIT_CANCEL = 3,
  HIT_THATSYOU = 4,
  HIT_CONTINUE = 5,
  HIT_FOUND_YES = 6,
  HIT_FOUND_NO = 7,
  HIT_WIFI_SCAN = 9,
  HIT_NEARBY0 = 10,
  HIT_FACT0 = 20,
  HIT_MENU0 = 30,
  HIT_WIFI_DISCONNECT = 36,
  HIT_WIFI_CONNECT = 37,
  HIT_WARN_YES = 38,
  HIT_WARN_NO = 39,
  HIT_GOTO_SETTINGS = 40,
  HIT_KB_SHIFT = 41,
  HIT_KB_NUM = 42,
  HIT_KB_BKSP = 43,
  HIT_KB_JOIN = 44,
  HIT_KB_SPACE = 45,
  HIT_NICK_EDIT = 46,
  HIT_NICK_CLEAR = 47,
  HIT_WIFI0 = 50,
  HIT_LEAGUE0 = 66,
  HIT_MLB0 = 70,
  HIT_SPOTIFY_PREV = 93,
  HIT_SPOTIFY_PLAY = 94,
  HIT_SPOTIFY_NEXT = 95,
  HIT_SPOTIFY_LOGOUT = 97,
  HIT_UPNP_PREV = 100,
  HIT_UPNP_PLAY = 101,
  HIT_UPNP_NEXT = 102,
  HIT_UPNP_REFRESH = 103,
  HIT_MENU_BACK = 104,
  HIT_SCHED_PREV = 105,
  HIT_SCHED_NEXT = 106,
  HIT_SLEEP_ASK = 107,
  HIT_SLEEP_NO = 108,
  HIT_SLEEP_YES = 109,
  HIT_DIV0 = 120,
  HIT_BJ_START = 130,
  HIT_BJ_TABLE0 = 131,
  HIT_BJ_END = 135,
  HIT_BJ_MINUS = 136,
  HIT_BJ_PLUS = 137,
  HIT_BJ_HIT = 139,
  HIT_BJ_STAND = 140,
  HIT_BJ_SPLIT = 141,
  HIT_AUTO_TOGGLE = 142,
  HIT_FAV_PREV = 143,
  HIT_FAV_NEXT = 144,
  HIT_FAV_PREVIEW = 145,
  HIT_GAME_BJ = 146,
  HIT_GAME_BIRD = 147,
  HIT_GAME_OUTBREAK = 148,
  HIT_HS_VIEW = 149,
  HIT_POSTCON0 = 110,
  HIT_CHAR0 = 256,
};

// Main menu: 3x2 tiles; "< Nametag" in the title bar is HIT_MENU_BACK.
enum MenuItem : uint8_t {
  MENU_FRIENDS = 0,
  MENU_REGISTRY = 1,
  MENU_LEADERS = 2,
  MENU_AWARDS = 3,
  MENU_SCHEDULE = 4,
  MENU_MORE = 5,
  MENU_COUNT = 6,
};

// "More" page (Mode::PostConMenu): settings plus the post-con apps.
enum PostConItem : uint8_t {
  POSTCON_DIVERSIONS = 0,
  POSTCON_SETTINGS = 1,
  POSTCON_ABOUT = 2,
  POSTCON_SPOTIFY = 3,
  POSTCON_UPNP = 4,
  POSTCON_COUNT = 5,
};

// "Diversions" (under More): scoreboard, Silly Animations, games.
enum DiversionItem : uint8_t {
  DIV_SCORES = 0,
  DIV_SILLY = 1,
  DIV_GAMES = 2,
  DIV_HIDESEEK = 3,
  DIV_COUNT = 4,
};

static constexpr int kBjListShown = 3;  // tables listed in the Blackjack lobby
