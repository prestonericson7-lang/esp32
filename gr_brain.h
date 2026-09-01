#pragma once
// ############################################################################
//  gr_brain.h  -  GHOST TX R40T  /  Deep Brain Processing Framework
//
//  13 tiers, 176 processing stages — algorithmic pipeline, NOT neural net.
//  Pattern matching, lookup tables, hash comparisons, statistical analysis.
//  Each stage runs in microseconds to milliseconds.
//  Section 04 + 14.8 of the master plan.
// ############################################################################

#include <Arduino.h>
#include "gr_config.h"

// ===== Tier definitions (stage counts from Section 04) =======================
enum GrTier : uint8_t {
  GR_TIER_SIGNAL_ACQ     = 0,   // 10 stages
  GR_TIER_MOD_PROBE      = 1,   // 10 stages
  GR_TIER_ENCODING       = 2,   // 10 stages
  GR_TIER_PROTOCOL_ID    = 3,   // 21 stages
  GR_TIER_DEVICE_CLASS   = 4,   // 40 stages
  GR_TIER_SECURITY       = 5,   // 15 stages
  GR_TIER_BEHAVIORAL     = 6,   // 10 stages
  GR_TIER_CROSS_SIGNAL   = 7,   // 10 stages
  GR_TIER_KNOWLEDGE      = 8,   // 10 stages
  GR_TIER_DECISION       = 9,   // 12 stages
  GR_TIER_SORT_QUEUE     = 10,  // 10 stages
  GR_TIER_PREDICTIVE     = 11,  // 10 stages
  GR_TIER_MULTI_NODE     = 12,  // 8 stages
  GR_TIER_COUNT          = 13,
};

static const uint8_t GR_TIER_STAGES[GR_TIER_COUNT] = {
  10, 10, 10, 21, 40, 15, 10, 10, 10, 12, 10, 10, 8
};

#define GR_TOTAL_STAGES 176

// ===== Pipeline state ========================================================
struct GrBrainState {
  bool     active;
  uint8_t  currentTier;
  uint8_t  currentStage;
  uint16_t totalProcessed;
  uint16_t signalsAnalyzed;
  uint8_t  confidence;
  uint8_t  rulesFired;
  uint8_t  lastDeviceType;
  uint8_t  securityRating;     // 0-100
  bool     fixedCodeVuln;
  bool     replayVuln;
  bool     jamVuln;
  uint32_t lastProcessMs;
  uint32_t processingTimeUs;
};

static GrBrainState grBrain = {};

// ===== Fast-path fingerprint cache ==========================================
// FNV-1a hash of first 6 data bytes + freq band + modulation.
// After 3 hits on the same fingerprint we skip tiers 0-5 and jump straight
// to behavioral analysis, cutting pipeline time by ~60% for repeated signals.
#define GR_FP_CACHE_SIZE 32
#define GR_FP_CACHE_MASK (GR_FP_CACHE_SIZE - 1)

struct GrFpCacheEntry {
  uint32_t hash;
  uint8_t  deviceType;
  uint8_t  securityRating;
  uint8_t  confidence;
  bool     fixedCode;
  bool     replayVuln;
  uint16_t hitCount;
};

static GrFpCacheEntry grFpCache[GR_FP_CACHE_SIZE] = {};
static uint16_t grFpCacheHits   = 0;
static uint16_t grFpCacheMisses = 0;

static inline uint32_t grFpHash(const uint8_t* d, uint8_t len, float freq, uint8_t mod) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < 6 && i < len; i++) { h ^= d[i]; h *= 16777619u; }
  h ^= (uint8_t)((int)(freq * 10.0f) & 0xFF);
  h *= 16777619u;
  h ^= mod;
  return h;
}

static inline GrFpCacheEntry* grFpLookup(uint32_t h) {
  for (int i = 0; i < 4; i++) {
    uint8_t idx = (h + i) & GR_FP_CACHE_MASK;
    if (grFpCache[idx].hash == h && grFpCache[idx].hitCount > 0)
      return &grFpCache[idx];
  }
  return nullptr;
}

static inline void grFpStore(uint32_t h, uint8_t devType, uint8_t sec,
                              uint8_t conf, bool fixed, bool replay) {
  int lru = h & GR_FP_CACHE_MASK;
  uint16_t minHits = UINT16_MAX;
  for (int i = 0; i < 4; i++) {
    uint8_t idx = (h + i) & GR_FP_CACHE_MASK;
    if (grFpCache[idx].hitCount == 0) { lru = idx; break; }
    if (grFpCache[idx].hitCount < minHits) { minHits = grFpCache[idx].hitCount; lru = idx; }
  }
  grFpCache[lru] = { h, devType, sec, conf, fixed, replay, 1 };
}

// ===== Tier name lookup ======================================================
static const char* grTierName(uint8_t tier) {
  switch (tier) {
    case GR_TIER_SIGNAL_ACQ:   return "Signal Acquisition";
    case GR_TIER_MOD_PROBE:    return "Modulation Probe";
    case GR_TIER_ENCODING:     return "Encoding & Framing";
    case GR_TIER_PROTOCOL_ID:  return "Protocol ID";
    case GR_TIER_DEVICE_CLASS: return "Device Classification";
    case GR_TIER_SECURITY:     return "Security Analysis";
    case GR_TIER_BEHAVIORAL:   return "Behavioral Analysis";
    case GR_TIER_CROSS_SIGNAL: return "Cross-Signal Intel";
    case GR_TIER_KNOWLEDGE:    return "Knowledge & Memory";
    case GR_TIER_DECISION:     return "Decision & Action";
    case GR_TIER_SORT_QUEUE:   return "Sort Queue Intel";
    case GR_TIER_PREDICTIVE:   return "Predictive Intel";
    case GR_TIER_MULTI_NODE:   return "Multi-Node Coord";
    default: return "Unknown";
  }
}

// ===== Tier 0: Signal Acquisition (10 stages) ================================
static uint8_t  grT0NoiseFloor[4]  = {255,255,255,255};
static uint8_t  grT0SignalPeak[4]  = {};
static float    grT0LastFreq[4]    = {};
static uint32_t grT0LastTime[4]    = {};
static uint8_t  grT0Energy         = 0;

static inline void grBrainT0(uint8_t stage, const GrCaptureRecord& rec) {
  switch (stage) {
    case 0: break; // L0.0 FIFO Byte Capture — already captured by Node 0
    case 1: { // L0.1 Noise Floor Measurement
      uint8_t mn = 255;
      for (int i = 0; i < rec.dataLen; i++)
        if (rec.data[i] < mn) mn = rec.data[i];
      if (rec.radioId < 4) grT0NoiseFloor[rec.radioId] = mn;
      grBrain.rulesFired++;
    } break;
    case 2: { // L0.2 SNR Scoring
      uint8_t pk = 0;
      for (int i = 0; i < rec.dataLen; i++)
        if (rec.data[i] > pk) pk = rec.data[i];
      if (rec.radioId < 4) grT0SignalPeak[rec.radioId] = pk;
      uint8_t nf = (rec.radioId < 4) ? grT0NoiseFloor[rec.radioId] : 0;
      if (pk > nf + 30) { grBrain.confidence += 5; grBrain.rulesFired++; }
    } break;
    case 3: // L0.3 AGC Config Read-back
      if (grT0SignalPeak[rec.radioId < 4 ? rec.radioId : 0] > 20 &&
          grT0SignalPeak[rec.radioId < 4 ? rec.radioId : 0] < 240)
        grBrain.rulesFired++;
      break;
    case 4: { // L0.4 Frequency Offset Estimation
      float bandCenter = 433.92f;
      if (rec.freqMHz >= 314.0f && rec.freqMHz <= 316.0f) bandCenter = 315.0f;
      else if (rec.freqMHz >= 867.0f && rec.freqMHz <= 870.0f) bandCenter = 868.35f;
      else if (rec.freqMHz >= 914.0f && rec.freqMHz <= 916.0f) bandCenter = 915.0f;
      float offset = rec.freqMHz - bandCenter;
      if (offset < 0) offset = -offset;
      if (offset < 0.5f) grBrain.rulesFired++;
    } break;
    case 5: // L0.5 Drift Tracking
      if (rec.radioId < 4) {
        float prev = grT0LastFreq[rec.radioId];
        grT0LastFreq[rec.radioId] = rec.freqMHz;
        if (prev > 0.0f) {
          float drift = rec.freqMHz - prev;
          if (drift < 0) drift = -drift;
          if (drift > 0.1f) grBrain.rulesFired++;
        }
      }
      break;
    case 6: // L0.6 Multi-signal Presence Detection
      if (rec.dataLen > 100) grBrain.rulesFired++;
      break;
    case 7: { // L0.7 Signal Separation
      uint8_t transitions = 0;
      for (int i = 1; i < rec.dataLen; i++) {
        int diff = (int)rec.data[i] - (int)rec.data[i-1];
        if (diff > 40 || diff < -40) transitions++;
      }
      if (transitions > rec.dataLen / 4) grBrain.rulesFired++;
    } break;
    case 8: { // L0.8 Signal Energy Profiling
      uint32_t energy = 0;
      for (int i = 0; i < rec.dataLen; i++) energy += rec.data[i] * rec.data[i];
      grT0Energy = (rec.dataLen > 0) ? (uint8_t)(energy / rec.dataLen / 256) : 0;
      if (grT0Energy > 10) grBrain.rulesFired++;
    } break;
    case 9: // L0.9 Burst Detection & Timing
      if (rec.radioId < 4) {
        uint32_t gap = rec.timestamp - grT0LastTime[rec.radioId];
        grT0LastTime[rec.radioId] = rec.timestamp;
        if (gap > 0 && gap < 50) { grBrain.rulesFired++; grBrain.confidence += 3; }
      }
      break;
  }
}

