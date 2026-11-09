#include "battery.h"
#include "blackjack.h"
#include "custom_gifs.h"
#include "klippy_bird.h"
#include "outbreak.h"
#include "hideseek.h"
#include "leds.h"
#include "config.h"
#include "game.h"
#include "lgfx_tdisplay.h"
#include "mlb.h"
#include "ota.h"
#include "net.h"
#include "radio.h"
#include "roster.h"
#include "scoreboard.h"
#include "schedule.h"
#include "settings.h"
#include "social.h"
#include "spotify.h"
#include "touch_input.h"
#include "ui.h"
#include "upnp.h"
#include "words.h"

#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_random.h>
#include <esp_sleep.h>
#include <string.h>
#include <vector>

static LGFX_TDisplayS3 tft;
static bool g_display_up = false;
static Mode g_mode = Mode::Unprovisioned;
static uint32_t g_last_ui = 0;
static bool g_btn_down = false;

static Peer g_lock;
static bool g_have_lock = false;
static bool g_me_ready = false;
static bool g_them_ready = false;
static Quiz g_quiz;
static int g_picked = -1;
static uint8_t g_home_score = 0;
static uint8_t g_visitor_score = 0;
static int g_met_scroll = 0;
static int g_sched_day = 0;
static int g_sched_scroll = 0;
static int g_sleep_scroll = 0;
static int g_ach_scroll = 0;
static int g_list_scroll = 0;
static int g_list_drag_y = 0;
static char g_hunt_word[kWordLen] = {};
static uint8_t g_hunt_color = 0;
static uint32_t g_hunt_offer_at = 0;

static String g_kb_ssid;
static String g_kb_pass;
static bool g_kb_shift = false;
static bool g_kb_num = false;
static bool g_wifi_scanning = false;
static int g_settings_page = 0;

static std::vector<MlbGame> g_mlb_games;
static MlbBoard g_mlb_board;
static String g_mlb_err;
static bool g_mlb_loading = false;
static uint32_t g_mlb_at = 0;
static League g_league = League::Mlb;

static SpotifyNowPlaying g_spot_np;
static std::vector<uint8_t> g_spot_art;
static String g_spot_art_url;
static String g_spot_err;
static bool g_spot_loading = false;
static uint32_t g_spot_at = 0;
static const char *g_wifi_need_detail = "This feature needs a network.";

enum class WifiWakeDest : uint8_t { None, Scoreboard, Spotify, Upnp };
static WifiWakeDest g_wifi_wake_dest = WifiWakeDest::None;
static bool g_upnp_wake_rescan = true;

static std::vector<uint8_t> g_upnp_art;
static String g_upnp_art_url;
static String g_upnp_err;
static bool g_upnp_loading = false;
static uint32_t g_upnp_at = 0;

static volatile bool g_msg_pending = false;
static BadgeMsg g_pending_msg;
static uint8_t g_pending_mac[6];

static bool lockIs(const char *id) {
  return g_have_lock && id && strncmp(g_lock.id, id, kIdLen) == 0;
}

static void clearHunt() {
  g_have_lock = false;
  g_me_ready = false;
  g_them_ready = false;
  g_hunt_word[0] = 0;
  g_hunt_color = 0;
}

static uint32_t g_tag_anim_at = 0;
static uint32_t g_tag_idle_at = 0;

static uint32_t nextTagIdleAt() {
  uint32_t span = kTagIdleMaxMs - kTagIdleMinMs + 1;
  return millis() + kTagIdleMinMs + (esp_random() % span);
}

static void bumpTagIdle() {
  g_tag_idle_at = nextTagIdleAt();
}

static void goNametag();
static void goSilly();
static void stopQueen();
static uint32_t g_queen_at = 0;

static bool g_clock_mode = false;
static uint32_t g_face_anim_at = 0;

static void markFaceAnim() {
  g_face_anim_at = millis() ? millis() : 1;
}

static uint32_t faceAnimMs() {
  return g_face_anim_at ? (millis() - g_face_anim_at) : 0;
}

// Face pages: 0..7 classic nametags. Toys live under Silly Animations.
static bool faceIsClassic(int page) { return page >= 0 && page < (int)kClassicFaceCount; }
// Classics 4..7 play a short idle burst; 0..3 stay put.
static bool faceHasIdleAnim(int page) {
  return faceIsClassic(page) && page >= (int)kTagStillStyleCount;
}

// Silly Animations gallery page, and Auto mode's nametag -> (goose) -> favourite cycle.
static int g_silly_toy = 0;
static uint32_t g_rot_at = 0;
enum RotPhase : uint8_t { ROT_TAG = 0, ROT_GOOSE, ROT_FAV };
static uint8_t g_rot_phase = ROT_TAG;

// Index of the favourite custom GIF, or -1 (not a GIF, or it was deleted).
static int favGifIndex() {
  const String &name = settingsFavGif();
  if (!name.length()) {
    return -1;
  }
  for (int i = 0; i < gifsCount(); i++) {
    if (gifsName(i) == name) {
      return i;
    }
  }
  return -1;
}

static bool rotating() {
  return settingsAutoMode() && (settingsFavToy() >= 0 || favGifIndex() >= 0);
}

// Goose-flagged roster entries alternate nametag (30 s) with the shame goose (10 s).
// It overrides the Silly rotation and can't be switched off.
static bool gooseBadge() {
  const Attendee *me = rosterMe();
  return me && me->goose;
}

static bool cycling() { return gooseBadge() || rotating(); }

static void resetRotation() {
  g_rot_at = millis() ? millis() : 1;
  g_rot_phase = ROT_TAG;
}

static uint8_t nextRotPhase(uint8_t p) {
  if (p == ROT_TAG && gooseBadge()) {
    return ROT_GOOSE;
  }
  if (p != ROT_FAV && rotating()) {
    return ROT_FAV;
  }
  return ROT_TAG;
}

static uint32_t rotDwell(uint8_t p) {
  if (p == ROT_GOOSE) {
    return kGooseShameMs;
  }
  if (p == ROT_FAV) {
    return kRotateToyMs;
  }
  return gooseBadge() ? kGooseTagMs : kRotateTagMs;
}

static void startTagAnim() {
  g_tag_anim_at = millis() ? millis() : 1;
  bumpTagIdle();
  uiNeedRedraw();
}

static int tagAnimMs() {
  if (!g_tag_anim_at) {
    return -1;
  }
  uint32_t dt = millis() - g_tag_anim_at;
  if (dt >= kTagIdleAnimMs) {
    g_tag_anim_at = 0;
    uiNeedRedraw();
    return -1;
  }
  return (int)dt;
}

static void drawCurrentFace() {
  int page = settingsFacePage();
  uint16_t color = settingsColorRgb();
  if (g_clock_mode) {
    uiDrawClock();
    return;
  }
  if (g_rot_phase == ROT_GOOSE && gooseBadge()) {
    const Attendee *me = rosterMe();
    uiDrawGoose(faceAnimMs(), me ? me->sign.c_str() : nullptr);
    return;
  }
  if (g_rot_phase == ROT_FAV && rotating()) {
    int gif = favGifIndex();
    if (gif >= 0) {
      uiDrawCustomGif(gifsPath(gif).c_str(), -1);
    } else {
      uiDrawEgg(faceAnimMs(), settingsFavToy(), false, color, false, false);
    }
    return;
  }
  if (faceIsClassic(page)) {
    String name = settingsDisplayName();
    if (page != 0 && !socialStylesUnlocked()) {
      // Locked style: show it dimmed with the hint; loop() snaps back to seven-segment.
      uiSetStyleLocked(true);
      uiDrawNametag(name.c_str(), color, (uint8_t)page, -1);
      uiSetStyleLocked(false);
      return;
    }
    uiDrawNametag(name.c_str(), color, (uint8_t)page, faceHasIdleAnim(page) ? tagAnimMs() : -1);
    return;
  }
  String name = settingsDisplayName();
  uiDrawNametag(name.c_str(), color, 0, -1);
}

// Nametag / menu / settings / eggs: radio fully off.
// Friends brings up ESP-NOW only; scoreboard brings up STA only.
// The Silly Animation inflater (miniz) keeps ~11 KB of tables on the stack; the default
// 8 KB loop stack overflowed on every frame and crashed after a few swipes.
SET_LOOP_TASK_STACK_SIZE(24 * 1024);

static bool g_bj_open = false;
static bool g_bird_open = false;
static bool g_ob_open = false;
static bool g_hs_open = false;

// ---- power: backlight dimming and CPU clock ----
static uint32_t g_input_at = 0;
static uint8_t g_bright_now = kBrightFull;
static uint32_t g_cpu_mhz = kBusyCpuMhz;

static void setBacklight(uint8_t level) {
  if (g_display_up && level != g_bright_now) {
    g_bright_now = level;
    tft.setBrightness(level);
  }
}

// Any touch or button press: full brightness again, and the idle timer restarts.
static void noteInput() {
  g_input_at = millis() ? millis() : 1;
  setBacklight(kBrightFull);
}

static void radiosOff() {
  // Leaving Blackjack: tell the table while the radio is still up.
  if (g_bj_open) {
    bjClose();
    g_bj_open = false;
  }
  if (g_bird_open) {
    birdClose();
    g_bird_open = false;
  }
  if (g_ob_open) {
    outbreakClose();
    uiSetOutbreakOverlay(false);
    g_ob_open = false;
  }
  if (g_hs_open) {
    hideseekClose();
    g_hs_open = false;
  }
  radioSetEnabled(false);
  radioStop();
  netDisconnect();
}

static void goNametag() {
  radiosOff();
  clearHunt();
  g_mode = rosterMe() ? Mode::Nametag : Mode::Unprovisioned;
  g_tag_anim_at = 0;
  g_clock_mode = false;
  resetRotation();
  markFaceAnim();
  bumpTagIdle();
  uiNeedRedraw();
}

static void goMenu() {
  radiosOff();
  g_mode = Mode::Menu;
  uiNeedRedraw();
}

static void goMetList() {
  radiosOff();
  g_met_scroll = 0;
  g_mode = Mode::MetList;
  uiNeedRedraw();
}

static void goSettings(bool reset_page = false) {
  radiosOff();
  if (reset_page) {
    g_settings_page = 0;
  }
  g_mode = Mode::Settings;
  uiNeedRedraw();
}

