#pragma once
// ############################################################################
//  gs_learner.h  —  LAYER 6: BEHAVIOR LEARNER
//
//  Learns from the user's operational patterns over time:
//    1. User Profile        — attack preferences, aggression level, skill estimate
//    2. Markov Chains       — transition probabilities between attack modes
//    3. Session Modeling    — per-session statistics and cross-session learning
//    4. Effectiveness Track — which modes work best in which conditions
//    5. Temporal Learning   — time-of-day patterns, session duration habits
//    6. Mode Preference     — weighted ranking of modes by usage and success
//    7. Adaptation Rate     — how quickly the user adopts recommendations
//    8. Risk Tolerance      — inferred from behavior vs. warnings
//
//  All learning is EMA-based so it adapts continuously without NVS writes
//  during operation. Session summaries can be persisted at shutdown.
// ############################################################################

#include <Arduino.h>
#include "gs_config.h"
#include "gs_telemetry.h"
#include "gs_signal.h"
#include "gs_features.h"
#include "gs_patterns.h"
#include "gs_anomaly.h"
#include "gs_rules.h"

// #############################################################################
//  USER PROFILE
// #############################################################################
struct GsUserProfile {
  float aggressionLevel;      // 0=passive 1=maximum (EMA of attack severity)
  float skillEstimate;        // 0=novice 1=expert (EMA based on mode diversity + efficiency)
  float riskTolerance;        // 0=cautious 1=reckless (ignores warnings = high)
  float preferredBandCount;   // EMA of typical band count during attacks
  float avgSessionMinutes;    // EMA of session length
  float avgAttackPct;         // EMA of time-in-attack percentage
  float modeExploration;      // 0=always same modes 1=tries everything
  float recommendFollowRate;  // how often they follow recommendations (0-1)
  uint32_t totalSessions;
  uint32_t totalAttacksSeen;
  bool valid;
};

static GsUserProfile gUserProfile = {};

// #############################################################################
//  MARKOV CHAIN — MODE TRANSITION MODEL
//
//  Tracks every mode→mode transition. From this we can:
//  - Predict the next mode the user will select
//  - Detect deviations from typical behavior
//  - Suggest optimal follow-up modes
// #############################################################################
struct GsMarkovModel {
  uint32_t trans[GS_ATK_COUNT][GS_ATK_COUNT]; // trans[from][to] = count
  uint32_t modeTotal[GS_ATK_COUNT];           // total exits from mode
  uint8_t  lastMode;
  bool     valid;
};

static GsMarkovModel gMarkov = {};

static void gsLearnMarkovUpdate(uint8_t newMode) {
  if (!gMarkov.valid) {
    gMarkov.lastMode = newMode;
    gMarkov.valid = true;
    return;
  }
  if (newMode == gMarkov.lastMode) return;
  if (gMarkov.lastMode >= GS_ATK_COUNT || newMode >= GS_ATK_COUNT) return;

  gMarkov.trans[gMarkov.lastMode][newMode]++;
  gMarkov.modeTotal[gMarkov.lastMode]++;
  gMarkov.lastMode = newMode;
}

static float gsLearnTransProb(uint8_t from, uint8_t to) {
  if (from >= GS_ATK_COUNT || to >= GS_ATK_COUNT) return 0;
  if (gMarkov.modeTotal[from] == 0) return 0;
  return (float)gMarkov.trans[from][to] / (float)gMarkov.modeTotal[from];
}

static uint8_t gsLearnMostLikelyNext(uint8_t from) {
  if (from >= GS_ATK_COUNT || gMarkov.modeTotal[from] == 0) return GS_ATK_NONE;
  uint32_t best = 0;
  uint8_t bestMode = GS_ATK_NONE;
  for (int i = 0; i < GS_ATK_COUNT; i++) {
    if (gMarkov.trans[from][i] > best) {
      best = gMarkov.trans[from][i];
      bestMode = i;
    }
  }
  return bestMode;
}

static float gsLearnTransEntropy(uint8_t from) {
  if (from >= GS_ATK_COUNT || gMarkov.modeTotal[from] == 0) return 0;
  float entropy = 0;
  float total = (float)gMarkov.modeTotal[from];
  for (int i = 0; i < GS_ATK_COUNT; i++) {
    if (gMarkov.trans[from][i] > 0) {
      float p = (float)gMarkov.trans[from][i] / total;
      entropy -= p * log2f(p);
    }
  }
  return entropy;
}