// ===== Tier 1: Modulation Probing (10 stages) ================================
static inline void grBrainT1(uint8_t stage, const GrCaptureRecord& rec) {
  switch (stage) {
    case 0: // L1.0 ASK/OOK Probe
      if (rec.modulation == 0) { grBrain.confidence += 10; grBrain.rulesFired++; }
      break;
    case 1: // L1.1 2-FSK Probe
      if (rec.modulation == 1) { grBrain.confidence += 10; grBrain.rulesFired++; }
      break;
    case 2: // L1.2 4-FSK Probe
      if (rec.modulation == 4) { grBrain.confidence += 10; grBrain.rulesFired++; }
      break;
    case 3: // L1.3 GFSK Probe
      if (rec.modulation == 2) { grBrain.confidence += 10; grBrain.rulesFired++; }
      break;
    case 4: // L1.4 MSK Probe
      if (rec.modulation == 3) { grBrain.confidence += 10; grBrain.rulesFired++; }
      break;
    case 5: // L1.5 Modulation Confidence Scoring
      if (grBrain.confidence > 0) grBrain.rulesFired++;
      break;
    case 6: { // L1.6 Deviation Measurement — byte value spread indicates deviation
      uint8_t lo = 255, hi = 0;
      for (int i = 0; i < rec.dataLen && i < 32; i++) {
        if (rec.data[i] < lo) lo = rec.data[i];
        if (rec.data[i] > hi) hi = rec.data[i];
      }
      if (hi - lo > 50) grBrain.rulesFired++;
    } break;
    case 7: { // L1.7 Symbol Rate Estimation — transition count estimates symbol rate
      uint8_t trans = 0;
      for (int i = 1; i < rec.dataLen && i < 32; i++)
        if ((rec.data[i] ^ rec.data[i-1]) > 0x40) trans++;
      if (trans > 4) grBrain.rulesFired++;
    } break;
    case 8: { // L1.8 Baud Rate Lock — consistent transition spacing
      if (rec.dataLen >= 8) {
        uint8_t gaps[7]; uint8_t gc = 0;
        for (int i = 1; i < rec.dataLen && i < 8; i++) {
          int diff = (int)rec.data[i] - (int)rec.data[i-1];
          gaps[gc++] = (diff < 0) ? -diff : diff;
        }
        uint8_t similar = 0;
        for (int i = 1; i < gc; i++) {
          int d = (int)gaps[i] - (int)gaps[0]; if (d < 0) d = -d;
          if (d < 10) similar++;
        }
        if (similar >= gc / 2) grBrain.rulesFired++;
      }
    } break;
    case 9: // L1.9 Modulation Parameter Extraction — aggregate mod results
      if (grBrain.confidence >= 10) { grBrain.rulesFired++; grBrain.confidence += 2; }
      break;
  }
}

// ===== Tier 2: Encoding & Framing (10 stages) ================================
static inline void grBrainT2(uint8_t stage, const GrCaptureRecord& rec) {
  if (rec.dataLen < 2) return;
  switch (stage) {
    case 0: // L2.0 Manchester Decode check
      if (rec.data[0] == 0xAA || rec.data[0] == 0x55) grBrain.rulesFired++;
      break;
    case 1: { // L2.1 NRZ Decode — look for long runs of same byte
      uint8_t maxRun = 1, run = 1;
      for (int i = 1; i < rec.dataLen; i++) {
        if (rec.data[i] == rec.data[i-1]) { run++; if (run > maxRun) maxRun = run; }
        else run = 1;
      }
      if (maxRun >= 4) grBrain.rulesFired++;
    } break;
    case 2: { // L2.2 PWM Decode — alternating high/low byte pattern
      uint8_t altCount = 0;
      for (int i = 1; i < rec.dataLen && i < 16; i++) {
        bool prevHi = rec.data[i-1] > 0x80;
        bool curHi  = rec.data[i]   > 0x80;
        if (prevHi != curHi) altCount++;
      }
      if (altCount >= 6) grBrain.rulesFired++;
    } break;
    case 3: { // L2.3 PPM Decode — position-encoded: check for sparse bit patterns
      uint8_t sparse = 0;
      for (int i = 0; i < rec.dataLen && i < 16; i++) {
        uint8_t bits = __builtin_popcount(rec.data[i]);
        if (bits == 1 || bits == 2) sparse++;
      }
      if (sparse >= 6) grBrain.rulesFired++;
    } break;
    case 4: { // L2.4 Differential Encoding Detection — XOR reveals constant
      if (rec.dataLen >= 4) {
        uint8_t xorVal = rec.data[0] ^ rec.data[1];
        uint8_t matches = 0;
        for (int i = 1; i < rec.dataLen - 1; i++)
          if ((rec.data[i] ^ rec.data[i+1]) == xorVal) matches++;
        if (matches >= rec.dataLen / 2) grBrain.rulesFired++;
      }
    } break;
    case 5: // L2.5 Preamble Pattern Search
      for (int i = 0; i < rec.dataLen - 1; i++) {
        if (rec.data[i] == 0xAA && rec.data[i+1] == 0xAA) {
          grBrain.rulesFired++; break;
        }
      }
      break;
    case 6: { // L2.6 Sync Word Extraction — find a 2-byte pattern repeated
      if (rec.dataLen >= 6) {
        uint16_t sw = ((uint16_t)rec.data[0] << 8) | rec.data[1];
        if (sw != 0x0000 && sw != 0xFFFF) {
          for (int i = 2; i < rec.dataLen - 1; i++) {
            uint16_t w = ((uint16_t)rec.data[i] << 8) | rec.data[i+1];
            if (w == sw) { grBrain.rulesFired++; grBrain.confidence += 3; break; }
          }
        }
      }
    } break;
    case 7: { // L2.7 Packet Length Detection — infer boundaries from zero padding
      int payload = rec.dataLen;
      while (payload > 1 && rec.data[payload - 1] == 0x00) payload--;
      if (payload < rec.dataLen && payload > 0) grBrain.rulesFired++;
    } break;
    case 8: // L2.8 CRC/Checksum Identification
      if (rec.dataLen >= 4) {
        uint8_t sum = 0;
        for (int i = 0; i < rec.dataLen - 1; i++) sum += rec.data[i];
        if (sum == rec.data[rec.dataLen - 1]) grBrain.rulesFired++;
      }
      break;
    case 9: { // L2.9 Whitening/Scrambling Detection — high byte entropy
      uint8_t buckets[4] = {};
      for (int i = 0; i < rec.dataLen && i < 32; i++)
        buckets[rec.data[i] >> 6]++;
      uint8_t used = (buckets[0] > 0) + (buckets[1] > 0) + (buckets[2] > 0) + (buckets[3] > 0);
      if (used == 4 && rec.dataLen >= 8) grBrain.rulesFired++;
    } break;
  }
}

// ===== Tier 3: Protocol Identification (21 stages) ===========================
static uint8_t grT3Hits = 0;

