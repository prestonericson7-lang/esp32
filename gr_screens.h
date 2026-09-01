#pragma once
// ############################################################################
//  gr_screens.h  -  KODI  /  5 display screens — portrait 320x480
//
//  Screen 0: Dashboard — mesh status cards, resource gauges, activity feed
//  Screen 1: Spectrum  — 2x2 radio panels, 16-sample RSSI, peak hold
//  Screen 2: Neural    — engine state, confidence trend, rule categories,
//                         risk gauges, alert log
//  Screen 3: Memory    — SD usage bar, scrollable folder list, activity
//  Screen 4: Config    — mesh topology, backbone intel, system vitals
// ############################################################################

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "gr_config.h"

// ---- shared data structures ------------------------------------------------

struct GrCaptureTick {
  uint8_t  deviceType;
  float    freqMHz;
  uint32_t timestampMs;
  bool     valid;
};
static GrCaptureTick grCapTicker[5] = {};
static uint8_t grCapTickerIdx = 0;
static uint32_t grTotalCaptures  = 0;
static uint32_t grTotalSorted    = 0;
static uint32_t grTotalAlerts    = 0;
static float    grCaptureRate    = 0.0f;

static inline void grCapTickerPush(uint8_t devType, float freq) {
  grCapTicker[grCapTickerIdx].deviceType   = devType;
  grCapTicker[grCapTickerIdx].freqMHz      = freq;
  grCapTicker[grCapTickerIdx].timestampMs  = millis();
  grCapTicker[grCapTickerIdx].valid        = true;
  grCapTickerIdx = (grCapTickerIdx + 1) % 5;
  grTotalCaptures++;
}

struct GrAlertEntry {
  uint8_t  level;
  uint32_t timestampMs;
  char     msg[21];
  bool     valid;
};
static GrAlertEntry grAlertLog[8] = {};
static uint8_t grAlertLogIdx = 0;

static inline void grAlertLogPush(uint8_t lvl, const char* msg) {
  GrAlertEntry& e = grAlertLog[grAlertLogIdx];
  e.level = lvl;
  e.timestampMs = millis();
  memset(e.msg, 0, 21);
  strncpy(e.msg, msg, 20);
  e.valid = true;
  grAlertLogIdx = (grAlertLogIdx + 1) % 8;
  grTotalAlerts++;
}

static uint8_t grConfHistory[16] = {};
static uint8_t grConfHistIdx = 0;

static inline void grConfHistPush(uint8_t conf) {
  grConfHistory[grConfHistIdx % 16] = conf;
  grConfHistIdx++;
}

// device type name for capture ticker
static const char* grDevTypeName(uint8_t dt) {
  switch (dt) {
    case 1:  return "GARAGE";
    case 2:  return "GARAGE_R";
    case 3:  return "GATE";
    case 4:  return "CAR FOB";
    case 5:  return "TPMS";
    case 6:  return "DOORBELL";
    case 7:  return "WEATHER";
    case 8:  return "ALARM_S";
    case 9:  return "ALARM_P";
    case 10: return "SMART_HM";
    case 11: return "UTILITY";
    case 12: return "INDUSTRL";
    case 13: return "MEDICAL";
    case 14: return "VEHICLE";
    case 15: return "ACCESS";
    case 16: return "PAGER";
    case 17: return "BABY_MON";
    case 18: return "GARDEN";
    case 19: return "RC_TOY";
    case 20: return "LORA";
    case 21: return "KEYFOB";
    case 22: return "LIGHT_SW";
    case 23: return "FAN_RMT";
    case 24: return "APPLIANC";
    default: return "UNKNOWN";
  }
}

static uint16_t grDevTypeColor(uint8_t dt) {
  switch (dt) {
    case 1: case 2: return GR_C_CYAN;
    case 3:         return GR_C_MAG;
    case 4: case 21: return GR_C_CYAN;
    case 5:         return GR_C_GRN;
    case 6:         return GR_C_GRN;
    case 7:         return GR_C_GRN;
    case 8: case 9: return GR_C_MAG;
    case 10:        return GR_C_GRN;
    case 11:        return GR_C_AMB;
    case 12:        return GR_C_AMB;
    case 13:        return GR_C_RED;
    case 14:        return GR_C_CYAN;
    case 15:        return GR_C_AMB;
    case 16:        return GR_C_DIM;
    case 17:        return GR_C_GRN;
    case 18:        return GR_C_GRN;
    case 19:        return GR_C_CYAN;
    case 20:        return GR_C_PUR;
    default:        return GR_C_RED;
  }
}

// icon paths removed — all icons now drawn procedurally via gr_icons.h
// grDevToIco() in gr_icons.h maps device type → icon enum

// helper: format seconds into age string
static void grFmtAge(char* buf, uint8_t sz, uint32_t ms) {
  uint32_t sec = ms / 1000;
  if (sec < 60) snprintf(buf, sz, "%lus", (unsigned long)sec);
  else if (sec < 3600) snprintf(buf, sz, "%lum", (unsigned long)(sec / 60));
  else snprintf(buf, sz, "%luh", (unsigned long)(sec / 3600));
}

