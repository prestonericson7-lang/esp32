#pragma once
// ############################################################################
//  gg_boot.h  -  KODI RF CONTROLLER  /  power-on animation
//
//  6-phase boot matching artifact 28f46b57, scaled for 128x64 OLED.
//  Phases: WAKE → REVEAL → ZOOM → DEEP ZOOM → EYE VOID → UI SETTLE
// ############################################################################

#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include "gg_config.h"

// procedural gecko for 128x64 OLED
static inline void ggDrawGecko(Adafruit_SSD1306& g, int cx, int cy, int sc, uint16_t col) {
  int bw = 5 * sc, bh = 3 * sc;
  g.fillRoundRect(cx - bw, cy - bh, bw * 2, bh * 2, bh, col);
  int hx = cx + bw - sc, hy = cy - bh / 2;
  g.fillCircle(hx, hy, 2 * sc, col);
  uint16_t bg = (col == SSD1306_WHITE) ? SSD1306_BLACK : SSD1306_WHITE;
  g.fillCircle(hx + sc, hy - sc / 2, sc, bg);
  g.drawPixel(hx + sc, hy - sc / 2, col);
  int tx = cx - bw;
  g.drawLine(tx, cy, tx - 3 * sc, cy + 2 * sc, col);
  g.drawLine(tx - 3 * sc, cy + 2 * sc, tx - 2 * sc, cy + 3 * sc, col);
  g.drawLine(cx + bw / 2, cy + bh, cx + bw / 2 + 2 * sc, cy + bh + 2 * sc, col);
  g.drawLine(cx - bw / 2, cy + bh, cx - bw / 2 - 2 * sc, cy + bh + 2 * sc, col);
  g.drawLine(cx + bw / 2, cy - bh, cx + bw / 2 + 2 * sc, cy - bh - 2 * sc, col);
  g.drawLine(cx - bw / 2, cy - bh, cx - bw / 2 - 2 * sc, cy - bh - 2 * sc, col);
  for (int i = -1; i <= 1; i++) {
    g.drawPixel(cx + bw / 2 + 2 * sc + i, cy + bh + 2 * sc + sc, col);
    g.drawPixel(cx - bw / 2 - 2 * sc + i, cy + bh + 2 * sc + sc, col);
    g.drawPixel(cx + bw / 2 + 2 * sc + i, cy - bh - 2 * sc - sc, col);
    g.drawPixel(cx - bw / 2 - 2 * sc + i, cy - bh - 2 * sc - sc, col);
  }
}