static inline void grBrainT3(uint8_t stage, const GrCaptureRecord& rec) {
  if (stage == 0) grT3Hits = 0;
  if (grT3Hits >= 2) return;
  uint8_t rulesBefore = grBrain.rulesFired;
  float f = rec.freqMHz;
  switch (stage) {
    case 0: // L3.0 LoRa Band Energy Detection
      if (f >= 902.0f && f <= 928.0f) { grBrain.rulesFired++; grBrain.confidence += 5; }
      break;
    case 1: // L3.1 Z-Wave Frame Matching
      if (f >= 908.0f && f <= 916.0f && rec.modulation == 2) grBrain.rulesFired++;
      break;
    case 2: // L3.2 Insteon — 915 MHz, FSK, 4-byte address
      if (f >= 914.0f && f <= 916.0f && rec.modulation == 1 && rec.dataLen >= 4)
        grBrain.rulesFired++;
      break;
    case 3: // L3.3 X10 — 310 MHz OOK, short fixed code
      if (f >= 309.0f && f <= 311.0f && rec.modulation == 0 && rec.dataLen <= 4)
        grBrain.rulesFired++;
      break;
    case 4: // L3.4 Oregon Scientific
      if (f >= 433.0f && f <= 434.0f && rec.modulation == 0 && rec.dataLen >= 8)
        grBrain.rulesFired++;
      break;
    case 5: // L3.5 Honeywell/Ademco
      if (f >= 345.0f && f <= 346.0f && rec.modulation == 0) grBrain.rulesFired++;
      break;
    case 6: // L3.6 DSC Alarm — 433 MHz OOK, contact ID format
      if (f >= 433.0f && f <= 434.0f && rec.modulation == 0 && rec.dataLen >= 5 && rec.dataLen <= 8)
        grBrain.rulesFired++;
      break;
    case 7: // L3.7 GE/Interlogix — 319.5 MHz OOK
      if (f >= 319.0f && f <= 320.0f && rec.modulation == 0 && rec.dataLen >= 4)
        grBrain.rulesFired++;
      break;
    case 8: // L3.8 EV1527/PT2262 fixed code
      if (rec.modulation == 0 && rec.dataLen >= 3 && rec.dataLen <= 6) {
        grBrain.rulesFired++;
        grBrain.fixedCodeVuln = true;
        grBrain.replayVuln = true;
      }
      break;
    case 9: // L3.9 KeeLoq Rolling Code
      if (rec.modulation == 0 && rec.dataLen >= 8 && rec.dataLen <= 12) {
        grBrain.rulesFired++;
        grBrain.confidence += 5;
      }
      break;
    case 10: // L3.10 CAME — 433 MHz OOK, 12-bit fixed code
      if (f >= 433.0f && f <= 434.0f && rec.modulation == 0 && rec.dataLen >= 2 && rec.dataLen <= 3) {
        grBrain.rulesFired++; grBrain.fixedCodeVuln = true;
      }
      break;
    case 11: // L3.11 NICE/FLO — 433 MHz OOK, 12-bit code
      if (f >= 433.0f && f <= 434.0f && rec.modulation == 0 && rec.dataLen >= 2 && rec.dataLen <= 4) {
        grBrain.rulesFired++; grBrain.fixedCodeVuln = true;
      }
      break;
    case 12: // L3.12 Chamberlain/LiftMaster Security+ — 315/390 MHz, rolling
      if ((f >= 314.0f && f <= 316.0f) || (f >= 389.0f && f <= 391.0f)) {
        if (rec.modulation == 0 && rec.dataLen >= 5) grBrain.rulesFired++;
      }
      break;
    case 13: // L3.13 Linear/MegaCode — 318 MHz OOK, 24-bit code
      if (f >= 317.0f && f <= 319.0f && rec.modulation == 0 && rec.dataLen >= 3) {
        grBrain.rulesFired++; grBrain.fixedCodeVuln = true;
      }
      break;
    case 14: // L3.14 Somfy RTS — 433.42 MHz, manchester, rolling
      if (f >= 433.0f && f <= 434.0f && rec.dataLen >= 7) {
        if (rec.data[0] == 0xA7 || rec.data[0] == 0xA4) grBrain.rulesFired++;
      }
      break;
    case 15: // L3.15 Marantec — 868 MHz, digital code
      if (f >= 867.0f && f <= 869.0f && rec.modulation == 0 && rec.dataLen >= 4)
        grBrain.rulesFired++;
      break;
    case 16: // L3.16 Doorbird/Intercom — 868 MHz FSK, longer packets
      if (f >= 867.0f && f <= 869.0f && rec.modulation >= 1 && rec.dataLen >= 10)
        grBrain.rulesFired++;
      break;
    case 17: // L3.17 TPMS
      if (f >= 314.0f && f <= 316.0f && rec.modulation == 1) grBrain.rulesFired++;
      if (f >= 433.0f && f <= 434.0f && rec.dataLen >= 8 && rec.dataLen <= 15)
        grBrain.rulesFired++;
      break;
    case 18: // L3.18 LoRaWAN MAC Frame — 902-928 MHz, long packets, MHDR byte
      if (f >= 902.0f && f <= 928.0f && rec.dataLen >= 12) {
        uint8_t mtype = (rec.data[0] >> 5) & 0x07;
        if (mtype <= 5) { grBrain.rulesFired++; grBrain.confidence += 5; }
      }
      break;
    case 19: // L3.19 Sigfox Band Energy — 868 MHz ultra-narrowband
      if (f >= 868.0f && f <= 868.6f && rec.dataLen >= 2 && rec.dataLen <= 12)
        grBrain.rulesFired++;
      break;
    case 20: { // L3.20 Custom/Proprietary Fingerprinting — unique byte patterns
      if (rec.dataLen >= 4) {
        uint32_t fp = ((uint32_t)rec.data[0] << 24) | ((uint32_t)rec.data[1] << 16) |
                      ((uint32_t)rec.data[2] << 8)  | rec.data[3];
        if (fp != 0x00000000 && fp != 0xFFFFFFFF) grBrain.rulesFired++;
      }
    } break;
  }
  if (grBrain.rulesFired > rulesBefore) grT3Hits++;
}

// ===== Tier 4: Device Classification (40 stages) =============================
static inline void grBrainT4(uint8_t stage, const GrCaptureRecord& rec) {
  (void)rec;
  // Tier 4 classification is handled by grClassify() in gr_sort.h.
  // These stages add refinement: each stage checks for a specific device
  // type and increments confidence if matched. L4.0-L4.39 per Section 04.
  if (stage < 40) {
    if (rec.deviceType == stage || (stage == 39 && rec.deviceType == GR_DEV_UNKNOWN)) {
      grBrain.rulesFired++;
      grBrain.confidence += 3;
    }
  }
}

// ===== Tier 5: Security Analysis (15 stages) =================================
static inline void grBrainT5(uint8_t stage, const GrCaptureRecord& rec) {
  switch (stage) {
    case 0: // L5.0 Fixed Code Detection
      if (rec.modulation == 0 && rec.dataLen <= 6) {
        grBrain.fixedCodeVuln = true;
        grBrain.securityRating = 20;
        grBrain.rulesFired++;
      }
      break;
    case 1: // L5.1 Rolling Code Detection
      if (rec.dataLen >= 8 && rec.dataLen <= 12 && rec.modulation == 0) {
        grBrain.securityRating = 60;
        grBrain.rulesFired++;
      }
      break;
    case 2: { // L5.2 Rolling Code Counter Extraction — look for incrementing bytes
      if (rec.dataLen >= 8) {
        bool ascending = true;
        for (int i = 5; i < rec.dataLen && i < 8; i++)
          if (rec.data[i] <= rec.data[i-1]) { ascending = false; break; }
        if (ascending) { grBrain.rulesFired++; grBrain.securityRating += 10; }
      }
    } break;
    case 3: // L5.3 Rolling Code Prediction — sequential counter implies predictable
      if (rec.dataLen >= 8 && grBrain.securityRating >= 60) {
        uint8_t low2 = rec.data[rec.dataLen - 1];
        if (low2 < 10) grBrain.rulesFired++;
      }
      break;
    case 4: // L5.4 Encryption Detection
      if (rec.dataLen >= 16) {
        uint8_t entropy = 0;
        for (int i = 0; i < rec.dataLen && i < 32; i++)
          entropy += __builtin_popcount(rec.data[i]);
        if (entropy > rec.dataLen * 3) {
          grBrain.securityRating += 20;
          grBrain.rulesFired++;
        }
      }
      break;
    case 5: { // L5.5 Encryption Strength Assessment — bit entropy depth
      if (rec.dataLen >= 16) {
        uint8_t byteDist[4] = {};
        for (int i = 0; i < rec.dataLen && i < 32; i++) byteDist[rec.data[i] >> 6]++;
        uint8_t spread = (byteDist[0]>0) + (byteDist[1]>0) + (byteDist[2]>0) + (byteDist[3]>0);
        if (spread == 4) { grBrain.securityRating += 15; grBrain.rulesFired++; }
      }
    } break;
    case 6: // L5.6 Jamming Vulnerability
      grBrain.jamVuln = true;
      grBrain.rulesFired++;
      break;
    case 7: // L5.7 Relay Attack Feasibility — short fixed codes are relayable
      if (grBrain.fixedCodeVuln && rec.dataLen <= 8) {
        grBrain.rulesFired++;
        grBrain.securityRating -= 10;
        if (grBrain.securityRating > 200) grBrain.securityRating = 0;
      }
      break;
    case 8: // L5.8 Replay Attack Feasibility
      if (grBrain.fixedCodeVuln) {
        grBrain.replayVuln = true;
        grBrain.rulesFired++;
      }
      break;
    case 9: // L5.9 Brute Force Feasibility
      if (rec.dataLen <= 3) {
        grBrain.rulesFired++;
      }
      break;
    case 10: // L5.10 De Bruijn Sequence Applicability — short codes can be bruted
      if (rec.dataLen <= 4 && rec.modulation == 0) {
        uint32_t keyspace = 1UL << (rec.dataLen * 8);
        if (keyspace <= 65536) grBrain.rulesFired++;
      }
      break;
    case 11: // L5.11 CVE Database Matching — check SD for known vuln sigs
      if (grSdReady && rec.dataLen >= 4) {
        char path[48];
        snprintf(path, sizeof(path), "/brain/models/cve_%02X%02X.dat",
                 rec.data[0], rec.data[1]);
        if (grSdFileExists(path)) { grBrain.rulesFired++; grBrain.securityRating -= 20; }
      }
      break;
    case 12: { // L5.12 Default Code/Key Detection — common factory codes
      if (rec.dataLen >= 3) {
        bool allSame = true;
        for (int i = 1; i < rec.dataLen && i < 6; i++)
          if (rec.data[i] != rec.data[0]) { allSame = false; break; }
        if (allSame) { grBrain.rulesFired++; grBrain.securityRating -= 15; }
      }
    } break;
    case 13: // L5.13 Cloning Feasibility Score
      if (grBrain.fixedCodeVuln && rec.dataLen <= 6) {
        grBrain.rulesFired++;
        grBrain.securityRating -= 10;
        if (grBrain.securityRating > 200) grBrain.securityRating = 0;
      }
      break;
    case 14: { // L5.14 Manufacturer Security Rating — high security if encrypted+rolling
      if (grBrain.securityRating >= 70 && !grBrain.fixedCodeVuln && !grBrain.replayVuln)
        grBrain.confidence += 5;
      grBrain.rulesFired++;
    } break;
  }
}