static void cycleSettingsPage(int dir) {
  int n = g_settings_page + dir;
  while (n < 0) {
    n += kSettingsPageCount;
  }
  g_settings_page = n % kSettingsPageCount;
  uiNeedRedraw();
}

static void startFriends() {
  if (!rosterMe()) {
    goNametag();
    return;
  }
  // ESP-NOW only — tear down any STA session first.
  netDisconnect();
  radioStop();
  radioStart();
  radioSetEnabled(true);
  clearHunt();
  g_mode = Mode::Nearby;
  uiNeedRedraw();
}

static void goNearby() {
  if (!rosterMe()) {
    goNametag();
    return;
  }
  if (netBusy()) {
    g_mode = Mode::WifiWarn;
    uiNeedRedraw();
    return;
  }
  startFriends();
}

// Open on today (or the nearest event day) scrolled to the session running now.
static void goSchedule() {
  radiosOff();
  const std::vector<ScheduleDay> &days = scheduleDays();
  int day = 0;
  int minute = 0;
  g_sched_scroll = 0;
  if (scheduleNow(&day, &minute) && day >= 0) {
    g_sched_day = day;
    const auto &items = days[day].items;
    for (size_t i = 0; i < items.size(); i++) {
      if (minute < items[i].end) {
        g_sched_scroll = (int)(i > 0 ? i - 1 : 0) * 26;
        break;
      }
    }
  } else if (g_sched_day >= (int)days.size()) {
    g_sched_day = 0;
  }
  g_mode = Mode::Schedule;
  uiNeedRedraw();
}

static void goDiversions() {
  radiosOff();
  g_mode = Mode::Diversions;
  uiNeedRedraw();
}

static void goGames() {
  radiosOff();
  g_mode = Mode::Games;
  uiNeedRedraw();
}

// Klippy Bird: ESP-NOW up (no Find Friends beacons) so best scores can be swapped.
static void goKlippyBird() {
  radiosOff();
  netDisconnect();
  radioStart();
  radioSetEnabled(false);
  birdOpen();
  g_bird_open = true;
  g_mode = Mode::KlippyBird;
  uiNeedRedraw();
}

// Outbreak: your nametag with a heartbeat over it, ESP-NOW up (no Find Friends beacons) so
// players can trade status. The radio is only on while this screen is open.
static void goOutbreak() {
  if (!rosterMe()) {
    return;
  }
  radiosOff();
  netDisconnect();
  radioStart();
  radioSetEnabled(false);
  outbreakOpen();
  uiSetOutbreakOverlay(true);
  g_ob_open = true;
  g_mode = Mode::Outbreak;
  uiNeedRedraw();
}

// Hide and Seek: ESP-NOW up (no Find Friends beacons) while the screen is open; it pings,
// seekers share their readings.
static void goHideSeek() {
  if (!rosterMe()) {
    return;
  }
  radiosOff();
  netDisconnect();
  radioStart();
  radioSetEnabled(false);
  hideseekOpen();
  g_hs_open = true;
  g_mode = Mode::HideSeek;
  uiNeedRedraw();
}

// Blackjack lobby: ESP-NOW up (Wi-Fi off) without Find Friends beacons.
static void goBlackjack() {
  if (!rosterMe()) {
    goNametag();
    return;
  }
  radiosOff();
  netDisconnect();
  radioStart();
  radioSetEnabled(false);
  bjOpen();
  g_bj_open = true;
  g_mode = Mode::Blackjack;
  uiNeedRedraw();
}

static void goPostConMenu() {
  radiosOff();
  g_mode = Mode::PostConMenu;
  uiNeedRedraw();
}

static void refreshSpotify(bool force_art = false, bool keep_err = false);
static void refreshUpnp(bool force_art = false, bool keep_err = false);

static void enterScoreboard() {
  g_mode = Mode::LeaguePick;
  uiNeedRedraw();
}

static void enterSpotify() {
  g_mode = Mode::Spotify;
  g_spot_at = 0;
  refreshSpotify(true);
}

static void enterUpnp(bool rescan) {
  g_mode = Mode::Upnp;
  g_upnp_art.clear();
  g_upnp_art_url = "";
  g_upnp_err = "";
  g_upnp_at = 0;
  if (rescan || !upnpDiscoverDone()) {
    upnpStartDiscover();
  } else {
    refreshUpnp(true);
  }
  uiNeedRedraw();
}

/** If STA is down but credentials exist, show wake UI instead of "WiFi required". */
static bool ensureWifiOrWake(WifiWakeDest dest, const char *need_detail, bool upnp_rescan = true) {
  radioSetEnabled(false);
  radioStop();
  if (!settingsHasWifi()) {
    g_wifi_need_detail = need_detail;
    g_wifi_wake_dest = WifiWakeDest::None;
    g_mode = Mode::MlbNeedWifi;
    uiNeedRedraw();
    return false;
  }
  if (netConnected()) {
    return true;
  }
  if (netState() != NetState::Connecting) {
    netConnect(settingsWifiSsid(), settingsWifiPass());
  }
  g_wifi_wake_dest = dest;
  g_upnp_wake_rescan = upnp_rescan;
  g_wifi_need_detail = need_detail;
  g_mode = Mode::WifiWake;
  uiNeedRedraw();
  return false;
}

static void finishWifiWake() {
  WifiWakeDest dest = g_wifi_wake_dest;
  bool upnp_rescan = g_upnp_wake_rescan;
  g_wifi_wake_dest = WifiWakeDest::None;
  switch (dest) {
    case WifiWakeDest::Scoreboard:
      enterScoreboard();
      break;
    case WifiWakeDest::Spotify:
      enterSpotify();
      break;
    case WifiWakeDest::Upnp:
      enterUpnp(upnp_rescan);
      break;
    default:
      goPostConMenu();
      break;
  }
}

static void goScoreboard() {
  if (!ensureWifiOrWake(WifiWakeDest::Scoreboard, "Scoreboard needs a network.")) {
    return;
  }
  enterScoreboard();
}

static void refreshSpotify(bool force_art, bool keep_err) {
  String kept = keep_err ? g_spot_err : String("");
  g_spot_loading = true;
  if (!keep_err) {
    g_spot_err = "";
  }
  uiNeedRedraw();
  if (!spotifyAuthed()) {
    g_spot_np = SpotifyNowPlaying{};
    g_spot_art.clear();
    g_spot_art_url = "";
    g_spot_loading = false;
    g_spot_at = millis();
    uiNeedRedraw();
    return;
  }
  if (!netConnected()) {
    g_spot_err = "Waiting for WiFi...";
    g_spot_loading = false;
    g_spot_at = millis();
    uiNeedRedraw();
    return;
  }
  SpotifyNowPlaying np;
  if (!spotifyFetchNowPlaying(&np)) {
    const char *e = spotifyLastError();
    g_spot_err = (e && e[0]) ? e : "Spotify unavailable";
    g_spot_loading = false;
    g_spot_at = millis();
    uiNeedRedraw();
    return;
  }
  g_spot_np = np;
  // Immediate-use art only — drop previous JPEG when the track art URL changes.
  if (force_art || np.art_url != g_spot_art_url) {
    g_spot_art.clear();
    g_spot_art_url = np.art_url;
    if (np.art_url.length()) {
      spotifyFetchArt(np.art_url, &g_spot_art);
    }
  }
  if (keep_err && kept.length()) {
    g_spot_err = kept;
  } else if (!keep_err) {
    g_spot_err = "";
  }
  g_spot_loading = false;
  g_spot_at = millis();
  uiNeedRedraw();
}

static void goSpotify() {
  if (spotifyAuthed()) {
    if (!ensureWifiOrWake(WifiWakeDest::Spotify, "Spotify needs a network.")) {
      return;
    }
  } else {
    radioSetEnabled(false);
    radioStop();
  }
  enterSpotify();
}

static void refreshUpnp(bool force_art, bool keep_err) {
  String kept = keep_err ? g_upnp_err : String("");
  g_upnp_loading = true;
  if (!keep_err) {
    g_upnp_err = "";
  }
  uiNeedRedraw();
  if (!upnpCount()) {
    g_upnp_art.clear();
    g_upnp_art_url = "";
    g_upnp_loading = false;
    g_upnp_at = millis();
    uiNeedRedraw();
    return;
  }
  if (!upnpRefreshSelected()) {
    const char *e = upnpLastError();
    g_upnp_err = (e && e[0]) ? e : "UPnP failed";
    g_upnp_loading = false;
    g_upnp_at = millis();
    uiNeedRedraw();
    return;
  }
  const UpnpRenderer *r = upnpCurrent();
  if (r && (force_art || r->art_url != g_upnp_art_url)) {
    g_upnp_art.clear();
    g_upnp_art_url = r->art_url;
    if (r->art_url.length()) {
      upnpFetchArt(r->art_url, &g_upnp_art);
    }
  }
  if (keep_err && kept.length()) {
    g_upnp_err = kept;
  } else if (!keep_err) {
    g_upnp_err = "";
  }
  g_upnp_loading = false;
  g_upnp_at = millis();
  uiNeedRedraw();
}

static void goUpnp(bool rescan = true) {
  if (!ensureWifiOrWake(WifiWakeDest::Upnp, "LAN Now Playing needs WiFi.", rescan)) {
    return;
  }
  enterUpnp(rescan);
}

static void goLeagueGames(League league) {
  g_league = league;
  g_mlb_loading = true;
  g_mlb_err = "";
  g_mlb_games.clear();
  g_list_scroll = 0;
  g_mode = Mode::MlbGames;
  uiNeedRedraw();
}

static void loadMlbGames() {
  g_mlb_err = "";
  if (!sportsLoadGames(g_league, &g_mlb_games, &g_mlb_err)) {
    g_mlb_games.clear();
  }
  g_mlb_loading = false;
  uiNeedRedraw();
}

static void loadMlbBoard() {
  if (!g_mlb_board.pk) {
    return;
  }
  sportsLoadBoard(&g_mlb_board);
  g_mlb_at = millis();
  uiNeedRedraw();
}

static void lockPeer(const Peer *p) {
  g_lock = *p;
  g_have_lock = true;
  g_me_ready = false;
  g_them_ready = false;
}

static void enterHunt(const char *word, uint8_t color) {
  strncpy(g_hunt_word, word ? word : "", kWordLen - 1);
  g_hunt_word[kWordLen - 1] = 0;
  g_hunt_color = color % kColorCount;
  g_lock.color = g_hunt_color;
  g_me_ready = false;
  g_them_ready = false;
  g_mode = Mode::Hunt;
  ledsBlink(3, 70, 80);
  uiNeedRedraw();
}

