#pragma once
// ############################################################################
//  gh_input.h  -  KODI NRF24  /  4x4 matrix keypad input for Hub
//
//  Replaces the four-button input (gg_input.h) with a 4x4 matrix keypad.
//  Produces the same GgEvents struct so all screen handlers work unchanged.
//
//  KEYPAD MAP:
//    2 = UP   8 = DOWN   4 = LEFT   6 = RIGHT
//    5 = SELECT (tap) / HOLD-SELECT (hold)
//    * = BACK (tap)  / HOME (hold)     # = HOME
//    1 = Category nav (tap) / Mute toggle (hold)
//    3 = Category nav (tap) / Brightness cycle (hold)
//    7,9,0,A,B,C,D = Category navigation shortcuts (home + category screens)
// ############################################################################

#include <Arduino.h>
#include <Keypad.h>
#include "gh_config.h"

struct GgEvents {
  bool up, down, left, right;
  bool sel, selLong;
  bool back, home;
  bool mute, bright;
};
static GgEvents gEv;
static bool gSelHeld = false;
static char gLastKey = '\0';

static const byte GG_KP_ROWS = 4;
static const byte GG_KP_COLS = 4;
static char ggKpKeys[GG_KP_ROWS][GG_KP_COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
static byte ggKpRowPins[GG_KP_ROWS] = {GG_PIN_KP_R0, GG_PIN_KP_R1, GG_PIN_KP_R2, GG_PIN_KP_R3};
static byte ggKpColPins[GG_KP_COLS] = {GG_PIN_KP_C0, GG_PIN_KP_C1, GG_PIN_KP_C2, GG_PIN_KP_C3};
static Keypad ggKeypad = Keypad(makeKeymap(ggKpKeys), ggKpRowPins, ggKpColPins, GG_KP_ROWS, GG_KP_COLS);

static uint32_t ggKpPressMs = 0;
static bool ggKpLongFired = false;
static char ggKpHeld = '\0';

static inline void ggInputBegin() {
  ggKeypad.setDebounceTime(GG_DEBOUNCE_MS);
  ggKeypad.setHoldTime(GG_LONGPRESS_MS);
}

static inline void ggInputPoll() {
  gEv = GgEvents{};
  gSelHeld = false;
  gLastKey = '\0';

  char key = ggKeypad.getKey();
  KeyState state = ggKeypad.getState();

  if (key) {
    gLastKey = key;
    ggKpHeld = key;
    ggKpPressMs = millis();
    ggKpLongFired = false;

    switch (key) {
      case '2': gEv.up    = true; break;
      case '8': gEv.down  = true; break;
      case '4': gEv.left  = true; break;
      case '6': gEv.right = true; break;
      case '5': /* sel on release */ break;
      case '*': /* back on release */ break;
      case '#': gEv.home = true; break;
      case '1': gLastKey = '\0'; break; // defer: hold=mute, tap=category nav
      case '3': gLastKey = '\0'; break; // defer: hold=bright, tap=category nav
      default: break;
    }
  }

  if (state == RELEASED && ggKpHeld != '\0') {
    if (!ggKpLongFired) {
      switch (ggKpHeld) {
        case '5': gEv.sel  = true; break;
        case '*': gEv.back = true; break;
        case '1': gLastKey = '1'; break;
        case '3': gLastKey = '3'; break;
        default: break;
      }
    }
    ggKpHeld = '\0';
  }

  if (state == HOLD && ggKpHeld != '\0' && !ggKpLongFired) {
    ggKpLongFired = true;
    switch (ggKpHeld) {
      case '5': gEv.selLong = true; gSelHeld = true; break;
      case '*': gEv.home    = true; break;
      case '1': gEv.mute   = true; break;
      case '3': gEv.bright = true; break;
      default: break;
    }
  }

  if (ggKpHeld == '5' && state == PRESSED) gSelHeld = true;

  // Auto-repeat for directional keys when held
  if (ggKpHeld == '2' || ggKpHeld == '8' || ggKpHeld == '4' || ggKpHeld == '6') {
    uint32_t held = millis() - ggKpPressMs;
    if (held > GG_NAV_DELAY_MS) {
      static uint32_t lastRep = 0;
      if (millis() - lastRep >= GG_REPEAT_MS) {
        lastRep = millis();
        if (ggKpHeld == '2') gEv.up    = true;
        if (ggKpHeld == '8') gEv.down  = true;
        if (ggKpHeld == '4') gEv.left  = true;
        if (ggKpHeld == '6') gEv.right = true;
      }
    }
  }
}