// ===== Tier 6: Behavioral Analysis (10 stages) ===============================
static uint32_t grBehavLastSeen[4]     = {};
static uint16_t grBehavSignalCount     = 0;
static uint8_t  grBehavDevTypeSeen[25] = {};
static uint16_t grBehavFreqHist[4]     = {};  // 315/433/868/915
static uint8_t  grBehavRadioHist[4]    = {};
static uint32_t grBehavFirstSeen       = 0;
static uint8_t  grBehavLastDevType     = 0xFF;

static inline void grBrainT6(uint8_t stage, const GrCaptureRecord& rec) {
  switch (stage) {
    case 0: // L6.0 Signal Timing Pattern
      if (rec.radioId < 4) {
        uint32_t gap = rec.timestamp - grBehavLastSeen[rec.radioId];
        grBehavLastSeen[rec.radioId] = rec.timestamp;
        if (gap < 100) grBrain.rulesFired++;
      }
      break;
    case 1: { // L6.1 Usage Pattern Recognition — same device type repeating
      if (rec.deviceType < 25) {
        grBehavDevTypeSeen[rec.deviceType]++;
        if (grBehavDevTypeSeen[rec.deviceType] >= 5) grBrain.rulesFired++;
      }
    } break;
    case 2: // L6.2 Device Proximity Estimation — strong signal = close
      if (grT0Energy > 30) { grBrain.rulesFired++; grBrain.confidence += 2; }
      break;
    case 3: { // L6.3 Movement Detection — device type changed since last
      if (grBehavLastDevType != 0xFF && rec.deviceType != grBehavLastDevType)
        grBrain.rulesFired++;
      grBehavLastDevType = rec.deviceType;
    } break;
    case 4: { // L6.4 Device Grouping — multiple radios seeing signals together
      uint8_t activeRadios = 0;
      for (int i = 0; i < 4; i++)
        if (rec.timestamp - grBehavLastSeen[i] < 2000) activeRadios++;
      if (activeRadios >= 2) grBrain.rulesFired++;
    } break;
    case 5: { // L6.5 Environment Mapping — track freq band usage
      uint8_t band = 1;
      if (rec.freqMHz < 316.0f) band = 0;
      else if (rec.freqMHz > 867.0f && rec.freqMHz < 870.0f) band = 2;
      else if (rec.freqMHz > 914.0f) band = 3;
      if (grBehavFreqHist[band] < 255) grBehavFreqHist[band]++;
      uint8_t bandsUsed = (grBehavFreqHist[0]>0) + (grBehavFreqHist[1]>0) +
                          (grBehavFreqHist[2]>0) + (grBehavFreqHist[3]>0);
      if (bandsUsed >= 2) grBrain.rulesFired++;
    } break;
    case 6: // L6.6 New Device Detection
      grBehavSignalCount++;
      if (grBehavSignalCount == 1) { grBehavFirstSeen = rec.timestamp; grBrain.rulesFired++; }
      break;
    case 7: { // L6.7 Missing Device Detection — radio went silent
      for (int i = 0; i < 4; i++) {
        if (grBehavLastSeen[i] > 0 && rec.timestamp - grBehavLastSeen[i] > 30000) {
          grBrain.rulesFired++; break;
        }
      }
    } break;
    case 8: // L6.8 Frequency Usage Histogram — per-radio activity
      if (rec.radioId < 4) {
        if (grBehavRadioHist[rec.radioId] < 255) grBehavRadioHist[rec.radioId]++;
        grBrain.rulesFired++;
      }
      break;
    case 9: { // L6.9 Channel Occupancy Heat Map — busiest band detection
      uint16_t busiest = 0;
      for (int i = 0; i < 4; i++)
        if (grBehavFreqHist[i] > busiest) busiest = grBehavFreqHist[i];
      if (busiest >= 10) grBrain.rulesFired++;
    } break;
  }
}

// ===== Tier 7: Cross-Signal Intelligence (10 stages) =========================
#define GR_T7_RING_SZ 8
struct GrT7Entry { float freq; uint8_t radio; uint8_t devType; uint32_t ts; uint8_t sig[4]; };
static GrT7Entry grT7Ring[GR_T7_RING_SZ] = {};
static uint8_t   grT7Idx = 0;
static uint8_t   grT7DupHash[16] = {};

static inline void grBrainT7(uint8_t stage, const GrCaptureRecord& rec) {
  switch (stage) {
    case 0: { // L7.0 Multi-device Correlation — same device on multiple radios
      for (int i = 0; i < GR_T7_RING_SZ; i++) {
        if (grT7Ring[i].ts > 0 && grT7Ring[i].radio != rec.radioId &&
            grT7Ring[i].devType == rec.deviceType &&
            rec.timestamp - grT7Ring[i].ts < 5000) {
          grBrain.rulesFired++; grBrain.confidence += 5; break;
        }
      }
    } break;
    case 1: { // L7.1 Timing Correlation — signals within 500ms window
      uint8_t correlated = 0;
      for (int i = 0; i < GR_T7_RING_SZ; i++)
        if (grT7Ring[i].ts > 0 && rec.timestamp - grT7Ring[i].ts < 500) correlated++;
      if (correlated >= 2) grBrain.rulesFired++;
    } break;
    case 2: { // L7.2 Frequency Hopping Pattern — different freqs in short time
      uint8_t freqs = 0;
      float seen[4] = {};
      for (int i = 0; i < GR_T7_RING_SZ; i++) {
        if (grT7Ring[i].ts == 0 || rec.timestamp - grT7Ring[i].ts > 3000) continue;
        bool dup = false;
        for (int j = 0; j < freqs; j++) {
          float d = seen[j] - grT7Ring[i].freq; if (d < 0) d = -d;
          if (d < 0.5f) { dup = true; break; }
        }
        if (!dup && freqs < 4) seen[freqs++] = grT7Ring[i].freq;
      }
      if (freqs >= 3) { grBrain.rulesFired++; grBrain.confidence += 5; }
    } break;
    case 3: { // L7.3 Multi-radio Triangulation — count active radios in window
      uint8_t radios = 0;
      bool rSeen[4] = {};
      for (int i = 0; i < GR_T7_RING_SZ; i++) {
        if (grT7Ring[i].ts > 0 && rec.timestamp - grT7Ring[i].ts < 5000 &&
            grT7Ring[i].radio < 4)
          rSeen[grT7Ring[i].radio] = true;
      }
      for (int i = 0; i < 4; i++) if (rSeen[i]) radios++;
      if (radios >= 3) grBrain.rulesFired++;
    } break;
    case 4: { // L7.4 Signal Chain Detection — sequential device types
      if (grT7Idx >= 2) {
        uint8_t prev = grT7Ring[(grT7Idx - 1) % GR_T7_RING_SZ].devType;
        uint8_t prev2 = grT7Ring[(grT7Idx - 2) % GR_T7_RING_SZ].devType;
        if (prev != rec.deviceType && prev2 != prev && prev2 != rec.deviceType)
          grBrain.rulesFired++;
      }
    } break;
    case 5: { // L7.5 Interference Mapping — overlapping freq on different radios
      for (int i = 0; i < GR_T7_RING_SZ; i++) {
        if (grT7Ring[i].ts > 0 && grT7Ring[i].radio != rec.radioId) {
          float d = rec.freqMHz - grT7Ring[i].freq; if (d < 0) d = -d;
          if (d < 1.0f) { grBrain.rulesFired++; break; }
        }
      }
    } break;
    case 6: { // L7.6 Optimal Frequency Assignment — least-used radio
      uint8_t minUse = 255; uint8_t minRadio = 0;
      for (int i = 0; i < 4; i++) {
        if (grBehavRadioHist[i] < minUse) { minUse = grBehavRadioHist[i]; minRadio = i; }
      }
      if (rec.radioId == minRadio) grBrain.rulesFired++;
    } break;
    case 7: // L7.7 Priority Queue Optimization — vuln signals get priority
      if (grBrain.fixedCodeVuln || grBrain.replayVuln)
        { grBrain.rulesFired++; grBrain.confidence += 5; }
      break;
    case 8: { // L7.8 Duplicate Signal Suppression — hash first 4 bytes
      uint8_t h = 0;
      for (int i = 0; i < 4 && i < rec.dataLen; i++) h ^= rec.data[i];
      h &= 0x0F;
      uint8_t sig = (rec.radioId << 4) | (rec.modulation & 0x0F);
      if (grT7DupHash[h] == sig) grBrain.rulesFired++;
      grT7DupHash[h] = sig;
    } break;
    case 9: // L7.9 Cross-session Pattern Matching — store entry to ring
      grT7Ring[grT7Idx % GR_T7_RING_SZ] = {
        rec.freqMHz, rec.radioId, rec.deviceType, rec.timestamp,
        {rec.data[0], (uint8_t)(rec.dataLen > 1 ? rec.data[1] : 0),
         (uint8_t)(rec.dataLen > 2 ? rec.data[2] : 0),
         (uint8_t)(rec.dataLen > 3 ? rec.data[3] : 0)}
      };
      grT7Idx++;
      grBrain.rulesFired++;
      break;
  }
}

