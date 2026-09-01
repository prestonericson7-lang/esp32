#pragma once
// ############################################################################
//  gr_boot.h — 1:1 port of artifact 28f46b57 drawBootAnimFrame(f)
//
//  6 phases, 110 frames, ~7 seconds. Every position/color from artifact JS.
//  Canvas calls → Arduino_GFX calls, same coordinates.
// ############################################################################

#include <Arduino.h>
#include "gr_config.h"

// grBeep, grToneOn, grToneOff, grAmpOn defined in gr_audio.h (included first)

#define GR_EYE1_X  0.8775f
#define GR_EYE1_Y  0.1414f
#define GR_EYE_R   0.0127f

// boot-specific RGB565 colors from artifact hex
#define GR_B_DARK   0x1082  // #111111
#define GR_B_CARD   0x0841  // #0a0a0a
#define GR_B_CBRD   0x2104  // #222222
#define GR_B_DK2    0x18C3  // #1a1a1a
#define GR_B_GRAY   0x9CF3  // #999999
#define GR_B_DGRAY  0x6B4D  // #666666

// ---- Phase 0: WAKE (artifact f0-f9) ----------------------------------------
static inline void grBootPhase0(Arduino_GFX* gfx) {
  // f0-f2: black
  gfx->fillScreen(GR_C_BG);
  delay(120);

  grBeep(60, 100);

  // f3-f8: cyan dot at center (160,240) growing r=2→10
  for (int f = 3; f <= 8; f++) {
    float t = (float)(f - 3) / 6.0f;
    int r = 2 + (int)(t * 8);
    gfx->fillCircle(160, 240, r, GR_C_CYAN);
    delay(40);
  }

  grBeep(2000, 10);

  // f9: horizontal ellipse at center
  gfx->fillCircle(160, 240, 11, GR_C_BG);
  gfx->fillRect(148, 237, 24, 6, GR_C_CYAN);
  delay(60);
}

// ---- Phase 1: REVEAL (artifact f10-f39) ------------------------------------
static inline void grBootPhase1(Arduino_GFX* gfx) {
  gfx->fillScreen(GR_C_BG);

  // gecko at (90,140) 140x140 — load from SD
  bool ok = false;
  if (grSdReady) {
    ok = grAssetDraw(gfx, "/assets/sprites/gecko/gecko_boot_140.raw", 90, 140);
    if (!ok) ok = grAssetDraw(gfx, "/assets/sprites/gecko/gecko_icon_140.raw", 90, 140);
  }
  if (!ok) {
    // procedural gecko silhouette (centered ~160,210)
    gfx->fillCircle(160, 185, 30, GR_C_CYAN);
    gfx->fillCircle(150, 175, 14, GR_C_CYAN);
    gfx->fillCircle(170, 175, 14, GR_C_CYAN);
    gfx->fillRect(130, 190, 60, 50, GR_C_CYAN);
    gfx->fillCircle(148, 178, 4, GR_C_BG);
    gfx->fillCircle(168, 178, 4, GR_C_BG);
    gfx->fillRect(125, 240, 15, 6, GR_C_CYAN);
    gfx->fillRect(180, 240, 15, 6, GR_C_CYAN);
    gfx->fillRect(140, 240, 40, 8, GR_C_CYAN);
  }
  delay(200);

  // KODI letters appear one at a time
  // artifact: bold 48px Courier, spacing=56, baseX=76(center), ly=365(baseline)
  // setCursor uses top-left: x = center - 18, y = baseline - 48 ≈ 317
  const int16_t lx[] = {58, 114, 170, 226};
  const char     lc[] = "KODI";
  const uint16_t hz[] = {800, 900, 1000, 1100};

  gfx->setTextSize(6);
  for (int i = 0; i < 4; i++) {
    // artifact: letters start white then go cyan
    gfx->setTextColor(GR_C_INK);
    gfx->setCursor(lx[i], 317);
    gfx->print(lc[i]);
    grBeep(hz[i], 40);
    delay(180);
    // transition to cyan
    gfx->setTextColor(GR_C_CYAN);
    gfx->setCursor(lx[i], 317);
    gfx->print(lc[i]);
  }

  // chord
  grBeep(523, 60);
  delay(70);
  grBeep(659, 60);
  delay(350);
}

