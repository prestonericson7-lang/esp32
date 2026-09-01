#pragma once
// ############################################################################
//  gr_audio.h  -  GHOST TX R40T  /  speaker driver
//
//  Speaker on GR_PIN_SPK (GPIO 26, DAC2) via LEDC PWM.
//  Amplifier enable on GR_PIN_AMP_EN (GPIO 4), active LOW.
//  Shimmed for Arduino-ESP32 core 2.x and 3.x LEDC APIs.
//  Same pattern as gg_audio.h (Node 0).
// ############################################################################

#include <Arduino.h>
#include "gr_config.h"

static uint8_t grVol = 6;
static bool    grMuted = false;

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  #define GR_LEDC3 1
#else
  #define GR_LEDC3 0
#endif

static inline void grAmpOn()  { digitalWrite(GR_PIN_AMP_EN, LOW);  }
static inline void grAmpOff() { digitalWrite(GR_PIN_AMP_EN, HIGH); }

static inline void grAudioBegin() {
  pinMode(GR_PIN_AMP_EN, OUTPUT);
  grAmpOff();

#if GR_LEDC3
  ledcAttach(GR_PIN_SPK, 2000, 8);
  ledcWrite(GR_PIN_SPK, 0);
#else
  ledcSetup(GR_SPK_CH, 2000, 8);
  ledcAttachPin(GR_PIN_SPK, GR_SPK_CH);
  ledcWrite(GR_SPK_CH, 0);
#endif

  Serial.println(F("[audio] speaker ready, GPIO 26 DAC2 + amp GPIO 4"));
}

static inline void grSetVolume(uint8_t v) { grVol = (v > 10) ? 10 : v; }
static inline void grSetMuted(bool m)     { grMuted = m; if (m) grAmpOff(); }

static inline void grToneOn(uint16_t freq) {
  if (grMuted || grVol == 0 || freq == 0) return;
  grAmpOn();
  uint8_t duty = (uint8_t)map(grVol, 0, 10, 0, 128);
#if GR_LEDC3
  ledcChangeFrequency(GR_PIN_SPK, freq, 8);
  ledcWrite(GR_PIN_SPK, duty);
#else
  ledcWriteTone(GR_SPK_CH, freq);
  ledcWrite(GR_SPK_CH, duty);
#endif
}

static inline void grToneOff() {
#if GR_LEDC3
  ledcWrite(GR_PIN_SPK, 0);
#else
  ledcWrite(GR_SPK_CH, 0);
#endif
  grAmpOff();
}

static inline void grBeep(uint16_t freq, uint16_t ms) {
  grToneOn(freq); delay(ms); grToneOff();
}

static inline void grClickMove()  { grBeep(1800, 25); }
static inline void grClickOk()    { grBeep(2200, 40); }
static inline void grClickBack()  { grBeep( 900, 30); }
static inline void grClickFire()  { grBeep(2800, 50); }
static inline void grChirpUp()    { grBeep(1800,55); delay(15); grBeep(2600,75); }
static inline void grChirpDown()  { grBeep(1600,55); delay(15); grBeep(1000,75); }

static inline void grBootChime() {
  grBeep(523, 80); delay(30);
  grBeep(659, 80); delay(30);
  grBeep(784, 120);
}

static inline void grAlertChime() {
  grBeep(2400, 60); delay(20);
  grBeep(2400, 60); delay(20);
  grBeep(2400, 100);
}

static inline void grSortDoneChime() {
  grBeep(784, 60); delay(15);
  grBeep(988, 60); delay(15);
  grBeep(1175, 100);
}