// ############################################################################
//  Screen 0 — DASHBOARD — artifact scr0() exact port
// ############################################################################
static inline void grScreenDashboard(Arduino_GFX* gfx, bool n0up, bool n1up,
                                      bool n3up, uint16_t sdMb, uint16_t heap,
                                      uint16_t queueDepth, bool sorting,
                                      uint8_t radioCount, bool attacking) {
  gfx->fillRect(0, GR_CONTENT_Y, GR_SCR_W, GR_CONTENT_H, GR_C_BG);
  gfx->setTextSize(1);
  uint32_t now = millis();
  int y = 32;

  // N0 Radio Body card — artifact: fr(6,y,4,38,GRN); sr(10,y,300,38,LINE); icon(272,y+7,24,'radar')
  gfx->fillRect(6, y, 4, 38, n0up ? GR_C_GRN : GR_C_RED);
  gfx->drawRect(10, y, 300, 38, GR_C_LINE);
  grIcon(gfx, 272, y + 7, 24, ICO_RADAR);
  gfx->setCursor(16, y + 4);
  gfx->setTextColor(GR_C_INK);
  gfx->print(F("N0 RADIO BODY"));
  gfx->setCursor(200, y + 4);
  gfx->setTextColor(n0up ? GR_C_GRN : GR_C_RED);
  gfx->print(n0up ? F("ONLINE") : F("OFFLINE"));
  gfx->setCursor(16, y + 14);
  gfx->setTextColor(GR_C_DIM);
  gfx->printf("Radios:%d  %s", radioCount, attacking ? "TX ACTIVE" : "IDLE");
  grIcon(gfx, 120, y + 12, 14, attacking ? ICO_RADIO_TX : ICO_RADIO_IDLE);
  char ageBuf[8];
  grFmtAge(ageBuf, sizeof(ageBuf), now - grPeer0.lastRx);
  gfx->setCursor(16, y + 26);
  gfx->printf("Last: %s ago", ageBuf);
  y += 42;

  // N1 Neural Engine card — artifact: fr(6,y,4,38,CYAN); icon(272,y+7,24,'brain')
  gfx->fillRect(6, y, 4, 38, n1up ? GR_C_CYAN : GR_C_RED);
  gfx->drawRect(10, y, 300, 38, GR_C_LINE);
  grIcon(gfx, 272, y + 7, 24, ICO_BRAIN);
  gfx->setCursor(16, y + 4);
  gfx->setTextColor(GR_C_INK);
  gfx->print(F("N1 NEURAL ENGINE"));
  gfx->setCursor(200, y + 4);
  gfx->setTextColor(n1up ? GR_C_GRN : GR_C_RED);
  gfx->print(n1up ? F("ONLINE") : F("OFFLINE"));
  gfx->setCursor(16, y + 14);
  gfx->setTextColor(GR_C_DIM);
  const char* stNames[] = {"BOOT","IDLE","MON","ALERT","ERR","LEARN","ANLYZ","ADVISE"};
  gfx->printf("State:%s Conf:%d%%",
              grNnState < 8 ? stNames[grNnState] : "?", grNnConfidence);
  grFmtAge(ageBuf, sizeof(ageBuf), now - grPeer1.lastRx);
  gfx->setCursor(16, y + 26);
  gfx->printf("Last: %s ago", ageBuf);
  y += 42;

  // N3 Backbone card — artifact: fr(6,y,4,38,MAG); icon(272,y+7,24,'mesh')
  gfx->fillRect(6, y, 4, 38, n3up ? GR_C_MAG : GR_C_RED);
  gfx->drawRect(10, y, 300, 38, GR_C_LINE);
  grIcon(gfx, 272, y + 7, 24, ICO_MESH);
  gfx->setCursor(16, y + 4);
  gfx->setTextColor(GR_C_INK);
  gfx->print(F("N3 BACKBONE"));
  gfx->setCursor(200, y + 4);
  gfx->setTextColor(n3up ? GR_C_GRN : GR_C_RED);
  gfx->print(n3up ? F("ONLINE") : F("OFFLINE"));
  gfx->setCursor(16, y + 14);
  gfx->setTextColor(GR_C_DIM);
  gfx->printf("BLE:%d Trk:%d(%d!)", grBbBleCount, grBbTrackers, grBbFlagged);
  grIcon(gfx, 148, y + 12, 14, ICO_MESH_OK);
  gfx->setCursor(16, y + 26);
  gfx->printf("WiFi:%d  Deauth:%s", grBbApCount, grBbStormActive ? "STORM!" : "CLR");
  y += 44;

  // Resource gauges — artifact: sr(10,y,145,14,LINE); fr(11,y+1,fill,12,GRN)
  gfx->drawRect(10, y, 145, 14, GR_C_LINE);
  int heapFill = min(143, (int)(heap * 143 / 250));
  gfx->fillRect(11, y + 1, heapFill, 12, GR_C_GRN);
  gfx->setCursor(14, y + 3);
  gfx->setTextColor(GR_C_INK);
  gfx->printf("HEAP %uKB", heap);

  gfx->drawRect(165, y, 145, 14, GR_C_LINE);
  int sdFill = min(143, (int)((float)sdMb / 128000.0f * 143));
  gfx->fillRect(166, y + 1, sdFill, 12, GR_C_GRN);
  gfx->setCursor(168, y + 3);
  if (sdMb > 1000) gfx->printf("SD %.1fGB", sdMb / 1024.0f);
  else gfx->printf("SD %uMB", sdMb);
  y += 20;

  // Mode banner — artifact: fr(0,y,320,20,CYAN); icon(4,y+2,16,'crosshair'); dt(100,y+6,'>> HUNTER MODE <<','#000')
  uint16_t bannerBg = GR_C_CYAN;
  uint8_t modeIco = ICO_CROSSHAIR;
  const char* modeName = "HUNTER";
  if (grBehaviorMode == 1) { bannerBg = GR_C_MAG; modeIco = ICO_SHIELD; modeName = "DEFENSIVE"; }
  if (grBehaviorMode == 2) { bannerBg = GR_C_PUR; modeIco = ICO_GHOST;  modeName = "STEALTH"; }
  gfx->fillRect(0, y, 320, 20, bannerBg);
  grIcon(gfx, 4, y + 2, 16, modeIco);
  gfx->setTextColor(GR_C_BG);
  gfx->setCursor(100, y + 6);
  gfx->printf(">> %s MODE <<", modeName);
  grIcon(gfx, 296, y + 2, 16, modeIco);
  y += 24;

  // Live activity — artifact: dt(10,y,'LIVE ACTIVITY',DIM); hl(10,y+10,300,LINE)
  gfx->setCursor(10, y);
  gfx->setTextColor(GR_C_DIM);
  gfx->print(F("LIVE ACTIVITY"));
  gfx->drawFastHLine(10, y + 10, 300, GR_C_LINE);
  y += 14;

  bool anyCaptures = false;
  for (int i = 0; i < 5; i++) {
    int idx = ((int)grCapTickerIdx - 1 - i + 10) % 5;
    if (!grCapTicker[idx].valid) continue;
    anyCaptures = true;
    GrCaptureTick& t = grCapTicker[idx];
    grFmtAge(ageBuf, sizeof(ageBuf), now - t.timestampMs);
    // artifact: icon(8,y,16,cap.i); dt(28,y+2,name,color); dt(88,y+2,freq+age,DIM)
    grIcon(gfx, 8, y, 16, grDevToIco(t.deviceType));
    gfx->setCursor(28, y + 2);
    gfx->setTextColor(grDevTypeColor(t.deviceType));
    gfx->printf("%-9s", grDevTypeName(t.deviceType));
    gfx->setCursor(88, y + 2);
    gfx->setTextColor(GR_C_DIM);
    gfx->printf("%.2fMHz %s", t.freqMHz, ageBuf);
    y += 20;
  }
  if (!anyCaptures) {
    gfx->setCursor(24, y + 10);
    gfx->setTextColor(GR_C_DIM);
    gfx->print(F("Waiting for captures..."));
    y += 30;
  }

  // sort + rate — artifact: dt(10,y,'SORT: ',DIM); dt(46,y,'3 pending',AMB)
  gfx->setCursor(10, y);
  gfx->setTextColor(GR_C_DIM);
  gfx->print(F("SORT: "));
  if (sorting) { gfx->setTextColor(GR_C_CYAN); gfx->printf("ACTIVE %u", queueDepth); }
  else if (queueDepth > 0) { gfx->setTextColor(GR_C_AMB); gfx->printf("%u pending", queueDepth); }
  else { gfx->setTextColor(GR_C_GRN); gfx->print(F("IDLE")); }
  y += 12;
  gfx->setCursor(10, y);
  gfx->setTextColor(GR_C_DIM);
  gfx->printf("Rate: %.1f/min", grCaptureRate);
  y += 14;

  // footer — artifact: hl(10,y,300,LINE); dt(10,y,'UP hh:mm:ss',DIM)
  gfx->drawFastHLine(10, y, 300, GR_C_LINE);
  y += 6;
  uint32_t up = now / 1000;
  gfx->setCursor(10, y);
  gfx->printf("UP %02lu:%02lu:%02lu", up / 3600, (up / 60) % 60, up % 60);
  gfx->setCursor(10, y + 10);
  gfx->printf("Cap:%lu Sort:%lu Alrt:%lu",
              (unsigned long)grTotalCaptures, (unsigned long)grTotalSorted, (unsigned long)grTotalAlerts);
}