static void requestHunt(const Peer *p) {
  if (!p) {
    return;
  }
  lockPeer(p);
  g_hunt_offer_at = millis();
  g_mode = Mode::HuntWait;
  radioSend(g_lock.mac, MSG_HUNT_REQ);
  uiNeedRedraw();
}

static void offerHunt(const Peer *p) {
  if (!p) {
    return;
  }
  lockPeer(p);
  g_hunt_offer_at = millis();
  g_mode = Mode::HuntAsk;
  uiNeedRedraw();
}

static void acceptHunt() {
  if (!g_have_lock) {
    goNearby();
    return;
  }
  const char *word = huntRandomWord();
  uint8_t color = (uint8_t)(esp_random() % kColorCount);
  radioSend(g_lock.mac, MSG_HUNT_ACK, color, word);
  enterHunt(word, color);
}

static void declineHunt() {
  if (g_have_lock) {
    radioSend(g_lock.mac, MSG_HUNT_DECLINE);
  }
  goNearby();
}

static void cancelHunt() {
  if (g_have_lock) {
    radioSend(g_lock.mac, MSG_HUNT_CANCEL);
  }
  goNearby();
}

static void startQuiz() {
  if (!g_have_lock || !gameBuildQuiz(g_lock.id, &g_quiz)) {
    goNearby();
    return;
  }
  g_picked = -1;
  g_mode = Mode::Quiz;
  uiNeedRedraw();
}

static void applyRoundResult() {
  if (!g_have_lock) {
    return;
  }
  radioNoteCooldown(g_lock.id, millis() + kCooldownMs);
  g_visitor_score = g_visitor_score < 99 ? (uint8_t)(g_visitor_score + 1) : 99;
  if (g_picked == g_quiz.truth_index) {
    g_home_score = g_home_score < 99 ? (uint8_t)(g_home_score + 1) : 99;
  }
  scoreboardSet(g_home_score, g_visitor_score);
}

static void finishRound() {
  applyRoundResult();
  goNearby();
}

static void leaveHuntRadio() {
  if (g_mode == Mode::HuntAsk && g_have_lock) {
    radioSend(g_lock.mac, MSG_HUNT_DECLINE);
  } else if ((g_mode == Mode::Hunt || g_mode == Mode::HuntWait) && g_have_lock) {
    radioSend(g_lock.mac, MSG_HUNT_CANCEL);
  }
  if (g_mode == Mode::Reveal) {
    applyRoundResult();
  }
  clearHunt();
  radioSetEnabled(false);
}

static void leaveToMenu() {
  leaveHuntRadio();
  goMenu();
}

static bool g_skip_menu_release = false;

static void leaveToNametag() {
  leaveHuntRadio();
  g_skip_menu_release = true;
  goNametag();
}

static void onRadioMsg(const uint8_t *mac, const BadgeMsg &msg, int /*rssi*/) {
  memcpy((void *)g_pending_mac, mac, 6);
  memcpy((void *)&g_pending_msg, &msg, sizeof(msg));
  g_msg_pending = true;
}

static void handlePending() {
  if (!g_msg_pending) {
    return;
  }
  g_msg_pending = false;
  BadgeMsg msg = g_pending_msg;
  uint8_t mac[6];
  memcpy(mac, (const void *)g_pending_mac, 6);
  Peer *p = radioFindByMac(mac);
  if (!p) {
    p = radioFindById(msg.id);
  }

  switch (msg.type) {
    case MSG_HUNT_REQ:
      if (g_mode == Mode::Nearby && p) {
        offerHunt(p);
      }
      break;
    case MSG_HUNT_ACK:
      if (g_mode == Mode::HuntWait && lockIs(msg.id) && msg.word[0]) {
        enterHunt(msg.word, msg.color);
      }
      break;
    case MSG_HUNT_DECLINE:
      if (g_mode == Mode::HuntWait && lockIs(msg.id)) {
        radioNoteCooldown(msg.id, millis() + kCooldownMs);
        goNearby();
      }
      break;
    case MSG_HUNT_CANCEL:
      if (lockIs(msg.id) &&
          (g_mode == Mode::HuntAsk || g_mode == Mode::HuntWait || g_mode == Mode::Hunt)) {
        goNearby();
      }
      break;
    case MSG_MEET_READY:
      if (g_mode == Mode::Hunt && lockIs(msg.id)) {
        g_them_ready = true;
        if (g_me_ready) {
          startQuiz();
        } else {
          uiNeedRedraw();
        }
      }
      break;
    default:
      break;
  }
}

static void draw() {
  switch (g_mode) {
    case Mode::Unprovisioned:
      uiDrawUnprovisioned();
      break;
    case Mode::Nametag: {
      drawCurrentFace();
      break;
    }
    case Mode::Menu:
      uiDrawMenu();
      break;
    case Mode::MetList:
      uiDrawMetList(settingsMet(), &g_met_scroll);
      break;
    case Mode::Nearby:
      uiDrawNearby(radioNearby(true));
      break;
    case Mode::HuntWait:
      uiDrawHuntWait(g_have_lock ? &g_lock : nullptr);
      break;
    case Mode::HuntAsk:
      uiDrawHuntAsk();
      break;
    case Mode::Hunt:
      uiDrawHunt(g_have_lock ? &g_lock : nullptr, g_hunt_word,
                 kColors[g_hunt_color % kColorCount].rgb, g_me_ready, g_them_ready);
      break;
    case Mode::Quiz:
      uiDrawQuiz(g_quiz);
      break;
    case Mode::Reveal:
      uiDrawReveal(g_quiz, g_picked, kColors[g_hunt_color % kColorCount].rgb);
      break;
    case Mode::Settings:
      uiDrawSettings(g_settings_page);
      break;
    case Mode::WifiScan:
      uiDrawWifiScan(&g_list_scroll, g_wifi_scanning);
      break;
    case Mode::WifiKeyboard:
      uiDrawKeyboard(g_kb_ssid.c_str(), g_kb_pass, g_kb_shift, g_kb_num);
      break;
    case Mode::NickKeyboard:
      uiDrawKeyboard("Nickname", g_kb_pass, g_kb_shift, g_kb_num);
      break;
    case Mode::WifiWarn:
      uiDrawWifiWarn();
      break;
    case Mode::MlbNeedWifi:
      uiDrawMlbNeedWifi(g_wifi_need_detail);
      break;
    case Mode::WifiWake:
      uiDrawWifiWake(settingsWifiSsid().c_str());
      break;
    case Mode::LeaguePick:
      uiDrawLeagues();
      break;
    case Mode::MlbGames:
      uiDrawMlbGames(g_mlb_games, g_league, &g_list_scroll, g_mlb_loading, g_mlb_err.c_str());
      break;
    case Mode::MlbBoard:
      uiDrawMlbBoard(g_mlb_board);
      break;
    case Mode::Egg:
      g_mode = Mode::Nametag;
      drawCurrentFace();
      break;
    case Mode::Silly:
      {
        int n = (int)kToyFaceCount + gifsCount() + 1;
        if (g_silly_toy >= n) {
          g_silly_toy = 0;
        }
        uiSetSillyTotal(n);
        int custom = g_silly_toy - (int)kToyFaceCount;
        if (custom < 0) {
          uiCustomGifStop();
          uiDrawEgg(faceAnimMs(), g_silly_toy, false, settingsColorRgb(), false, true);
        } else if (custom < gifsCount()) {
          uiDrawCustomGif(gifsPath(custom).c_str(), g_silly_toy);
        } else {
          uiCustomGifStop();
          uiDrawGifHelp(g_silly_toy, gifsCount(), kGifMaxCount);
        }
      }
      break;
    case Mode::Queen:
      uiDrawQueen(millis() - g_queen_at);
      break;
    case Mode::Leaderboard:
      uiDrawLeaderboard();
      break;
    case Mode::Schedule:
      uiDrawSchedule(g_sched_day, &g_sched_scroll);
      break;
    case Mode::About:
      uiDrawAbout();
      break;
    case Mode::SleepConfirm:
      uiDrawSleepConfirm(&g_sleep_scroll);
      break;
    case Mode::Diversions:
      uiDrawDiversions();
      break;
    case Mode::Blackjack:
      uiDrawBlackjack();
      break;
    case Mode::Games:
      uiDrawGames();
      break;
    case Mode::KlippyBird:
      uiDrawKlippyBird();
      break;
    case Mode::Outbreak: {
      String name = settingsDisplayName();
      int page = settingsFacePage();
      bool classic = faceIsClassic(page) && (page == 0 || socialStylesUnlocked());
      uiDrawNametag(name.c_str(), settingsColorRgb(), classic ? (uint8_t)page : 0, -1);
      break;
    }
    case Mode::HideSeek:
      uiDrawHideSeek();
      break;
    case Mode::Achievements:
      uiDrawAchievements(&g_ach_scroll);
      break;
    case Mode::PostConMenu:
      uiDrawPostConMenu();
      break;
    case Mode::Spotify:
      uiDrawSpotify(g_spot_np, &g_spot_art, g_spot_loading,
                    g_spot_err.length() ? g_spot_err.c_str() : nullptr, spotifyAuthed());
      break;
    case Mode::Upnp:
      uiDrawUpnp(upnpCurrent(), upnpSelected(), (int)upnpCount(), &g_upnp_art, upnpDiscoverBusy(),
                 g_upnp_loading, g_upnp_err.length() ? g_upnp_err.c_str() : upnpLastError());
      break;
  }
}

static void joinWifi(const String &ssid, const String &pass, bool open) {
  settingsSetWifi(ssid, open ? String("") : pass);
  netConnect(ssid, open ? String("") : pass);
  g_mode = Mode::Settings;
  uiNeedRedraw();
}

static void sleepByChoice();