// #############################################################################
//  SESSION MODEL — per-session statistics
// #############################################################################
static GsSessionProfile gCurrentSession = {};
static GsSessionProfile gSessionHistory[GS_LEARN_SESSION_MAX];
static uint8_t gSessionHistCount = 0;

static void gsLearnSessionInit() {
  memset(&gCurrentSession, 0, sizeof(gCurrentSession));
  gCurrentSession.startMs = millis();
  gCurrentSession.valid = true;
}

static void gsLearnSessionUpdate() {
  if (!gCurrentSession.valid) return;
  uint32_t now = millis();
  uint32_t elapsed = now - gCurrentSession.startMs;

  // Track attack time
  if (gFeatVec.raw.attacking > 0.5f) {
    gCurrentSession.totalAttackMs += GS_NEURAL_TICK_MS;
    uint32_t currentAtkMs = (uint32_t)(gFeatVec.raw.attackDuration * 1000.0f);
    if (currentAtkMs > gCurrentSession.longestAttackMs)
      gCurrentSession.longestAttackMs = currentAtkMs;
  }

  // Peak tracking
  float bands = gFeatVec.raw.bandCount;
  if ((uint8_t)bands > gCurrentSession.peakBandCount)
    gCurrentSession.peakBandCount = (uint8_t)bands;

  float thermal = gFeatVec.derived.thermalRisk;
  if (thermal > gCurrentSession.peakThermalRisk)
    gCurrentSession.peakThermalRisk = thermal;

  // Running average effectiveness
  float effect = gFeatVec.derived.effectivenessScore;
  static uint32_t effectSamples = 0;
  gCurrentSession.avgEffectiveness =
    (gCurrentSession.avgEffectiveness * effectSamples + effect) / (effectSamples + 1);
  effectSamples++;

  // Count unique modes used
  static bool modesUsed[GS_ATK_COUNT] = {};
  uint8_t mode = (uint8_t)gFeatVec.raw.attackMode;
  if (mode < GS_ATK_COUNT && !modesUsed[mode]) {
    modesUsed[mode] = true;
    gCurrentSession.uniqueModesUsed++;
  }

  // Track mode changes and alerts
  if (gDelta.modeChanged) gCurrentSession.modeChanges++;
  gCurrentSession.alertsFired = gRulesTriggered;
}

static void gsLearnSessionEnd() {
  if (!gCurrentSession.valid) return;

  // Store in history ring
  if (gSessionHistCount < GS_LEARN_SESSION_MAX) {
    gSessionHistory[gSessionHistCount++] = gCurrentSession;
  } else {
    memmove(gSessionHistory, gSessionHistory + 1,
            sizeof(GsSessionProfile) * (GS_LEARN_SESSION_MAX - 1));
    gSessionHistory[GS_LEARN_SESSION_MAX - 1] = gCurrentSession;
  }
}

// #############################################################################
//  EFFECTIVENESS TRACKER — per-mode effectiveness measurement
//
//  For each mode, tracks how effective it was based on packet output,
//  duration, and thermal cost. Modes that produce more results per
//  unit of thermal cost are ranked higher.
// #############################################################################
struct GsModeEffectiveness {
  float avgEffectiveness;     // EMA of effectiveness score when in this mode
  float avgDuration;          // EMA of how long user stays in this mode
  float avgThermalCost;       // EMA of thermal risk generated
  float avgDetectionCost;     // EMA of detection risk generated
  float roi;                  // return on investment: effect / (thermal + detect)
  uint32_t usageCount;        // times this mode was entered
  uint32_t totalMs;           // total time in this mode
  bool valid;
};

static GsModeEffectiveness gModeEffect[GS_ATK_COUNT] = {};