// ############################################################################
//  Screen 1 — SPECTRUM / SIGNAL INTELLIGENCE
// ############################################################################

static int8_t  grSpecRssi[4][16] = {};
static uint8_t grSpecIdx = 0;
static float   grSpecFreqs[4] = {433.92f, 315.0f, 868.0f, 915.0f};
static int8_t  grSpecPeak[4] = {-120, -120, -120, -120};
static uint32_t grSpecPeakMs[4] = {};
static int8_t  grSpecNoiseFloor[4] = {-100, -100, -100, -100};
static bool    grSpecSignalPresent[4] = {};
static uint32_t grSpecStateMs[4] = {};

static inline void grSpecFeedRssi(uint8_t radio, int8_t rssi) {
  if (radio >= 4) return;
  grSpecRssi[radio][grSpecIdx % 16] = rssi;
  if (rssi > grSpecPeak[radio]) {
    grSpecPeak[radio] = rssi;
    grSpecPeakMs[radio] = millis();
  }
  // noise floor EMA
  grSpecNoiseFloor[radio] = (int8_t)(grSpecNoiseFloor[radio] * 0.9f + rssi * 0.1f);
  bool wasSig = grSpecSignalPresent[radio];
  grSpecSignalPresent[radio] = (rssi > grSpecNoiseFloor[radio] + 10);
  if (grSpecSignalPresent[radio] != wasSig) grSpecStateMs[radio] = millis();
}

