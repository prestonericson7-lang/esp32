#pragma once
// ############################################################################
//  gh_boot.h  -  KODI NRF24  /  Dual-OLED boot animation
//
//  UI OLED:     signal-lock sweep, name reveal
//  Status OLED: system checklist
// ############################################################################

#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include "gh_config.h"

static inline void ggBootAnimate(Adafruit_SSD1306& ui, Adafruit_SSD1306& st,
                                  bool stOk) {
  // ---- STATUS OLED: boot header ----
  if (stOk) {
    st.clearDisplay();
    st.setTextColor(SSD1306_WHITE);
    st.setTextSize(1);
    st.setCursor(16, 2);
    st.print(F("INITIALIZING"));
    st.drawFastHLine(0, 12, 128, SSD1306_WHITE);
    st.display();
  }

  // ---- UI OLED phase 1: noise field (8 frames, density fades) ----
  for (int f = 0; f < 8; f++) {
    ui.clearDisplay();
    int dots = 160 - f * 18;
    for (int p = 0; p < dots; p++)
      ui.drawPixel(random(0, 128), random(0, 64), SSD1306_WHITE);
    ui.display();
    delay(35);
  }

  // STATUS: CPU check
  if (stOk) {
    st.setCursor(6, 15);
    st.print(F("[OK] CPU"));
    st.display();
  }

  // ---- UI phase 2: blank pause, then center line grows outward ----
  ui.clearDisplay();
  ui.display();
  delay(80);

  for (int w = 1; w <= 64; w += 3) {
    ui.clearDisplay();
    ui.drawFastHLine(64 - w, 32, w * 2, SSD1306_WHITE);
    ui.display();
    delay(12);
  }

  // STATUS: OLED check
  if (stOk) {
    st.setCursor(6, 25);
    st.print(F("[OK] DISPLAY"));
    st.display();
  }

  // ---- UI phase 3: line splits into two, moving apart ----
  for (int s = 0; s <= 12; s++) {
    ui.clearDisplay();
    ui.drawFastHLine(0, 32 - s, 128, SSD1306_WHITE);
    ui.drawFastHLine(0, 32 + s, 128, SSD1306_WHITE);
    ui.display();
    delay(18);
  }
  // lines settle at y=20 and y=44

  // STATUS: input check
  if (stOk) {
    st.setCursor(6, 35);
    st.print(F("[OK] INPUT"));
    st.display();
  }

  // ---- UI phase 4: name appears between lines ----
  ui.clearDisplay();
  ui.drawFastHLine(0, 20, 128, SSD1306_WHITE);
  ui.drawFastHLine(0, 44, 128, SSD1306_WHITE);
  ui.setTextColor(SSD1306_WHITE);
  ui.setTextSize(2);
  int nw = 4 * 12;   // "KODI" = 4 chars at size 2 (12px each)
  ui.setCursor((128 - nw) / 2, 24);
  ui.print(F("KODI"));
  ui.setTextSize(1);
  int nw2 = 13 * 6;   // "NRF24 Edition" = 13 chars at size 1 (6px each)
  ui.setCursor((128 - nw2) / 2, 38);
  ui.print(F("NRF24 Edition"));
  ui.display();
  delay(300);

  // STATUS: radio check + ready
  if (stOk) {
    st.setCursor(6, 45);
    st.print(F("[OK] ESP-NOW"));
    st.drawFastHLine(0, 54, 128, SSD1306_WHITE);
    st.setCursor(42, 56);
    st.print(F("ONLINE"));
    st.display();
  }

  // ---- UI phase 5: version appears below bottom line ----
  ui.setTextSize(1);
  int vw = strlen(GG_VERSION) * 6;
  ui.setCursor((128 - vw) / 2, 48);
  ui.print(GG_VERSION);
  ui.display();

  delay(500);
}