// ===== Tier 8: Knowledge & Memory (10 stages) ================================
static uint16_t grT8SuccessCount = 0;
static uint16_t grT8FailCount    = 0;
static uint8_t  grT8AnomalyCount = 0;

static inline void grBrainT8(uint8_t stage, const GrCaptureRecord& rec) {
  switch (stage) {
    case 0: // L8.0 Signal Fingerprint Database — store fingerprint to SD
      if (grSdReady && rec.dataLen >= 4) {
        char path[48];
        snprintf(path, sizeof(path), "/brain/knowledge/fp_%02X%02X%02X%02X.dat",
                 rec.data[0], rec.data[1], rec.data[2], rec.data[3]);
        if (!grSdFileExists(path)) {
          char line[80];
          snprintf(line, sizeof(line), "%.2f,%d,%d,%d,%lu",
                   rec.freqMHz, rec.modulation, rec.dataLen, rec.deviceType,
                   (unsigned long)rec.timestamp);
          grSdAppendLine(path, line);
          grBrain.rulesFired++;
        }
      }
      break;
    case 1: // L8.1 Device Profile Database — log device type stats
      if (grSdReady && rec.classified && rec.deviceType < 25) {
        char line[64];
        snprintf(line, sizeof(line), "%lu,%d,%.2f,%d",
                 (unsigned long)rec.timestamp, rec.deviceType,
                 rec.freqMHz, rec.dataLen);
        grSdAppendLine("/brain/knowledge/device_profiles.csv", line);
        grBrain.rulesFired++;
      }
      break;
    case 2: // L8.2 Episode Memory — log to SD
      if (grSdReady && rec.classified) {
        char path[48];
        snprintf(path, sizeof(path), "/brain/episodes/ep_%lu.log",
                 (unsigned long)(rec.timestamp / 60000));
        File f = SD.open(path, FILE_APPEND);
        if (f) {
          f.printf("%lu,%.2f,%d,%d,%d\n", (unsigned long)rec.timestamp,
                   rec.freqMHz, rec.radioId, rec.modulation, rec.deviceType);
          f.close();
          grBrain.rulesFired++;
        }
      }
      break;
    case 3: // L8.3 Success/Failure Tracking
      if (rec.classified && rec.deviceType != GR_DEV_UNKNOWN) grT8SuccessCount++;
      else grT8FailCount++;
      grBrain.rulesFired++;
      break;
    case 4: { // L8.4 Model Weight Updates — adjust confidence based on success rate
      uint16_t total = grT8SuccessCount + grT8FailCount;
      if (total >= 10) {
        uint8_t rate = (grT8SuccessCount * 100) / total;
        if (rate >= 80) grBrain.confidence += 5;
        else if (rate < 40) grBrain.confidence = (grBrain.confidence > 5) ? grBrain.confidence - 5 : 0;
        grBrain.rulesFired++;
      }
    } break;
    case 5: // L8.5 Anomaly History — log unusual signals to SD
      if (grSdReady && rec.deviceType == GR_DEV_UNKNOWN && rec.dataLen > 20) {
        grT8AnomalyCount++;
        char line[64];
        snprintf(line, sizeof(line), "%lu,%.2f,%d,%d,len=%d",
                 (unsigned long)rec.timestamp, rec.freqMHz,
                 rec.radioId, rec.modulation, rec.dataLen);
        grSdAppendLine("/brain/knowledge/anomalies.csv", line);
        grBrain.rulesFired++;
      }
      break;
    case 6: // L8.6 Frequency Profile Library — log freq usage to SD
      if (grSdReady && grBrain.signalsAnalyzed % 50 == 0 && grBrain.signalsAnalyzed > 0) {
        char line[80];
        snprintf(line, sizeof(line), "%lu,315=%d,433=%d,868=%d,915=%d",
                 (unsigned long)rec.timestamp,
                 grBehavFreqHist[0], grBehavFreqHist[1],
                 grBehavFreqHist[2], grBehavFreqHist[3]);
        grSdAppendLine("/brain/knowledge/freq_profile.csv", line);
        grBrain.rulesFired++;
      }
      break;
    case 7: // L8.7 Signal Template Library — store first 8 bytes as template
      if (grSdReady && rec.classified && rec.dataLen >= 8 &&
          rec.deviceType != GR_DEV_UNKNOWN) {
        char path[48];
        snprintf(path, sizeof(path), "/brain/models/tmpl_dev%d.dat", rec.deviceType);
        if (!grSdFileExists(path))
          grSdWriteFile(path, rec.data, 8);
        grBrain.rulesFired++;
      }
      break;
    case 8: // L8.8 Attack Playbook Storage — log vuln signals
      if (grSdReady && (grBrain.fixedCodeVuln || grBrain.replayVuln)) {
        char line[80];
        snprintf(line, sizeof(line), "%lu,%.2f,fixed=%d,replay=%d,jam=%d,sec=%d",
                 (unsigned long)rec.timestamp, rec.freqMHz,
                 grBrain.fixedCodeVuln, grBrain.replayVuln,
                 grBrain.jamVuln, grBrain.securityRating);
        grSdAppendLine("/brain/knowledge/attack_playbook.csv", line);
        grBrain.rulesFired++;
      }
      break;
    case 9: { // L8.9 Manufacturer Database — match known byte prefixes
      if (rec.dataLen >= 3) {
        uint32_t prefix = ((uint32_t)rec.data[0] << 16) |
                          ((uint32_t)rec.data[1] << 8) | rec.data[2];
        if (prefix == 0xD391D3 || prefix == 0xAABBCC || prefix == 0x555555)
          grBrain.rulesFired++;
      }
    } break;
  }
}

// ===== Tier 9: Decision & Action (12 stages) =================================
static uint8_t  grT9ThreatScore   = 0;
static uint8_t  grT9AlertLevel    = 0;
static uint32_t grT9LastAlertMs   = 0;
static uint32_t grT9LastSweepMs   = 0;

