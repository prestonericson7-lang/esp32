#pragma once
// ############################################################################
//  gr_display.h  -  KODI  /  ST7796 320x480 PORTRAIT display on HSPI
//
//  Uses Arduino_GFX library. Backlight on GPIO 27.
//  5 screens: Dashboard / Spectrum / Neural / Memory / Config
// ############################################################################

#include <Arduino.h>
#include <SPI.h>
#include <Arduino_GFX_Library.h>
#include "gr_config.h"

// ---- bus + display objects -------------------------------------------------
static Arduino_DataBus* grBus = nullptr;
static Arduino_GFX*     grGfx = nullptr;
static uint8_t          grScreen = GR_SCREEN_DASHBOARD;
static bool             grScreenDirty = true;

// ---- init ------------------------------------------------------------------
static inline bool grDisplayBegin() {
  grBus = new Arduino_ESP32SPI(
    GR_PIN_LCD_DC, GR_PIN_LCD_CS,
    GR_PIN_LCD_SCK, GR_PIN_LCD_MOSI, GR_PIN_LCD_MISO
  );
  grGfx = new Arduino_ST7796(grBus, -1 /* RST */, 0 /* rotation = portrait */);

  if (!grGfx->begin()) {
    Serial.println(F("[lcd] init FAILED"));
    return false;
  }

  // backlight on
  pinMode(GR_PIN_LCD_BL, OUTPUT);
  digitalWrite(GR_PIN_LCD_BL, HIGH);

  grGfx->fillScreen(GR_C_BG);
  Serial.println(F("[lcd] ST7796 320x480 portrait ready"));
  return true;
}

// ---- screen navigation -----------------------------------------------------
static inline void grScreenNext() {
  grScreen = (grScreen + 1) % GR_SCREEN_COUNT;
  grScreenDirty = true;
}

static inline void grScreenPrev() {
  grScreen = (grScreen + GR_SCREEN_COUNT - 1) % GR_SCREEN_COUNT;
  grScreenDirty = true;
}

static inline void grScreenSet(uint8_t s) {
  if (s < GR_SCREEN_COUNT && s != grScreen) {
    grScreen = s;
    grScreenDirty = true;
  }
}

// ---- status bar (top 28px) — artifact statusBar() exact port ---------------
static inline void grDrawStatusBar(bool node0Up, bool node1Up, bool node3Up,
                                    uint16_t sdMb, uint16_t heapFree) {
  grGfx->fillRect(0, 0, 320, GR_STATUS_H, GR_C_BAR);
  grGfx->setTextSize(1);

  grGfx->setTextColor(GR_C_CYAN);
  grGfx->setCursor(4, 8);
  grGfx->print(F("KODI"));

  // artifact: fc(42,14,4,GRN); dt(48,8,'N0',GRN)
  grGfx->fillCircle(42, 14, 4, node0Up ? GR_C_GRN : GR_C_RED);
  grGfx->setCursor(48, 8);
  grGfx->setTextColor(node0Up ? GR_C_GRN : GR_C_RED);
  grGfx->print(F("N0"));

  // artifact: fc(72,14,4,GRN); dt(78,8,'N1',GRN)
  grGfx->fillCircle(72, 14, 4, node1Up ? GR_C_GRN : GR_C_RED);
  grGfx->setCursor(78, 8);
  grGfx->setTextColor(node1Up ? GR_C_GRN : GR_C_RED);
  grGfx->print(F("N1"));

  // artifact: fc(102,14,4,GRN); dt(108,8,'N3',GRN)
  grGfx->fillCircle(102, 14, 4, node3Up ? GR_C_GRN : GR_C_RED);
  grGfx->setCursor(108, 8);
  grGfx->setTextColor(node3Up ? GR_C_GRN : GR_C_RED);
  grGfx->print(F("N3"));

  // artifact: icon(142,5,16,'sd_card'); dt(160,8,'SD:121G',DIM)
  grIcon(grGfx, 142, 5, 16, ICO_SD_CARD);
  grGfx->setCursor(160, 8);
  grGfx->setTextColor(GR_C_DIM);
  if (sdMb > 1000) grGfx->printf("SD:%uG", sdMb / 1024);
  else grGfx->printf("SD:%uM", sdMb);

  // artifact: dt(262,8,'H:182K',DIM)
  grGfx->setCursor(262, 8);
  grGfx->printf("H:%uK", heapFree);
}

