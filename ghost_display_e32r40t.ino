// ############################################################################
//  ghost_display_e32r40t.ino  -  KODI  /  Deep Archivist
//
//  Node 2 of the KODI mesh.
//  ESP32-R40T (classic ESP32) with 4.0" ST7796 display, 128GB SD, speaker.
//
//  BUILD:
//    Board:  "ESP32 Dev Module"
//    FQBN:   esp32:esp32:esp32:PartitionScheme=huge_app,UploadSpeed=115200
//    Serial: 115200
//    Libs:   Arduino_GFX_Library (Library Manager)
// ############################################################################

#include "gr_config.h"
#include "gr_espnow.h"
#include "gr_audio.h"
#include "gr_sd.h"
#include "gr_rf_tx.h"
#include "gr_assets.h"
#include "gr_icons.h"
#include "gr_sort.h"
#include "gr_brain.h"
#include "gr_screens.h"
#include "gr_display.h"
#include "gr_input.h"
#include "gr_boot.h"

static uint32_t grLastStatusTx = 0;
#define GR_STATUS_INTERVAL 3000

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("  KODI — Deep Archivist"));
  Serial.println(F("  " GR_VERSION));
  Serial.println(F("========================================"));

  // 1. Audio (need speaker for boot animation)
  grAudioBegin();

  // 2. Display
  if (!grDisplayBegin()) {
    Serial.println(F("[FATAL] display init failed"));
  }

  // 3. SD card
  grSdBegin();

  // 4. ESP-NOW mesh
  if (!grLinkBegin()) {
    Serial.println(F("[FATAL] ESP-NOW init failed"));
  }

  // 5. Deep brain
  grBrainBegin();

  // 6. Input
  grInputBegin();

  // 7. Boot animation (uses display + speaker + SD status)
  grBootAnimate(grGfx);

  // 8. Show dashboard
  grScreenDirty = true;
  grDisplayUpdate(grPeer0.up, grPeer1.up, grPeer3.up, grSdFreeMb,
                  (uint16_t)(ESP.getFreeHeap() / 1024),
                  grSortQueueDepth, grSortActive,
                  grPeer0.radioCount, grPeer0.attacking);

  Serial.println(F("[setup] KODI ready"));
}