// ---- Phase 2+3: ZOOM (artifact f40-f79) ------------------------------------
static inline void grBootPhase2(Arduino_GFX* gfx) {
  // eye convergence point from artifact:
  // baseEyeX = 90 + 0.8775*140 = 212.85 ≈ 213
  // baseEyeY = 140 + 0.1414*140 = 159.8 ≈ 160
  const int eyeX = 160, eyeY = 200;

  grBeep(1200, 30);

  // artifact: black closes in toward eye with cubic easing over 40 frames
  for (int s = 0; s < 20; s++) {
    float t = (float)s / 19.0f;
    float e = t * t * t;

    int l  = (int)(e * eyeX);
    int r  = 320 - (int)(e * (320 - eyeX));
    int tp = (int)(e * eyeY);
    int bt = 480 - (int)(e * (480 - eyeY));

    if (l > 0)   gfx->fillRect(0, 0, l, 480, GR_C_BG);
    if (r < 320) gfx->fillRect(r, 0, 320 - r, 480, GR_C_BG);
    if (tp > 0)  gfx->fillRect(l, 0, r - l, tp, GR_C_BG);
    if (bt < 480) gfx->fillRect(l, bt, r - l, 480 - bt, GR_C_BG);

    if (s == 5) grBeep(600, 20);
    if (s == 10) grBeep(300, 40);
    delay(35);
  }

  // deep zoom — rumble + cyan flickers at center
  gfx->fillScreen(GR_C_BG);
  grToneOn(60);

  for (int i = 0; i < 5; i++) {
    grToneOn(300 - i * 50);
    gfx->fillCircle(160, 240, 4 - i, GR_C_CYAN);
    delay(80);
    gfx->fillCircle(160, 240, 5, GR_C_BG);
    delay(60);
  }

  grToneOff();
  delay(100);

  // void pulse
  grToneOn(60);
  delay(120);
  grBeep(200, 80);
  grToneOff();
  delay(80);
}