static void gsLearnEffectivenessUpdate() {
  uint8_t mode = (uint8_t)gFeatVec.raw.attackMode;
  if (mode >= GS_ATK_COUNT || mode == GS_ATK_NONE) return;
  if (gFeatVec.raw.attacking < 0.5f) return;

  GsModeEffectiveness& me = gModeEffect[mode];
  float effect = gFeatVec.derived.effectivenessScore;
  float thermal = gFeatVec.derived.thermalRisk;
  float detect = gFeatVec.derived.detectionRisk;
  float dur = gFeatVec.raw.attackDuration;

  if (!me.valid) {
    me.avgEffectiveness = effect;
    me.avgDuration = dur;
    me.avgThermalCost = thermal;
    me.avgDetectionCost = detect;
    me.usageCount = 1;
    me.valid = true;
  } else {
    float surprise = fabsf(effect - me.avgEffectiveness);
    float alpha = GS_LEARN_EMA_ALPHA * (1.0f + surprise * 4.0f);
    if (alpha > 0.3f) alpha = 0.3f;
    me.avgEffectiveness = me.avgEffectiveness * (1.0f - alpha) + effect * alpha;
    me.avgThermalCost   = me.avgThermalCost * (1.0f - alpha) + thermal * alpha;
    me.avgDetectionCost = me.avgDetectionCost * (1.0f - alpha) + detect * alpha;
  }

  me.totalMs += GS_NEURAL_TICK_MS;

  // Track mode entry
  if (gDelta.attackStarted) {
    me.usageCount++;
    me.avgDuration = me.avgDuration * (1.0f - GS_LEARN_EMA_ALPHA)
                   + dur * GS_LEARN_EMA_ALPHA;
  }

  // ROI: effectiveness per unit cost
  float cost = me.avgThermalCost + me.avgDetectionCost;
  me.roi = (cost > 0.01f) ? me.avgEffectiveness / cost : me.avgEffectiveness * 10.0f;
}

static uint8_t gsLearnBestMode() {
  float bestROI = -1;
  uint8_t bestMode = GS_ATK_NONE;
  for (int i = 1; i < GS_ATK_COUNT; i++) {
    if (gModeEffect[i].valid && gModeEffect[i].usageCount >= 3) {
      if (gModeEffect[i].roi > bestROI) {
        bestROI = gModeEffect[i].roi;
        bestMode = i;
      }
    }
  }
  return bestMode;
}

static uint8_t gsLearnWorstMode() {
  float worstROI = 999;
  uint8_t worstMode = GS_ATK_NONE;
  for (int i = 1; i < GS_ATK_COUNT; i++) {
    if (gModeEffect[i].valid && gModeEffect[i].usageCount >= 3) {
      if (gModeEffect[i].roi < worstROI) {
        worstROI = gModeEffect[i].roi;
        worstMode = i;
      }
    }
  }
  return worstMode;
}

// #############################################################################
//  MODE PREFERENCE RANKING
//
//  Weighted combination of usage frequency and effectiveness to produce
//  a ranked list of user's preferred (and best) modes.
// #############################################################################
struct GsModeRank {
  uint8_t mode;
  float   score;
};

static GsModeRank gModeRanking[GS_ATK_COUNT];
static uint8_t    gModeRankCount = 0;

static void gsLearnRankModes() {
  gModeRankCount = 0;

  uint32_t maxMs = 1;
  for (int i = 1; i < GS_ATK_COUNT; i++)
    if (gModeEffect[i].totalMs > maxMs) maxMs = gModeEffect[i].totalMs;

  for (int i = 1; i < GS_ATK_COUNT; i++) {
    if (!gModeEffect[i].valid || gModeEffect[i].usageCount == 0) continue;

    float freqScore = min(1.0f, (float)gModeEffect[i].usageCount / 20.0f);
    float effScore  = gModeEffect[i].avgEffectiveness;
    float roiScore  = min(1.0f, gModeEffect[i].roi);
    float recency   = (float)gModeEffect[i].totalMs / (float)maxMs;
    float diversity  = (gModeEffect[i].usageCount < 5) ? 0.15f : 0.0f;

    float combined = freqScore * 0.2f + effScore * 0.35f +
                     roiScore * 0.25f + recency * 0.1f + diversity;

    gModeRanking[gModeRankCount].mode = i;
    gModeRanking[gModeRankCount].score = combined;
    gModeRankCount++;
  }

  // Sort descending by score (insertion sort, max 43 elements)
  for (int i = 1; i < gModeRankCount; i++) {
    GsModeRank key = gModeRanking[i];
    int j = i - 1;
    while (j >= 0 && gModeRanking[j].score < key.score) {
      gModeRanking[j + 1] = gModeRanking[j];
      j--;
    }
    gModeRanking[j + 1] = key;
  }
}

