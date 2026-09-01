#pragma once
// ############################################################################
//  gs_input.h  -  GHOST TX S3 NEURAL CO-PROCESSOR  /  five buttons
//
//  Five buttons on S3 general-purpose GPIOs (38, 39, 40, 41, 42):
//    UP(0) / DOWN(1) / SELECT(2) / BACK(3) / SWAP(4)
//    ALL forwarded to main ESP via ESP-NOW as GT_CMD_BUTTON
//
//  The ESP32-S3 supports INPUT_PULLUP on ALL GPIOs (unlike classic ESP32's
//  GPIO 34..39 which are input-only with no pull-up). No external resistors
//  needed. Button shorts pin to GND when pressed.
//
//  gpio_reset_pin() is called before pinMode() to detach the pin from any
//  peripheral (JTAG uses GPIO 39-42 by default on some S3 boards).
//  Stuck-button guard: if a pin reads LOW for > 3 seconds continuously,
//  suppress all events until it reads HIGH again.
// ############################################################################

#include <Arduino.h>
#include <driver/gpio.h>
#include "gs_config.h"

// ---- event flags ----------------------------------------------------------
struct GsEvents {
  bool up, down;           // press / auto-repeat
  bool sel, selLong;       // SELECT tap / long-hold
  bool back, home;         // BACK tap / BACK long-hold = HOME
  bool swap;               // SWAP tap (forwarded to main ESP)
};
static GsEvents gsEv;
static bool gsSelHeld = false;

// kind: 0=UP, 1=DOWN, 2=SELECT, 3=BACK, 4=SWAP
struct GsBtn {
  uint8_t  pin;
  uint8_t  kind;
  bool     down;
  uint32_t tEdge;
  uint32_t tRep;
  bool     longFired;
  bool     stuck;          // true = pin has been LOW too long, ignore it
};

static GsBtn gsB[5] = {
  { GS_PIN_UP,     0, false, 0, 0, false, false },
  { GS_PIN_DOWN,   1, false, 0, 0, false, false },
  { GS_PIN_SELECT, 2, false, 0, 0, false, false },
  { GS_PIN_BACK,   3, false, 0, 0, false, false },
  { GS_PIN_SWAP,   4, false, 0, 0, false, false },
};

#define GS_INPUT_BOOT_GRACE_MS  3000
#define GS_INPUT_STUCK_MS       3000

static uint32_t gsInputBootMs = 0;

static inline void gsInputBegin() {
  // Force all button pins into GPIO mode — detach from JTAG or any peripheral
  gpio_reset_pin((gpio_num_t)GS_PIN_UP);
  gpio_reset_pin((gpio_num_t)GS_PIN_DOWN);
  gpio_reset_pin((gpio_num_t)GS_PIN_SELECT);
  gpio_reset_pin((gpio_num_t)GS_PIN_BACK);
  gpio_reset_pin((gpio_num_t)GS_PIN_SWAP);

  pinMode(GS_PIN_UP,     INPUT_PULLUP);
  pinMode(GS_PIN_DOWN,   INPUT_PULLUP);
  pinMode(GS_PIN_SELECT, INPUT_PULLUP);
  pinMode(GS_PIN_BACK,   INPUT_PULLUP);
  pinMode(GS_PIN_SWAP,   INPUT_PULLUP);

  gsInputBootMs = millis();

  // Let pullups settle then check for stuck pins at boot
  delay(50);
  for (int i = 0; i < 5; i++) {
    if (digitalRead(gsB[i].pin) == LOW) {
      gsB[i].stuck = true;
      gsB[i].down  = true;
      gsB[i].tEdge = millis();
      Serial.printf("[input] WARNING: pin %d reads LOW at boot — marked stuck\n", gsB[i].pin);
    }
  }
}

static inline void gsInputPoll() {
  gsEv = GsEvents{};
  gsSelHeld = false;
  const uint32_t now = millis();

  // Boot grace: ignore all input for the first 3 seconds
  if (now - gsInputBootMs < GS_INPUT_BOOT_GRACE_MS) return;

  for (int i = 0; i < 5; i++) {
    GsBtn& b = gsB[i];
    bool raw = (digitalRead(b.pin) == LOW);  // pressed = LOW (pull-up to 3.3V)

    // If pin was stuck but now reads HIGH, it's recovered
    if (b.stuck && !raw) {
      b.stuck = false;
      b.down  = false;
      b.tEdge = now;
      Serial.printf("[input] pin %d recovered from stuck\n", b.pin);
      continue;
    }

    // If stuck, ignore completely
    if (b.stuck) continue;

    // Stuck detection: if held LOW for > 3 seconds, mark stuck
    if (b.down && (now - b.tEdge) > GS_INPUT_STUCK_MS) {
      b.stuck = true;
      Serial.printf("[input] pin %d stuck LOW > 3s — suppressing\n", b.pin);
      continue;
    }

    // Debounce: only register edge if enough time has passed
    if (raw != b.down && (now - b.tEdge) > GS_DEBOUNCE_MS) {
      b.down = raw;
      b.tEdge = now;

      if (raw) {
        // Fresh press
        b.longFired = false;
        b.tRep = now;
        if (b.kind == 0) gsEv.up   = true;   // UP fires on press
        if (b.kind == 1) gsEv.down = true;   // DOWN fires on press
        if (b.kind == 4) gsEv.swap = true;   // SWAP fires on press
      } else if (!b.longFired) {
        // Clean release = tap (for SELECT and BACK)
        if (b.kind == 2) gsEv.sel  = true;   // SELECT tap
        if (b.kind == 3) gsEv.back = true;   // BACK tap
      }
    }

    if (b.down) {
      // UP/DOWN auto-repeat while held
      if ((b.kind == 0 || b.kind == 1) && (now - b.tEdge) > GS_NAV_DELAY_MS
          && (now - b.tRep) >= GS_REPEAT_MS) {
        b.tRep = now;
        if (b.kind == 0) gsEv.up = true;
        else              gsEv.down = true;
      }

      // SELECT held state + long action
      if (b.kind == 2) gsSelHeld = true;
      if (!b.longFired && (now - b.tEdge) >= GS_LONGPRESS_MS) {
        b.longFired = true;
        if (b.kind == 2) gsEv.selLong = true;  // hold SELECT
        if (b.kind == 3) gsEv.home    = true;   // hold BACK = HOME
      }
    }
  }
}
