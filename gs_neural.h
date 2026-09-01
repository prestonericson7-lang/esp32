#pragma once
// ############################################################################
//  gs_neural.h  —  10-LAYER NEURAL ENGINE ORCHESTRATOR
//
//  This is the MASTER controller that wires all 10 layers together into
//  a single processing pipeline. Each tick (250ms / 4Hz):
//
//    L0  gsTelemIngest()      — ingest telemetry, update metric streams
//    L1  gsSigProcessAll()    — signal processing on all metrics
//    L2  gsFeatExtractAll()   — extract 76-feature vector
//    L3  gsPatProcessAll()    — pattern recognition
//    L4  gsAnomProcessAll()   — anomaly detection
//    L5  gsRulesEvalAll()     — 120-rule engine evaluation
//    L6  gsLearnProcessAll()  — behavior learning
//    L7  gsRiskProcessAll()   — risk assessment
//    L8  gsDecProcessAll()    — decision engine
//    L9  gsSelfMonProcessAll()— self-monitoring
//
//  State machine:
//    BOOT       → IDLE (after init)
//    IDLE       → MONITORING (first telemetry received)
//    MONITORING → ALERT (critical rule triggered)
//    MONITORING → ANALYZING (anomaly detected)
//    MONITORING → LEARNING (stable operation)
//    ALERT      → MONITORING (alert resolved)
//    ALERT      → ADVISING (recommendation generated)
//    ANALYZING  → MONITORING (analysis complete)
//    ADVISING   → MONITORING (advice delivered)
//    LEARNING   → MONITORING (learning cycle complete)
//    ANY        → ERROR (engine health critical)
//    ERROR      → BOOT (recovery attempt)
//
//  The orchestrator also manages:
//  - ESP-NOW alert dispatch to the main ESP
//  - Serial debug output
//  - NeoPixel status indication
//  - Periodic diagnostic dumps
// ############################################################################

#include <Arduino.h>
#include "gs_config.h"
#include "gs_telemetry.h"
#include "gs_signal.h"
#include "gs_features.h"
#include "gs_patterns.h"
#include "gs_anomaly.h"
#include "gs_rules.h"
#include "gs_learner.h"
#include "gs_risk.h"
#include "gs_decision.h"
#include "gs_selfmon.h"

// #############################################################################
//  ENGINE STATE
// #############################################################################
static uint8_t  gNeuralState      = GS_NN_BOOT;
static uint8_t  gNeuralPrevState  = GS_NN_BOOT;
static uint32_t gNeuralStateMs    = 0;
static uint32_t gNeuralLastTickMs = 0;
static uint32_t gNeuralTickCount  = 0;
static bool     gNeuralReady      = false;

// #############################################################################
//  STATE TRANSITION
// #############################################################################
static void gsNeuralSetState(uint8_t newState) {
  if (newState == gNeuralState) return;
  if (newState >= GS_NN_STATE_COUNT) return;

  gNeuralPrevState = gNeuralState;
  gNeuralState = newState;
  gNeuralStateMs = millis();

  Serial.printf("[NN] State: %s -> %s\n",
    GS_NN_STATE_NAMES[gNeuralPrevState],
    GS_NN_STATE_NAMES[newState]);
}

// #############################################################################
//  INITIALIZATION — boot all 10 layers
// #############################################################################
static void gsNeuralInit() {
  Serial.println(F("========================================"));
  Serial.println(F("  KODI S3 — NEURAL ENGINE v2.0"));
  Serial.println(F("  10-Layer Architecture"));
  Serial.println(F("========================================"));
  Serial.printf("Build: %s %s\n", GS_BUILD_DATE, GS_BUILD_TIME);
  Serial.printf("Free heap: %lu bytes\n", (unsigned long)ESP.getFreeHeap());

  uint32_t t0 = micros();

  gsTelemInit();
  gsSigInit();
  gsFeatInit();
  gsPatInit();
  gsAnomInit();
  gsRulesInit();
  gsLearnInit();
  gsRiskInit();
  gsDecInit();
  gsSelfMonInit();

  uint32_t initTime = micros() - t0;
  Serial.printf("All layers initialized in %lu us\n", initTime);
  Serial.printf("Free heap after init: %lu bytes\n", (unsigned long)ESP.getFreeHeap());

  gNeuralState = GS_NN_IDLE;
  gNeuralStateMs = millis();
  gNeuralLastTickMs = millis();
  gNeuralReady = true;

  Serial.println(F("[NN] Engine ready. Waiting for telemetry..."));
}