static inline void grBrainT9(uint8_t stage, const GrCaptureRecord& rec) {
  switch (stage) {
    case 0: { // L9.0 Threat Priority Scoring
      grT9ThreatScore = 0;
      if (grBrain.fixedCodeVuln) grT9ThreatScore += 30;
      if (grBrain.replayVuln)    grT9ThreatScore += 20;
      if (grBrain.jamVuln)       grT9ThreatScore += 10;
      if (grBrain.securityRating < 30) grT9ThreatScore += 20;
      if (grT9ThreatScore > 0) grBrain.rulesFired++;
    } break;
    case 1: // L9.1 Capture Priority Queue — flag high-value captures
      if (grT9ThreatScore >= 30 || rec.deviceType == GR_DEV_ACCESS_CONTROL ||
          rec.deviceType == GR_DEV_CAR_FOB || rec.deviceType == GR_DEV_ALARM_PANEL)
        { grBrain.rulesFired++; grBrain.confidence += 5; }
      break;
    case 2: { // L9.2 Radio Assignment Optimizer — check if radio matches band
      bool optimal = false;
      if (rec.radioId == 0 && rec.freqMHz >= 314.0f && rec.freqMHz <= 316.0f) optimal = true;
      if (rec.radioId == 1 && rec.freqMHz >= 433.0f && rec.freqMHz <= 434.0f) optimal = true;
      if (rec.radioId == 2 && rec.freqMHz >= 867.0f && rec.freqMHz <= 870.0f) optimal = true;
      if (rec.radioId == 3 && rec.freqMHz >= 914.0f && rec.freqMHz <= 916.0f) optimal = true;
      if (optimal) grBrain.rulesFired++;
    } break;
    case 3: // L9.3 Radio Sweep Scheduler — suggest sweep if gap > 60s
      if (rec.timestamp - grT9LastSweepMs > 60000) {
        grT9LastSweepMs = rec.timestamp;
        grBrain.rulesFired++;
      }
      break;
    case 4: // L9.4 Auto-replay Decision — only fixed codes are replayable
      if (grBrain.fixedCodeVuln && rec.dataLen >= 3 && rec.dataLen <= 6)
        grBrain.rulesFired++;
      break;
    case 5: // L9.5 Alert Generation — high threat or deauth storm
      if (grT9ThreatScore >= 50 || grBbStormActive) {
        if (rec.timestamp - grT9LastAlertMs > 10000) {
          grT9AlertLevel = (grT9ThreatScore >= 70) ? 3 : (grT9ThreatScore >= 50) ? 2 : 1;
          grT9LastAlertMs = rec.timestamp;
          grBrain.rulesFired++;
        }
      }
      break;
    case 6: // L9.6 Display Update Priority — security findings force refresh
      if (grBrain.securityRating < 30 || grT9ThreatScore >= 40)
        grBrain.rulesFired++;
      break;
    case 7: // L9.7 Storage Management — warn if SD space low
      if (grSdFreeMb < 100) grBrain.rulesFired++;
      break;
    case 8: // L9.8 Behavior Mode Switching — tracker or deauth → defensive
      if (grBbTrackers > 0 || grBbAttackSeverity >= 3)
        grBrain.rulesFired++;
      break;
    case 9: // L9.9 Multi-node Task Distribution — check node availability
      if (grPeer0.up && grPeer1.up && grPeer3.up)
        grBrain.rulesFired++;
      break;
    case 10: // L9.10 Sorting Queue Trigger — trigger sort if queue deep enough
      if (grSortQueueDepth >= 5 && !grSortActive)
        grBrain.rulesFired++;
      break;
    case 11: { // L9.11 Self-healing — detect heap pressure
      uint32_t freeHeap = ESP.getFreeHeap();
      if (freeHeap < 30000) grBrain.rulesFired++;
    } break;
  }
}

// ===== Tier 10: Sorting Queue Intelligence (10 stages) =======================
static uint16_t grT10Ingested   = 0;
static uint16_t grT10Dupes      = 0;
static uint8_t  grT10LastHash[8] = {};

static inline void grBrainT10(uint8_t stage, const GrCaptureRecord& rec) {
  switch (stage) {
    case 0: // L10.0 Queue Ingestion — count items entering the pipeline
      grT10Ingested++;
      grBrain.rulesFired++;
      break;
    case 1: { // L10.1 Quick-scan Triage — fast reject: too short or all zeros
      if (rec.dataLen < 2) break;
      bool allZero = true;
      for (int i = 0; i < rec.dataLen && i < 8; i++)
        if (rec.data[i] != 0) { allZero = false; break; }
      if (!allZero) grBrain.rulesFired++;
    } break;
    case 2: // L10.2 Deep Classification — verify Tier 3+4 results
      if (rec.classified && rec.deviceType != GR_DEV_UNKNOWN)
        { grBrain.rulesFired++; grBrain.confidence += 3; }
      break;
    case 3: // L10.3 Meta File Generation — build meta for classified signals
      if (grSdReady && rec.classified) {
        char line[80];
        snprintf(line, sizeof(line), "%lu,dev=%d,freq=%.2f,mod=%d,len=%d,sec=%d",
                 (unsigned long)rec.timestamp, rec.deviceType,
                 rec.freqMHz, rec.modulation, rec.dataLen, grBrain.securityRating);
        grSdAppendLine("/brain/sorting_queue/meta.csv", line);
        grBrain.rulesFired++;
      }
      break;
    case 4: { // L10.4 Folder Routing — determine target directory
      const char* folder = "unidentified";
      if (rec.deviceType == GR_DEV_GARAGE_FIXED || rec.deviceType == GR_DEV_GARAGE_ROLLING)
        folder = "garage_door";
      else if (rec.deviceType == GR_DEV_CAR_FOB) folder = "car_fob";
      else if (rec.deviceType == GR_DEV_WEATHER) folder = "weather";
      else if (rec.deviceType == GR_DEV_ALARM_SENSOR || rec.deviceType == GR_DEV_ALARM_PANEL)
        folder = "alarm";
      else if (rec.deviceType == GR_DEV_TPMS) folder = "tpms";
      else if (rec.deviceType == GR_DEV_DOORBELL) folder = "doorbell";
      if (folder[0] != 'u') grBrain.rulesFired++;
      (void)folder;
    } break;
    case 5: { // L10.5 Duplicate Check — hash first 4 bytes + freq
      uint8_t h = (uint8_t)((int)(rec.freqMHz * 10.0f) & 0x07);
      for (int i = 0; i < 4 && i < rec.dataLen; i++) h ^= rec.data[i];
      bool found = false;
      for (int i = 0; i < 8; i++) {
        if (grT10LastHash[i] == h) { found = true; grT10Dupes++; break; }
      }
      if (!found) {
        memmove(grT10LastHash + 1, grT10LastHash, 7);
        grT10LastHash[0] = h;
        grBrain.rulesFired++;
      }
    } break;
    case 6: // L10.6 Knowledge Base Update — update counts
      if (rec.classified && rec.deviceType < 25) {
        if (grBehavDevTypeSeen[rec.deviceType] < 255)
          grBehavDevTypeSeen[rec.deviceType]++;
        grBrain.rulesFired++;
      }
      break;
    case 7: // L10.7 Episode Journaling — per-sort-burst log
      if (grSdReady && grSortActive) {
        char line[48];
        snprintf(line, sizeof(line), "%lu,sorted=%d,dupes=%d",
                 (unsigned long)rec.timestamp, grSortItemsSorted, grT10Dupes);
        grSdAppendLine("/brain/episodes/sort_journal.csv", line);
        grBrain.rulesFired++;
      }
      break;
    case 8: // L10.8 Queue Depth Monitor — warn if queue growing
      if (grSortQueueDepth > 20) grBrain.rulesFired++;
      break;
    case 9: // L10.9 Opportunistic Processing — process if idle
      if (!grSortActive && grSortQueueDepth > 0 &&
          rec.timestamp - grSortLastCapture > 2000)
        grBrain.rulesFired++;
      break;
  }
}

// ===== Tier 11: Predictive Intelligence (10 stages) ==========================
#define GR_T11_HIST 8
static float    grT11FreqHist[GR_T11_HIST]  = {};
static uint32_t grT11TimeHist[GR_T11_HIST]  = {};
static uint8_t  grT11DevHist[GR_T11_HIST]   = {};
static uint8_t  grT11HistIdx                = 0;
static float    grT11PredictedFreq          = 0.0f;
static uint32_t grT11PredictedGapMs         = 0;