// #############################################################################
//  USER PROFILE UPDATER
//
//  Continuously refines the user profile based on observed behavior.
// #############################################################################
static uint32_t gsLearnProfileTicks = 0;

static void gsLearnProfileUpdate() {
  gsLearnProfileTicks++;
  float maturity = min(1.0f, (float)gsLearnProfileTicks / 2400.0f);
  const float ALPHA = 0.02f + 0.08f * (1.0f - maturity);
  GsUserProfile& p = gUserProfile;

  if (!p.valid) {
    p.aggressionLevel = 0.5f;
    p.skillEstimate = 0.5f;
    p.riskTolerance = 0.5f;
    p.preferredBandCount = 1.0f;
    p.avgSessionMinutes = 15.0f;
    p.avgAttackPct = 0.3f;
    p.modeExploration = 0.5f;
    p.recommendFollowRate = 0.5f;
    p.valid = true;
  }

  // Aggression: based on current mode's thermal + detection coefficients
  uint8_t mode = (uint8_t)gFeatVec.raw.attackMode;
  if (mode > 0 && mode < GS_ATK_COUNT) {
    float modeAggression = (GS_ATK_THERMAL[mode] + GS_ATK_DETECT[mode]) / 2.0f;
    p.aggressionLevel = p.aggressionLevel * (1.0f - ALPHA) + modeAggression * ALPHA;
  }

  // Skill: based on mode diversity and effectiveness
  if (gCurrentSession.valid && gCurrentSession.uniqueModesUsed > 0) {
    float diversity = min(1.0f, (float)gCurrentSession.uniqueModesUsed / 15.0f);
    float efficiency = gFeatVec.derived.pktRateEfficiency;
    float skill = diversity * 0.5f + efficiency * 0.5f;
    p.skillEstimate = p.skillEstimate * (1.0f - ALPHA) + skill * ALPHA;
  }

  // Risk tolerance: inferred from behavior relative to warnings
  // If user continues attacking after warnings, tolerance goes up
  if (gRulesTriggered > 5 && gFeatVec.raw.attacking > 0.5f) {
    float riskSignal = min(1.0f, (float)gRulesTriggered / 20.0f);
    p.riskTolerance = p.riskTolerance * (1.0f - ALPHA) + riskSignal * ALPHA;
  } else if (gRulesTriggered < 2) {
    p.riskTolerance = p.riskTolerance * (1.0f - ALPHA) + 0.3f * ALPHA;
  }

  // Preferred band count
  float bands = gFeatVec.raw.bandCount;
  if (bands > 0)
    p.preferredBandCount = p.preferredBandCount * (1.0f - ALPHA) + bands * ALPHA;

  // Attack percentage of session time
  if (gCurrentSession.valid) {
    uint32_t elapsed = millis() - gCurrentSession.startMs;
    if (elapsed > 10000) {
      float atkPct = (float)gCurrentSession.totalAttackMs / (float)elapsed;
      p.avgAttackPct = p.avgAttackPct * (1.0f - ALPHA) + atkPct * ALPHA;
    }
  }

  // Mode exploration (entropy of Markov transitions)
  float totalEntropy = 0;
  int entropyCount = 0;
  for (int i = 0; i < GS_ATK_COUNT; i++) {
    if (gMarkov.modeTotal[i] > 3) {
      totalEntropy += gsLearnTransEntropy(i);
      entropyCount++;
    }
  }
  if (entropyCount > 0) {
    float avgEntropy = totalEntropy / (float)entropyCount;
    float exploration = min(1.0f, avgEntropy / 3.0f);
    p.modeExploration = p.modeExploration * (1.0f - ALPHA) + exploration * ALPHA;
  }
}