// #############################################################################
//  STATE MACHINE LOGIC
// #############################################################################
static void gsNeuralUpdateState() {
  uint32_t now = millis();
  uint32_t inState = now - gNeuralStateMs;

  switch (gNeuralState) {

    case GS_NN_IDLE:
      if (gTelemValid && gTelemCount >= 3)
        gsNeuralSetState(GS_NN_MONITORING);
      break;

    case GS_NN_MONITORING: {
      // Check for critical alerts
      uint8_t highest = gsRulesHighestAlert();
      if (highest >= GS_ALERT_CRITICAL) {
        gsNeuralSetState(GS_NN_ALERT);
        break;
      }

      // Check for anomalies
      if (gsAnomIsActive() && gsAnomGlobal() > 0.5f) {
        gsNeuralSetState(GS_NN_ANALYZING);
        break;
      }

      // Check for stable learning opportunity
      if (inState > 30000 && !gsAnomIsActive() &&
          highest <= GS_ALERT_INFO &&
          gFeatVec.raw.attacking > 0.5f) {
        gsNeuralSetState(GS_NN_LEARNING);
        break;
      }

      // Check for recommendations to deliver
      if (gsDecHasUrgent()) {
        gsNeuralSetState(GS_NN_ADVISING);
        break;
      }
      break;
    }

    case GS_NN_ALERT:
      // Stay in alert until conditions improve
      if (gsRulesHighestAlert() < GS_ALERT_CRITICAL && inState > 5000) {
        if (gsDecHasUrgent())
          gsNeuralSetState(GS_NN_ADVISING);
        else
          gsNeuralSetState(GS_NN_MONITORING);
      }
      break;

    case GS_NN_ANALYZING:
      // Analyzing anomaly — return to monitoring after analysis period
      if (inState > 10000 || !gsAnomIsActive())
        gsNeuralSetState(GS_NN_MONITORING);
      break;

    case GS_NN_LEARNING:
      // Learning mode — return to monitoring after learning period
      if (inState > 15000 || gsRulesHighestAlert() >= GS_ALERT_WARNING)
        gsNeuralSetState(GS_NN_MONITORING);
      break;

    case GS_NN_ADVISING:
      // Advice delivered — return to monitoring
      if (inState > 5000)
        gsNeuralSetState(GS_NN_MONITORING);
      break;

    case GS_NN_ERROR:
      // Error recovery — attempt restart after 10 seconds
      if (inState > 10000) {
        Serial.println(F("[NN] Attempting recovery..."));
        gsNeuralSetState(GS_NN_IDLE);
      }
      break;

    default:
      gsNeuralSetState(GS_NN_IDLE);
      break;
  }

  // Global error check
  if (gNeuralState != GS_NN_ERROR && gNeuralState != GS_NN_BOOT) {
    if (!gsSelfMonHealthy() && gsSelfMonTicks() > 100) {
      Serial.println(F("[NN] ENGINE HEALTH CRITICAL"));
      gsNeuralSetState(GS_NN_ERROR);
    }
  }
}

// #############################################################################
//  SERIAL STATUS OUTPUT (compact, for monitoring)
// #############################################################################
static void gsNeuralPrintStatus() {
  static uint32_t lastPrintMs = 0;
  uint32_t now = millis();
  if (now - lastPrintMs < 2000) return;
  lastPrintMs = now;

  uint8_t mode = (uint8_t)gFeatVec.raw.attackMode;
  const char* modeName = (mode < GS_ATK_COUNT) ? GS_ATK_NAMES[mode] : "???";

  Serial.printf("[NN|%s] M:%s R:%.0f%% T:%.0f%% D:%.0f%% H:%.0f%% "
                "E:%.0f%% A:%u/%u Rules:%u/%u\n",
    GS_NN_STATE_NAMES[gNeuralState],
    modeName,
    gsRiskComposite() * 100.0f,
    gsRiskThermal() * 100.0f,
    gsRiskDetection() * 100.0f,
    gsRiskHardware() * 100.0f,
    gFeatVec.derived.effectivenessScore * 100.0f,
    gsAnomActiveCount(), (uint8_t)GS_ANOMALY_METRICS,
    gsRulesActiveCount(), 120);

  // Print top recommendation if any
  if (gsDecCount() > 0) {
    const GsRecommendation& top = gsDecAt(0);
    Serial.printf("  >> [P%u C%u%%] %s: %s\n",
      top.priority, top.confidence,
      GS_REC_NAMES[top.action], top.reason);
  }

  // Print top alert if any
  GsAlert* alert = gsRuleNextUnsent();
  if (alert) {
    Serial.printf("  !! [%s] %s\n",
      GS_ALERT_LEVEL_NAMES[alert->level], alert->msg);
  }
}

