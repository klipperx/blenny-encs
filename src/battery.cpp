#include "battery.h"

#include "config.h"

#include "HWCDC.h"

// T-Display S3: GPIO4 is a 1:1 divider on the LiPo rail.
// TP4065 has no STAT GPIO, so USB + voltage/slope stand in for charge state.
static constexpr uint32_t kPollMs = 400;
static constexpr uint32_t kSlopeMs = 5000;
static constexpr int kChargedEnterMv = 4180;
static constexpr int kChargedLeaveMv = 4080;
static constexpr int kRiseMv = 20;
static constexpr int kEmptyMv = 3300;
static constexpr int kEmptyHardMv = 3200;
static constexpr int kEmptyClearMv = 3450;
static constexpr uint8_t kEmptyHits = 10;

static uint32_t g_last_poll = 0;
static uint32_t g_slope_at = 0;
static float g_mv = 0;
static float g_slope_mv = 0;
static uint8_t g_seg = 5;
static uint8_t g_empty_hits = 0;
static bool g_usb = false;
static bool g_usb_raw = false;
static uint32_t g_usb_edge = 0;
static bool g_rising = false;
static bool g_empty = false;
static BatteryStatus g_status = BatteryStatus::Discharging;
RTC_DATA_ATTR static uint8_t s_cut = 0;

// Rise into N bars / fall out of N bars. Gaps keep the icon from flickering.
static const int kRise[] = {3480, 3650, 3780, 3920, 4050};
static const int kFall[] = {3400, 3550, 3700, 3840, 3980};

static int readMilliVolts() {
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) {
    sum += analogReadMilliVolts(PIN_BAT_VOLT);
  }
  return (int)((sum / 8) * 2);
}

static void applySeg(int mv) {
  while (g_seg < 5 && mv >= kRise[g_seg]) {
    g_seg++;
  }
  while (g_seg > 0 && mv <= kFall[g_seg - 1]) {
    g_seg--;
  }
}

static bool usbPresent() {
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE
  return HWCDC::isPlugged();
#else
  return false;
#endif
}

static void applyStatus(int mv) {
  if (g_usb) {
    if (g_status == BatteryStatus::Charged) {
      if (mv < kChargedLeaveMv) {
        g_status = BatteryStatus::Charging;
      }
    } else if (mv >= kChargedEnterMv) {
      g_status = BatteryStatus::Charged;
    } else {
      g_status = BatteryStatus::Charging;
    }
    return;
  }
  // Wall chargers often have no USB data, so SOF never fires. A steadily
  // rising pack voltage is the fallback "charging" signal.
  g_status = g_rising ? BatteryStatus::Charging : BatteryStatus::Discharging;
}

static void applyEmpty(int mv) {
  bool feeding = g_usb || g_status == BatteryStatus::Charging;
  if (feeding || mv >= kEmptyClearMv) {
    g_empty_hits = 0;
    g_empty = false;
    s_cut = 0;
    return;
  }
  if (mv <= kEmptyHardMv || (s_cut && mv <= kEmptyMv)) {
    g_empty = true;
    g_empty_hits = kEmptyHits;
    return;
  }
  if (mv <= kEmptyMv) {
    if (g_empty_hits < 255) {
      g_empty_hits++;
    }
    if (g_empty_hits >= kEmptyHits) {
      g_empty = true;
    }
    return;
  }
  g_empty_hits = 0;
}

void batteryBegin() {
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BAT_VOLT, ADC_11db);
  delay(2);
  g_mv = (float)readMilliVolts();
  g_slope_mv = g_mv;
  g_seg = 0;
  g_usb = usbPresent();
  g_usb_raw = g_usb;
  g_usb_edge = millis();
  applySeg((int)(g_mv + 0.5f));
  applyStatus((int)(g_mv + 0.5f));
  applyEmpty((int)(g_mv + 0.5f));
  g_last_poll = millis();
  g_slope_at = g_last_poll;
}

void batteryPoll() {
  uint32_t now = millis();
  if (g_last_poll && now - g_last_poll < kPollMs) {
    return;
  }
  g_last_poll = now;
  float sample = (float)readMilliVolts();
  if (g_mv < 1.0f) {
    g_mv = sample;
  } else {
    g_mv = g_mv * 0.82f + sample * 0.18f;
  }
  bool plugged = usbPresent();
  if (plugged != g_usb_raw) {
    g_usb_raw = plugged;
    g_usb_edge = now;
  }
  if (plugged == g_usb) {
    g_usb_edge = now;
  } else if (now - g_usb_edge >= 800) {
    g_usb = plugged;
  }
  if (!g_slope_at || now - g_slope_at >= kSlopeMs) {
    float d = g_mv - g_slope_mv;
    g_rising = d >= (float)kRiseMv;
    g_slope_mv = g_mv;
    g_slope_at = now;
  }
  int mv = (int)(g_mv + 0.5f);
  applySeg(mv);
  applyStatus(mv);
  applyEmpty(mv);
}

uint8_t batterySegments() { return g_seg; }

int batteryMillivolts() { return (int)(g_mv + 0.5f); }

BatteryStatus batteryStatus() { return g_status; }

bool batteryUsb() { return g_usb; }

bool batteryEmpty() { return g_empty; }

void batteryArmCutoff() { s_cut = 1; }

const char *batteryStatusName() {
  switch (g_status) {
    case BatteryStatus::Charging:
      return "charging";
    case BatteryStatus::Charged:
      return "charged";
    default:
      return "battery";
  }
}