static void handleHit(int hit) {
  if (g_mode == Mode::KlippyBird) {
    // Anywhere flaps; "< Games" only on the title screen.
    if (hit == HIT_MENU_BACK && birdState().phase == BIRD_TITLE) {
      goGames();
    } else {
      birdTap();
    }
    uiNeedRedraw();
    return;
  }
  if (g_mode == Mode::Outbreak) {
    if (hit == HIT_MENU_BACK) {
      goGames();
    }
    return;
  }
  if (g_mode == Mode::HideSeek) {
    if (hit == HIT_MENU_BACK) {
      goDiversions();
    } else if (hit == HIT_HS_VIEW) {
      uiHideSeekToggleView();
      uiNeedRedraw();
    }
    return;
  }
  if (g_mode == Mode::Games) {
    if (hit == HIT_GAME_OUTBREAK) {
      goOutbreak();
    } else if (hit == HIT_GAME_BJ) {
      goBlackjack();
    } else if (hit == HIT_GAME_BIRD) {
      goKlippyBird();
    } else if (hit == HIT_MENU_BACK) {
      goDiversions();
    }
    return;
  }
  if (hit == HIT_NAMETAG_BACK && g_mode == Mode::Nearby) {
    goMenu();
    return;
  }
  if (hit == HIT_CANCEL && (g_mode == Mode::Hunt || g_mode == Mode::HuntWait)) {
    cancelHunt();
    return;
  }
  if (hit == HIT_FOUND_YES && g_mode == Mode::HuntAsk) {
    acceptHunt();
    return;
  }
  if (hit == HIT_FOUND_NO && g_mode == Mode::HuntAsk) {
    declineHunt();
    return;
  }
  if (hit == HIT_THATSYOU && g_mode == Mode::Hunt && g_have_lock && !g_me_ready) {
    g_me_ready = true;
    radioSend(g_lock.mac, MSG_MEET_READY);
    if (g_them_ready) {
      startQuiz();
    } else {
      uiNeedRedraw();
    }
    return;
  }
  if (hit >= HIT_NEARBY0 && hit < HIT_NEARBY0 + kNearbyRows && g_mode == Mode::Nearby) {
    auto peers = radioNearby();
    int idx = hit - HIT_NEARBY0;
    if (idx >= 0 && idx < (int)peers.size()) {
      if (radioCooldownRemaining(peers[idx]->id)) {
        return;
      }
      requestHunt(peers[idx]);
    }
    return;
  }
  if (hit >= HIT_FACT0 && hit < HIT_FACT0 + 3 && g_mode == Mode::Quiz) {
    g_picked = hit - HIT_FACT0;
    if (g_picked == g_quiz.truth_index) {
      settingsMarkMet(g_quiz.target_id.c_str());
      ledsBlink(5, 70, 70);
    } else {
      ledsBlink(2, 160, 240);
    }
    g_mode = Mode::Reveal;
    uiNeedRedraw();
    return;
  }
  if (hit == HIT_CONTINUE && g_mode == Mode::Reveal) {
    finishRound();
    return;
  }
  if (g_mode == Mode::Schedule && (hit == HIT_SCHED_PREV || hit == HIT_SCHED_NEXT)) {
    int n = (int)scheduleDays().size();
    int d = g_sched_day + (hit == HIT_SCHED_NEXT ? 1 : -1);
    if (d >= 0 && d < n) {
      g_sched_day = d;
      g_sched_scroll = 0;
      uiNeedRedraw();
    }
    return;
  }
  if (g_mode == Mode::Menu && hit == HIT_SLEEP_ASK) {
    g_sleep_scroll = 0;
    g_mode = Mode::SleepConfirm;
    uiNeedRedraw();
    return;
  }
  if (g_mode == Mode::SleepConfirm) {
    if (hit == HIT_SLEEP_YES) {
      sleepByChoice();
    } else if (hit == HIT_SLEEP_NO) {
      goMenu();
    }
    return;
  }
  if (hit == HIT_MENU_BACK &&
      (g_mode == Mode::Menu || g_mode == Mode::PostConMenu || g_mode == Mode::Diversions)) {
    if (g_mode == Mode::Menu) {
      goNametag();
    } else if (g_mode == Mode::Diversions) {
      goPostConMenu();
    } else {
      goMenu();
    }
    return;
  }
  if (g_mode == Mode::Diversions && hit >= HIT_DIV0 && hit < HIT_DIV0 + DIV_COUNT) {
    switch (hit - HIT_DIV0) {
      case DIV_SCORES:
        goScoreboard();
        break;
      case DIV_SILLY:
        goSilly();
        break;
      case DIV_GAMES:
        goGames();
        break;
      case DIV_HIDESEEK:
        goHideSeek();
        break;
      default:
        break;
    }
    return;
  }
  if (g_mode == Mode::Blackjack) {
    if (hit == HIT_BJ_START) {
      bjStartTable();
    } else if (hit >= HIT_BJ_TABLE0 && hit < HIT_BJ_TABLE0 + kBjListShown) {
      std::vector<BjListing> tables = bjListings();
      int i = hit - HIT_BJ_TABLE0;
      if (i < (int)tables.size()) {
        bjJoin(tables[i].table_id);
      }
    } else if (hit == HIT_BJ_END) {
      if (bjRole() == BJ_HOST) {
        bjEndTable();
      } else {
        bjLeave();
      }
    } else if (hit == HIT_BJ_MINUS) {
      bjBetDelta(-5);
    } else if (hit == HIT_BJ_PLUS) {
      bjBetDelta(5);
    } else if (hit == HIT_BJ_HIT) {
      bjHit();
    } else if (hit == HIT_BJ_STAND) {
      bjStand();
    } else if (hit == HIT_BJ_SPLIT) {
      bjSplit();
    }
    uiNeedRedraw();
    return;
  }
  if (g_mode == Mode::Menu && hit >= HIT_MENU0 && hit < HIT_MENU0 + MENU_COUNT) {
    switch (hit - HIT_MENU0) {
      case MENU_FRIENDS:
        goNearby();
        break;
      case MENU_REGISTRY:
        goMetList();
        break;
      case MENU_SCHEDULE:
        goSchedule();
        break;
      case MENU_LEADERS:
        radiosOff();
        g_mode = Mode::Leaderboard;
        uiNeedRedraw();
        break;
      case MENU_AWARDS:
        radiosOff();
        g_ach_scroll = 0;
        g_mode = Mode::Achievements;
        uiNeedRedraw();
        break;
      case MENU_MORE:
        goPostConMenu();
        break;
      default:
        break;
    }
    return;
  }
  if (g_mode == Mode::PostConMenu && hit >= HIT_POSTCON0 && hit < HIT_POSTCON0 + POSTCON_COUNT) {
    switch (hit - HIT_POSTCON0) {
      case POSTCON_DIVERSIONS:
        goDiversions();
        break;
      case POSTCON_SETTINGS:
        goSettings(true);
        break;
      case POSTCON_ABOUT:
        radiosOff();
        g_mode = Mode::About;
        uiNeedRedraw();
        break;
      case POSTCON_SPOTIFY:
        goSpotify();
        break;
      case POSTCON_UPNP:
        goUpnp(true);
        break;
      default:
        break;
    }
    return;
  }
  if (g_mode == Mode::Upnp) {
    if (hit == HIT_UPNP_REFRESH) {
      goUpnp(true);
      return;
    }
    if (!upnpCount()) {
      return;
    }
    if (hit == HIT_UPNP_PREV) {
      bool ok = upnpPrevious();
      if (!ok && upnpLastError()[0]) {
        g_upnp_err = upnpLastError();
      }
      delay(200);
      refreshUpnp(true, !ok);
      return;
    }
    if (hit == HIT_UPNP_NEXT) {
      bool ok = upnpNext();
      if (!ok && upnpLastError()[0]) {
        g_upnp_err = upnpLastError();
      }
      delay(200);
      refreshUpnp(true, !ok);
      return;
    }
    if (hit == HIT_UPNP_PLAY) {
      const UpnpRenderer *r = upnpCurrent();
      bool playing = r && r->transport == "PLAYING";
      bool ok = playing ? upnpPause() : upnpPlay();
      if (!ok && upnpLastError()[0]) {
        g_upnp_err = upnpLastError();
      }
      delay(200);
      refreshUpnp(false, !ok);
      return;
    }
  }
  if (g_mode == Mode::Spotify) {
    if (hit == HIT_SPOTIFY_LOGOUT) {
      spotifyClearAuth();
      g_spot_np = SpotifyNowPlaying{};
      g_spot_art.clear();
      g_spot_art_url = "";
      g_spot_err = "";
      uiNeedRedraw();
      return;
    }
    if (!spotifyAuthed()) {
      return;
    }
    if (hit == HIT_SPOTIFY_PREV) {
      bool ok = spotifyPrevious();
      if (!ok && spotifyLastError()[0]) {
        g_spot_err = spotifyLastError();
      }
      refreshSpotify(true, !ok);
      return;
    }
    if (hit == HIT_SPOTIFY_NEXT) {
      bool ok = spotifyNext();
      if (!ok && spotifyLastError()[0]) {
        g_spot_err = spotifyLastError();
      }
      refreshSpotify(true, !ok);
      return;
    }
    if (hit == HIT_SPOTIFY_PLAY) {
      bool ok = g_spot_np.playing ? spotifyPause() : spotifyPlay();
      if (!ok && spotifyLastError()[0]) {
        g_spot_err = spotifyLastError();
      }
      // Brief settle so currently-playing reflects the command.
      delay(250);
      refreshSpotify(false, !ok);
      return;
    }
  }
  if (g_mode == Mode::Settings &&
      (hit == HIT_AUTO_TOGGLE || hit == HIT_FAV_PREV || hit == HIT_FAV_NEXT ||
       hit == HIT_FAV_PREVIEW)) {
    // Favourite index: built-ins 0..kToyFaceCount-1, then custom GIFs.
    const int n = (int)kToyFaceCount + gifsCount();
    int sel = settingsFavToy() >= 0 ? settingsFavToy()
                                    : (favGifIndex() >= 0 ? (int)kToyFaceCount + favGifIndex() : -1);
    auto pick = [&](int i) {
      if (i < (int)kToyFaceCount) {
        settingsSetFavToy(i);
      } else {
        settingsSetFavGif(gifsName(i - (int)kToyFaceCount));
      }
    };
    if (hit == HIT_AUTO_TOGGLE) {
      bool on = !settingsAutoMode();
      if (on && sel < 0) {
        pick(0);  // nothing chosen yet: start with the first animation
      }
      settingsSetAutoMode(on);
    } else if (hit == HIT_FAV_PREV || hit == HIT_FAV_NEXT) {
      int step = hit == HIT_FAV_NEXT ? 1 : -1;
      pick(((sel < 0 ? 0 : sel + step) % n + n) % n);
    } else if (hit == HIT_FAV_PREVIEW && sel >= 0) {
      g_silly_toy = sel;
      goSilly();
      return;
    }
    resetRotation();
    g_settings_page = 2;
    uiNeedRedraw();
    return;
  }
  if (g_mode == Mode::Settings) {
    if (hit == HIT_NICK_EDIT) {
      g_settings_page = 0;
      g_kb_ssid = "Nickname";
      g_kb_pass = settingsNickname();
      g_kb_shift = false;
      g_kb_num = false;
      g_mode = Mode::NickKeyboard;
      uiNeedRedraw();
      return;
    }
    if (hit == HIT_NICK_CLEAR) {
      g_settings_page = 0;
      settingsClearNickname();
      uiNeedRedraw();
      return;
    }
    if (hit == HIT_WIFI_SCAN) {
      g_settings_page = 1;
      g_mode = Mode::WifiScan;
      g_list_scroll = 0;
      g_wifi_scanning = true;
      uiNeedRedraw();
      if (uiConsumeRedraw()) {
        draw();
      }
      netScan();
      g_wifi_scanning = false;
      uiNeedRedraw();
      return;
    }
    if (hit == HIT_WIFI_DISCONNECT) {
      g_settings_page = 1;
      radiosOff();
      uiNeedRedraw();
      return;
    }
    if (hit == HIT_WIFI_CONNECT && settingsHasWifi()) {
      g_settings_page = 1;
      netConnect(settingsWifiSsid(), settingsWifiPass());
      uiNeedRedraw();
      return;
    }
  }
  if (g_mode == Mode::WifiWarn) {
    if (hit == HIT_WARN_YES) {
      startFriends();
    } else if (hit == HIT_WARN_NO) {
      goMenu();
    }
    return;
  }
  if (hit == HIT_GOTO_SETTINGS && g_mode == Mode::MlbNeedWifi) {
    g_settings_page = 1;
    goSettings();
    return;
  }
  if (g_mode == Mode::WifiScan && hit >= HIT_WIFI0 && hit < HIT_WIFI0 + (int)kMaxWifiAps) {
    int idx = hit - HIT_WIFI0;
    const auto &aps = netAps();
    if (idx >= 0 && idx < (int)aps.size()) {
      g_kb_ssid = aps[idx].ssid;
      g_kb_pass = "";
      g_kb_shift = false;
      g_kb_num = false;
      if (aps[idx].open) {
        joinWifi(g_kb_ssid, "", true);
      } else {
        g_mode = Mode::WifiKeyboard;
        uiNeedRedraw();
      }
    }
    return;
  }
  if (g_mode == Mode::WifiKeyboard || g_mode == Mode::NickKeyboard) {
    const int max_len = (g_mode == Mode::NickKeyboard) ? 16 : 63;
    if (hit == HIT_KB_SHIFT) {
      g_kb_shift = !g_kb_shift;
      if (g_kb_num) {
        g_kb_num = false;
      }
      uiNeedRedraw();
      return;
    }
    if (hit == HIT_KB_NUM) {
      g_kb_num = !g_kb_num;
      uiNeedRedraw();
      return;
    }
    if (hit == HIT_KB_BKSP) {
      if (g_kb_pass.length()) {
        g_kb_pass.remove(g_kb_pass.length() - 1);
        uiNeedRedraw();
      }
      return;
    }
    if (hit == HIT_KB_SPACE && g_kb_pass.length() < max_len) {
      g_kb_pass += ' ';
      uiNeedRedraw();
      return;
    }
    if (hit == HIT_KB_JOIN) {
      if (g_mode == Mode::NickKeyboard) {
        settingsSetNickname(g_kb_pass);
        goSettings();
      } else {
        joinWifi(g_kb_ssid, g_kb_pass, false);
      }
      return;
    }
    if (hit >= HIT_CHAR0 && hit < HIT_CHAR0 + 128 && g_kb_pass.length() < max_len) {
      g_kb_pass += (char)(hit - HIT_CHAR0);
      g_kb_shift = false;
      uiNeedRedraw();
    }
    return;
  }
  if (g_mode == Mode::LeaguePick && hit >= HIT_LEAGUE0 && hit < HIT_LEAGUE0 + (int)kLeagueCount) {
    goLeagueGames((League)(hit - HIT_LEAGUE0));
    return;
  }
  if (g_mode == Mode::MlbGames && hit >= HIT_MLB0 && hit < HIT_MLB0 + (int)kMaxMlbGames) {
    int idx = hit - HIT_MLB0;
    if (idx >= 0 && idx < (int)g_mlb_games.size()) {
      g_mlb_board = MlbBoard{};
      g_mlb_board.league = g_mlb_games[idx].league;
      g_mlb_board.pk = g_mlb_games[idx].pk;
      g_mlb_board.away = g_mlb_games[idx].away;
      g_mlb_board.home = g_mlb_games[idx].home;
      g_mlb_board.away_name = g_mlb_games[idx].away_name;
      g_mlb_board.home_name = g_mlb_games[idx].home_name;
      g_mlb_board.away_id = g_mlb_games[idx].away_id;
      g_mlb_board.home_id = g_mlb_games[idx].home_id;
      g_mlb_board.away_color = g_mlb_games[idx].away_color;
      g_mlb_board.home_color = g_mlb_games[idx].home_color;
      g_mlb_board.away_runs = g_mlb_games[idx].away_score;
      g_mlb_board.home_runs = g_mlb_games[idx].home_score;
      g_mlb_board.away_logo.clear();
      g_mlb_board.home_logo.clear();
      g_mlb_board.ok = false;
      g_mode = Mode::MlbBoard;
      uiNeedRedraw();
      if (uiConsumeRedraw()) {
        draw();
      }
      loadMlbBoard();
      sportsLoadLogos(&g_mlb_board);
      uiNeedRedraw();
    }
  }
}