void loop() {
  uint32_t now = millis();

  // ---- ESP-NOW pump (heartbeat, timeout, peer management) ----
  grLinkPump();

  // ---- process incoming capture data from Node 0 ----
  if (grNewCapture) {
    grNewCapture = false;
    grSortQueueCapture(grCapFreqMHz, grCapRadioId, grCapMod,
                       (const uint8_t*)grCapData, grCapDataLen);

    // run deep brain 176-stage analysis on this capture
    GrCaptureRecord brainRec = {};
    brainRec.timestamp  = millis();
    brainRec.freqMHz    = grCapFreqMHz;
    brainRec.radioId    = grCapRadioId;
    brainRec.modulation = grCapMod;
    brainRec.dataLen    = grCapDataLen;
    memcpy(brainRec.data, (const uint8_t*)grCapData, grCapDataLen);
    brainRec.deviceType = grClassify(brainRec);
    brainRec.classified = 1;
    grBrainAnalyze(brainRec);

    // update neural state for display
    grNnState      = 2; // MONITORING
    grNnConfidence = grBrain.confidence;
    grNnRulesFired = grBrain.rulesFired;

    // feed capture ticker for Dashboard live activity
    grCapTickerPush(brainRec.deviceType, grCapFreqMHz);

    // capture rate EMA (captures per minute)
    static uint32_t grLastCapMs = 0;
    if (grLastCapMs > 0 && now > grLastCapMs) {
      float intervalMin = (float)(now - grLastCapMs) / 60000.0f;
      if (intervalMin > 0.001f) {
        float instantRate = 1.0f / intervalMin;
        grCaptureRate = grCaptureRate * 0.8f + instantRate * 0.2f;
      }
    }
    grLastCapMs = now;

    // SD capture log — append CSV line
    {
      char logLine[128];
      uint32_t upSec = now / 1000;
      snprintf(logLine, sizeof(logLine), "%lu,%.2f,%d,%d,%d,%d,%d,%lu",
               (unsigned long)upSec, grCapFreqMHz, grCapRadioId, grCapMod,
               grCapDataLen, brainRec.deviceType, grBrain.confidence,
               (unsigned long)grBrain.processingTimeUs);
      grSdAppendLine("/logs/captures/capture_log.csv", logLine);
    }

    Serial.printf("[capture] queued %.2fMHz radio=%d mod=%d len=%d  depth=%u  brain:%uus rules:%d conf:%d%%\n",
                  grCapFreqMHz, grCapRadioId, grCapMod, grCapDataLen,
                  grSortQueueDepth, (unsigned)grBrain.processingTimeUs,
                  grBrain.rulesFired, grBrain.confidence);
  }

  // ---- sort burst logic ----
  if (grSortShouldTrigger()) {
    if (!grSortActive) {
      grSortActive = true;
      grSortItemsSorted = 0;
      grScreenDirty = true;
      Serial.println(F("[sort] burst triggered"));
    }
    uint16_t processed = grSortBurstTick();
    if (processed > 0) grScreenDirty = true;
    if (!grSortActive && grSortItemsSorted > 0) {
      grTotalSorted += grSortItemsSorted;
      grSortDoneChime();
      Serial.printf("[sort] done, %u items classified\n", grSortItemsSorted);
      grScreenDirty = true;
    }
  }

  // ---- input ----
  GrEvents ev = grInputPoll();

  if (ev.bootBtn || ev.down) {
    grScreenNext();
    grClickMove();
  }
  if (ev.up) {
    grScreenPrev();
    grClickMove();
  }
  if (ev.sel) {
    grClickOk();
    grScreenDirty = true;
  }
  if (ev.back) {
    grClickBack();
    grScreenSet(GR_SCREEN_DASHBOARD);
  }

  // ---- alert handler ----
  if (grNewAlert) {
    grNewAlert = false;
    grAlertLogPush(grLastAlertLvl, grLastAlertMsg);
    grAlertChime();
    grScreenDirty = true;
    Serial.printf("[alert] lvl=%d msg=%s\n", grLastAlertLvl, grLastAlertMsg);
  }

  // ---- periodic display refresh (every 1s for live data) ----
  static uint32_t lastRefresh = 0;
  if (now - lastRefresh >= 1000) {
    lastRefresh = now;
    grScreenDirty = true;
  }

  // ---- display update ----
  grDisplayUpdate(grPeer0.up, grPeer1.up, grPeer3.up, grSdFreeMb,
                  (uint16_t)(ESP.getFreeHeap() / 1024),
                  grSortQueueDepth, grSortActive,
                  grPeer0.radioCount, grPeer0.attacking);

  // ---- periodic status broadcast to mesh ----
  if (now - grLastStatusTx >= GR_STATUS_INTERVAL) {
    grLastStatusTx = now;
    grSdUpdateFree();
    grLinkSendR40tStatus(grScreen, grSdFreeMb,
                          (uint16_t)(ESP.getFreeHeap() / 1024), grMuted);
  }

  // ---- SD health log (every 60s) ----
  static uint32_t grLastHealthLog = 0;
  if (now - grLastHealthLog >= 60000) {
    grLastHealthLog = now;
    char hlLine[160];
    uint32_t upSec = now / 1000;
    snprintf(hlLine, sizeof(hlLine),
             "%lu,%u,%u,%d,%d,%d,%d,%d,%u,%.1f",
             (unsigned long)upSec,
             (unsigned)(ESP.getFreeHeap() / 1024), grSdFreeMb,
             grBbMeshHealth, grNnState,
             grPeer0.up ? 1 : 0, grPeer1.up ? 1 : 0, grPeer3.up ? 1 : 0,
             grSortQueueDepth, grCaptureRate);
    grSdAppendLine("/logs/system/health.csv", hlLine);
  }

  // ---- SD backbone data archive (every 30s) ----
  static uint32_t grLastBbLog = 0;
  if (grPeer3.up && now - grLastBbLog >= 30000) {
    grLastBbLog = now;
    uint32_t upSec = now / 1000;
    char bbLine[160];

    snprintf(bbLine, sizeof(bbLine),
             "%lu,%d,%d,%d,%d,%d,%lu,%d",
             (unsigned long)upSec, grBbApCount, grBbOpenNets,
             grBbHiddenNets, grBbStrongestRssi, grBbBusiestCh,
             (unsigned long)grBbEnvFingerprint, grBbEnvChanged ? 1 : 0);
    grSdAppendLine("/logs/backbone/wifi_env.csv", bbLine);

    snprintf(bbLine, sizeof(bbLine),
             "%lu,%d,%d,%d,%d,%d,%d",
             (unsigned long)upSec, grBbDeauthPerMin,
             grBbDisassocPerMin, grBbAttackType,
             grBbAttackSeverity, grBbAttackChannel,
             grBbStormActive ? 1 : 0);
    grSdAppendLine("/logs/backbone/deauth_events.csv", bbLine);

    snprintf(bbLine, sizeof(bbLine),
             "%lu,%d,%d,%d,%d,%d,%d",
             (unsigned long)upSec, grBbTotalUniqueBle, grBbActiveNow,
             grBbNewRate, grBbPhones, grBbTrackersActive, grBbAvgDwell);
    grSdAppendLine("/logs/backbone/ble_history.csv", bbLine);

    snprintf(bbLine, sizeof(bbLine),
             "%lu,%d,%d,%d,%d,%d,%d",
             (unsigned long)upSec, grBbZoneFar, grBbZoneNear,
             grBbZoneClose, grBbZoneImmediate,
             grBbClosestRssi, grBbApproaching);
    grSdAppendLine("/logs/backbone/proximity.csv", bbLine);

    snprintf(bbLine, sizeof(bbLine),
             "%lu,%d,%d,%d,%d",
             (unsigned long)upSec, grBbBeaconCount, grBbIBeacons,
             grBbEddystones, grBbNearBeaconRssi);
    grSdAppendLine("/logs/backbone/beacons.csv", bbLine);

    snprintf(bbLine, sizeof(bbLine),
             "%lu,%d,%d,%d",
             (unsigned long)upSec, grBbWifiClients,
             grBbActiveClients, grBbProbeCount);
    grSdAppendLine("/logs/backbone/wifi_clients.csv", bbLine);
  }

  // ---- learner state persistence (S3 ↔ SD) ----
  if (grLearnerSavePending) {
    grLearnerSavePending = false;
    grBrainSaveLearnerState(grLearnerBuf, grLearnerBufLen);
  }
  if (grLearnerLoadRequested) {
    grLearnerLoadRequested = false;
    uint8_t loadBuf[2048];
    size_t n = grBrainLoadLearnerState(loadBuf, sizeof(loadBuf));
    if (n > 0) grLinkSendLearnerState(loadBuf, n);
    else Serial.println(F("[learner] no saved state on SD"));
  }

  delay(4);
}