// #############################################################################
//  DIAGNOSTIC DUMP (every 60 seconds)
// #############################################################################
static void gsNeuralDiagDump() {
  static uint32_t lastDiagMs = 0;
  uint32_t now = millis();
  if (now - lastDiagMs < 60000) return;
  lastDiagMs = now;

  gsSelfMonPrintDiag();

  // Print user profile summary
  Serial.println(F("--- User Profile ---"));
  Serial.printf("  Aggression: %.2f  Skill: %.2f  RiskTol: %.2f\n",
    gsLearnAggression(), gsLearnSkill(), gsLearnRiskTolerance());
  Serial.printf("  Follow rate: %.0f%%  Exploration: %.2f\n",
    gsLearnFollowRate() * 100.0f, gUserProfile.modeExploration);

  // Print risk trajectory
  Serial.println(F("--- Risk Trajectory ---"));
  for (int i = 0; i < GS_RISK_PREDICT_STEPS; i++) {
    const GsRiskPoint& rp = gsRiskTrajectoryAt(i);
    Serial.printf("  +%.0fs: T:%.0f%% D:%.0f%% H:%.0f%% C:%.0f%%\n",
      rp.timeSec, rp.thermal * 100, rp.detection * 100,
      rp.hardware * 100, rp.composite * 100);
  }

  // Print mode ranking
  Serial.println(F("--- Mode Ranking (top 5) ---"));
  for (int i = 0; i < min(5, (int)gModeRankCount); i++) {
    uint8_t m = gsLearnRankedMode(i);
    Serial.printf("  #%d %s score:%.2f roi:%.2f\n",
      i + 1, (m < GS_ATK_COUNT) ? GS_ATK_NAMES[m] : "?",
      gsLearnRankedScore(i), gsLearnModeROI(m));
  }
}

// #############################################################################
//  MASTER TICK — the heartbeat of the neural engine
//
//  Called from the main loop at GS_NEURAL_TICK_MS intervals (250ms / 4Hz).
//  Runs all 10 layers in sequence with performance measurement.
// #############################################################################
static void gsNeuralTick() {
  if (!gNeuralReady) return;

  uint32_t now = millis();
  uint16_t tickMs = GS_NEURAL_TICK_MS;
  if (gNeuralState == GS_NN_ALERT || gNeuralState == GS_NN_ANALYZING)
    tickMs = GS_NEURAL_TICK_MS / 2;
  else if (gNeuralState == GS_NN_IDLE)
    tickMs = GS_NEURAL_TICK_MS * 4;
  if (now - gNeuralLastTickMs < tickMs) return;
  gNeuralLastTickMs = now;
  gNeuralTickCount++;

  gsSelfMonTickStart();

  // L0: Telemetry (already ingested via gsTelemIngest() from ESP-NOW callback)
  gsSelfMonLayerStart(0);
  gsSelfMonLayerEnd(0);

  static uint32_t lastTelemCount = 0;
  bool telemChanged = (gTelemCount != lastTelemCount);
  lastTelemCount = gTelemCount;

  if (telemChanged) {
    // L1: Signal Processing
    gsSelfMonLayerStart(1);
    gsSigProcessAll();
    gsSelfMonLayerEnd(1);

    // L2: Feature Extraction
    gsSelfMonLayerStart(2);
    gsFeatExtractAll();
    gsSelfMonLayerEnd(2);

    // L3: Pattern Recognition
    gsSelfMonLayerStart(3);
    gsPatProcessAll();
    gsSelfMonLayerEnd(3);

    // L4: Anomaly Detection
    gsSelfMonLayerStart(4);
    gsAnomProcessAll();
    gsSelfMonLayerEnd(4);
  }

  // L5: Rule Engine
  gsSelfMonLayerStart(5);
  gsRulesEvalAll();
  gsSelfMonLayerEnd(5);

  // L6: Behavior Learner
  gsSelfMonLayerStart(6);
  gsLearnProcessAll();
  gsSelfMonLayerEnd(6);

  // L7: Risk Assessment
  gsSelfMonLayerStart(7);
  gsRiskProcessAll();
  gsSelfMonLayerEnd(7);

  // L8: Decision Engine
  gsSelfMonLayerStart(8);
  gsDecProcessAll();
  gsSelfMonLayerEnd(8);

  // L9: Self-Monitor
  gsSelfMonLayerStart(9);
  gsSelfMonProcessAll();
  gsSelfMonLayerEnd(9);

  // State machine
  gsNeuralUpdateState();

  gsSelfMonTickEnd();

  // Outputs
  gsNeuralPrintStatus();
  gsNeuralDiagDump();
}

// #############################################################################
//  QUERY HELPERS
// #############################################################################
static inline uint8_t gsNeuralGetState()     { return gNeuralState; }
static inline bool    gsNeuralIsReady()      { return gNeuralReady; }
static inline uint32_t gsNeuralGetTickCount(){ return gNeuralTickCount; }
static inline bool    gsNeuralIsAlert()      { return gNeuralState == GS_NN_ALERT; }
static inline bool    gsNeuralIsError()      { return gNeuralState == GS_NN_ERROR; }

// Get the highest-priority alert to send to the main ESP
static GsAlert* gsNeuralGetPendingAlert() {
  return gsRuleNextUnsent();
}

// Mark an alert as sent
static void gsNeuralMarkAlertSent(GsAlert* a) {
  if (a) a->sent = true;
}

// Get the top recommendation
static const GsRecommendation& gsNeuralGetTopRecommendation() {
  return gsDecAt(0);
}