static inline void grScreenSpectrum(Arduino_GFX* gfx) {
  gfx->fillRect(0, GR_CONTENT_Y, GR_SCR_W, GR_CONTENT_H, GR_C_BG);
  uint32_t now = millis();

  gfx->setTextSize(1);
  // artifact: icon(10,31,14,'sweep'); dt(28,32,'SIGNAL INTELLIGENCE',MAG)
  grIcon(gfx, 10, 31, 14, ICO_SWEEP);
  gfx->setTextColor(GR_C_MAG);
  gfx->setCursor(28, 32);
  gfx->print(F("SIGNAL INTELLIGENCE"));
  gfx->drawFastHLine(10, 44, 300, GR_C_LINE);

  // 2x2 grid: each panel ~148x185
  int panelW = 148;
  int panelH = 185;
  int gapX = 4;
  int gapY = 4;
  int startY = 46;

  for (int r = 0; r < 4; r++) {
    int col = r % 2;
    int row = r / 2;
    int px = 10 + col * (panelW + gapX);
    int py = startY + row * (panelH + gapY);

    // panel border
    gfx->drawRect(px, py, panelW, panelH, GR_C_LINE);

    // artifact: icon(px+4,py+2,14,'radio_idle'); dt(px+20,py+4,label)
    grIcon(gfx, px + 4, py + 2, 14, ICO_RADIO_IDLE);
    gfx->setCursor(px + 20, py + 4);
    gfx->setTextColor(GR_C_CYAN);
    gfx->printf("CC1101-%c", 'A' + r);

    gfx->setCursor(px + 100, py + 4);
    gfx->setTextColor(GR_C_DIM);
    gfx->printf("%.1f", grSpecFreqs[r]);

    // ISM band tag
    const char* band = "SUB-GHz";
    if (grSpecFreqs[r] >= 430 && grSpecFreqs[r] <= 440) band = "ISM 433";
    else if (grSpecFreqs[r] >= 310 && grSpecFreqs[r] <= 320) band = "ISM 315";
    else if (grSpecFreqs[r] >= 863 && grSpecFreqs[r] <= 870) band = "ISM 868";
    else if (grSpecFreqs[r] >= 900 && grSpecFreqs[r] <= 928) band = "ISM 915";
    gfx->setCursor(px + 4, py + 14);
    gfx->setTextColor(GR_C_DIM);
    gfx->printf("[%s]", band);

    // RSSI bar chart — 16 bars
    int barBase = py + 145;
    int barMaxH = 95;
    int barW = 7;
    int barGap = 1;
    int barsStartX = px + 8;

    for (int i = 0; i < 16; i++) {
      int8_t rssi = grSpecRssi[r][i];
      int barH = 0;
      if (rssi < 0) barH = (int)((-rssi - 20) * barMaxH / 80);
      if (barH < 2) barH = 2;
      if (barH > barMaxH) barH = barMaxH;

      uint16_t col16 = GR_C_GRN;
      if (rssi > -40) col16 = GR_C_RED;
      else if (rssi > -60) col16 = GR_C_AMB;

      gfx->fillRect(barsStartX + i * (barW + barGap), barBase - barH, barW, barH, col16);
    }

    // peak hold marker (fades after 3s)
    if (grSpecPeak[r] > -120 && (now - grSpecPeakMs[r]) < 3000) {
      int peakH = (int)((-grSpecPeak[r] - 20) * barMaxH / 80);
      if (peakH >= 2 && peakH <= barMaxH) {
        uint16_t pkCol = ((now - grSpecPeakMs[r]) < 1500) ? GR_C_INK : GR_C_DIM;
        gfx->drawFastHLine(barsStartX, barBase - peakH, 16 * (barW + barGap), pkCol);
      }
    } else {
      grSpecPeak[r] = -120;
    }

    // noise floor dashed line
    int nfH = (int)((-grSpecNoiseFloor[r] - 20) * barMaxH / 80);
    if (nfH >= 2 && nfH <= barMaxH) {
      for (int dx = 0; dx < 16 * (barW + barGap); dx += 6) {
        gfx->drawPixel(barsStartX + dx, barBase - nfH, GR_C_DIM);
        gfx->drawPixel(barsStartX + dx + 1, barBase - nfH, GR_C_DIM);
      }
    }

    // signal indicator
    gfx->setCursor(px + 4, py + 150);
    if (grSpecSignalPresent[r]) {
      gfx->fillCircle(px + 8, py + 154, 3, GR_C_GRN);
      gfx->setCursor(px + 16, py + 150);
      gfx->setTextColor(GR_C_GRN);
      char ageBuf[8];
      grFmtAge(ageBuf, sizeof(ageBuf), now - grSpecStateMs[r]);
      gfx->printf("SIG %s", ageBuf);
    } else {
      gfx->fillCircle(px + 8, py + 154, 3, GR_C_DIM);
      gfx->setCursor(px + 16, py + 150);
      gfx->setTextColor(GR_C_DIM);
      gfx->print(F("QUIET"));
    }

    // current RSSI readout
    int8_t curRssi = grSpecRssi[r][grSpecIdx > 0 ? (grSpecIdx - 1) % 16 : 0];
    gfx->setCursor(px + 80, py + 150);
    uint16_t rssiCol = GR_C_GRN;
    if (curRssi > -40) rssiCol = GR_C_RED;
    else if (curRssi > -60) rssiCol = GR_C_AMB;
    gfx->setTextColor(rssiCol);
    gfx->printf("%ddBm", curRssi);

    // activity dot in header
    gfx->fillCircle(px + 72, py + 7, 3, grSpecSignalPresent[r] ? GR_C_GRN : GR_C_DIM);
  }

  // bottom info bar
  int infoY = startY + 2 * (panelH + gapY) + 4;
  gfx->setCursor(10, infoY);
  gfx->setTextColor(GR_C_DIM);
  gfx->printf("Sweep: %.0f/%.0f/%.0f/%.0f MHz",
              grSpecFreqs[0], grSpecFreqs[1], grSpecFreqs[2], grSpecFreqs[3]);

  // find strongest
  int8_t best = -127;
  uint8_t bestR = 0;
  for (int r = 0; r < 4; r++) {
    int8_t cur = grSpecRssi[r][grSpecIdx > 0 ? (grSpecIdx - 1) % 16 : 0];
    if (cur > best) { best = cur; bestR = r; }
  }
  gfx->setCursor(10, infoY + 12);
  gfx->printf("Strongest: CC1101-%c %ddBm", 'A' + bestR, best);

  grSpecIdx++;
}

// ############################################################################
//  Screen 2 — NEURAL INTELLIGENCE CENTER
// ############################################################################