static inline void ggBootAnimate(Adafruit_SSD1306& g,
                                  bool ccA, bool ccB, bool ccC, bool ccD) {
  const int CX = 64, GY = 28;

  // LED startup flash
  digitalWrite(GG_PIN_LED_TX, HIGH); delay(60);
  digitalWrite(GG_PIN_LED_RX, HIGH); delay(60);
  digitalWrite(GG_PIN_LED_STAT, HIGH); delay(60);
  delay(60);
  digitalWrite(GG_PIN_LED_TX, LOW);
  digitalWrite(GG_PIN_LED_RX, LOW);
  digitalWrite(GG_PIN_LED_STAT, LOW);

  // ---- Phase 0: WAKE — cyan spark grows to eye slit ----
  g.clearDisplay(); g.display();
  delay(80);
  ledcWriteTone(GG_SPK_CH, 60); delay(60); ledcWriteTone(GG_SPK_CH, 0);

  for (int r = 1; r <= 4; r++) {
    g.clearDisplay();
    g.fillCircle(CX, 32, r, SSD1306_WHITE);
    g.display();
    delay(35);
  }
  ledcWriteTone(GG_SPK_CH, 2000); delay(10); ledcWriteTone(GG_SPK_CH, 0);

  // dot → horizontal ellipse (eye slit)
  g.clearDisplay();
  g.fillRect(CX - 8, 31, 16, 3, SSD1306_WHITE);
  g.display();
  delay(50);

  // ---- Phase 1: REVEAL — gecko + KODI letters appear ----
  g.clearDisplay();
  ggDrawGecko(g, CX, GY, 1, SSD1306_WHITE);
  g.display();
  delay(150);

  // KODI letters one at a time
  const char* kodi = "KODI";
  const uint16_t hz[] = {800, 900, 1000, 1100};
  g.setTextColor(SSD1306_WHITE);
  g.setTextSize(2);
  for (int i = 0; i < 4; i++) {
    g.setCursor(32 + i * 16, 0);
    g.write(kodi[i]);
    g.display();
    ledcWriteTone(GG_SPK_CH, hz[i]); delay(30); ledcWriteTone(GG_SPK_CH, 0);
    delay(120);
  }

  // chord
  ledcWriteTone(GG_SPK_CH, 523); delay(50);
  ledcWriteTone(GG_SPK_CH, 659); delay(50);
  ledcWriteTone(GG_SPK_CH, 0);
  delay(200);

  // ---- Phase 2+3: ZOOM — converge toward eye, go black ----
  int eyeX = CX + 6, eyeY = GY - 2;
  ledcWriteTone(GG_SPK_CH, 1200); delay(20); ledcWriteTone(GG_SPK_CH, 0);

  // black closes in from edges toward eye
  for (int s = 0; s < 12; s++) {
    float t = (float)s / 11.0f;
    float e = t * t * t;
    int l  = (int)(e * eyeX);
    int r  = 128 - (int)(e * (128 - eyeX));
    int tp = (int)(e * eyeY);
    int bt = 64 - (int)(e * (64 - eyeY));
    if (l > 0)   g.fillRect(0, 0, l, 64, SSD1306_BLACK);
    if (r < 128) g.fillRect(r, 0, 128 - r, 64, SSD1306_BLACK);
    if (tp > 0)  g.fillRect(l, 0, r - l, tp, SSD1306_BLACK);
    if (bt < 64) g.fillRect(l, bt, r - l, 64 - bt, SSD1306_BLACK);
    g.display();
    if (s == 4) { ledcWriteTone(GG_SPK_CH, 600); delay(15); ledcWriteTone(GG_SPK_CH, 0); }
    if (s == 8) { ledcWriteTone(GG_SPK_CH, 300); delay(30); ledcWriteTone(GG_SPK_CH, 0); }
    delay(25);
  }

  // deep zoom rumble + flicker
  g.clearDisplay(); g.display();
  ledcWriteTone(GG_SPK_CH, 60);
  for (int i = 0; i < 3; i++) {
    ledcWriteTone(GG_SPK_CH, 200 - i * 50);
    g.drawPixel(CX, 32, SSD1306_WHITE);
    g.display();
    delay(60);
    g.drawPixel(CX, 32, SSD1306_BLACK);
    g.display();
    delay(40);
  }
  ledcWriteTone(GG_SPK_CH, 0);
  delay(80);

  // ---- Phase 4+5: UI SETTLE — radio status screen ----
  g.clearDisplay();

  // inverted title bar
  g.fillRect(0, 0, 128, 11, SSD1306_WHITE);
  g.setTextColor(SSD1306_BLACK);
  g.setTextSize(1);
  g.setCursor(10, 2);
  g.print(F("KODI RF CONTROLLER"));

  // radio status
  g.setTextColor(SSD1306_WHITE);
  g.setCursor(4, 16);
  g.printf("A:%s  B:%s", ccA ? "OK" : "--", ccB ? "OK" : "--");
  g.setCursor(4, 26);
  g.printf("C:%s  D:%s", ccC ? "OK" : "--", ccD ? "OK" : "--");

  // version
  g.setCursor(4, 38);
  g.print(GG_VERSION);

  // radio count summary
  int n = (ccA ? 1 : 0) + (ccB ? 1 : 0) + (ccC ? 1 : 0) + (ccD ? 1 : 0);
  g.setCursor(4, 50);
  if (n > 0) {
    digitalWrite(GG_PIN_LED_STAT, HIGH);
    g.printf("%d/4 radios online", n);
  } else {
    g.print(F("NO RADIOS FOUND!"));
  }

  g.display();

  // completion fanfare
  ledcWriteTone(GG_SPK_CH, 523); delay(50);
  ledcWriteTone(GG_SPK_CH, 659); delay(50);
  ledcWriteTone(GG_SPK_CH, 784); delay(60);
  ledcWriteTone(GG_SPK_CH, 1047); delay(100);
  ledcWriteTone(GG_SPK_CH, 0);
  delay(400);
}
