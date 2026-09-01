// ############################################################################
//  ghost_tx_s3_nn.ino  —  KODI S3 NEURAL CO-PROCESSOR
//
//  ESP32-S3-DevKitC-1 (no antenna jack, onboard PCB antenna, NeoPixel G48)
//  Pure AI watchdog / neural overseer paired with ESP32-WROOM-32U via ESP-NOW.
//
//  No radios, no OLEDs on this board.
//  LAFVIN audio codec with dual mics + speaker via I2S.
//  Communication is entirely via ESP-NOW to the main ESP.
//
//  What this ESP does:
//    1. Receives telemetry from the main ESP (radio state, attack mode, RSSI)
//    2. Runs a 10-layer neural engine:
//         L0 Telemetry → L1 Signal → L2 Features → L3 Patterns →
//         L4 Anomaly → L5 Rules(120) → L6 Learner → L7 Risk →
//         L8 Decision → L9 SelfMon
//    3. Sends alerts, display text, and audio triggers to the main ESP
//    4. Forwards ALL 5 button presses to the main ESP via ESP-NOW
//    5. Shows system state on the onboard NeoPixel LED
//    6. Audio codec: speaker alerts + dual-mic voice detection
//    7. Voice wake word "Hey Kodi" + command vocabulary
//
//  ---- BUILD ---------------------------------------------------------------
//  Board:   "ESP32-S3 Dev Module"  (esp32 core for S3)
//  Serial:  115200
//  USB CDC: Enabled (for serial output via USB)
//  Libraries (Library Manager):
//     * Adafruit NeoPixel  by Adafruit
//  WiFi / esp_now ship with the core.
//
//  ---- PIN MAP (S3-DevKitC-1) -----------------------------------------------
//  Buttons:     UP=G38  DOWN=G39  SELECT=G40  BACK=G41  SWAP=G42
//  NeoPixel:    G48 (onboard WS2812)
//  Audio I2S:   BCLK=G4  WS=G5  DIN=G6  DOUT=G7  MCLK=G8
//  Audio I2C:   SCL=G14  SDA=G15
//  PA Enable:   G16 (HIGH = speaker amp on)
//  GPIO 0, 19, 20, 26-37, 43-46: RESERVED (boot/USB/flash/serial) — do NOT use
// ############################################################################

#include "gs_config.h"
#include "gs_espnow.h"
#include "gs_input.h"
#include "gs_audio.h"
#include "gs_voice.h"
#include "gs_neural.h"
#include "gs_watchdog.h"

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("  KODI S3 — 10-LAYER NEURAL ENGINE"));
  Serial.println(F("========================================"));
  Serial.println(GS_VERSION);
  Serial.printf("Board type: %d (S3 neural co-processor)\n", GS_BOARD_TYPE);
  Serial.printf("Free heap: %lu bytes\n", (unsigned long)ESP.getFreeHeap());
  randomSeed(micros());

  // 1. Input: 5 buttons with internal pull-ups
  gsInputBegin();
  Serial.println(F("[input] 5 buttons: UP=G38 DOWN=G39 SEL=G40 BACK=G41 SWAP=G42"));

  // 2. ESP-NOW link to main ESP
  bool linkOk = gsLinkBegin();
  if (!linkOk) {
    Serial.println(F("[FATAL] ESP-NOW failed to initialize!"));
  }

  // 3. Audio codec (LAFVIN I2S module)
  bool audioOk = gsAudioBegin();
  if (audioOk) Serial.println(F("[audio] LAFVIN codec ready (BCLK=4 WS=5 DIN=6 DOUT=7 MCLK=8)"));
  else Serial.println(F("[audio] codec init failed (optional)"));

  // 4. Neural engine initialization (all 10 layers)
  gsNeuralInit();

  // 5. Watchdog (NeoPixel, boot LED flash, initial heartbeat)
  bool wdOk = gsWatchdogBoot();
  if (!wdOk) {
    Serial.println(F("[WARN] Watchdog boot check failed"));
  }

  // 6. Voice detection (VAD + wake word framework)
  gsVoiceBegin();

  if (audioOk) gsAudioBootChime();

  Serial.println();
  Serial.printf("[boot] KODI S3-NN ready. Heap: %lu bytes\n",
    (unsigned long)ESP.getFreeHeap());
  Serial.println(F("[boot] Waiting for main ESP heartbeat..."));
  Serial.println();

  // 7. Request learner state from Node 2 (will arrive via ESP-NOW chunks)
  gsLinkRequestLearnerState();
}