static inline void grScreenNeural(Arduino_GFX* gfx, uint8_t nnState,
                                   uint8_t confidence, uint8_t rulesFired,
                                   uint8_t alertLvl, const char* alertMsg,
                                   uint8_t mode) {
  gfx->fillRect(0, GR_CONTENT_Y, GR_SCR_W, GR_CONTENT_H, GR_C_BG);
  uint32_t now = millis();

  // push confidence sample
  grConfHistPush(confidence);

  // ---- Zone A: Engine Header (y=30 to y=68) ----
  const char* stateNames[] = {
    "BOOT", "IDLE", "MONITORING", "ALERT",
    "ERROR", "LEARNING", "ANALYZING", "ADVISING"
  };
  uint16_t stateColors[] = {
    GR_C_DIM, GR_C_DIM, GR_C_GRN, GR_C_RED,
    GR_C_RED, GR_C_CYAN, GR_C_MAG, GR_C_PUR
  };

  // artifact: icon(10,30,20,'cpu')
  grIcon(gfx, 10, 30, 20, ICO_CPU);
  gfx->setTextSize(2);
  gfx->setTextColor(nnState < 8 ? stateColors[nnState] : GR_C_DIM);
  gfx->setCursor(38, 32);
  gfx->print(nnState < 8 ? stateNames[nnState] : "?");

  gfx->setTextSize(1);
  gfx->setTextColor(GR_C_DIM);
  gfx->setCursor(10, 52);
  uint32_t upSec = now / 1000;
  gfx->printf("Running %02lu:%02lu:%02lu", upSec / 3600, (upSec / 60) % 60, upSec % 60);
  gfx->setCursor(200, 52);
  gfx->print(F("4.0 Hz"));

  gfx->drawFastHLine(10, 64, 300, GR_C_LINE);

  // ---- Zone B: Confidence Gauge (y=68 to y=92) ----
  int barY = 70;
  gfx->drawRect(10, barY, 300, 14, GR_C_LINE);
  uint16_t confCol = (confidence > 70) ? GR_C_GRN : ((confidence > 40) ? GR_C_AMB : GR_C_RED);
  int confFill = min(298, (int)(confidence * 298 / 100));
  gfx->fillRect(11, barY + 1, confFill, 12, confCol);
  gfx->setTextColor(GR_C_INK);
  gfx->setCursor(14, barY + 3);
  gfx->printf("CONFIDENCE %d%%", confidence);

  // trend indicator
  if (grConfHistIdx >= 4) {
    int recent = 0, older = 0;
    int count = min((int)grConfHistIdx, 16);
    int half = count / 2;
    for (int i = 0; i < half; i++) {
      older  += grConfHistory[(grConfHistIdx - count + i) % 16];
      recent += grConfHistory[(grConfHistIdx - half + i) % 16];
    }
    float avgOld = (float)older / half;
    float avgNew = (float)recent / half;
    gfx->setCursor(280, barY + 3);
    if (avgNew > avgOld + 3) {
      gfx->setTextColor(GR_C_GRN);
      gfx->print((char)0x18);  // up arrow
    } else if (avgNew < avgOld - 3) {
      gfx->setTextColor(GR_C_RED);
      gfx->print((char)0x19);  // down arrow
    } else {
      gfx->setTextColor(GR_C_DIM);
      gfx->print((char)0x1A);  // right arrow (stable)
    }
  }

  // ---- Zone C: Rules Fired + Behavior (y=88 to y=118) ----
  int y = 90;
  gfx->setCursor(10, y);
  gfx->setTextColor(GR_C_DIM);
  gfx->print(F("RULES FIRED: "));
  gfx->setTextColor(GR_C_INK);
  gfx->printf("%d / 120", rulesFired);

  y += 14;
  gfx->setCursor(10, y);
  gfx->setTextColor(GR_C_DIM);
  gfx->print(F("BEHAVIOR: "));
  switch (mode) {
    case 0: gfx->setTextColor(GR_C_CYAN); gfx->print(F("HUNTER")); break;
    case 1: gfx->setTextColor(GR_C_MAG);  gfx->print(F("DEFENSIVE")); break;
    case 2: gfx->setTextColor(GR_C_PUR);  gfx->print(F("STEALTH")); break;
  }

  y += 18;
  gfx->drawFastHLine(10, y, 300, GR_C_LINE);
  y += 6;

  // artifact: icon(10,y-2,16,'eye'); dt(30,y,'BACKBONE BLE',DIM)
  grIcon(gfx, 10, y - 2, 16, ICO_EYE);
  gfx->setCursor(30, y);
  gfx->setTextColor(GR_C_DIM);
  gfx->print(F("BACKBONE BLE"));
  y += 12;
  gfx->setCursor(14, y);
  gfx->setTextColor(GR_C_INK);
  gfx->printf("Unique:%d Active:%d New:%d/5m",
              grBbTotalUniqueBle, grBbActiveNow, grBbNewRate);
  y += 12;
  gfx->setCursor(14, y);
  gfx->printf("Phones:%d Trackers:%d Dwell:%dm",
              grBbPhones, grBbTrackersActive, grBbAvgDwell);
  y += 12;

  // proximity zones
  gfx->setCursor(14, y);
  gfx->setTextColor(GR_C_DIM);
  gfx->print(F("Prox: "));
  gfx->setTextColor(GR_C_INK);
  gfx->printf("F:%d N:%d C:%d I:%d",
              grBbZoneFar, grBbZoneNear, grBbZoneClose, grBbZoneImmediate);
  if (grBbApproaching > 0) {
    gfx->setTextColor(GR_C_AMB);
    gfx->printf(" %d approaching", grBbApproaching);
  }
  y += 14;

  // beacons
  gfx->setCursor(14, y);
  gfx->setTextColor(GR_C_DIM);
  gfx->printf("Beacons:%d iB:%d Eddy:%d",
              grBbBeaconCount, grBbIBeacons, grBbEddystones);
  y += 16;

  gfx->drawFastHLine(10, y, 300, GR_C_LINE);
  y += 6;

  // ---- Zone E: Alert History Log (y~210 to end) ----
  gfx->setCursor(10, y);
  gfx->setTextColor(GR_C_DIM);
  gfx->print(F("ALERT LOG"));
  gfx->drawFastHLine(10, y + 10, 300, GR_C_LINE);
  y += 16;

  bool anyAlerts = false;
  for (int i = 0; i < 8; i++) {
    int idx = ((int)grAlertLogIdx - 1 - i + 16) % 8;
    if (!grAlertLog[idx].valid) continue;
    if (y > 440) break;
    anyAlerts = true;
    GrAlertEntry& a = grAlertLog[idx];

    uint32_t aSec = a.timestampMs / 1000;
    uint16_t alCol = GR_C_DIM;
    if (a.level >= 4)      alCol = GR_C_RED;
    else if (a.level >= 3) alCol = GR_C_MAG;
    else if (a.level >= 2) alCol = GR_C_AMB;

    // artifact alert icons: L4=skull, L3=bolt/eye, L2=flame/meter
    uint8_t alIco = (a.level >= 4) ? ICO_SKULL :
                    (a.level >= 3) ? ICO_BOLT :
                    (a.level >= 2) ? ICO_FLAME : ICO_CHAIN;
    grIcon(gfx, 10, y - 2, 16, alIco);
    gfx->setCursor(28, y);
    gfx->setTextColor(alCol);
    gfx->printf("[L%d]", a.level);
    gfx->setTextColor(GR_C_DIM);
    gfx->printf(" %02lu:%02lu ", aSec / 3600, (aSec / 60) % 60);
    gfx->setTextColor(alCol);
    gfx->print(a.msg);
    y += 18;
  }
  if (!anyAlerts) {
    gfx->setCursor(14, y);
    gfx->setTextColor(GR_C_GRN);
    gfx->print(F("No alerts - system nominal"));
  }
}

