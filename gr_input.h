#pragma once
// ############################################################################
//  gr_input.h  -  GHOST TX R40T  /  input handler
//
//  Physical: BOOT button (GPIO 0) always cycles screens.
//  Remote:   ESP-NOW button events from Node 1 (via grEn* volatiles).
//  Same debounce/longpress pattern as gs_input.h (Node 1).
//  Section 14.8 of the master plan.
// ############################################################################

#include <Arduino.h>
#include "gr_config.h"

struct GrEvents {
  bool up;
  bool down;
  bool sel;
  bool back;
  bool swap;
  bool bootBtn;  // local BOOT button press
};

static uint32_t grBootLastEdge = 0;
static bool     grBootLast     = true;  // BOOT is active LOW, pulled up

static inline void grInputBegin() {
  pinMode(GR_PIN_BOOT, INPUT_PULLUP);
}

static inline GrEvents grInputPoll() {
  GrEvents ev = {};
  uint32_t now = millis();

  // ---- local BOOT button (GPIO 0) — always cycles screens ----
  bool raw = digitalRead(GR_PIN_BOOT);
  if (raw != grBootLast && (now - grBootLastEdge >= GR_DEBOUNCE_MS)) {
    grBootLastEdge = now;
    grBootLast = raw;
    if (!raw) {  // pressed (active LOW)
      ev.bootBtn = true;
    }
  }

  // ---- remote ESP-NOW buttons (from volatile flags in gr_espnow.h) ----
  if (grEnUp)   { grEnUp   = false; ev.up   = true; }
  if (grEnDown) { grEnDown = false; ev.down = true; }
  if (grEnSel)  { grEnSel  = false; ev.sel  = true; }
  if (grEnBack) { grEnBack = false; ev.back = true; }
  if (grEnSwap) { grEnSwap = false; ev.swap = true; }

  return ev;
}