static void goBack() {
  switch (g_mode) {
    case Mode::Nametag:
    case Mode::Unprovisioned:
      break;
    case Mode::Menu:
      goNametag();
      break;
    case Mode::Settings:
    case Mode::About:
    case Mode::Diversions:
      goPostConMenu();
      break;
    case Mode::MlbNeedWifi:
    case Mode::LeaguePick:
      g_wifi_wake_dest = WifiWakeDest::None;
      goDiversions();
      break;
    case Mode::Blackjack:
      // At a table: leave (or end) it first; from the lobby, back to Diversions.
      if (bjRole() == BJ_HOST) {
        bjEndTable();
        uiNeedRedraw();
      } else if (bjRole() == BJ_PLAYER) {
        bjLeave();
        uiNeedRedraw();
      } else {
        goGames();
      }
      break;
    case Mode::Games:
    case Mode::HideSeek:
      goDiversions();
      break;
    case Mode::KlippyBird:
    case Mode::Outbreak:
      goGames();
      break;
    case Mode::MetList:
    case Mode::Leaderboard:
    case Mode::Achievements:
    case Mode::Schedule:
    case Mode::SleepConfirm:
    case Mode::WifiWarn:
    case Mode::WifiWake:
      g_wifi_wake_dest = WifiWakeDest::None;
      goMenu();
      break;
    case Mode::PostConMenu:
      goMenu();
      break;
    case Mode::MlbGames:
      g_mode = Mode::LeaguePick;
      uiNeedRedraw();
      break;
    case Mode::WifiScan:
      goSettings();
      break;
    case Mode::WifiKeyboard:
      g_mode = Mode::WifiScan;
      uiNeedRedraw();
      break;
    case Mode::NickKeyboard:
      goSettings();
      break;
    case Mode::MlbBoard:
      g_mode = Mode::MlbGames;
      uiNeedRedraw();
      break;
    case Mode::Spotify:
      g_spot_art.clear();
      g_spot_art_url = "";
      goPostConMenu();
      break;
    case Mode::Upnp:
      upnpReset();
      g_upnp_art.clear();
      g_upnp_art_url = "";
      goPostConMenu();
      break;
    case Mode::Egg:
      g_clock_mode = false;
      goNametag();
      break;
    case Mode::Silly:
      goDiversions();
      break;
    case Mode::Queen:
      stopQueen();
      break;
    case Mode::Nearby:
      goMenu();
      break;
    case Mode::HuntWait:
    case Mode::Hunt:
      cancelHunt();
      break;
    case Mode::HuntAsk:
      declineHunt();
      break;
    case Mode::Quiz:
      goNearby();
      break;
    case Mode::Reveal:
      finishRound();
      break;
  }
}

static bool g_boot_down = false;

// Queen easter egg: hold USER 10 s. Any button press afterwards goes back to the nametag.
static bool g_queen_swallow = false;

static void startQueen() {
  if (g_mode == Mode::Queen) {
    return;
  }
  radiosOff();
  clearHunt();
  g_clock_mode = false;
  g_mode = Mode::Queen;
  g_queen_at = millis() ? millis() : 1;
  g_queen_swallow = true;
  socialUnlock(ACH_ROYALTY);
  uiQueenStart();
  uiNeedRedraw();
}

static void stopQueen() {
  uiQueenStop();
  g_skip_menu_release = true;
  goNametag();
}

static void goSilly() {
  radiosOff();
  clearHunt();
  g_mode = Mode::Silly;
  markFaceAnim();
  uiNeedRedraw();
}