// #############################################################################
//  TEMPORAL LEARNING — time-of-session patterns
//
//  Learns what the user typically does at different points in a session:
//  - Early session: scanning/recon?
//  - Mid session: active attacks?
//  - Late session: cleanup/monitoring?
// #############################################################################
struct GsTemporalBin {
  float avgAggression;
  float avgEffectiveness;
  float avgBandCount;
  float avgSwitchRate;
  uint32_t sampleCount;
};

#define GS_TEMPORAL_BINS 8  // 8 bins across session lifetime

static GsTemporalBin gTemporalBins[GS_TEMPORAL_BINS] = {};

static void gsLearnTemporalUpdate() {
  if (!gCurrentSession.valid) return;
  uint32_t elapsed = millis() - gCurrentSession.startMs;
  float sessionMinutes = (float)elapsed / 60000.0f;

  // Bin by session progress (normalize to expected session length)
  float expectedLen = max(5.0f, gUserProfile.avgSessionMinutes);
  float progress = min(1.0f, sessionMinutes / expectedLen);
  int bin = min(GS_TEMPORAL_BINS - 1, (int)(progress * GS_TEMPORAL_BINS));

  GsTemporalBin& tb = gTemporalBins[bin];
  const float ALPHA = 0.05f;

  float aggression = gUserProfile.aggressionLevel;
  float effect = gFeatVec.derived.effectivenessScore;
  float bands = gFeatVec.raw.bandCount;
  float switchRate = gFeatVec.raw.switchRate;

  if (tb.sampleCount == 0) {
    tb.avgAggression = aggression;
    tb.avgEffectiveness = effect;
    tb.avgBandCount = bands;
    tb.avgSwitchRate = switchRate;
  } else {
    tb.avgAggression     = tb.avgAggression * (1.0f - ALPHA) + aggression * ALPHA;
    tb.avgEffectiveness  = tb.avgEffectiveness * (1.0f - ALPHA) + effect * ALPHA;
    tb.avgBandCount      = tb.avgBandCount * (1.0f - ALPHA) + bands * ALPHA;
    tb.avgSwitchRate     = tb.avgSwitchRate * (1.0f - ALPHA) + switchRate * ALPHA;
  }
  tb.sampleCount++;
}

static float gsLearnExpectedAggression() {
  if (!gCurrentSession.valid) return gUserProfile.aggressionLevel;
  uint32_t elapsed = millis() - gCurrentSession.startMs;
  float sessionMinutes = (float)elapsed / 60000.0f;
  float expectedLen = max(5.0f, gUserProfile.avgSessionMinutes);
  float progress = min(1.0f, sessionMinutes / expectedLen);
  int bin = min(GS_TEMPORAL_BINS - 1, (int)(progress * GS_TEMPORAL_BINS));
  return (gTemporalBins[bin].sampleCount > 0) ?
    gTemporalBins[bin].avgAggression : gUserProfile.aggressionLevel;
}

// #############################################################################
//  RECOMMENDATION RESPONSE TRACKER
//
//  Measures whether the user follows AI recommendations. If the AI suggests
//  "SWITCH MODE" and the user switches within 30 seconds, that's a follow.
//  If they ignore it, that's a rejection.
// #############################################################################
struct GsRecommendTrack {
  uint8_t  lastRecAction;     // GS_REC_*
  uint32_t lastRecTimeMs;
  bool     pending;
};

static GsRecommendTrack gRecTrack = {};
static float gRecommendFollowRate = 0.5f;

static void gsLearnRecommendUpdate() {
  if (!gRecTrack.pending) return;

  uint32_t now = millis();
  uint32_t age = now - gRecTrack.lastRecTimeMs;

  if (age > 30000) {
    // Recommendation expired without action — rejection
    gRecommendFollowRate = gRecommendFollowRate * 0.95f + 0.0f * 0.05f;
    gRecTrack.pending = false;
    return;
  }

  // Check if user followed the recommendation
  bool followed = false;
  switch (gRecTrack.lastRecAction) {
    case GS_REC_STOP_ATTACK:
      if (gFeatVec.raw.attacking < 0.5f && gDelta.attackStopped)
        followed = true;
      break;
    case GS_REC_SWITCH_MODE:
    case GS_REC_ROTATE_MODE:
      if (gDelta.modeChanged) followed = true;
      break;
    case GS_REC_CHANGE_FREQ:
      if (gDelta.modeChanged) followed = true;
      break;
    default:
      // Can't easily track other recommendation types
      break;
  }

  if (followed) {
    float speed = (age < 5000) ? 0.2f : 0.1f;
    gRecommendFollowRate = gRecommendFollowRate * (1.0f - speed) + 1.0f * speed;
    gRecTrack.pending = false;
    gUserProfile.recommendFollowRate = gRecommendFollowRate;
  }
}