void loop() {
  // 1. Poll buttons
  gsInputPoll();

  // 2. Forward button presses based on gsButtonTarget (0=Node 0, 2=R40T)
  {
    const char* dest = (gsButtonTarget == 2) ? "R40T" : "main";
    if (gsEv.up)   { gsLinkSendButton(0); Serial.printf("[input] UP -> %s\n", dest); }
    if (gsEv.down) { gsLinkSendButton(1); Serial.printf("[input] DOWN -> %s\n", dest); }
    if (gsEv.sel)  { gsLinkSendButton(2); Serial.printf("[input] SELECT -> %s\n", dest); }
    if (gsEv.back) { gsLinkSendButton(3); Serial.printf("[input] BACK -> %s\n", dest); }
    if (gsEv.swap) { gsLinkSendButton(4); Serial.printf("[input] SWAP -> %s\n", dest); }
  }

  // 3. Long-press events (local log — not forwarded in current protocol)
  if (gsEv.selLong) Serial.println(F("[input] SELECT LONG"));
  if (gsEv.home)    Serial.println(F("[input] HOME (BACK long)"));

  // 4. Handle incoming commands from main ESP
  uint8_t cmd = gsLinkPopCmd();
  if (cmd != 0xFF) {
    switch (cmd) {
      case GT_CMD_FLOOD:
        Serial.println(F("[link] main ESP: FLOOD"));
        break;
      case GT_CMD_STOP:
        Serial.println(F("[link] main ESP: STOP"));
        break;
      case GT_CMD_SYNC:
        Serial.println(F("[link] main ESP: SYNC"));
        break;
      default:
        Serial.printf("[link] main ESP: cmd %d\n", cmd);
        break;
    }
  }

  // 4b. Handle neural commands from main ESP
  uint8_t ncmd = gsLinkPopNeuralCmd();
  if (ncmd != 0xFF) {
    Serial.printf("[neural] executing command %d\n", ncmd);
    gsAudioChime(1000, 1500, 60, 60);
  }

  // 5. Voice detection tick
  float energy = gsAudioEnergy();
  uint8_t voiceCmd = gsVoiceTick(energy);
  if (voiceCmd != 0xFF) {
    Serial.printf("[voice] command recognized: %d\n", voiceCmd);
    if (voiceCmd > 0 && voiceCmd < 255) {
      gsLinkSendNeuralCmd(voiceCmd);
    }
    gsAudioChime(1500, 2000, 60, 60);
  }

  // 6. Watchdog tick (link pump, heartbeat, timeout, neural report, LED, alert dispatch)
  gsWatchdogTick();

  // 7. Neural engine tick (10-layer pipeline at 4Hz)
  gsNeuralTick();

  // 8. Restore learner state from Node 2 if received
  if (gsLearnerLoaded) {
    gsLearnerLoaded = false;
    if (gsLearnDeserialize(gsLearnerBuf, gsLearnerBufLen))
      Serial.println(F("[learner] prior state restored from R40T SD"));
  }

  // 9. Periodic learner state save to Node 2 (every 5 minutes)
  {
    static uint32_t lastLearnSave = 0;
    if (millis() - lastLearnSave > 300000 && gsLinkR40tUp) {
      lastLearnSave = millis();
      uint8_t buf[sizeof(GsLearnerBlob)];
      size_t n = gsLearnSerialize(buf, sizeof(buf));
      if (n > 0) gsLinkSendLearnerState(buf, n);
    }
  }

  // 10. Yield
  delay(4);
}
