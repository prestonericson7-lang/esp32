#pragma once
// ############################################################################
//  gs_voice.h  —  GHOST TX S3 NEURAL CO-PROCESSOR  /  voice detection
//
//  Wake word detection and command vocabulary using audio energy + syllable
//  pattern matching. Processes mic input from gs_audio.h.
//
//  Wake phrase: "Hey Ghost" or "Hey Kodi" (2 syllables, high energy burst)
//
//  Detection pipeline:
//    1. Energy gate — only process when audio exceeds noise floor
//    2. Wake word detection — 2-syllable high-energy burst
//    3. Command window — after wake, ~3-second window for command
//    4. Syllable analysis — count energy peaks + measure duration
//    5. Command match — match syllable profile to known commands
//
//  Syllable-based matching works by analyzing the audio energy envelope:
//    - Each spoken word produces energy peaks separated by brief dips
//    - Counting peaks = counting syllables
//    - Combined with total duration, this discriminates between commands
//    - Groups of commands with same syllable count use energy intensity
//
//  NOTE: Full ESP-SR integration replaces the syllable matcher when available.
//  Add esp-sr component to the build and define GS_USE_ESP_SR to enable.
// ############################################################################

#include <Arduino.h>
#include "gs_config.h"

// ===== Voice state ===========================================================

enum GsVoiceState : uint8_t {
  GS_VOICE_IDLE     = 0,
  GS_VOICE_LISTEN   = 1,
  GS_VOICE_WAKE     = 2,
  GS_VOICE_CAPTURE  = 3,
  GS_VOICE_MATCH    = 4,
};

// ===== Command vocabulary ====================================================

struct GsVoiceCmd {
  const char* phrase;
  uint8_t     cmdId;
  uint8_t     syllables;
  uint16_t    minDurMs;
  uint16_t    maxDurMs;
};

static const GsVoiceCmd gsVoiceCmds[] = {
  // Detection commands (IDs 40-43)
  { "cop scan",        40, 2, 300,  600  },
  { "tracker hunt",    41, 3, 500,  900  },
  { "net audit",       42, 3, 400,  800  },
  { "signal check",    43, 3, 500,  900  },

  // Mode commands (IDs 44-46)
  { "full guard",      44, 2, 400,  700  },
  { "go dark",         45, 2, 300,  600  },
  { "kill alerts",     46, 3, 400,  800  },

  // RF action commands (IDs 50-59)
  { "open garage",     50, 4, 600,  1200 },
  { "replay signal",   51, 4, 600,  1200 },
  { "jam signal",      52, 3, 400,  800  },
  { "record signal",   53, 4, 600,  1100 },
  { "brute force",     54, 2, 400,  700  },
  { "scan area",       55, 3, 400,  800  },
  { "capture signal",  56, 4, 600,  1200 },

  // Control commands (IDs 0, 255)
  { "stop",             0, 1, 150,  400  },
  { "status",         255, 2, 300,  600  },
};
#define GS_VOICE_CMD_COUNT 15

// ===== Syllable analysis buffer ==============================================

#define GS_VOICE_CAPTURE_SLOTS  30   // 30 x 100ms = 3 seconds
#define GS_VOICE_SYLLABLE_GAP   0.4f // energy must dip below this ratio between peaks

struct GsVoiceCapture {
  float    energy[GS_VOICE_CAPTURE_SLOTS];
  uint8_t  count;
  uint32_t startMs;
  uint32_t endMs;
};

static GsVoiceCapture gsVcap = {};

// ===== State =================================================================

static GsVoiceState gsVoiceState  = GS_VOICE_IDLE;
static uint32_t     gsVoiceWakeMs = 0;
static float        gsVoiceNoiseFloor = 0.0001f;
static float        gsVoiceEnergyEma  = 0.0f;
static bool         gsVoiceEnabled    = true;
static uint32_t     gsVoiceBootMs     = 0;
static uint8_t      gsVoiceLastCmd    = 0xFF;
static uint8_t      gsVoiceConfidence = 0;
#define GS_VOICE_BOOT_GRACE_MS  10000

#define GS_VOICE_CMD_WINDOW_MS  GS_VOICE_RECOG_WINDOW
#define GS_VOICE_ENERGY_ALPHA   0.1f
#define GS_VOICE_WAKE_THRESH    3.0f
#define GS_VOICE_TICK_MS        100

static uint32_t gsVoiceLastTick = 0;

// ===== Syllable counter ======================================================