static void gsLearnRecommendIssued(uint8_t recAction) {
  gRecTrack.lastRecAction = recAction;
  gRecTrack.lastRecTimeMs = millis();
  gRecTrack.pending = true;
}

// #############################################################################
//  ATTACK SEQUENCE PREDICTOR
//
//  Using Markov chain + temporal model, predict what the user will do next
//  and how long they'll do it.
// #############################################################################
struct GsPrediction {
  uint8_t  nextMode;
  float    probability;
  float    expectedDurSec;
  float    expectedEffect;
};

static GsPrediction gsLearnPredict() {
  GsPrediction pred = {};
  uint8_t current = (uint8_t)gFeatVec.raw.attackMode;

  pred.nextMode = gsLearnMostLikelyNext(current);
  pred.probability = gsLearnTransProb(current, pred.nextMode);

  if (pred.nextMode < GS_ATK_COUNT && gModeEffect[pred.nextMode].valid) {
    pred.expectedDurSec = gModeEffect[pred.nextMode].avgDuration;
    pred.expectedEffect = gModeEffect[pred.nextMode].avgEffectiveness;
  }

  return pred;
}

// #############################################################################
//  MODE SIMILARITY — find similar modes to a given one
//
//  Based on attack flags and thermal/detection profile similarity.
//  Used to recommend alternative modes when current one isn't working.
// #############################################################################
static float gsLearnModeSimilarity(uint8_t a, uint8_t b) {
  if (a >= GS_ATK_COUNT || b >= GS_ATK_COUNT) return 0;

  // Flag similarity (Jaccard index on attack flags)
  uint8_t fa = GS_ATK_FLAGS[a], fb = GS_ATK_FLAGS[b];
  uint8_t intersection = __builtin_popcount(fa & fb);
  uint8_t unionBits = __builtin_popcount(fa | fb);
  float flagSim = (unionBits > 0) ? (float)intersection / (float)unionBits : 0;

  // Profile similarity (thermal + detection distance)
  float tDist = fabsf(GS_ATK_THERMAL[a] - GS_ATK_THERMAL[b]);
  float dDist = fabsf(GS_ATK_DETECT[a] - GS_ATK_DETECT[b]);
  float profileSim = max(0.0f, 1.0f - (tDist + dDist) / 2.0f);

  return flagSim * 0.5f + profileSim * 0.5f;
}

static uint8_t gsLearnSuggestAlternative(uint8_t current) {
  if (current >= GS_ATK_COUNT) return GS_ATK_NONE;

  float bestScore = -1;
  uint8_t bestMode = GS_ATK_NONE;

  for (int i = 1; i < GS_ATK_COUNT; i++) {
    if (i == current) continue;

    float sim = gsLearnModeSimilarity(current, i);
    if (sim < 0.3f) continue;

    // Prefer modes with better ROI
    float roi = 0;
    if (gModeEffect[i].valid) roi = gModeEffect[i].roi;
    float score = sim * 0.4f + min(1.0f, roi) * 0.6f;

    if (score > bestScore) {
      bestScore = score;
      bestMode = i;
    }
  }

  return bestMode;
}

// #############################################################################
//  MASTER UPDATE — run all learning components
// #############################################################################
static void gsLearnProcessAll() {
  if (!gTelemValid || gTelemCount < 5) return;

  // Update Markov model on mode changes
  if (gDelta.modeChanged) {
    gsLearnMarkovUpdate((uint8_t)gFeatVec.raw.attackMode);
  }

  gsLearnSessionUpdate();
  gsLearnEffectivenessUpdate();
  gsLearnTemporalUpdate();
  gsLearnRecommendUpdate();
  gsLearnProfileUpdate();

  // Periodic mode ranking (every 20 ticks = 5 seconds)
  static uint8_t rankCounter = 0;
  if (++rankCounter >= 20) {
    gsLearnRankModes();
    rankCounter = 0;
  }
}