// ############################################################################
//  Screen 3 — MEMORY BROWSER (Full Archive)
// ############################################################################

static uint16_t grFolderCounts[23] = {};
static uint32_t grFolderSizeKb[23] = {};
static uint32_t grFolderCacheMs = 0;
#define GR_FOLDER_CACHE_MS 10000

static int8_t grMemScrollOffset = 0;

struct GrFolderEntry {
  const char* name;
  const char* path;
  uint16_t    dotColor;
};
static const GrFolderEntry grFolders[] = {
  {"garage_doors",     "/signals/garage_doors",     GR_C_CYAN},
  {"gates_barriers",   "/signals/gates_barriers",   GR_C_CYAN},
  {"car_fobs",         "/signals/car_fobs",         GR_C_CYAN},
  {"tpms",             "/signals/tpms",             GR_C_GRN},
  {"doorbells",        "/signals/doorbells",        GR_C_GRN},
  {"weather_stations", "/signals/weather_stations", GR_C_GRN},
  {"alarm_systems",    "/signals/alarm_systems",    GR_C_MAG},
  {"smart_home",       "/signals/smart_home",       GR_C_GRN},
  {"utility_meters",   "/signals/utility_meters",   GR_C_AMB},
  {"industrial",       "/signals/industrial",       GR_C_AMB},
  {"medical",          "/signals/medical",          GR_C_RED},
  {"vehicles",         "/signals/vehicles",         GR_C_CYAN},
  {"access_control",   "/signals/access_control",   GR_C_AMB},
  {"pagers",           "/signals/pagers",           GR_C_DIM},
  {"baby_monitors",    "/signals/baby_monitors",    GR_C_GRN},
  {"garden_outdoor",   "/signals/garden_outdoor",   GR_C_GRN},
  {"rc_toys",          "/signals/rc_toys",          GR_C_CYAN},
  {"lora",             "/signals/lora",             GR_C_PUR},
  {"unidentified",     "/signals/unidentified",     GR_C_RED},
  {"sorting_queue",    "/brain/sorting_queue",      GR_C_AMB},
  {"episodes",         "/brain/episodes",           GR_C_CYAN},
  {"knowledge",        "/brain/knowledge",          GR_C_PUR},
  {"models",           "/brain/models",             GR_C_DIM},
};
#define GR_FOLDER_COUNT 23

static inline void grScreenMemory(Arduino_GFX* gfx, uint16_t sdMb) {
  gfx->fillRect(0, GR_CONTENT_Y, GR_SCR_W, GR_CONTENT_H, GR_C_BG);
  uint32_t now = millis();

  // refresh folder counts every 10s
  if (now - grFolderCacheMs > GR_FOLDER_CACHE_MS) {
    grFolderCacheMs = now;
    for (int i = 0; i < GR_FOLDER_COUNT; i++) {
      grFolderCounts[i] = grSdCountFiles(grFolders[i].path);
    }
  }

  // artifact: icon(10,30,20,'database'); dt(34,32,'SIGNAL ARCHIVE',AMB)
  gfx->setTextSize(1);
  grIcon(gfx, 10, 30, 20, ICO_DATABASE);
  gfx->setTextColor(GR_C_AMB);
  gfx->setCursor(34, 32);
  gfx->print(F("SIGNAL ARCHIVE"));
  gfx->drawFastHLine(10, 46, 300, GR_C_LINE);

  // artifact: icon(10,50,14,'sd_card'); sr(28,50,282,14,LINE)
  grIcon(gfx, 10, 50, 14, ICO_SD_CARD);
  int barY = 50;
  gfx->drawRect(28, barY, 282, 14, GR_C_LINE);
  // artifact: fr(29,51,4,12,CYAN) — small usage indicator
  float usedPct = 1.0f - (float)sdMb / 128000.0f;
  int usedFill = max(4, (int)(usedPct * 280));
  gfx->fillRect(29, barY + 1, usedFill, 12, GR_C_CYAN);
  gfx->setTextColor(GR_C_INK);
  gfx->setCursor(36, barY + 2);
  if (sdMb > 1000)
    gfx->printf("SD: %.1f GB free / 128 GB", sdMb / 1024.0f);
  else
    gfx->printf("SD: %u MB free / 128 GB", sdMb);

  int y = 70;
  int maxVisible = 20;
  int startIdx = grMemScrollOffset;
  if (startIdx < 0) startIdx = 0;
  if (startIdx > GR_FOLDER_COUNT - maxVisible) startIdx = GR_FOLDER_COUNT - maxVisible;
  if (startIdx < 0) startIdx = 0;

  // separator before brain folders
  for (int vi = 0; vi < maxVisible && (startIdx + vi) < GR_FOLDER_COUNT; vi++) {
    int i = startIdx + vi;
    if (y > 340) break;

    // draw separator before brain section
    if (i == 19 && vi > 0) {
      gfx->setCursor(10, y);
      gfx->setTextColor(GR_C_DIM);
      gfx->print(F("-- brain --"));
      y += 12;
      if (y > 340) break;
    }

    uint16_t dotCol = grFolders[i].dotColor;
    if (grFolderCounts[i] == 0) dotCol = GR_C_DIM;

    // artifact: icon(8,y-1,14,folderIcon) per folder type
    if (i < 19)       grIcon(gfx, 8, y - 1, 14, grDevToIco(i + 1));
    else if (i == 19)  grIcon(gfx, 8, y - 1, 14, ICO_HOURGLASS);
    else if (i == 20)  grIcon(gfx, 8, y - 1, 14, ICO_CPU);
    else if (i == 21)  grIcon(gfx, 8, y - 1, 14, ICO_BOOK);
    else               grIcon(gfx, 8, y - 1, 14, ICO_DATABASE);

    gfx->setCursor(26, y);
    gfx->setTextColor(GR_C_INK);
    gfx->printf("%-15s", grFolders[i].name);
    gfx->setTextColor(grFolderCounts[i] > 0 ? dotCol : GR_C_DIM);
    gfx->printf("%3u", grFolderCounts[i]);
    y += 15;
  }

  // scroll indicators
  if (startIdx > 0) {
    gfx->setCursor(300, 68);
    gfx->setTextColor(GR_C_DIM);
    gfx->print((char)0x18);  // up arrow
  }
  if (startIdx + maxVisible < GR_FOLDER_COUNT) {
    gfx->setCursor(300, 336);
    gfx->setTextColor(GR_C_DIM);
    gfx->print((char)0x19);  // down arrow
  }

  // ---- Zone C: Activity Summary (y~348 to y~428) ----
  y = 350;
  gfx->drawFastHLine(10, y, 300, GR_C_LINE);
  y += 6;

  gfx->setCursor(10, y);
  gfx->setTextColor(GR_C_DIM);
  gfx->printf("Total: %lu captures  Sorted: %lu  Queue: %u",
              (unsigned long)grTotalCaptures,
              (unsigned long)grTotalSorted,
              grSortQueueDepth);
  y += 14;

  gfx->setCursor(10, y);
  gfx->printf("Rate: %.1f/min", grCaptureRate);
  y += 14;

  // last 3 sorted items
  gfx->setCursor(10, y);
  gfx->setTextColor(GR_C_DIM);
  gfx->print(F("Last sorted:"));
  y += 12;
  for (int i = 0; i < 3; i++) {
    int idx = ((int)grCapTickerIdx - 1 - i + 10) % 5;
    if (!grCapTicker[idx].valid) break;
    GrCaptureTick& t = grCapTicker[idx];
    char ageBuf[8];
    grFmtAge(ageBuf, sizeof(ageBuf), millis() - t.timestampMs);
    gfx->setCursor(18, y);
    gfx->setTextColor(grDevTypeColor(t.deviceType));
    gfx->printf("%s %.2fMHz (%s)", grDevTypeName(t.deviceType), t.freqMHz, ageBuf);
    y += 12;
  }
}

