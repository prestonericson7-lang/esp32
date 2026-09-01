#pragma once
// ############################################################################
//  gg_oled2.h  -  KODI RF CONTROLLER  /  second OLED status display
//
//  Second SSD1306 128x64 on Wire1 (SDA=16, SCL=17) at address 0x3C.
//  Separate I2C bus from the main OLED (Wire, SDA=21, SCL=22).
//
//  Boot animation, live system status dashboard, and active-mode overlays.
// ############################################################################

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include "gg_config.h"

static Adafruit_SSD1306 ggOled2(128, 64, &Wire1, -1);
static bool ggOled2Ok = false;

// ===== Screen 2 state =======================================================

static float    ggS2FreqA = 433.92f;
static float    ggS2FreqB = 433.92f;
static float    ggS2FreqC = 433.92f;
static float    ggS2FreqD = 433.92f;
static char     ggS2NnCmd[16] = "";
static uint32_t ggS2LastUpdate = 0;
static uint32_t ggOled2InUseUntil = 0;
static bool     gScreen2Active      = false;
static bool     gScreen2DisplayIdle = false;
static uint32_t gScreen2LastInput   = 0;

static const char* const ggNnStateNames[] = {
  "BOOT", "IDLE", "MONITOR", "ALERT", "ERROR", "LEARN", "ANALYZE", "ADVISE"
};
#define GG_NN_STATE_COUNT 8

// ===== Init ==================================================================

static inline void ggOled2Begin() {
  Wire1.begin(GG_PIN_SDA2, GG_PIN_SCL2, GG_I2C_HZ);
  ggOled2Ok = ggOled2.begin(SSD1306_SWITCHCAPVCC, GG_OLED2_ADDR);
  if (ggOled2Ok) {
    ggOled2.ssd1306_command(0x8D); ggOled2.ssd1306_command(0x14);
    ggOled2.ssd1306_command(0x81); ggOled2.ssd1306_command(0xFF);
    ggOled2.clearDisplay(); ggOled2.display();
  }
}

static inline void ggOled2SetFreqs(float a, float b, float c, float d) {
  ggS2FreqA = a; ggS2FreqB = b; ggS2FreqC = c; ggS2FreqD = d;
}

// ===== Header bar ============================================================

static inline void ggOled2Header(const char* title) {
  ggOled2.fillRect(0, 0, 128, 15, SSD1306_WHITE);
  ggOled2.setTextColor(SSD1306_BLACK); ggOled2.setTextSize(1);
  ggOled2.setCursor(3, 4); ggOled2.print(title);
  ggOled2.setTextColor(SSD1306_WHITE);
  ggOled2.drawRect(0, 15, 128, 49, SSD1306_WHITE);
}

static inline void ggOled2Clear() {
  ggOled2.clearDisplay();
  ggOled2Header("STATUS");
}

// ===== Boot animation ========================================================

static inline void ggOled2BootAnimate(bool ccA, bool ccB, bool ccC, bool ccD) {
  if (!ggOled2Ok) return;

  int nRadios = (ccA?1:0) + (ccB?1:0) + (ccC?1:0) + (ccD?1:0);
  const char* title = "KODI NEURAL LINK";
  int titleLen = strlen(title);

  // Phase 1: scan lines sweep down, header types in
  for (int f = 0; f < 16; f++) {
    ggOled2.clearDisplay();
    // random noise blocks
    for (int b = 0; b < 3; b++)
      ggOled2.fillRect(random(0, 100), random(12, 60), random(8, 40), 1, SSD1306_WHITE);
    // sweep line
    int sweepY = map(f, 0, 15, 12, 63);
    ggOled2.drawFastHLine(0, sweepY, 128, SSD1306_WHITE);
    // header types in progressively
    int shown = map(f, 0, 15, 0, titleLen);
    if (shown > 0) {
      ggOled2.fillRect(0, 0, 128, 11, SSD1306_WHITE);
      ggOled2.setTextColor(SSD1306_BLACK); ggOled2.setTextSize(1);
      ggOled2.setCursor(2, 2);
      for (int i = 0; i < shown && i < titleLen; i++) ggOled2.write(title[i]);
      ggOled2.setTextColor(SSD1306_WHITE);
    }
    ggOled2.display();
    delay(50);
  }

  // Phase 2: system check — each line ticks in
  struct { const char* label; const char* result; } checks[] = {
    { "RADIOS",  nullptr },
    { "ESPNOW",  ggLinkReady ? "READY" : "FAIL" },
    { "NEURAL",  "ONLINE" },
    { "LINK",    ggLinkUp ? "CONNECTED" : "WAITING" },
  };
  char radioBuf[8];
  snprintf(radioBuf, sizeof(radioBuf), "%d/4", nRadios);
  checks[0].result = radioBuf;

  for (int line = 0; line < 4; line++) {
    ggOled2.clearDisplay();
    ggOled2Header("KODI NEURAL LINK");
    ggOled2.setTextSize(1);

    // draw all completed lines
    for (int prev = 0; prev < line; prev++) {
      ggOled2.setCursor(3, 18 + prev * 9);
      ggOled2.printf("%-8s %s", checks[prev].label, checks[prev].result);
    }

    // animate dots for current line
    for (int dots = 0; dots <= 4; dots++) {
      ggOled2.fillRect(1, 18 + line * 9, 126, 9, SSD1306_BLACK);
      ggOled2.setCursor(3, 18 + line * 9);
      ggOled2.print(checks[line].label);
      for (int d = 0; d < dots; d++) ggOled2.print('.');
      ggOled2.display();
      delay(60);
    }

    // show result
    ggOled2.fillRect(1, 18 + line * 9, 126, 9, SSD1306_BLACK);
    ggOled2.setCursor(3, 18 + line * 9);
    ggOled2.printf("%-8s %s", checks[line].label, checks[line].result);
    ggOled2.display();
    delay(100);
  }

  // version at bottom
  ggOled2.setCursor(3, 55);
  ggOled2.print(GG_VERSION);
  ggOled2.display();
  delay(800);
}

