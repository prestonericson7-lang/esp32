#pragma once
// ############################################################################
//  gg_audio.h  -  GHOST TX CC1101 EDITION  /  single speaker driver
//
//  One speaker on GG_PIN_SPK (GPIO 32) via LEDC PWM.
//  Shimmed for both Arduino-ESP32 core 2.x and 3.x LEDC APIs.
// ############################################################################

#include <Arduino.h>
#include "gg_config.h"

static uint8_t ggVol = 6;

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  #define GG_LEDC3 1
#else
  #define GG_LEDC3 0
#endif

static inline void ggAudioBegin() {
#if GG_LEDC3
  ledcAttach(GG_PIN_SPK, 2000, 8);
  ledcWrite(GG_PIN_SPK, 0);
#else
  ledcSetup(GG_SPK_CH, 2000, 8);
  ledcAttachPin(GG_PIN_SPK, GG_SPK_CH);
  ledcWrite(GG_SPK_CH, 0);
#endif
}

static inline void ggSetVolume(uint8_t v) { ggVol = (v > 10) ? 10 : v; }

static inline void ggToneOn(uint16_t freq) {
  if (ggVol == 0 || freq == 0) return;
  uint8_t duty = (uint8_t)map(ggVol, 0, 10, 0, 128);
#if GG_LEDC3
  ledcChangeFrequency(GG_PIN_SPK, freq, 8);
  ledcWrite(GG_PIN_SPK, duty);
#else
  ledcWriteTone(GG_SPK_CH, freq);
  ledcWrite(GG_SPK_CH, duty);
#endif
}

static inline void ggToneOff() {
#if GG_LEDC3
  ledcWrite(GG_PIN_SPK, 0);
#else
  ledcWrite(GG_SPK_CH, 0);
#endif
}

static inline void ggBeep(uint16_t freq, uint16_t ms) {
  ggToneOn(freq); delay(ms); ggToneOff();
}

static inline void ggClickMove()  { ggBeep(1800, 25); }
static inline void ggClickOk()    { ggBeep(2200, 40); }
static inline void ggClickBack()  { ggBeep( 900, 30); }
static inline void ggClickFire()  { ggBeep(2800, 50); }
static inline void ggChirpUp()    { ggBeep(1800,55); delay(15); ggBeep(2600,75); }
static inline void ggChirpDown()  { ggBeep(1600,55); delay(15); ggBeep(1000,75); }