static inline void grBrainT11(uint8_t stage, const GrCaptureRecord& rec) {
  switch (stage) {
    case 0: { // L11.0 Next-signal Prediction — predict freq from history
      if (grT11HistIdx >= 3) {
        float sum = 0; uint8_t n = 0;
        for (int i = 0; i < GR_T11_HIST && i < grT11HistIdx; i++) { sum += grT11FreqHist[i]; n++; }
        grT11PredictedFreq = sum / n;
        float d = rec.freqMHz - grT11PredictedFreq; if (d < 0) d = -d;
        if (d < 1.0f) { grBrain.rulesFired++; grBrain.confidence += 3; }
      }
      grT11FreqHist[grT11HistIdx % GR_T11_HIST] = rec.freqMHz;
    } break;
    case 1: // L11.1 Pre-tune Radio — if predicted freq matches a band, flag it
      if (grT11PredictedFreq > 0.0f) {
        if ((grT11PredictedFreq >= 314.0f && grT11PredictedFreq <= 316.0f) ||
            (grT11PredictedFreq >= 433.0f && grT11PredictedFreq <= 434.0f) ||
            (grT11PredictedFreq >= 867.0f && grT11PredictedFreq <= 870.0f) ||
            (grT11PredictedFreq >= 914.0f && grT11PredictedFreq <= 916.0f))
          grBrain.rulesFired++;
      }
      break;
    case 2: { // L11.2 Activity Window Prediction — estimate inter-capture gap
      if (grT11HistIdx >= 2) {
        uint8_t prev = (grT11HistIdx - 1) % GR_T11_HIST;
        if (grT11TimeHist[prev] > 0) {
          grT11PredictedGapMs = rec.timestamp - grT11TimeHist[prev];
          grBrain.rulesFired++;
        }
      }
      grT11TimeHist[grT11HistIdx % GR_T11_HIST] = rec.timestamp;
    } break;
    case 3: { // L11.3 Device Schedule Learning — detect periodic device type
      if (grT11HistIdx >= 4) {
        uint8_t matches = 0;
        for (int i = 0; i < GR_T11_HIST && i < grT11HistIdx; i++)
          if (grT11DevHist[i] == rec.deviceType) matches++;
        if (matches >= 3) { grBrain.rulesFired++; grBrain.confidence += 2; }
      }
      grT11DevHist[grT11HistIdx % GR_T11_HIST] = rec.deviceType;
    } break;
    case 4: { // L11.4 Coverage Gap Detection — find unused bands
      uint8_t gaps = 0;
      for (int i = 0; i < 4; i++)
        if (grBehavFreqHist[i] == 0) gaps++;
      if (gaps > 0) grBrain.rulesFired++;
    } break;
    case 5: // L11.5 Scan Plan Generation — recommend scan based on gaps
      if (grBehavFreqHist[0] == 0 || grBehavFreqHist[2] == 0 || grBehavFreqHist[3] == 0)
        grBrain.rulesFired++;
      break;
    case 6: // L11.6 Threat Forecasting — escalating deauth = incoming attack
      if (grBbDeauthPerMin > 10 || grBbAttackSeverity >= 3) {
        grBrain.rulesFired++;
        if (grBbAttackSeverity >= 4) grBrain.confidence += 5;
      }
      break;
    case 7: { // L11.7 Resource Forecasting — estimate SD fill rate
      if (grSdFreeMb > 0 && grT10Ingested > 0) {
        uint32_t bytesPerCapture = 216 + 80;
        uint32_t capturesLeft = ((uint32_t)grSdFreeMb * 1024 * 1024) / bytesPerCapture;
        if (capturesLeft < 10000) grBrain.rulesFired++;
      }
    } break;
    case 8: // L11.8 Auto-purge Suggestion — low space + old data
      if (grSdFreeMb < 50 && grBrain.signalsAnalyzed > 100)
        grBrain.rulesFired++;
      break;
    case 9: // L11.9 Mission Planning — store prediction state, advance index
      grT11HistIdx++;
      if (grT11PredictedGapMs > 0 && grT11PredictedFreq > 0.0f)
        grBrain.rulesFired++;
      break;
  }
}

// ===== Tier 12: Multi-Node Coordination (8 stages) ===========================
static uint8_t grT12NodesUp     = 0;
static uint8_t grT12DegradeMode = 0;

static inline void grBrainT12(uint8_t stage, const GrCaptureRecord& rec) {
  switch (stage) {
    case 0: { // L12.0 Node Health Aggregation — count live peers
      grT12NodesUp = 0;
      if (grPeer0.up) grT12NodesUp++;
      if (grPeer1.up) grT12NodesUp++;
      if (grPeer3.up) grT12NodesUp++;
      if (grT12NodesUp >= 2) grBrain.rulesFired++;
    } break;
    case 1: // L12.1 Load Balancing — check if N0 is attacking (high load)
      if (grPeer0.up && grPeer0.attacking) {
        grBrain.rulesFired++;
      }
      break;
    case 2: // L12.2 ESP-NOW Channel Optimization — mesh on ch1, check for congestion
      if (grBbBusiestCh == 1) grBrain.rulesFired++;
      break;
    case 3: // L12.3 Packet Priority Queuing — high-threat → priority relay
      if (grT9ThreatScore >= 40 && grPeer0.up)
        grBrain.rulesFired++;
      break;
    case 4: { // L12.4 Sync Verification — check peer freshness
      uint32_t now = rec.timestamp;
      bool synced = true;
      if (grPeer0.up && now - grPeer0.lastRx > 5000) synced = false;
      if (grPeer1.up && now - grPeer1.lastRx > 5000) synced = false;
      if (grPeer3.up && now - grPeer3.lastRx > 5000) synced = false;
      if (synced && grT12NodesUp > 0) grBrain.rulesFired++;
    } break;
    case 5: // L12.5 Graceful Degradation — adapt when nodes go down
      grT12DegradeMode = (grT12NodesUp < 2) ? 1 : 0;
      if (grT12DegradeMode) {
        grBrain.rulesFired++;
      }
      break;
    case 6: // L12.6 Sort Burst Trigger — coordinate sort when N0 not attacking
      if (grSortQueueDepth >= 3 && grPeer0.up && !grPeer0.attacking && !grSortActive)
        grBrain.rulesFired++;
      break;
    case 7: { // L12.7 Cross-node Deduplication — skip if backbone already flagged
      if (grBbTrackers > 0 && rec.deviceType == GR_DEV_UNKNOWN) {
        grBrain.rulesFired++;
      }
    } break;
  }
}

// ===== Main pipeline dispatch ================================================
static inline void grBrainProcessStage(uint8_t tier, uint8_t stage,
                                        const GrCaptureRecord& rec) {
  switch (tier) {
    case GR_TIER_SIGNAL_ACQ:   grBrainT0(stage, rec); break;
    case GR_TIER_MOD_PROBE:    grBrainT1(stage, rec); break;
    case GR_TIER_ENCODING:     grBrainT2(stage, rec); break;
    case GR_TIER_PROTOCOL_ID:  grBrainT3(stage, rec); break;
    case GR_TIER_DEVICE_CLASS: grBrainT4(stage, rec); break;
    case GR_TIER_SECURITY:     grBrainT5(stage, rec); break;
    case GR_TIER_BEHAVIORAL:   grBrainT6(stage, rec); break;
    case GR_TIER_CROSS_SIGNAL: grBrainT7(stage, rec); break;
    case GR_TIER_KNOWLEDGE:    grBrainT8(stage, rec); break;
    case GR_TIER_DECISION:     grBrainT9(stage, rec); break;
    case GR_TIER_SORT_QUEUE:   grBrainT10(stage, rec); break;
    case GR_TIER_PREDICTIVE:   grBrainT11(stage, rec); break;
    case GR_TIER_MULTI_NODE:   grBrainT12(stage, rec); break;
  }
}

// forward declaration — defined after knowledge persistence block
static inline void grBrainUpdateKnowledge(const GrCaptureRecord& rec);

// ===== Run full pipeline on a capture record =================================
static inline void grBrainAnalyze(GrCaptureRecord& rec) {
  uint32_t startUs = micros();

  grBrain.active = true;
  grBrain.confidence = 0;
  grBrain.rulesFired = 0;
  grBrain.fixedCodeVuln = false;
  grBrain.replayVuln = false;
  grBrain.jamVuln = false;
  grBrain.securityRating = 50;
  grBrain.totalProcessed = 0;

  uint32_t fpH = grFpHash(rec.data, rec.dataLen, rec.freqMHz, rec.modulation);
  GrFpCacheEntry* cached = grFpLookup(fpH);
  uint8_t startTier = 0;

  if (cached && cached->hitCount >= 3) {
    grBrain.confidence    = cached->confidence;
    grBrain.securityRating = cached->securityRating;
    grBrain.fixedCodeVuln = cached->fixedCode;
    grBrain.replayVuln    = cached->replayVuln;
    rec.deviceType = cached->deviceType;
    rec.classified = 1;
    cached->hitCount++;
    grFpCacheHits++;
    startTier = GR_TIER_BEHAVIORAL;
  } else {
    grFpCacheMisses++;
  }

  for (uint8_t tier = startTier; tier < GR_TIER_COUNT; tier++) {
    grBrain.currentTier = tier;

    if (tier == GR_TIER_CROSS_SIGNAL && grBrain.confidence >= 85)
      continue;

    for (uint8_t stage = 0; stage < GR_TIER_STAGES[tier]; stage++) {
      grBrain.currentStage = stage;
      grBrainProcessStage(tier, stage, rec);
      grBrain.totalProcessed++;
    }
  }

  if (grBrain.confidence > 100) grBrain.confidence = 100;
  grBrain.lastDeviceType = rec.deviceType;
  grBrain.signalsAnalyzed++;
  grBrain.processingTimeUs = micros() - startUs;
  grBrain.lastProcessMs = millis();
  grBrain.active = false;

  if (!cached && grBrain.confidence >= 20)
    grFpStore(fpH, rec.deviceType, grBrain.securityRating,
              grBrain.confidence, grBrain.fixedCodeVuln, grBrain.replayVuln);

  grBrainUpdateKnowledge(rec);
}