// ===== Live system status dashboard ==========================================

static inline void ggOled2UpdateStatus() {
  if (!ggOled2Ok) return;
  uint32_t now = millis();
  if (now < ggOled2InUseUntil) return;
  if (now - ggS2LastUpdate < 500) return;
  ggS2LastUpdate = now;

  ggOled2.clearDisplay();
  ggOled2Header("SYSTEM STATUS");
  ggOled2.setTextSize(1);

  // radio status — 2 per line with frequency
  ggOled2.setCursor(3, 18);
  ggOled2.printf("A:%.0f %s B:%.0f %s",
    ggS2FreqA, ggCcPresentA ? "OK" : "--",
    ggS2FreqB, ggCcPresentB ? "OK" : "--");

  ggOled2.setCursor(3, 27);
  ggOled2.printf("C:%.0f %s D:%.0f %s",
    ggS2FreqC, ggCcPresentC ? "OK" : "--",
    ggS2FreqD, ggCcPresentD ? "OK" : "--");

  // ESP-NOW link to S3
  ggOled2.setCursor(3, 36);
  ggOled2.printf("LINK: S3 %s", ggLinkUp ? "CONNECTED" : "WAITING");

  // neural engine state
  ggOled2.setCursor(3, 45);
  if (ggS2NnCmd[0] != '\0') {
    ggOled2.printf("NN: %s ACTIVE", ggS2NnCmd);
  } else {
    uint8_t st = ggNnState;
    const char* stName = (st < GG_NN_STATE_COUNT) ? ggNnStateNames[st] : "???";
    ggOled2.printf("NN: %s %d%%", stName, ggNnConfidence);
  }

  // bottom hint + heap
  ggOled2.setCursor(3, 55);
  ggOled2.printf("HEAP %lukB free", (unsigned long)(ESP.getFreeHeap() / 1024));

  ggOled2.display();
}

// ===== AI Command Menu (Screen 2 focused) ====================================

struct GgNeuralCmd {
  const char* name;
  const char* hint;
  uint8_t     cmdId;
};

static const GgNeuralCmd ggNeuralCmds[] = {
  { "COP SCAN",      "Scan for police comms",    40 },
  { "TRACKER HUNT",  "Detect tracking devices",  41 },
  { "NET AUDIT",     "Audit nearby networks",    42 },
  { "SIGNAL METER",  "Signal strength meter",    43 },
  { "FULL GUARD",    "Monitor all, alert threats", 44 },
  { "GO DARK",       "Passive only, silence all", 45 },
  { "KILL ALERTS",   "Silence current alert",     46 },
  { "OPEN GARAGE",   "Replay last on 315MHz",     50 },
  { "REPLAY",        "Replay last recording",     51 },
  { "JAM SIGNAL",    "Start CC1101 jammer",       52 },
  { "RECORD",        "Record sub-GHz signal",     53 },
  { "BRUTE FORCE",   "Brute force RF codes",      54 },
  { "SCAN AREA",     "Freq scan all bands",       55 },
  { "CAPTURE",       "Capture sub-GHz signal",    56 },
  { "STOP",          "Stop all transmissions",     0 },
};
#define GG_NEURAL_CMD_COUNT 15

static uint8_t ggCmdSel = 0;
static uint8_t ggCmdTop = 0;
#define GG_CMD_ROWS 4

static inline void ggOled2DrawCmdMenu() {
  if (!ggOled2Ok) return;
  ggOled2InUseUntil = millis() + 1000;
  ggOled2.clearDisplay();
  ggOled2Header("AI COMMANDS");
  ggOled2.setTextSize(1);

  if (ggCmdSel < ggCmdTop) ggCmdTop = ggCmdSel;
  if (ggCmdSel > ggCmdTop + GG_CMD_ROWS - 1) ggCmdTop = ggCmdSel - (GG_CMD_ROWS - 1);

  for (int i = 0; i < GG_CMD_ROWS; i++) {
    int idx = ggCmdTop + i;
    if (idx >= GG_NEURAL_CMD_COUNT) break;
    int y = 18 + i * 9;
    if (idx == ggCmdSel) {
      ggOled2.fillRect(1, y - 1, 126, 10, SSD1306_WHITE);
      ggOled2.setTextColor(SSD1306_BLACK);
    } else {
      ggOled2.setTextColor(SSD1306_WHITE);
    }
    ggOled2.setCursor(4, y);
    ggOled2.print(ggNeuralCmds[idx].name);
  }
  ggOled2.setTextColor(SSD1306_WHITE);

  ggOled2.drawFastHLine(1, 54, 126, SSD1306_WHITE);
  ggOled2.setCursor(3, 56);
  ggOled2.print(ggNeuralCmds[ggCmdSel].hint);
  ggOled2.display();
}