static void pollButtons() {
  bool user = digitalRead(PIN_BUTTON_USER) == LOW;
  bool boot = digitalRead(PIN_BUTTON_BOOT) == LOW;
  if (user || boot) {
    noteInput();
  }
  uint32_t now = millis();
  static uint32_t last_menu = 0;
  static uint32_t last_back = 0;
  static bool chord_fired = false;
  static bool user_saw_boot = false;
  static uint32_t user_down_at = 0;
  static bool unlock_hold_fired = false;

  if (user) {
    if (!g_btn_down) {
      user_down_at = now;
      unlock_hold_fired = false;
    } else if (!unlock_hold_fired && g_mode == Mode::Nametag && now - user_down_at >= kQueenHoldMs) {
      unlock_hold_fired = true;
      startQueen();
    }
  }

  if (g_mode == Mode::Queen) {
    bool user_up = !user && g_btn_down;
    bool boot_up = !boot && g_boot_down;
    g_btn_down = user;
    g_boot_down = boot;
    if (g_queen_swallow) {
      // Ignore the release of the 10 s hold that started the Queen.
      if (user_up) {
        g_queen_swallow = false;
      }
      if (boot_up) {
        stopQueen();
      }
      return;
    }
    if (user_up || boot_up) {
      stopQueen();
    }
    return;
  }

  if (g_mode == Mode::Nametag) {
    if (user && boot) {
      user_saw_boot = true;
      // Chord no longer starts idle spectrum cinema.
      chord_fired = true;
    }
    if (!user && !boot) {
      chord_fired = false;
    }
    if (user && !g_btn_down) {
      user_saw_boot = boot;
    }
    if (!user && g_btn_down && !user_saw_boot && !g_skip_menu_release &&
        !unlock_hold_fired && now - last_menu >= kBtnBounceMs) {
      last_menu = now;
      leaveToMenu();
    }
    if (!user) {
      g_skip_menu_release = false;
    }
    g_btn_down = user;
    g_boot_down = boot;
    return;
  }

  bool click = user && !g_btn_down;
  g_btn_down = user;
  if (click && g_mode == Mode::KlippyBird) {
    birdTap();  // top button flaps too
    uiNeedRedraw();
  } else if (click && now - last_menu >= kBtnBounceMs) {
    last_menu = now;
    leaveToNametag();
  }

  bool boot_click = boot && !g_boot_down;
  g_boot_down = boot;
  if (boot_click && now - last_back >= kBtnBounceMs) {
    last_back = now;
    goBack();
  }
}

static String g_raw_line;
static uint32_t g_serial_at = 0;  // last USB serial input (pauses OTA checks)

static void pollSerial() {
  if (Serial.available()) {
    g_serial_at = millis() ? millis() : 1;
  }
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') {
      continue;
    }
    if (c != '\n') {
      if (g_raw_line.length() < 4096) {  // GIFDATA lines carry ~2.7 KB of base64
        g_raw_line += c;
      }
      continue;
    }
    String raw = g_raw_line;
    g_raw_line = "";
    raw.trim();
    if (!raw.length()) {
      continue;
    }
    int sp = raw.indexOf(' ');
    String verb = (sp < 0) ? raw : raw.substring(0, sp);
    String arg = (sp < 0) ? "" : raw.substring(sp + 1);
    verb.toUpperCase();
    if (gifsHandleCommand(verb, arg)) {
      continue;
    }
    if (verb == "WHO") {
      const Attendee *me = rosterMe();
      Serial.printf("ID %s NAME %s COLOR %u ROSTER %u FACTS %u MET %u WIFI %s BAT %dmV/%u %s%s TOUCH %s I2C%s"
                    " FW %s RV %u RK %s LED %s MAC %012llX\n",
                    rosterMyId().c_str(), me ? me->name.c_str() : "-",
                    settingsColor(), (unsigned)rosterCount(), (unsigned)factsCount(),
                    (unsigned)settingsMet().size(),
                    netConnected() ? netSsid().c_str() : "off",
                    batteryMillivolts(), (unsigned)batterySegments(),
                    batteryStatusName(), batteryUsb() ? " USB" : "",
                    touchOk() ? "ok" : "missing",
                    touchI2c()[0] ? touchI2c() : " none", kFirmwareVersion,
                    (unsigned)rosterVersion(), otaHasRosterKey() ? "yes" : "no",
                    ledsEnabled() ? "on" : "off",
                    (unsigned long long)__builtin_bswap64(ESP.getEfuseMac() << 16));
      Serial.println(outbreakDescribe());
      Serial.println(hideseekDescribe());
    } else if (verb == "LIST") {
      for (const auto &a : rosterAll()) {
        Serial.printf("%s\t%s\n", a.id.c_str(), a.name.c_str());
      }
    } else if (verb == "SETID") {
      arg.trim();
      if (!rosterFind(arg.c_str())) {
        Serial.printf("ERR unknown id %s\n", arg.c_str());
        continue;
      }
      rosterSetMyId(arg);
      settingsResetForNewPerson();
      socialReset();
      Serial.printf("OK %s %s\n", arg.c_str(), rosterMe()->name.c_str());
      goNametag();
    } else if (verb == "COLOR") {
      settingsSetColor((uint8_t)arg.toInt());
      Serial.println("OK");
      uiNeedRedraw();
    } else if (verb == "SETWIFI") {
      // SETWIFI <ssid>\t<password> — tab-separated so either side can contain spaces.
      int tab = arg.indexOf('\t');
      String ssid = tab < 0 ? arg : arg.substring(0, tab);
      String pass = tab < 0 ? "" : arg.substring(tab + 1);
      ssid.trim();
      if (!ssid.length()) {
        Serial.println("ERR SETWIFI needs <ssid>\\t<password>");
        continue;
      }
      settingsSetWifi(ssid, pass);
      Serial.printf("OK WIFI %s\n", ssid.c_str());  // never echo the password
    } else if (verb == "SETLEDS") {
      // SETLEDS <0|1> [brightness 0-2]: carrier LEDs on this badge (stored in NVS).
      arg.trim();
      int sp2 = arg.indexOf(' ');
      bool on = (sp2 < 0 ? arg : arg.substring(0, sp2)).toInt() != 0;
      int level = sp2 < 0 ? ledsBrightness() : arg.substring(sp2 + 1).toInt();
      if (level < 0 || level > 2) {
        level = 2;
      }
      ledsSetEnabled(on, (uint8_t)level);
      if (on) {
        ledsBlink(3, 120, 120);
      }
      Serial.printf("OK LEDS %s %d\n", on ? "on" : "off", level);
    } else if (verb == "SETROSTERKEY") {
      arg.trim();
      // Never echo the key.
      Serial.println(otaSetRosterKey(arg) ? "OK ROSTERKEY" : "ERR ROSTERKEY needs 64 hex chars");
    } else if (verb == "SETOTA") {
      arg.trim();
      otaSetUrl(arg);
      Serial.printf("OK OTA %s\n", otaUrl().length() ? otaUrl().c_str() : "(none)");
    } else if (verb == "OTACHECK") {
      otaCheckNow();
      Serial.printf("OK OTACHECK %s\n", netConnected() ? "now" : "on next Wi-Fi connect");
    } else if (verb == "SETNICK") {
      arg.trim();
      settingsSetNickname(arg);
      Serial.printf("OK NICK %s\n", settingsDisplayName().c_str());
      goNametag();
    } else if (verb == "SETSPOTIFY_CLIENT") {
      arg.trim();
      spotifySetClientId(arg);
      Serial.printf("OK client %s\n", spotifyHasClientId() ? "set" : "cleared");
      uiNeedRedraw();
    } else if (verb == "SETSPOTIFY_REFRESH") {
      arg.trim();
      spotifySetRefreshToken(arg);
      Serial.printf("OK refresh %s\n", spotifyHasRefresh() ? "set" : "cleared");
      uiNeedRedraw();
    } else if (verb == "SPOTIFY_CLEAR") {
      spotifyClearAuth();
      Serial.println("OK");
      uiNeedRedraw();
    } else {
      Serial.println(
          "cmds: WHO LIST SETID <id> COLOR <n> SETNICK <name> SETSPOTIFY_CLIENT <id> "
          "SETSPOTIFY_REFRESH <token> SPOTIFY_CLEAR SETWIFI <ssid>\\t<pass> SETROSTERKEY <hex> SETOTA <url> OTACHECK SETLEDS <0|1> [0-2]");
    }
  }
}

static void deepSleep(bool battery);

static void sleepForBattery(bool show) {
  ledsOff();
  batteryArmCutoff();
  Serial.printf("battery empty %dmV — sleep\n", batteryMillivolts());
  radioSetEnabled(false);
  radioStop();
  netDisconnect();
  if (show && g_display_up) {
    uiDrawDeadBattery();
    delay(2400);
    tft.sleep();
  }
  deepSleep(true);
}

// Panel power off, buttons armed as RTC wake sources, then deep sleep. `battery` keeps the
// 2-minute battery re-check and BOOT wake; a user sleep wakes only on the top (USER) button,
// since BOOT (GPIO 0) is also the download-mode strap.
static void deepSleep(bool battery) {
  ledsOff();
  digitalWrite(PIN_POWER_ON, LOW);
  gpio_hold_en((gpio_num_t)PIN_POWER_ON);
  gpio_deep_sleep_hold_en();
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  rtc_gpio_init((gpio_num_t)PIN_BUTTON_USER);
  rtc_gpio_init((gpio_num_t)PIN_BUTTON_BOOT);
  rtc_gpio_pullup_en((gpio_num_t)PIN_BUTTON_USER);
  rtc_gpio_pullup_en((gpio_num_t)PIN_BUTTON_BOOT);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_BUTTON_USER);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_BUTTON_BOOT);
  uint64_t pins = 1ULL << PIN_BUTTON_USER;
  if (battery) {
    pins |= 1ULL << PIN_BUTTON_BOOT;
    esp_sleep_enable_timer_wakeup(120ULL * 1000000ULL);
  }
  esp_sleep_enable_ext1_wakeup(pins, ESP_EXT1_WAKEUP_ANY_LOW);
  esp_deep_sleep_start();
}

static void sleepByChoice() {
  Serial.println("deep sleep (user)");
  socialFlush();
  settingsSave();
  radioSetEnabled(false);
  radioStop();
  netDisconnect();
  uiDrawSleeping();
  delay(1500);
  // Don't fall asleep on a held button; the next press is the wake.
  while (digitalRead(PIN_BUTTON_USER) == LOW) {
    delay(20);
  }
  tft.sleep();
  deepSleep(false);
}

