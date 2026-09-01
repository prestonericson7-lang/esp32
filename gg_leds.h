#pragma once
// ############################################################################
//  gg_leds.h  -  GHOST TX CC1101 EDITION  /  3-LED status driver
//
//  TX  (RED   GPIO 33) - pulse on radio transmit
//  RX  (GREEN GPIO 26) - pulse on data received
//  STAT(BLUE  GPIO 15) - solid when radios ready, slow blink when busy
//
//  No 4th ALERT LED — alert functions are no-ops for UI compatibility.
// ############################################################################

#include <Arduino.h>
#include "gg_config.h"

static uint32_t ggLedTxOff   = 0;
static uint32_t ggLedRxOff   = 0;

#define GG_LED_PULSE_MS  50

static inline void ggLedBegin() {
  pinMode(GG_PIN_LED_TX,   OUTPUT);  digitalWrite(GG_PIN_LED_TX,   LOW);
  pinMode(GG_PIN_LED_RX,   OUTPUT);  digitalWrite(GG_PIN_LED_RX,   LOW);
  pinMode(GG_PIN_LED_STAT, OUTPUT);  digitalWrite(GG_PIN_LED_STAT, LOW);
}

static inline void ggLedTx() {
  digitalWrite(GG_PIN_LED_TX, HIGH);
  ggLedTxOff = millis() + GG_LED_PULSE_MS;
}
static inline void ggLedRx() {
  digitalWrite(GG_PIN_LED_RX, HIGH);
  ggLedRxOff = millis() + GG_LED_PULSE_MS;
}

static inline void ggLedStatOn()  { digitalWrite(GG_PIN_LED_STAT, HIGH); }
static inline void ggLedStatOff() { digitalWrite(GG_PIN_LED_STAT, LOW);  }

// no ALERT LED on this build — keep API for UI compatibility
static inline void ggLedAlertOn()  {}
static inline void ggLedAlertOff() {}

static inline void ggLedUpdate() {
  uint32_t now = millis();
  if (ggLedTxOff && now >= ggLedTxOff) {
    digitalWrite(GG_PIN_LED_TX, LOW);
    ggLedTxOff = 0;
  }
  if (ggLedRxOff && now >= ggLedRxOff) {
    digitalWrite(GG_PIN_LED_RX, LOW);
    ggLedRxOff = 0;
  }
}