// ===== Radio status overview =================================================

static inline void ggOled2ShowRadioStatus(bool ccA, bool ccB, bool ccC, bool ccD) {
  if (!ggOled2Ok) return;
  ggOled2.clearDisplay();
  ggOled2Header("RADIOS");
  ggOled2.setTextSize(1);
  ggOled2.setCursor(3, 18); ggOled2.printf("CC1101-A: %s", ccA ? "OK" : "--");
  ggOled2.setCursor(3, 27); ggOled2.printf("CC1101-B: %s", ccB ? "OK" : "--");
  ggOled2.setCursor(3, 36); ggOled2.printf("CC1101-C: %s", ccC ? "OK" : "--");
  ggOled2.setCursor(3, 45); ggOled2.printf("CC1101-D: %s", ccD ? "OK" : "--");
  int cnt = (int)ccA + ccB + ccC + ccD;
  ggOled2.setCursor(3, 55); ggOled2.printf("%d/4 radios online", cnt);
  ggOled2.display();
}

// ===== Active TX info ========================================================

static inline void ggOled2ShowTxStatus(const char* radioName, float freqMHz,
                                       uint32_t count, int rssi) {
  if (!ggOled2Ok) return;
  ggOled2InUseUntil = millis() + 1000;
  ggOled2.clearDisplay();
  ggOled2Header("TX ACTIVE");
  ggOled2.setTextSize(1);
  ggOled2.setCursor(3, 18); ggOled2.printf("Radio: %s", radioName);
  ggOled2.setCursor(3, 27); ggOled2.printf("Freq:  %.2f MHz", freqMHz);
  ggOled2.setTextSize(2);
  ggOled2.setCursor(3, 38); ggOled2.print(count);
  ggOled2.setTextSize(1);
  ggOled2.setCursor(80, 42); ggOled2.print("pkts");
  ggOled2.setCursor(3, 55); ggOled2.printf("RSSI: %d dBm", rssi);
  ggOled2.display();
}

// ===== RX status =============================================================

static inline void ggOled2ShowRxStatus(const char* radioName, float freqMHz,
                                       uint32_t count,
                                       const uint8_t* lastData, uint8_t lastLen) {
  if (!ggOled2Ok) return;
  ggOled2InUseUntil = millis() + 1000;
  ggOled2.clearDisplay();
  ggOled2Header("RX LISTEN");
  ggOled2.setTextSize(1);
  ggOled2.setCursor(3, 18); ggOled2.printf("%s  %.2fMHz", radioName, freqMHz);
  ggOled2.setCursor(3, 27); ggOled2.printf("Received: %lu", (unsigned long)count);
  ggOled2.setCursor(3, 36);
  for (int i = 0; i < lastLen && i < 10; i++) ggOled2.printf("%02X", lastData[i]);
  ggOled2.setCursor(3, 45);
  for (int i = 0; i < lastLen && i < 10; i++) {
    char c = lastData[i]; ggOled2.write((c >= 32 && c < 127) ? c : '.');
  }
  ggOled2.setCursor(3, 55); ggOled2.printf("len %u bytes", lastLen);
  ggOled2.display();
}

// ===== Idle standby ==========================================================

static inline void ggOled2ShowIdle() {
  if (!ggOled2Ok) return;
  ggOled2.clearDisplay();
  ggOled2Header("KODI RF");
  ggOled2.setTextSize(1);
  ggOled2.setCursor(3, 18); ggOled2.print("All radios standby");
  ggOled2.setCursor(3, 36); ggOled2.printf("Heap: %lu B", (unsigned long)ESP.getFreeHeap());
  ggOled2.setCursor(3, 55); ggOled2.print("CC1101 EDITION");
  ggOled2.display();
}

// ===== Active attack status ==================================================

static inline void ggOled2ShowAttack(const char* name, uint32_t elapsed,
                                      uint32_t pktCount) {
  if (!ggOled2Ok) return;
  ggOled2InUseUntil = millis() + 1000;
  ggOled2.clearDisplay();
  ggOled2Header("ATTACK");
  ggOled2.setTextSize(1);
  ggOled2.setCursor(3, 18); ggOled2.print(name);
  ggOled2.setCursor(3, 28); ggOled2.printf("Time: %lus", (unsigned long)elapsed);
  ggOled2.setTextSize(2);
  ggOled2.setCursor(3, 40); ggOled2.print(pktCount);
  ggOled2.setTextSize(1);
  ggOled2.setCursor(80, 44); ggOled2.print("pkts");
  ggOled2.setCursor(3, 55); ggOled2.print("SEL=stop");
  ggOled2.display();
}