// ===== SD Knowledge Persistence ==============================================

#define GR_KNOW_MAGIC  0x4B42  // "KB"
#define GR_KNOW_VER    2

#pragma pack(push, 1)
struct GrKnowledgeBlock {
  uint16_t magic;
  uint8_t  version;
  uint32_t totalSignals;
  uint32_t bootCount;
  uint32_t lastSaveMs;

  // Device type histogram (how often each type is seen)
  uint16_t devTypeCounts[25];

  // Per-band signal counts
  uint16_t band315Count;
  uint16_t band433Count;
  uint16_t band868Count;
  uint16_t band915Count;

  // Noise floor baselines per radio
  uint8_t  noiseFloorBaseline[4];

  // Threat statistics
  uint16_t fixedCodeDevices;
  uint16_t replayVulnDevices;
  uint16_t jamVulnDevices;

  // Cross-signal correlation state
  float    topFreqs[8];
  uint8_t  topFreqDevType[8];
  uint16_t topFreqCount[8];

  // Predictive model: common time gaps between signals (ms)
  uint32_t commonGapsMs[8];
  uint16_t gapCounts[8];

  // Behavioral: mode transition counts from Node 1 learner
  uint8_t  learnerAggression;
  uint8_t  learnerSkill;
  uint8_t  learnerRiskTolerance;

  uint8_t  reserved[32];
};
#pragma pack(pop)

static GrKnowledgeBlock grKnowledge = {};
static uint32_t grKnowledgeLastSave = 0;
#define GR_KNOWLEDGE_SAVE_INTERVAL_MS  60000
#define GR_KNOWLEDGE_PATH "/brain/knowledge/brain_state.kb"

static inline bool grBrainLoadKnowledge() {
  if (!grSdReady) return false;
  if (!grSdFileExists(GR_KNOWLEDGE_PATH)) {
    Serial.println(F("[brain] no prior knowledge on SD — starting fresh"));
    return false;
  }

  size_t r = grSdReadFile(GR_KNOWLEDGE_PATH,
                          (uint8_t*)&grKnowledge, sizeof(grKnowledge));
  if (r != sizeof(grKnowledge) ||
      grKnowledge.magic != GR_KNOW_MAGIC ||
      grKnowledge.version != GR_KNOW_VER) {
    Serial.println(F("[brain] knowledge file corrupt/version mismatch — fresh start"));
    memset(&grKnowledge, 0, sizeof(grKnowledge));
    return false;
  }

  grKnowledge.bootCount++;

  // Restore noise floor baselines from prior sessions
  for (int i = 0; i < 4; i++) {
    if (grKnowledge.noiseFloorBaseline[i] > 0 &&
        grKnowledge.noiseFloorBaseline[i] < 255)
      grT0NoiseFloor[i] = grKnowledge.noiseFloorBaseline[i];
  }

  // Restore cross-signal ring from saved top frequencies
  for (int i = 0; i < 8 && i < GR_T7_RING_SZ; i++) {
    if (grKnowledge.topFreqs[i] > 0.0f) {
      grT7Ring[i].freq = grKnowledge.topFreqs[i];
      grT7Ring[i].devType = grKnowledge.topFreqDevType[i];
    }
  }

  // Restore predictive gaps
  for (int i = 0; i < 8 && i < GR_T11_HIST; i++) {
    if (grKnowledge.commonGapsMs[i] > 0)
      grT11TimeHist[i] = grKnowledge.commonGapsMs[i];
  }

  Serial.printf("[brain] loaded knowledge — %lu signals over %lu boots\n",
                (unsigned long)grKnowledge.totalSignals,
                (unsigned long)grKnowledge.bootCount);
  return true;
}

static inline bool grBrainSaveKnowledge() {
  if (!grSdReady) return false;

  grKnowledge.magic = GR_KNOW_MAGIC;
  grKnowledge.version = GR_KNOW_VER;
  grKnowledge.totalSignals = grBrain.signalsAnalyzed + grKnowledge.totalSignals;
  grKnowledge.lastSaveMs = millis();

  // Save noise floor baselines
  for (int i = 0; i < 4; i++)
    grKnowledge.noiseFloorBaseline[i] = grT0NoiseFloor[i];

  // Save cross-signal ring
  for (int i = 0; i < 8 && i < GR_T7_RING_SZ; i++) {
    grKnowledge.topFreqs[i] = grT7Ring[i].freq;
    grKnowledge.topFreqDevType[i] = grT7Ring[i].devType;
  }

  // Save predictive gaps
  for (int i = 0; i < 8 && i < GR_T11_HIST; i++) {
    grKnowledge.commonGapsMs[i] = grT11TimeHist[i];
    grKnowledge.gapCounts[i]++;
  }

  // Save threat tallies
  if (grBrain.fixedCodeVuln)  grKnowledge.fixedCodeDevices++;
  if (grBrain.replayVuln)     grKnowledge.replayVulnDevices++;
  if (grBrain.jamVuln)        grKnowledge.jamVulnDevices++;

  bool ok = grSdWriteFile(GR_KNOWLEDGE_PATH,
                          (const uint8_t*)&grKnowledge, sizeof(grKnowledge));
  if (ok)
    Serial.printf("[brain] knowledge saved — %lu total signals\n",
                  (unsigned long)grKnowledge.totalSignals);
  return ok;
}

// Called after each analysis to update knowledge counts
static inline void grBrainUpdateKnowledge(const GrCaptureRecord& rec) {
  if (rec.classified && rec.deviceType < 25)
    grKnowledge.devTypeCounts[rec.deviceType]++;

  if (rec.freqMHz >= 314.0f && rec.freqMHz <= 316.0f) grKnowledge.band315Count++;
  else if (rec.freqMHz >= 432.0f && rec.freqMHz <= 435.0f) grKnowledge.band433Count++;
  else if (rec.freqMHz >= 867.0f && rec.freqMHz <= 870.0f) grKnowledge.band868Count++;
  else if (rec.freqMHz >= 914.0f && rec.freqMHz <= 916.0f) grKnowledge.band915Count++;

  // Periodic save
  if (millis() - grKnowledgeLastSave > GR_KNOWLEDGE_SAVE_INTERVAL_MS) {
    grKnowledgeLastSave = millis();
    grBrainSaveKnowledge();
  }
}

// ===== Learner state persistence (received from Node 1 via ESP-NOW) ==========

#define GR_LEARNER_PATH "/brain/models/learner_state.dat"

static inline bool grBrainSaveLearnerState(const uint8_t* data, size_t len) {
  if (!grSdReady || len == 0 || len > 4096) return false;
  bool ok = grSdWriteFile(GR_LEARNER_PATH, data, len);
  if (ok) Serial.printf("[brain] learner state saved (%u bytes)\n", (unsigned)len);
  return ok;
}

static inline size_t grBrainLoadLearnerState(uint8_t* buf, size_t maxLen) {
  if (!grSdReady) return 0;
  if (!grSdFileExists(GR_LEARNER_PATH)) return 0;
  size_t r = grSdReadFile(GR_LEARNER_PATH, buf, maxLen);
  if (r > 0) Serial.printf("[brain] learner state loaded (%u bytes)\n", (unsigned)r);
  return r;
}

// ===== Begin (called from setup) =============================================
static inline void grBrainBegin() {
  memset(&grBrain, 0, sizeof(grBrain));
  memset(&grKnowledge, 0, sizeof(grKnowledge));

  grBrainLoadKnowledge();

  // Pre-warm fingerprint cache from stored top-frequency data
  for (int i = 0; i < 8; i++) {
    if (grKnowledge.topFreqs[i] > 0.0f && grKnowledge.topFreqCount[i] >= 3) {
      uint8_t d[6] = {};
      uint32_t h = grFpHash(d, 0, grKnowledge.topFreqs[i], 0);
      grFpStore(h, grKnowledge.topFreqDevType[i], 50, 30, false, false);
    }
  }

  Serial.printf("[brain] deep brain ready — %d tiers, %d stages, cache=%d\n",
                GR_TIER_COUNT, GR_TOTAL_STAGES, GR_FP_CACHE_SIZE);
  if (grKnowledge.bootCount > 0)
    Serial.printf("[brain] prior experience: %lu signals, %lu boots, "
                  "%u fixed-code %u replay-vuln %u jam-vuln devices\n",
                  (unsigned long)grKnowledge.totalSignals,
                  (unsigned long)grKnowledge.bootCount,
                  grKnowledge.fixedCodeDevices,
                  grKnowledge.replayVulnDevices,
                  grKnowledge.jamVulnDevices);
}