// ---- Phase 4+5: Dashboard build-up (artifact f80-f109) ---------------------
static inline void grBootPhase4(Arduino_GFX* gfx) {
  gfx->fillScreen(GR_C_BG);
  gfx->setTextSize(1);

  // artifact f80-f83: fading cyan rim circle (t<0.3)
  for (int r = 120; r <= 144; r += 8) {
    gfx->drawCircle(160, 240, r, GR_C_CYAN);
    delay(30);
  }
  gfx->fillScreen(GR_C_BG);
  delay(40);

  // -- dashT > 0: status bar --
  // artifact: fillRect(0,0,320,24,'#111'); fillText('KODI',8,16)
  gfx->fillRect(0, 0, 320, 24, GR_B_DARK);
  gfx->setTextColor(GR_C_CYAN);
  gfx->setCursor(8, 8);
  gfx->print(F("KODI"));
  // artifact: fillText('N0● N1● N3●', 200, 16) — green dots as fillCircle
  gfx->setTextColor(GR_C_GRN);
  gfx->setCursor(200, 8);
  gfx->print(F("N0"));
  gfx->fillCircle(216, 12, 3, GR_C_GRN);
  gfx->setCursor(222, 8);
  gfx->print(F("N1"));
  gfx->fillCircle(238, 12, 3, GR_C_GRN);
  gfx->setCursor(244, 8);
  gfx->print(F("N3"));
  gfx->fillCircle(260, 12, 3, GR_C_GRN);
  gfx->setTextColor(GR_B_GRAY);
  gfx->setCursor(266, 8);
  uint16_t sdG = grSdFreeMb / 1024;
  uint16_t hK = ESP.getFreeHeap() / 1024;
  gfx->printf("SD:%uG %uK", sdG, hK);
  delay(60);

  uint16_t toneHz = 120;
  grToneOn(toneHz);

  // -- dashT > 0.1: nav bar --
  gfx->fillRect(0, 456, 320, 24, GR_B_DARK);
  gfx->fillRect(0, 456, 64, 24, GR_C_CYAN);
  gfx->setTextColor(GR_C_BG);
  gfx->setCursor(16, 464);
  gfx->print(F("DASH"));
  gfx->setTextColor(GR_B_DGRAY);
  const char* navTabs[] = {"SPEC", "NEUR", "MEM", "CFG"};
  for (int i = 0; i < 4; i++) {
    gfx->setCursor(80 + i * 64, 464);
    gfx->print(navTabs[i]);
  }
  delay(50);

  toneHz += 80;
  grToneOn(toneHz);

  // -- dashT > 0.2: 3 node cards --
  const char* cardNames[] = {
    "N0 RADIO BODY", "N1 NEURAL ENGINE", "N3 BACKBONE"
  };
  const char* cardDetails[] = {
    "Radios: 4/4  TX: ACTIVE",
    "State: MONITORING  87%",
    "BLE: 23  Trackers: 2"
  };
  for (int i = 0; i < 3; i++) {
    int cy = 34 + i * 42;
    gfx->fillRect(6, cy, 308, 38, GR_B_CARD);
    gfx->drawRect(6, cy, 308, 38, GR_B_CBRD);
    gfx->fillRect(6, cy, 3, 38, GR_C_GRN);
    gfx->setTextColor(GR_C_CYAN);
    gfx->setCursor(14, cy + 6);
    gfx->print(cardNames[i]);
    gfx->fillCircle(14 + strlen(cardNames[i]) * 6 + 8, cy + 9, 3, GR_C_GRN);
    gfx->setTextColor(GR_B_DGRAY);
    gfx->setCursor(14, cy + 20);
    gfx->print(cardDetails[i]);
    delay(40);
  }

  toneHz += 80;
  grToneOn(toneHz);

  // -- dashT > 0.4: resource gauges --
  gfx->fillRect(6, 166, 150, 12, GR_B_DK2);
  gfx->fillRect(164, 166, 150, 12, GR_B_DK2);
  gfx->fillRect(7, 167, 107, 10, GR_C_GRN);
  gfx->fillRect(165, 167, 142, 10, GR_C_GRN);
  gfx->setTextColor(GR_C_INK);
  gfx->setCursor(20, 168);
  gfx->printf("HEAP %uKB", hK);
  gfx->setCursor(178, 168);
  gfx->printf("SD %u.%uGB", sdG, (grSdFreeMb % 1024) * 10 / 1024);
  delay(50);

  toneHz += 80;
  grToneOn(toneHz);

  // -- dashT > 0.5: mode banner --
  gfx->fillRect(6, 186, 308, 18, GR_C_CYAN);
  gfx->setTextColor(GR_C_BG);
  gfx->setCursor(90, 191);
  gfx->print(F(">> HUNTER MODE <<"));
  delay(50);

  toneHz += 80;
  grToneOn(toneHz);

  // -- dashT > 0.6: activity feed --
  gfx->setTextColor(GR_B_DGRAY);
  gfx->setCursor(6, 212);
  gfx->print(F("LIVE ACTIVITY"));

  const uint16_t feedCol[] = {GR_C_CYAN, GR_C_GRN, GR_C_AMB, GR_C_MAG, GR_C_RED};
  const char* feedText[] = {
    "GARAGE    433.92MHz   12s",
    "WEATHER   868.35MHz   1m",
    "CAR FOB   315.00MHz   3m",
    "ALARM     433.42MHz   5m",
    "UNKNOWN   915.00MHz   8m"
  };
  for (int i = 0; i < 5; i++) {
    gfx->fillCircle(10, 232 + i * 14, 3, feedCol[i]);
    gfx->setTextColor(GR_B_GRAY);
    gfx->setCursor(18, 227 + i * 14);
    gfx->print(feedText[i]);
    delay(25);
  }

  toneHz += 80;
  grToneOn(toneHz);

  // -- dashT > 0.7: sort + uptime --
  gfx->setTextColor(GR_B_DGRAY);
  gfx->setCursor(6, 302);
  gfx->print(F("SORT: IDLE  Rate: 0.0/min"));
  gfx->drawLine(6, 320, 314, 320, GR_B_DK2);
  gfx->setCursor(6, 327);
  uint32_t up = millis() / 1000;
  gfx->printf("UP %02lu:%02lu:%02lu  Cap:0  Sort:0",
              (unsigned long)(up / 3600),
              (unsigned long)((up / 60) % 60),
              (unsigned long)(up % 60));
  delay(50);

  // -- Phase 5: UI SETTLE — scanline sweep + fanfare --
  grToneOn(800);
  delay(50);
  grToneOff();
  grBeep(523, 60);
  delay(30);
  grBeep(659, 60);
  delay(30);
  grBeep(784, 80);

  // artifact scanline: faint cyan line sweeping down
  uint16_t scanCol = gfx->color565(0, 24, 24);
  for (int sy = 0; sy < 480; sy += 3) {
    gfx->drawFastHLine(0, sy, 320, scanCol);
  }

  // final blips
  grBeep(1800, 10);
  delay(15);
  grBeep(1800, 10);
  delay(15);
  grBeep(1800, 10);
  delay(25);
  grBeep(1047, 120);
  grToneOff();

  delay(200);
}

// ---- main entry point -------------------------------------------------------
static inline void grBootAnimate(Arduino_GFX* gfx) {
  grAmpOn();

  Serial.println(F("[boot] animation start (artifact 28f46b57 port)"));
  uint32_t t0 = millis();

  grBootPhase0(gfx);   // WAKE: spark → eye slit
  grBootPhase1(gfx);   // REVEAL: gecko + KODI letters
  grBootPhase2(gfx);   // ZOOM + DEEP ZOOM: into the eye
  grBootPhase4(gfx);   // EYE VOID + UI SETTLE: dashboard build-up

  uint32_t dur = millis() - t0;
  Serial.printf("[boot] complete in %lu ms\n", (unsigned long)dur);
}