// #############################################################################
//  INITIALIZATION
// #############################################################################
static void gsLearnInit() {
  memset(&gUserProfile, 0, sizeof(gUserProfile));
  memset(&gMarkov, 0, sizeof(gMarkov));
  memset(&gCurrentSession, 0, sizeof(gCurrentSession));
  memset(gSessionHistory, 0, sizeof(gSessionHistory));
  memset(gModeEffect, 0, sizeof(gModeEffect));
  memset(gModeRanking, 0, sizeof(gModeRanking));
  memset(gTemporalBins, 0, sizeof(gTemporalBins));
  memset(&gRecTrack, 0, sizeof(gRecTrack));
  gSessionHistCount = 0;
  gModeRankCount = 0;
  gRecommendFollowRate = 0.5f;

  gsLearnSessionInit();
}

// #############################################################################
//  LEARNER STATE SERIALIZATION (for SD persistence via Node 2)
//
//  Packs the essential learning state into a compact binary blob that can be:
//    1. Sent to Node 2 via ESP-NOW (chunked into 200-byte packets)
//    2. Saved to /brain/models/learner_state.dat on the 128GB SD
//    3. Loaded on boot and sent back to Node 1 to restore learning
// #############################################################################

#define GS_LEARN_MAGIC  0x4C53  // "LS"
#define GS_LEARN_VER    1

#pragma pack(push, 1)
struct GsLearnerBlob {
  uint16_t magic;
  uint8_t  version;

  // User profile
  uint8_t  aggression;       // 0-255 mapped from 0.0-1.0
  uint8_t  skill;
  uint8_t  riskTolerance;
  uint8_t  followRate;
  uint8_t  exploration;
  uint32_t totalSessions;

  // Markov chain (compressed: top 3 transitions per mode)
  struct { uint8_t to; uint16_t count; } topTrans[GS_ATK_COUNT][3];

  // Mode effectiveness (top 20 modes by usage)
  struct { uint8_t mode; uint8_t effectiveness; uint8_t roi; } topModes[20];

  // Temporal bins
  uint8_t  temporalBinModes[8];
  uint8_t  temporalBinCounts[8];

  uint8_t  reserved[16];
};
#pragma pack(pop)

static inline size_t gsLearnSerialize(uint8_t* buf, size_t maxLen) {
  if (maxLen < sizeof(GsLearnerBlob)) return 0;

  GsLearnerBlob* b = (GsLearnerBlob*)buf;
  memset(b, 0, sizeof(GsLearnerBlob));
  b->magic = GS_LEARN_MAGIC;
  b->version = GS_LEARN_VER;

  b->aggression = (uint8_t)(gUserProfile.aggressionLevel * 255.0f);
  b->skill = (uint8_t)(gUserProfile.skillEstimate * 255.0f);
  b->riskTolerance = (uint8_t)(gUserProfile.riskTolerance * 255.0f);
  b->followRate = (uint8_t)(gRecommendFollowRate * 255.0f);
  b->exploration = (uint8_t)(gUserProfile.modeExploration * 255.0f);
  b->totalSessions = gUserProfile.totalSessions;

  // Compress Markov: keep top 3 transitions per mode
  for (int from = 0; from < GS_ATK_COUNT; from++) {
    uint8_t best[3] = {0, 0, 0};
    uint16_t bestC[3] = {0, 0, 0};
    for (int to = 0; to < GS_ATK_COUNT; to++) {
      uint32_t c = gMarkov.trans[from][to];
      if (c == 0) continue;
      uint16_t cc = (c > 65535) ? 65535 : (uint16_t)c;
      for (int k = 0; k < 3; k++) {
        if (cc > bestC[k]) {
          for (int j = 2; j > k; j--) { best[j] = best[j-1]; bestC[j] = bestC[j-1]; }
          best[k] = to; bestC[k] = cc;
          break;
        }
      }
    }
    for (int k = 0; k < 3; k++) {
      b->topTrans[from][k].to = best[k];
      b->topTrans[from][k].count = bestC[k];
    }
  }

  // Top 20 modes by effectiveness
  int mIdx = 0;
  for (int m = 0; m < GS_ATK_COUNT && mIdx < 20; m++) {
    if (gModeEffect[m].valid) {
      b->topModes[mIdx].mode = m;
      b->topModes[mIdx].effectiveness = (uint8_t)(gModeEffect[m].avgEffectiveness * 255.0f);
      b->topModes[mIdx].roi = (uint8_t)((gModeEffect[m].roi > 0 ? gModeEffect[m].roi : 0) * 50.0f);
      mIdx++;
    }
  }

  // Temporal bins
  for (int i = 0; i < 8; i++) {
    b->temporalBinModes[i] = 0;
    b->temporalBinCounts[i] = (gTemporalBins[i].sampleCount > 255) ?
                               255 : gTemporalBins[i].sampleCount;
  }

  return sizeof(GsLearnerBlob);
}