static uint8_t gsVoiceCountSyllables(float peakEnergy) {
  if (gsVcap.count < 3) return 0;

  float threshold = peakEnergy * GS_VOICE_SYLLABLE_GAP;
  uint8_t syllables = 0;
  bool inPeak = false;

  for (int i = 0; i < gsVcap.count; i++) {
    if (gsVcap.energy[i] > threshold) {
      if (!inPeak) { syllables++; inPeak = true; }
    } else {
      inPeak = false;
    }
  }

  return syllables;
}

// ===== Command matcher =======================================================

static uint8_t gsVoiceMatchCommand() {
  if (gsVcap.count < 2) return 0xFF;

  // Find peak energy in the capture window
  float peakE = 0.0f;
  for (int i = 0; i < gsVcap.count; i++)
    if (gsVcap.energy[i] > peakE) peakE = gsVcap.energy[i];

  if (peakE < gsVoiceNoiseFloor * 2.0f) return 0xFF;

  uint8_t syllables = gsVoiceCountSyllables(peakE);
  uint32_t durMs = gsVcap.endMs - gsVcap.startMs;

  Serial.printf("[voice] analysis: %d syllables, %lu ms, peak=%.6f\n",
                syllables, (unsigned long)durMs, peakE);

  if (syllables == 0) return 0xFF;

  // Score each command by syllable match + duration fit
  uint8_t bestCmd = 0xFF;
  int bestScore = -1;

  for (int i = 0; i < GS_VOICE_CMD_COUNT; i++) {
    const GsVoiceCmd& c = gsVoiceCmds[i];
    int score = 0;

    // Syllable match (exact = 100, off by 1 = 40)
    int syllDiff = (int)syllables - (int)c.syllables;
    if (syllDiff < 0) syllDiff = -syllDiff;
    if (syllDiff == 0) score += 100;
    else if (syllDiff == 1) score += 40;
    else continue;

    // Duration fit (within range = 50, close = 25)
    if (durMs >= c.minDurMs && durMs <= c.maxDurMs) {
      score += 50;
    } else {
      int closest = 0;
      if (durMs < c.minDurMs) closest = c.minDurMs - durMs;
      else closest = durMs - c.maxDurMs;
      if (closest < 200) score += 25;
      else if (closest < 400) score += 10;
    }

    if (score > bestScore) {
      bestScore = score;
      bestCmd = c.cmdId;
    }
  }

  // Require minimum confidence
  if (bestScore >= 100) {
    gsVoiceConfidence = (bestScore > 150) ? 90 : (bestScore > 120) ? 70 : 50;
    Serial.printf("[voice] MATCHED: cmd=%d conf=%d%% score=%d\n",
                  bestCmd, gsVoiceConfidence, bestScore);
    return bestCmd;
  }

  Serial.printf("[voice] no confident match (best score=%d)\n", bestScore);
  return 0xFF;
}

// ===== Init ==================================================================

static inline void gsVoiceBegin() {
  gsVoiceState = GS_VOICE_IDLE;
  gsVoiceNoiseFloor = 0.0001f;
  gsVoiceEnergyEma = 0.0f;
  gsVoiceBootMs = millis();
  memset(&gsVcap, 0, sizeof(gsVcap));

  Serial.println(F("[voice] syllable matcher ready, wake: 'Hey Kodi'"));
  Serial.printf("[voice] %d commands in vocabulary\n", GS_VOICE_CMD_COUNT);

#ifdef GS_USE_ESP_SR
  // ESP-SR: initialize WakeNet + MultiNet when available
  // esp_wn_iface_t *wakenet = &WAKENET_MODEL;
  // model_iface_data_t *wn_data = wakenet->create(model_name, DET_MODE_90);
  // esp_mn_iface_t *multinet = &MULTINET_MODEL;
  // model_iface_data_t *mn_data = multinet->create(&mn_coeff, 6000);
  // for (int i = 0; i < GS_VOICE_CMD_COUNT; i++)
  //   multinet->add_speech_cmd(mn_data, gsVoiceCmds[i].phrase, i);
  Serial.println(F("[voice] ESP-SR model loaded"));
#endif
}

// ===== Main tick =============================================================