// ############################################################################
//  Screen 4 — SYSTEM CONTROL CENTER (Config)
// ############################################################################

static bool     grCfgR40tMode = false;
static uint8_t  grCfgSelIndex = 0;

static inline void grScreenConfig(Arduino_GFX* gfx, uint8_t mode, bool muted) {
  gfx->fillRect(0, GR_CONTENT_Y, GR_SCR_W, GR_CONTENT_H, GR_C_BG);
  uint32_t now = millis();

  gfx->setTextSize(1);

  // artifact: icon(10,30,20,'mesh'); dt(34,32,'MESH NETWORK',PUR)
  grIcon(gfx, 10, 30, 20, ICO_MESH);
  gfx->setTextColor(GR_C_PUR);
  gfx->setCursor(34, 32);
  gfx->print(F("MESH NETWORK"));
  gfx->drawFastHLine(10, 46, 300, GR_C_LINE);

  int y = 50;
  // artifact: icon(10,y,20,'radar'); fc(34,y+6,3,GRN)
  grIcon(gfx, 10, y, 20, ICO_RADAR);
  gfx->fillCircle(34, y + 6, 3, grPeer0.up ? GR_C_GRN : GR_C_RED);
  // artifact: dt(40,y,'N0 RADIO BODY',INK); dt(170,y,'ONLINE',GRN)
  gfx->setCursor(40, y);
  gfx->setTextColor(GR_C_INK);
  gfx->print(F("N0 RADIO BODY"));
  gfx->setCursor(170, y);
  gfx->setTextColor(grPeer0.up ? GR_C_GRN : GR_C_RED);
  gfx->print(grPeer0.up ? F("ONLINE") : F("OFFLINE"));
  y += 12;
  gfx->setCursor(40, y);
  gfx->setTextColor(GR_C_DIM);
  gfx->printf("Radios:%d Attack:%s", grPeer0.radioCount,
              grPeer0.attacking ? "ACTIVE" : "IDLE");
  y += 16;

  // artifact: icon(10,y,20,'brain'); fc(34,y+6,3,GRN)
  grIcon(gfx, 10, y, 20, ICO_BRAIN);
  gfx->fillCircle(34, y + 6, 3, grPeer1.up ? GR_C_GRN : GR_C_RED);
  gfx->setCursor(40, y);
  gfx->setTextColor(GR_C_INK);
  gfx->print(F("N1 NEURAL ENG"));
  gfx->setCursor(170, y);
  gfx->setTextColor(grPeer1.up ? GR_C_GRN : GR_C_RED);
  gfx->print(grPeer1.up ? F("ONLINE") : F("OFFLINE"));
  y += 12;
  gfx->setCursor(40, y);
  gfx->setTextColor(GR_C_DIM);
  gfx->printf("Conf:%d%% Rules:%d", grNnConfidence, grNnRulesFired);
  y += 16;

  // artifact: icon(10,y,20,'mesh'); fc(34,y+6,3,GRN)
  grIcon(gfx, 10, y, 20, ICO_MESH);
  gfx->fillCircle(34, y + 6, 3, grPeer3.up ? GR_C_GRN : GR_C_RED);
  gfx->setCursor(40, y);
  gfx->setTextColor(GR_C_INK);
  gfx->print(F("N3 BACKBONE"));
  gfx->setCursor(170, y);
  gfx->setTextColor(grPeer3.up ? GR_C_GRN : GR_C_RED);
  gfx->print(grPeer3.up ? F("ONLINE") : F("OFFLINE"));
  y += 12;
  gfx->setCursor(40, y);
  gfx->setTextColor(GR_C_DIM);
  gfx->printf("BLE:%d Trk:%d(%d!) Mesh:%d%%",
              grBbBleCount, grBbTrackers, grBbFlagged, grBbMeshHealth);
  y += 18;

  // artifact: hl(10,y,300,LINE); icon(10,y-2,16,'eye'); dt(30,y,'BACKBONE INTEL',MAG)
  gfx->drawFastHLine(10, y, 300, GR_C_LINE);
  y += 6;
  grIcon(gfx, 10, y - 2, 16, ICO_EYE);
  gfx->setCursor(30, y);
  gfx->setTextColor(GR_C_MAG);
  gfx->print(F("BACKBONE INTEL"));
  y += 12;

  gfx->setCursor(14, y);
  gfx->setTextColor(GR_C_DIM);
  gfx->printf("WiFi: %d APs  Busiest: CH%d  Best:%ddBm",
              grBbApCount, grBbBusiestCh, grBbStrongestRssi);
  y += 12;

  gfx->setCursor(14, y);
  gfx->print(F("Deauth: "));
  if (grBbStormActive) {
    gfx->setTextColor(GR_C_RED);
    gfx->printf("STORM! (%d/min)", grBbDeauthPerMin);
  } else {
    gfx->setTextColor(GR_C_GRN);
    gfx->printf("CLEAR (%d/min)", grBbDeauthPerMin);
  }
  y += 12;

  gfx->setCursor(14, y);
  gfx->setTextColor(GR_C_DIM);
  gfx->printf("BLE Tot:%d New:%d/5m Active Trk:%d",
              grBbTotalUniqueBle, grBbNewRate, grBbTrackersActive);
  y += 12;

  gfx->setCursor(14, y);
  gfx->printf("Env: %s (0x%08lX)",
              grBbEnvChanged ? "MOVED!" : "STABLE",
              (unsigned long)grBbEnvFingerprint);
  if (grBbEnvChanged) {
    gfx->setTextColor(GR_C_AMB);
    gfx->setCursor(240, y);
    gfx->print(F("!"));
  }
  y += 16;

  // artifact: hl(10,y,300,LINE); icon(10,y-2,16,'gear'); dt(30,y,'SYSTEM',DIM)
  gfx->drawFastHLine(10, y, 300, GR_C_LINE);
  y += 6;
  grIcon(gfx, 10, y - 2, 16, ICO_GEAR);
  gfx->setCursor(30, y);
  gfx->setTextColor(GR_C_DIM);
  gfx->print(F("SYSTEM"));
  y += 12;

  gfx->setCursor(14, y);
  uint32_t heapNow = ESP.getFreeHeap();
  gfx->setTextColor(GR_C_DIM);
  gfx->printf("Heap: %lu KB free", (unsigned long)(heapNow / 1024));
  y += 12;

  gfx->setCursor(14, y);
  gfx->printf("CPU: %d MHz", ESP.getCpuFreqMHz());
  y += 12;

  uint32_t upSec = now / 1000;
  gfx->setCursor(14, y);
  if (upSec >= 86400) {
    gfx->printf("Uptime: %lud %02lu:%02lu:%02lu",
                (unsigned long)(upSec / 86400),
                (unsigned long)((upSec / 3600) % 24),
                (unsigned long)((upSec / 60) % 60),
                (unsigned long)(upSec % 60));
  } else {
    gfx->printf("Uptime: %02lu:%02lu:%02lu",
                (unsigned long)(upSec / 3600),
                (unsigned long)((upSec / 60) % 60),
                (unsigned long)(upSec % 60));
  }
  y += 12;

  gfx->setCursor(14, y);
  gfx->printf("ESP-NOW CH1  WiFi Clients:%d Probes:%d",
              grBbWifiClients, grBbProbeCount);
  y += 16;

  // ---- Zone D: Audio & Sort (y~294 to y~356) ----
  gfx->drawFastHLine(10, y, 300, GR_C_LINE);
  y += 4;

  // artifact: icon(10,y-2,16,'speaker'/'mute')
  grIcon(gfx, 10, y - 2, 16, muted ? ICO_MUTE : ICO_SPEAKER);
  gfx->setCursor(34, y);
  gfx->setTextColor(GR_C_DIM);
  gfx->print(F("Speaker: "));
  gfx->setTextColor(muted ? GR_C_RED : GR_C_GRN);
  gfx->print(muted ? F("MUTED") : F("ON"));
  y += 18;

  gfx->setCursor(14, y);
  gfx->setTextColor(GR_C_DIM);
  gfx->printf("Volume: ");
  for (int v = 0; v < 10; v++) {
    if (v < grVol) gfx->fillRect(80 + v * 12, y, 10, 8, GR_C_CYAN);
    else gfx->fillRect(80 + v * 12, y, 10, 8, GR_C_LINE);
  }
  gfx->setCursor(210, y);
  gfx->printf("%d/10", grVol);
  y += 14;

  gfx->setCursor(14, y);
  gfx->setTextColor(GR_C_DIM);
  gfx->printf("Sort: %u pending  Sorted: %lu",
              grSortQueueDepth, (unsigned long)grTotalSorted);
  y += 12;

  gfx->setCursor(14, y);
  gfx->printf("Brain processed: %lu signals", (unsigned long)grTotalCaptures);
  y += 16;

  // ---- Zone E: Version & Identity (y~370 to y~428) ----
  gfx->drawFastHLine(10, y, 300, GR_C_LINE);
  y += 6;

  // artifact: drawBootGecko(58,y+56,80); dt(100,y+24,'KODI v1.0',CYAN,2)
  grIcon(gfx, 10, y, 48, ICO_GECKO);
  gfx->setTextColor(GR_C_CYAN);
  gfx->setTextSize(2);
  gfx->setCursor(100, y + 24);
  gfx->print(GR_VERSION);
  gfx->setTextSize(1);
  gfx->setCursor(100, y + 50);
  gfx->setTextColor(GR_C_DIM);
  gfx->printf("Node 2 | boardType=%d", GR_BOARD_TYPE);
  gfx->setCursor(100, y + 62);
  gfx->printf("Built: %s", __DATE__);
  y += 14;

  // R40T mode exit hint
  if (grCfgR40tMode) {
    gfx->setCursor(10, y);
    gfx->setTextColor(GR_C_AMB);
    gfx->print(F(">> Hold SELECT to Exit R40T Mode <<"));
  }
}