static inline bool gsLearnDeserialize(const uint8_t* buf, size_t len) {
  if (len < sizeof(GsLearnerBlob)) return false;

  const GsLearnerBlob* b = (const GsLearnerBlob*)buf;
  if (b->magic != GS_LEARN_MAGIC || b->version != GS_LEARN_VER) return false;

  gUserProfile.aggressionLevel = b->aggression / 255.0f;
  gUserProfile.skillEstimate = b->skill / 255.0f;
  gUserProfile.riskTolerance = b->riskTolerance / 255.0f;
  gRecommendFollowRate = b->followRate / 255.0f;
  gUserProfile.modeExploration = b->exploration / 255.0f;
  gUserProfile.totalSessions = b->totalSessions;
  gUserProfile.valid = true;

  // Restore Markov chain from top transitions
  for (int from = 0; from < GS_ATK_COUNT; from++) {
    for (int k = 0; k < 3; k++) {
      uint8_t to = b->topTrans[from][k].to;
      uint16_t c = b->topTrans[from][k].count;
      if (c > 0 && to < GS_ATK_COUNT) {
        gMarkov.trans[from][to] = c;
        gMarkov.modeTotal[from] += c;
      }
    }
  }
  gMarkov.valid = true;

  // Restore mode effectiveness
  for (int i = 0; i < 20; i++) {
    uint8_t m = b->topModes[i].mode;
    if (m < GS_ATK_COUNT && b->topModes[i].effectiveness > 0) {
      gModeEffect[m].avgEffectiveness = b->topModes[i].effectiveness / 255.0f;
      gModeEffect[m].roi = b->topModes[i].roi / 50.0f;
      gModeEffect[m].valid = true;
    }
  }

  // Restore temporal bins
  for (int i = 0; i < 8; i++) {
    gTemporalBins[i].sampleCount = b->temporalBinCounts[i];
  }

  Serial.printf("[learner] restored: aggr=%.2f skill=%.2f risk=%.2f sessions=%lu\n",
                gUserProfile.aggressionLevel, gUserProfile.skillEstimate,
                gUserProfile.riskTolerance, (unsigned long)gUserProfile.totalSessions);
  return true;
}

// #############################################################################
//  QUERY HELPERS
// #############################################################################
static inline const GsUserProfile& gsLearnProfile()  { return gUserProfile; }
static inline float gsLearnAggression()               { return gUserProfile.aggressionLevel; }
static inline float gsLearnSkill()                    { return gUserProfile.skillEstimate; }
static inline float gsLearnRiskTolerance()            { return gUserProfile.riskTolerance; }
static inline float gsLearnFollowRate()               { return gRecommendFollowRate; }
static inline const GsSessionProfile& gsLearnSession(){ return gCurrentSession; }
static inline uint8_t gsLearnRankedMode(uint8_t rank) {
  return (rank < gModeRankCount) ? gModeRanking[rank].mode : GS_ATK_NONE;
}
static inline float gsLearnRankedScore(uint8_t rank) {
  return (rank < gModeRankCount) ? gModeRanking[rank].score : 0;
}
static inline float gsLearnModeROI(uint8_t mode) {
  return (mode < GS_ATK_COUNT && gModeEffect[mode].valid) ? gModeEffect[mode].roi : 0;
}