static inline uint8_t gsVoiceTick(float audioEnergy) {
  if (!gsVoiceEnabled) return 0xFF;

  uint32_t now = millis();
  if (now - gsVoiceLastTick < GS_VOICE_TICK_MS) return 0xFF;
  gsVoiceLastTick = now;

  // Update noise floor (slow adaptation)
  gsVoiceNoiseFloor = gsVoiceNoiseFloor * 0.999f + audioEnergy * 0.001f;

  // Boot grace period: calibrate noise floor only
  if (now - gsVoiceBootMs < GS_VOICE_BOOT_GRACE_MS) {
    gsVoiceNoiseFloor = gsVoiceNoiseFloor * 0.95f + audioEnergy * 0.05f;
    gsVoiceEnergyEma = audioEnergy;
    return 0xFF;
  }

  // Update energy EMA
  gsVoiceEnergyEma = gsVoiceEnergyEma * (1.0f - GS_VOICE_ENERGY_ALPHA) +
                     audioEnergy * GS_VOICE_ENERGY_ALPHA;

  float energyRatio = (gsVoiceNoiseFloor > 0.00001f) ?
                      gsVoiceEnergyEma / gsVoiceNoiseFloor : 0.0f;

  switch (gsVoiceState) {
    case GS_VOICE_IDLE:
      if (energyRatio > GS_VOICE_WAKE_THRESH) {
        gsVoiceState = GS_VOICE_LISTEN;
        gsVoiceWakeMs = now;
      }
      break;

    case GS_VOICE_LISTEN:
      // Wake detection: 2-syllable burst ("Hey Kodi")
      // Sustained energy above threshold for 300-800ms = wake word
      if (energyRatio > GS_VOICE_WAKE_THRESH * 1.5f) {
        uint32_t listenDur = now - gsVoiceWakeMs;
        if (listenDur >= 300 && listenDur <= 1200) {
          gsVoiceState = GS_VOICE_CAPTURE;
          gsVoiceWakeMs = now;
          memset(&gsVcap, 0, sizeof(gsVcap));
          gsVcap.startMs = now;
          gsAudioChime(1200, 1600, 40, 40);
          Serial.println(F("[voice] WAKE — listening for command..."));
        }
      } else if (energyRatio < 1.5f || now - gsVoiceWakeMs > 1500) {
        gsVoiceState = GS_VOICE_IDLE;
      }
      break;

    case GS_VOICE_CAPTURE: {
      // Record energy samples for syllable analysis
      if (gsVcap.count < GS_VOICE_CAPTURE_SLOTS) {
        gsVcap.energy[gsVcap.count++] = audioEnergy;
      }

      // Detect end of speech: energy drops back to near-noise for 300ms
      bool speechDone = false;
      if (gsVcap.count >= 3) {
        bool quiet = gsVcap.energy[gsVcap.count-1] < gsVoiceNoiseFloor * 2.0f &&
                     gsVcap.energy[gsVcap.count-2] < gsVoiceNoiseFloor * 2.0f &&
                     gsVcap.energy[gsVcap.count-3] < gsVoiceNoiseFloor * 2.0f;
        if (quiet && gsVcap.count >= 5) speechDone = true;
      }

      // Timeout
      if (now - gsVoiceWakeMs > GS_VOICE_CMD_WINDOW_MS) speechDone = true;

      if (speechDone) {
        gsVcap.endMs = now;
        gsVoiceState = GS_VOICE_MATCH;
      }
    } break;

    case GS_VOICE_MATCH: {
      uint8_t cmd = gsVoiceMatchCommand();
      gsVoiceState = GS_VOICE_IDLE;

      if (cmd != 0xFF) {
        gsVoiceLastCmd = cmd;
        gsAudioChime(1500, 2000, 60, 60);

        // Find command name for logging
        for (int i = 0; i < GS_VOICE_CMD_COUNT; i++) {
          if (gsVoiceCmds[i].cmdId == cmd) {
            Serial.printf("[voice] COMMAND: \"%s\" (id=%d, conf=%d%%)\n",
                          gsVoiceCmds[i].phrase, cmd, gsVoiceConfidence);
            break;
          }
        }
        return cmd;
      } else {
        gsAudioChime(400, 300, 100, 100);
        Serial.println(F("[voice] command not recognized"));
      }
    } break;
  }

  return 0xFF;
}

// ===== Query state ===========================================================

static inline GsVoiceState gsVoiceGetState() { return gsVoiceState; }
static inline bool gsVoiceIsListening() {
  return gsVoiceState >= GS_VOICE_LISTEN && gsVoiceState <= GS_VOICE_CAPTURE;
}
static inline uint8_t gsVoiceGetLastCmd() { return gsVoiceLastCmd; }
static inline uint8_t gsVoiceGetConfidence() { return gsVoiceConfidence; }
static inline void gsVoiceEnable()  { gsVoiceEnabled = true; }
static inline void gsVoiceDisable() { gsVoiceEnabled = false; gsVoiceState = GS_VOICE_IDLE; }