// ---- bottom nav bar — artifact navBar(a) exact port ------------------------
static const char* grNavNames[] = {"DASH","SPEC","NEUR","MEM","CFG"};
static const uint8_t grNavIcos[] = {ICO_CROSSHAIR,ICO_SWEEP,ICO_BRAIN,ICO_DATABASE,ICO_GEAR};

static inline void grDrawNavBar() {
  const int ny = 456;
  grGfx->fillRect(0, ny, 320, 24, GR_C_BAR);
  grGfx->setTextSize(1);

  for (int i = 0; i < 5; i++) {
    int nx = i * 64;
    if (i == grScreen) {
      grGfx->fillRect(nx, ny, 64, 24, GR_C_CYAN);
      grIcon(grGfx, nx + 2, ny + 4, 16, grNavIcos[i]);
      grGfx->setTextColor(GR_C_BG);
      grGfx->setCursor(nx + 20, ny + 7);
      grGfx->print(grNavNames[i]);
    } else {
      grIcon(grGfx, nx + 2, ny + 4, 16, grNavIcos[i]);
      grGfx->setTextColor(GR_C_DIM);
      grGfx->setCursor(nx + 20, ny + 7);
      grGfx->print(grNavNames[i]);
    }
  }
}

// ---- sort burst overlay (replaces normal screen during sorting) ------------
static inline void grDrawSortOverlay(uint16_t queueDepth, uint16_t sorted) {
  grGfx->fillRect(0, GR_CONTENT_Y, GR_SCR_W, GR_CONTENT_H, GR_C_BG);

  grIcon(grGfx, 136, 120, 48, ICO_GECKO);

  grGfx->setTextSize(3);
  grGfx->setTextColor(GR_C_AMB);
  grGfx->setCursor(30, 180);
  grGfx->print(F("LOGGING DATA"));

  grIcon(grGfx, 26, 216, 24, ICO_DATABASE);
  grGfx->setTextSize(1);
  grGfx->setTextColor(GR_C_DIM);
  grGfx->setCursor(54, 220);
  grGfx->printf("Sorted: %u  Remaining: %u", sorted, queueDepth);

  // progress bar — 280px wide centered
  uint16_t total = sorted + queueDepth;
  if (total > 0) {
    int barW = (int)((float)sorted / total * 280.0f);
    grGfx->fillRect(20, 245, barW, 16, GR_C_GRN);
    grGfx->drawRect(20, 245, 280, 16, GR_C_LINE);

    // bar color sprites on the fill
    // progress bar fill
  }
}

// ---- main draw dispatch (uses gr_screens.h for real content) ---------------
static inline void grDisplayUpdate(bool node0Up, bool node1Up, bool node3Up,
                                    uint16_t sdMb, uint16_t heapFree,
                                    uint16_t queueDepth, bool sorting,
                                    uint8_t radioCount, bool attacking) {
  if (!grScreenDirty) return;
  grScreenDirty = false;

  grDrawStatusBar(node0Up, node1Up, node3Up, sdMb, heapFree);
  grDrawNavBar();

  // sort burst overlay overrides normal screen
  if (sorting) {
    grDrawSortOverlay(queueDepth, grSortItemsSorted);
    return;
  }

  switch (grScreen) {
    case GR_SCREEN_DASHBOARD:
      grScreenDashboard(grGfx, node0Up, node1Up, node3Up, sdMb, heapFree,
                         queueDepth, sorting, radioCount, attacking);
      break;
    case GR_SCREEN_SPECTRUM:
      grScreenSpectrum(grGfx);
      break;
    case GR_SCREEN_NEURAL:
      grScreenNeural(grGfx, grNnState, grNnConfidence, grNnRulesFired,
                      grLastAlertLvl, grLastAlertMsg, grBehaviorMode);
      break;
    case GR_SCREEN_MEMORY:
      grScreenMemory(grGfx, sdMb);
      break;
    case GR_SCREEN_CONFIG:
      grScreenConfig(grGfx, grBehaviorMode, grMuted);
      break;
  }
}