void setup() {
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)PIN_POWER_ON);
  gpio_hold_dis((gpio_num_t)PIN_TOUCH_RES);
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);
  pinMode(PIN_BUTTON_USER, INPUT_PULLUP);
  pinMode(PIN_BUTTON_BOOT, INPUT_PULLUP);

  Serial.setRxBufferSize(8192);  // GIF uploads stream long lines
  Serial.begin(115200);
  delay(200);
  ledsLoad();
  ledsBegin();
  Serial.println("BLENNY ready");
  batteryBegin();

  auto bringUpDisplay = []() {
    if (g_display_up) {
      return;
    }
    tft.init();
    tft.setRotation(1);
    tft.setBrightness(200);
    uiBegin(&tft);
    touchBegin();
    g_display_up = true;
  };

  if (batteryEmpty()) {
    bool show = esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER;
    if (show) {
      bringUpDisplay();
    }
    sleepForBattery(show);
  }

  bringUpDisplay();

  uint32_t splash_at = millis();
  while (millis() - splash_at < kSplashMs) {
    ledsTick(LedShow::Breathe);
    uiDrawSplash(millis() - splash_at);
    int tx, ty;
    bool pressed = false;
    touchPoll(&tx, &ty, &pressed);
    if (millis() - splash_at > 350 &&
        (pressed || digitalRead(PIN_BUTTON_USER) == LOW)) {
      break;
    }
    delay(16);
  }

  ledsTick(LedShow::Idle);
  rosterLoad();
  birdLoad();
  outbreakLoad();
  hideseekLoad();
  gifsLoad();
  scheduleLoad();
  socialLoad();
  settingsLoad();
  otaBegin();
  spotifyLoad();
  radioOnIncoming(onRadioMsg);
  scoreboardBegin();
  // Boot quiet: no STA auto-connect, no ESP-NOW until Friends / Scoreboard.
  radiosOff();

  g_mode = rosterMe() ? Mode::Nametag : Mode::Unprovisioned;
  bumpTagIdle();
  uiNeedRedraw();
}

static bool isListMode() {
  return g_mode == Mode::MetList || g_mode == Mode::WifiScan || g_mode == Mode::MlbGames ||
         g_mode == Mode::Schedule || g_mode == Mode::SleepConfirm || g_mode == Mode::Achievements;
}

// Screens where the radio is idle, so the updater may bring Wi-Fi up briefly to check.
// Not during the game (ESP-NOW) or on screens that manage Wi-Fi themselves.
static bool otaMayWake() {
  switch (g_mode) {
    case Mode::Nametag:
    case Mode::Unprovisioned:
    case Mode::Menu:
    case Mode::Silly:
    case Mode::MetList:
    case Mode::PostConMenu:
    case Mode::Leaderboard:
    case Mode::Achievements:
    case Mode::Schedule:
    case Mode::About:
    case Mode::Diversions:
    case Mode::Games:
      return true;
    default:
      return false;
  }
}

static int *listScroll() {
  if (g_mode == Mode::Schedule) {
    return &g_sched_scroll;
  }
  if (g_mode == Mode::SleepConfirm) {
    return &g_sleep_scroll;
  }
  if (g_mode == Mode::Achievements) {
    return &g_ach_scroll;
  }
  return g_mode == Mode::MetList ? &g_met_scroll : &g_list_scroll;
}

// Carrier LEDs (when enabled): sweep while searching, pulse together during a hunt.
static LedShow ledShowForMode() {
  if (g_mode == Mode::Nearby) {
    return LedShow::Scanner;
  }
  if (g_mode == Mode::Hunt) {
    return LedShow::Unison;
  }
  return LedShow::Idle;
}

// Screens that only change on input (or once in a while): the CPU can idle at a low clock.
static bool screenIsStill() {
  switch (g_mode) {
    case Mode::Menu:
    case Mode::PostConMenu:
    case Mode::Diversions:
    case Mode::Settings:
    case Mode::MetList:
    case Mode::Leaderboard:
    case Mode::Achievements:
    case Mode::Schedule:
    case Mode::SleepConfirm:
    case Mode::Unprovisioned:
    case Mode::Games:
      return true;
    case Mode::Nametag: {
      int page = settingsFacePage();
      bool crt = page == (int)kTagStillStyleCount + 2;
      return !g_tag_anim_at && !cycling() && !g_clock_mode && !crt;
    }
    default:
      return false;
  }
}

// Dim the backlight when nobody's touched the badge for a minute (not mid-game), and run the
// CPU at 80 MHz with a short rest per loop on still screens. Wi-Fi, ESP-NOW and USB sessions
// keep full speed.
static void powerTick() {
  uint32_t now = millis();
  bool radios = netConnected() || radioEnabled() || g_bj_open;
  bool usb = g_serial_at && now - g_serial_at < 60000;
  bool watching = g_mode == Mode::Queen || g_mode == Mode::KlippyBird || g_mode == Mode::Outbreak ||
                  g_mode == Mode::HideSeek ||
                  g_mode == Mode::Nearby || g_mode == Mode::Hunt ||
                  g_mode == Mode::HuntWait || g_mode == Mode::HuntAsk || g_mode == Mode::Quiz ||
                  g_mode == Mode::Reveal;
  if (!g_input_at) {
    g_input_at = now ? now : 1;
  }
  if (!watching && !g_bj_open && now - g_input_at >= kDimAfterMs) {
    setBacklight(kBrightDim);
  }
  bool still = screenIsStill() && !radios && !usb;
  uint32_t want = still ? kIdleCpuMhz : kBusyCpuMhz;
  if (want != g_cpu_mhz) {
    g_cpu_mhz = want;
    setCpuFrequencyMhz(want);
  }
  if (still) {
    delay(15);  // nothing is moving; let the idle task run
  }
}

