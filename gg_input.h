#pragma once
// ############################################################################
//  gg_input.h  -  GHOST TX CC1101 EDITION  /  button input via ESP-NOW
//
//  NO local buttons on the main ESP32-WROOM-32U.
//  GPIO 34/35/36/39 are INPUT-ONLY with NO internal pull-up. With no buttons
//  wired and no external pull-ups, these pins FLOAT and produce random reads.
//  All button input comes from the ESP32-S3 via ESP-NOW (gg_espnow.h).
//
//  The volatile flags (ggEnUp, ggEnDown, ggEnSel, ggEnBack, ggEnSwap) are
//  set by the ESP-NOW receive callback and consumed here.
// ############################################################################

#include <Arduino.h>
#include "gg_config.h"

struct GgEvents {
  bool up, down;
  bool sel, selLong;
  bool back, home;
  bool swap;
};
static GgEvents gEv;
static bool gSelHeld = false;

// GgBtn kept for struct compatibility — no longer read from GPIO
struct GgBtn { uint8_t pin; uint8_t kind; bool down; uint32_t tEdge; uint32_t tRep; bool longFired; };
static GgBtn ggB[4] = {
  { GG_PIN_UP,     0, false, 0, 0, false },
  { GG_PIN_DOWN,   1, false, 0, 0, false },
  { GG_PIN_SELECT, 2, false, 0, 0, false },
  { GG_PIN_BACK,   3, false, 0, 0, false },
};

static inline void ggInputBegin() {
  // NO pin setup — GPIO 34-39 float without external pull-ups.
  // All input comes from S3 via ESP-NOW.
}

static inline void ggInputPoll() {
  gEv = GgEvents{};
  gSelHeld = false;

  // consume volatile flags from ESP-NOW callback (gg_espnow.h)
  if (ggEnUp)   { ggEnUp   = false; gEv.up   = true; }
  if (ggEnDown) { ggEnDown = false; gEv.down = true; }
  if (ggEnSel)  { ggEnSel  = false; gEv.sel  = true; }
  if (ggEnBack) { ggEnBack = false; gEv.back = true; }
  if (ggEnSwap) { ggEnSwap = false; gEv.swap = true; }
}