void loop() {
  ledsTick(ledShowForMode());
  pollSerial();
  {
    // A GIF arriving over USB: show progress, then open it in Silly Animations.
    static uint32_t last_prog = 0;
    int pct = 0;
    String name;
    if (gifsReceiving(&pct, &name)) {
      if (millis() - last_prog >= 250) {
        last_prog = millis();
        char d[40];
        snprintf(d, sizeof(d), "%s  %d%%", name.c_str(), pct);
        uiDrawOta("Receiving GIF", d, pct);
      }
      return;
    }
    String added = gifsTakeAdded();
    if (added.length()) {
      goSilly();
      for (int i = 0; i < gifsCount(); i++) {
        if (gifsName(i) == added) {
          g_silly_toy = (int)kToyFaceCount + i;
        }
      }
      uiToast((String("Added: ") + added).c_str(), 3000);
      uiNeedRedraw();
    }
  }
  handlePending();
  radioLoop();
  batteryPoll();
  if (batteryEmpty()) {
    sleepForBattery(true);
  }
  if (netLoop()) {
    uiNeedRedraw();
  }
  // Someone is talking to the badge over USB (station, GIF uploader): hold OTA entirely, since
  // a check blocks the loop for seconds mid-transfer. It resumes a minute after the last byte.
  if (!g_serial_at || millis() - g_serial_at >= 60000) {
    otaLoop(otaMayWake());
  }
  socialLoop();
  if (g_mode == Mode::WifiWake) {
    if (netConnected()) {
      finishWifiWake();
    } else if (netState() == NetState::Failed) {
      g_wifi_wake_dest = WifiWakeDest::None;
      g_mode = Mode::MlbNeedWifi;
      uiNeedRedraw();
    }
  }
  pollButtons();

  if (g_have_lock) {
    if (Peer *p = radioFindById(g_lock.id)) {
      g_lock.rssi = p->rssi;
      memcpy(g_lock.mac, p->mac, 6);
    }
  }

  if (g_mode == Mode::HuntWait || g_mode == Mode::HuntAsk) {
    if (millis() - g_hunt_offer_at > kHuntOfferMs) {
      if (g_mode == Mode::HuntAsk && g_have_lock) {
        radioSend(g_lock.mac, MSG_HUNT_DECLINE);
      } else if (g_mode == Mode::HuntWait && g_have_lock) {
        radioSend(g_lock.mac, MSG_HUNT_CANCEL);
      }
      goNearby();
    }
  }

  if (g_mode == Mode::MlbGames && g_mlb_loading) {
    if (uiConsumeRedraw()) {
      draw();
    }
    loadMlbGames();
  }

  if (g_mode == Mode::MlbBoard && g_mlb_board.pk && millis() - g_mlb_at >= kMlbRefreshMs) {
    loadMlbBoard();
  }

  if (g_mode == Mode::Spotify && spotifyAuthed() &&
      millis() - g_spot_at >= kSpotifyPollMs && !g_spot_loading) {
    refreshSpotify(false);
  }

  if (g_mode == Mode::Upnp) {
    if (upnpDiscoverBusy()) {
      upnpDiscoverLoop();
      uiNeedRedraw();
    } else if (upnpDiscoverDone() && upnpCount() && !g_upnp_loading &&
               millis() - g_upnp_at >= kUpnpUiPollMs) {
      refreshUpnp(false);
    } else if (upnpDiscoverDone() && !upnpCount() && !g_upnp_at) {
      g_upnp_at = millis();
      uiNeedRedraw();
    }
  }

  int tx, ty;
  bool pressed = false;
  bool down = touchPoll(&tx, &ty, &pressed);
  static bool was_down = false;
  static bool ignore_until_up = false;
  if (down) {
    bool was_dim = g_bright_now != kBrightFull;
    noteInput();
    if (was_dim && pressed) {
      ignore_until_up = true;  // first touch on a dimmed screen only wakes it
    }
  }
  static int list_down_y = 0;
  static int list_hit = HIT_NONE;
  static int tag_start_x = 0;
  static int tag_start_y = 0;
  static int tag_min_x = 0;
  static int tag_max_x = 0;
  static int tag_min_y = 0;
  static int tag_max_y = 0;
  static uint32_t tag_down_at = 0;
  static bool tag_gesture_fired = false;
  static bool tag_finger_down = false;

  if (!down) {
    if (was_down && isListMode() && !ignore_until_up && list_hit > HIT_NONE) {
      handleHit(list_hit);
    }
    if (was_down && g_mode == Mode::Nametag && !ignore_until_up && !tag_gesture_fired) {
      int span_x = tag_max_x - tag_min_x;
      int span_y = tag_max_y - tag_min_y;
      int right = tag_max_x - tag_start_x;
      int left = tag_start_x - tag_min_x;
      int up = tag_start_y - tag_min_y;
      int down_y = tag_max_y - tag_start_y;
      int dx = (right >= left) ? right : -left;
      int dy = (down_y >= up) ? down_y : -up;
      uint32_t held = millis() - tag_down_at;
      bool hud = tag_start_x >= (int)kScreenW - 72 && tag_start_y <= 22;
      bool quick_ghost = held < 70 && span_x < 28 && span_y < 28;
      if (hud || quick_ghost) {
        // charger/LCD ghost taps
      } else if (held >= kScreenLongPressMs && span_x < 36 && span_y < 36) {
        // Long-press screen: switch Auto mode off.
        if (rotating()) {
          settingsSetAutoMode(false);
          resetRotation();
          bumpTagIdle();
          uiNeedRedraw();
        }
      } else if (span_y >= 48 && span_y >= span_x + 10) {
        // Vertical swipe: up -> clock, down -> back.
        g_clock_mode = dy < 0;
        bumpTagIdle();
        uiNeedRedraw();
      } else if (span_x < 28 && span_y < 28) {
        // Tap: cycle colour (name + accents).
        int page = settingsFacePage();
        if (faceIsClassic(page)) {
          settingsCycleColor();
          bumpTagIdle();
          uiNeedRedraw();
        }
      }
    }
    if (was_down && g_mode == Mode::Silly && !ignore_until_up && !tag_gesture_fired) {
      int span_x = tag_max_x - tag_min_x;
      int span_y = tag_max_y - tag_min_y;
      uint32_t held = millis() - tag_down_at;
      if (held >= kScreenLongPressMs && span_x < 36 && span_y < 36) {
        // Long-press an animation: make it the Auto mode favourite and turn Auto on.
        int custom = g_silly_toy - (int)kToyFaceCount;
        if (custom < 0) {
          settingsSetFavToy(g_silly_toy);
        } else if (custom < gifsCount()) {
          settingsSetFavGif(gifsName(custom));
        }
        if (custom < gifsCount()) {
          settingsSetAutoMode(true);
          uiToast("Auto mode on", 2500);
          goNametag();
        }
      }
    }
    tag_finger_down = false;
    tag_gesture_fired = false;
    if (was_down && g_mode == Mode::Settings && !ignore_until_up) {
      int span_x = tag_max_x - tag_min_x;
      int span_y = tag_max_y - tag_min_y;
      int right = tag_max_x - tag_start_x;
      int left = tag_start_x - tag_min_x;
      int dx = (right >= left) ? right : -left;
      if (span_x >= 56 && span_x >= span_y + 12) {
        cycleSettingsPage(dx < 0 ? 1 : -1);
      } else if (list_hit > HIT_NONE && span_x < 36 && span_y < 36) {
        handleHit(list_hit);
      }
    }
    if (was_down && g_mode == Mode::Upnp && !ignore_until_up) {
      int span_x = tag_max_x - tag_min_x;
      int span_y = tag_max_y - tag_min_y;
      int right = tag_max_x - tag_start_x;
      int left = tag_start_x - tag_min_x;
      int dx = (right >= left) ? right : -left;
      if (span_x >= 56 && span_x >= span_y + 12) {
        upnpCycle(dx < 0 ? 1 : -1);
        g_upnp_art.clear();
        g_upnp_art_url = "";
        refreshUpnp(true);
      } else if (list_hit > HIT_NONE && span_x < 36 && span_y < 36) {
        handleHit(list_hit);
      }
    }
    ignore_until_up = false;
    list_hit = HIT_NONE;
  } else if (isListMode()) {
    if (pressed) {
      list_down_y = ty;
      list_hit = uiHit(tx, ty);
      g_list_drag_y = ty;
    } else if (!ignore_until_up) {
      *listScroll() += g_list_drag_y - ty;
      g_list_drag_y = ty;
      if (abs(ty - list_down_y) >= 12) {
        list_hit = HIT_NONE;
      }
      uiNeedRedraw();
    }
  } else if (g_mode == Mode::Nametag || g_mode == Mode::Unprovisioned ||
             g_mode == Mode::Settings || g_mode == Mode::Upnp || g_mode == Mode::Silly) {
    if (pressed) {
      tag_start_x = tag_min_x = tag_max_x = tx;
      tag_start_y = tag_min_y = tag_max_y = ty;
      tag_down_at = millis() ? millis() : 1;
      tag_gesture_fired = false;
      tag_finger_down = (g_mode == Mode::Nametag || g_mode == Mode::Silly);
      if (g_mode == Mode::Settings || g_mode == Mode::Upnp) {
        list_hit = uiHit(tx, ty);
      }
    }
    if (tx < tag_min_x) {
      tag_min_x = tx;
    }
    if (tx > tag_max_x) {
      tag_max_x = tx;
    }
    if (ty < tag_min_y) {
      tag_min_y = ty;
    }
    if (ty > tag_max_y) {
      tag_max_y = ty;
    }
    // Commit horizontal page-swipe as soon as the gesture is clear (snappier UX).
    if ((g_mode == Mode::Nametag || g_mode == Mode::Silly) && down && !tag_gesture_fired &&
        !ignore_until_up) {
      int span_x = tag_max_x - tag_min_x;
      int span_y = tag_max_y - tag_min_y;
      if (span_x >= 40 && span_x >= span_y + 8) {
        int right = tag_max_x - tag_start_x;
        int left = tag_start_x - tag_min_x;
        int dx = (right >= left) ? right : -left;
        tag_gesture_fired = true;
        if (g_mode == Mode::Silly) {
          int n = (int)kToyFaceCount + gifsCount() + 1;  // built-ins, custom GIFs, "Add a GIF"
          g_silly_toy = (g_silly_toy + (dx < 0 ? 1 : -1) + n) % n;
        } else {
          settingsCycleFace(dx < 0 ? 1 : -1);
        }
        markFaceAnim();
        bumpTagIdle();
        uiNeedRedraw();
      }
    }
  } else if (pressed && !ignore_until_up) {
    handleHit(uiHit(tx, ty));
  }
  was_down = down;

  uint32_t now = millis();
  static uint8_t last_hud = 255;
  uint8_t hud = (uint8_t)((batterySegments() & 7) | (netConnected() ? 8 : 0) |
                          (radioEnabled() ? 16 : 0) |
                          ((uint8_t)batteryStatus() << 5));
  if (hud != last_hud) {
    last_hud = hud;
    if (g_mode == Mode::Nametag || g_mode == Mode::Unprovisioned) {
      uiPaintHud();
    } else {
      uiNeedRedraw();
    }
  }
  bool timed = false;
  if (g_mode == Mode::Nearby || g_mode == Mode::Hunt || g_mode == Mode::Quiz ||
      g_mode == Mode::Reveal || g_mode == Mode::MetList || g_mode == Mode::Settings ||
      g_mode == Mode::MlbBoard) {
    uint32_t every = kNearbyRefreshMs;
    if (g_mode == Mode::Settings) {
      every = kUiRefreshMs;
    }
    timed = (now - g_last_ui >= every);
  }
  if (g_bird_open) {
    birdLoop();
    if (g_mode == Mode::KlippyBird && now - g_last_ui >= 33) {
      timed = true;  // ~30 fps
    }
  } else {
    birdDrain();  // scores heard in Find Friends beacons
  }
  if (g_hs_open) {
    hideseekLoop();
    if (g_mode == Mode::HideSeek && (now - g_last_ui >= 100 || hideseekChanged())) {
      timed = true;  // pulsing meter
    }
  }
  if (g_ob_open) {
    outbreakLoop();
    if (g_mode == Mode::Outbreak && (now - g_last_ui >= 50 || outbreakChanged())) {
      timed = true;  // ~20 fps heartbeat
    }
  }
  if (g_bj_open) {
    bjLoop();
    if (const char *n = bjTakeNotice()) {
      uiToast(n, 3000);
    }
    if (g_mode == Mode::Blackjack && bjChanged()) {
      uiNeedRedraw();
    }
  }
  // About: keep the logo shimmer moving.
  if (g_mode == Mode::About && now - g_last_ui >= kAboutRefreshMs) {
    timed = true;
  }
  // Schedule: move the "now" highlight along without a touch.
  if (g_mode == Mode::Schedule && now - g_last_ui >= 15000) {
    timed = true;
  }
  if (g_mode == Mode::Nametag) {
    // A locked style is only a preview: after a few untouched seconds, back to seven-segment.
    static uint32_t locked_since = 0;
    if (settingsFacePage() != 0 && !socialStylesUnlocked()) {
      if (!locked_since || tag_finger_down) {
        locked_since = now ? now : 1;
      } else if (now - locked_since >= kLockedPreviewMs) {
        locked_since = 0;
        settingsSetFacePage(0);
        uiNeedRedraw();
      }
    } else {
      locked_since = 0;
    }
    int page = settingsFacePage();
    // Classics 0..3 stay put; 4..7 play a short idle burst every 20–45 s untouched.
    static int idle_page = -1;
    if (page != idle_page) {
      idle_page = page;
      if (g_tag_anim_at) {
        g_tag_anim_at = 0;
        uiNeedRedraw();
      }
      bumpTagIdle();
    }
    // Auto mode: nametag 10 s, then the favourite 6 s. Goose badges: nametag 30 s, shame
    // goose 10 s, then the favourite if Auto is on.
    if (cycling() && !g_clock_mode && !tag_finger_down) {
      if (now - g_rot_at >= rotDwell(g_rot_phase)) {
        g_rot_phase = nextRotPhase(g_rot_phase);
        g_rot_at = now;
        g_tag_anim_at = 0;
        markFaceAnim();
        bumpTagIdle();
        uiNeedRedraw();
      }
    }
    bool toy_phase = cycling() && g_rot_phase != ROT_TAG;
    if (faceHasIdleAnim(page) && !toy_phase && !g_clock_mode && !tag_finger_down &&
        !g_tag_anim_at && (int32_t)(now - g_tag_idle_at) >= 0) {
      startTagAnim();
    }
    // Pause heavy anim redraws while a finger is down so swipes stay responsive.
    bool living = !tag_finger_down && (g_clock_mode || toy_phase || g_tag_anim_at != 0);
    if (living && now - g_last_ui >= 66) {
      timed = true;
    }
    // CRT style: slow redraw so the end-of-name cursor keeps blinking.
    if (page == (int)kTagStillStyleCount + 2 && !toy_phase && !g_clock_mode &&
        !tag_finger_down && now - g_last_ui >= 500) {
      timed = true;
    }
  }
  if (g_mode == Mode::Silly && !tag_finger_down && now - g_last_ui >= 66) {
    timed = true;
  }
  if (g_mode == Mode::Queen && now - g_last_ui >= 40) {
    timed = true;
  }
  if (timed) {
    g_last_ui = now;
    uiNeedRedraw();
  }
  if (uiConsumeRedraw()) {
    draw();
  }
  if (g_mode != Mode::Silly && !(g_mode == Mode::Nametag && g_rot_phase == ROT_FAV)) {
    uiCustomGifStop();  // release the GIF file once nothing is showing it
  }
  powerTick();
}
