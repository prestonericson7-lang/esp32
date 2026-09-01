#pragma once
// ############################################################################
//  gg_cc1101_tools.h  -  GHOST TX CC1101 EDITION  /  sub-GHz tools + attacks
//
//  Higher-level tools built on gg_cc1101.h: jammer, recorder, replay,
//  brute force, frequency scanner, RSSI monitor, signal generator.
//  The UI screens in gg_ui.h call these functions.
// ############################################################################

#include <Arduino.h>
#include "gg_config.h"
#include "gg_cc1101.h"

// ===== frequency scanner ===================================================
// Sweeps a frequency range reading RSSI at each step. Results stored in an
// array the UI can render as a bar graph.
#define GG_SCAN_STEPS 64
static int8_t  ggCcScanRssi[GG_SCAN_STEPS];
static float   ggCcScanLoMHz = 433.0f;
static float   ggCcScanHiMHz = 434.8f;
static uint8_t ggCcScanBest = 0;

static inline void ggCcScanInit(uint8_t csn, float loMHz, float hiMHz) {
  ggCcScanLoMHz = loMHz;
  ggCcScanHiMHz = hiMHz;
  memset(ggCcScanRssi, -128, sizeof(ggCcScanRssi));
  ggCcIdle(csn);
}

static inline void ggCcScanPass(uint8_t csn) {
  float step = (ggCcScanHiMHz - ggCcScanLoMHz) / GG_SCAN_STEPS;
  int8_t best = -128; uint8_t bestIdx = 0;
  for (int i = 0; i < GG_SCAN_STEPS; i++) {
    float f = ggCcScanLoMHz + i * step;
    ggCcSetFreq(csn, f);
    ggCcStrobe(csn, CC1101_SRX);
    delayMicroseconds(800);
    int8_t r = ggCcGetRssi(csn);
    ggCcScanRssi[i] = r;
    if (r > best) { best = r; bestIdx = i; }
    ggCcStrobe(csn, CC1101_SIDLE);
  }
  ggCcScanBest = bestIdx;
}

static inline float ggCcScanBestFreq() {
  float step = (ggCcScanHiMHz - ggCcScanLoMHz) / GG_SCAN_STEPS;
  return ggCcScanLoMHz + ggCcScanBest * step;
}

// ===== RSSI monitor (single frequency) =====================================
static int8_t ggCcRssiLive = -128;
static int8_t ggCcRssiPeak = -128;
static int8_t ggCcRssiHistory[128];
static uint8_t ggCcRssiIdx = 0;

static inline void ggCcRssiInit(uint8_t csn, float freqMHz) {
  ggCcSetFreq(csn, freqMHz);
  ggCcStrobe(csn, CC1101_SRX);
  ggCcRssiLive = -128;
  ggCcRssiPeak = -128;
  ggCcRssiIdx = 0;
  memset(ggCcRssiHistory, -128, sizeof(ggCcRssiHistory));
}

static inline void ggCcRssiPoll(uint8_t csn) {
  int8_t r = ggCcGetRssi(csn);
  ggCcRssiLive = r;
  if (r > ggCcRssiPeak) ggCcRssiPeak = r;
  ggCcRssiHistory[ggCcRssiIdx] = r;
  ggCcRssiIdx = (ggCcRssiIdx + 1) % 128;
}

// ===== sub-GHz jammer ======================================================
// Rapidly hops across the target frequency band blasting random data from both
// CC1101s, spreading wideband noise. Similar to the nRF noise generator but on
// sub-GHz frequencies.
static bool    ggCcJamActive = false;
static float   ggCcJamCenterMHz = 433.92f;
static float   ggCcJamWidthMHz = 2.0f;
static uint8_t ggCcJamPosA = 0, ggCcJamPosB = 0;
#define GG_JAM_HOPS 8

static inline void ggCcJamStart(float centerMHz, float widthMHz) {
  ggCcJamCenterMHz = centerMHz;
  ggCcJamWidthMHz = widthMHz;
  ggCcJamPosA = 0;
  ggCcJamPosB = GG_JAM_HOPS / 2;

  GgCcProfile jp{};
  jp.freqMHz = centerMHz;
  jp.modulation = GG_CM_2FSK;
  jp.dataRate = GG_CR_250K;
  jp.power = GG_CP_10;
  jp.plLen = GG_CC_PAYLOAD_MAX;
  jp.syncWord[0] = 0xFF; jp.syncWord[1] = 0xFF;

  if (ggCcPresentA) ggCcInit(GG_PIN_CCA_CSN, jp);
  if (ggCcPresentB) ggCcInit(GG_PIN_CCB_CSN, jp);
  ggCcJamActive = true;
}

static inline void ggCcJamPump() {
  if (!ggCcJamActive) return;
  float step = ggCcJamWidthMHz / GG_JAM_HOPS;

  if (ggCcPresentA) {
    float f = ggCcJamCenterMHz - ggCcJamWidthMHz / 2 + ggCcJamPosA * step;
    ggCcSetFreq(GG_PIN_CCA_CSN, f);
    uint8_t junk[GG_CC_PAYLOAD_MAX];
    for (int i = 0; i < GG_CC_PAYLOAD_MAX; i++) junk[i] = (uint8_t)(esp_random());
    ggCcTxPacket(GG_PIN_CCA_CSN, junk, GG_CC_PAYLOAD_MAX);
    ggCcJamPosA = (ggCcJamPosA + 1) % GG_JAM_HOPS;
  }

  if (ggCcPresentB) {
    float f = ggCcJamCenterMHz - ggCcJamWidthMHz / 2 + ggCcJamPosB * step;
    ggCcSetFreq(GG_PIN_CCB_CSN, f);
    uint8_t junk[GG_CC_PAYLOAD_MAX];
    for (int i = 0; i < GG_CC_PAYLOAD_MAX; i++) junk[i] = (uint8_t)(esp_random());
    ggCcTxPacket(GG_PIN_CCB_CSN, junk, GG_CC_PAYLOAD_MAX);
    ggCcJamPosB = (ggCcJamPosB + 1) % GG_JAM_HOPS;
  }
}

static inline void ggCcJamStop() {
  if (!ggCcJamActive) return;
  if (ggCcPresentA) ggCcIdle(GG_PIN_CCA_CSN);
  if (ggCcPresentB) ggCcIdle(GG_PIN_CCB_CSN);
  ggCcJamActive = false;
}

// ===== signal recorder =====================================================
// Captures packets on a given frequency into a GgCcRecording buffer.
static bool ggCcRecording = false;
static GgCcRecording ggCcRecBuf;
static uint32_t ggCcRecStart = 0;
#define GG_CC_REC_TIMEOUT_MS 10000

static inline void ggCcRecordStart(uint8_t csn, float freqMHz) {
  memset(&ggCcRecBuf, 0, sizeof(ggCcRecBuf));
  ggCcRecBuf.freqMHz = freqMHz;
  ggCcRecBuf.modulation = GG_CM_ASK;
  ggCcRecBuf.dataRate = GG_CR_2K;
  ggCcRecBuf.valid = false;

  GgCcProfile rp{};
  rp.freqMHz = freqMHz;
  rp.modulation = GG_CM_ASK;
  rp.dataRate = GG_CR_2K;
  rp.power = GG_CP_0;
  rp.plLen = 32;
  rp.syncWord[0] = 0xD3; rp.syncWord[1] = 0x91;
  ggCcInit(csn, rp);
  ggCcRxBegin(csn);
  ggCcRecording = true;
  ggCcRecStart = millis();
}

static inline bool ggCcRecordPoll(uint8_t csn) {
  if (!ggCcRecording) return false;
  if (millis() - ggCcRecStart > GG_CC_REC_TIMEOUT_MS) {
    ggCcRecording = false;
    return false;
  }
  if (ggCcRxAvailable(csn)) {
    uint8_t buf[GG_CC_PAYLOAD_MAX];
    uint8_t len = ggCcRxRead(csn, buf, GG_CC_PAYLOAD_MAX);
    if (len > 0 && ggCcRecBuf.len + len <= GG_CC_REC_MAX) {
      memcpy(ggCcRecBuf.data + ggCcRecBuf.len, buf, len);
      ggCcRecBuf.len += len;
      ggCcRecBuf.valid = true;
    }
    ggCcRxBegin(csn);
    return true;
  }
  return false;
}

static inline void ggCcRecordStop(uint8_t csn) {
  ggCcIdle(csn);
  ggCcRecording = false;
}

// ===== signal replay =======================================================
static bool ggCcReplaying = false;
static uint16_t ggCcReplayPos = 0;
static uint32_t ggCcReplayCount = 0;

static inline bool ggCcReplayBegin(uint8_t csn, const GgCcRecording& rec) {
  if (!rec.valid || rec.len == 0) return false;
  GgCcProfile rp{};
  rp.freqMHz = rec.freqMHz;
  rp.modulation = rec.modulation;
  rp.dataRate = rec.dataRate;
  rp.power = GG_CP_10;
  rp.plLen = 32;
  rp.syncWord[0] = 0xD3; rp.syncWord[1] = 0x91;
  ggCcInit(csn, rp);
  ggCcReplayPos = 0;
  ggCcReplayCount = 0;
  ggCcReplaying = true;
  return true;
}

static inline bool ggCcReplayPump(uint8_t csn, const GgCcRecording& rec) {
  if (!ggCcReplaying || !rec.valid) return false;
  uint8_t chunkLen = 32;
  if (ggCcReplayPos + chunkLen > rec.len) chunkLen = rec.len - ggCcReplayPos;
  if (chunkLen == 0) { ggCcReplayPos = 0; return false; }
  ggCcTxPacket(csn, rec.data + ggCcReplayPos, chunkLen);
  ggCcReplayPos += chunkLen;
  ggCcReplayCount++;
  if (ggCcReplayPos >= rec.len) ggCcReplayPos = 0;
  return true;
}

static inline void ggCcReplayStop(uint8_t csn) {
  ggCcIdle(csn);
  ggCcReplaying = false;
}

// ===== brute force ==========================================================
// Sequentially transmits incrementing byte patterns. For testing fixed-code
// systems on your own hardware.
static bool     ggCcBruteActive = false;
static uint32_t ggCcBruteCode = 0;
static uint32_t ggCcBruteMax = 0;
static uint8_t  ggCcBruteLen = 2;
static uint32_t ggCcBruteSent = 0;
static uint32_t ggCcBruteLastMs = 0;
static uint16_t ggCcBruteGapMs = 10;

static inline void ggCcBruteStart(uint8_t csn, const GgCcProfile& p, uint8_t codeLen, uint16_t gapMs) {
  ggCcInit(csn, p);
  ggCcBruteLen = (codeLen < 1) ? 1 : (codeLen > 4) ? 4 : codeLen;
  ggCcBruteCode = 0;
  ggCcBruteMax = 1;
  for (int i = 0; i < ggCcBruteLen; i++) ggCcBruteMax <<= 8;
  ggCcBruteMax--;
  ggCcBruteSent = 0;
  ggCcBruteGapMs = gapMs;
  ggCcBruteLastMs = 0;
  ggCcBruteActive = true;
}

static inline bool ggCcBrutePump(uint8_t csn) {
  if (!ggCcBruteActive) return false;
  uint32_t now = millis();
  if (now - ggCcBruteLastMs < ggCcBruteGapMs) return true;
  ggCcBruteLastMs = now;

  uint8_t buf[4];
  for (int i = ggCcBruteLen - 1; i >= 0; i--) {
    buf[i] = (uint8_t)(ggCcBruteCode >> (8 * (ggCcBruteLen - 1 - i)));
  }
  ggCcTxPacket(csn, buf, ggCcBruteLen);
  ggCcBruteSent++;
  ggCcBruteCode++;
  if (ggCcBruteCode > ggCcBruteMax) {
    ggCcBruteActive = false;
    return false;
  }
  return true;
}

static inline void ggCcBruteStop(uint8_t csn) {
  ggCcIdle(csn);
  ggCcBruteActive = false;
}

// ===== signal generator (CW carrier) =======================================
// Constant carrier for antenna testing, similar to nRF carrier mode.
// CC1101 doesn't have a native CW mode, so we transmit a continuous stream
// of 0xFF bytes with no sync word to approximate it.
static bool ggCcCwActive = false;

static inline void ggCcCwStart(uint8_t csn, float freqMHz) {
  GgCcProfile cw{};
  cw.freqMHz = freqMHz;
  cw.modulation = GG_CM_2FSK;
  cw.dataRate = GG_CR_250K;
  cw.power = GG_CP_10;
  cw.plLen = GG_CC_PAYLOAD_MAX;
  cw.syncWord[0] = 0xFF; cw.syncWord[1] = 0xFF;
  ggCcInit(csn, cw);
  ggCcCwActive = true;
}

static inline void ggCcCwPump(uint8_t csn) {
  if (!ggCcCwActive) return;
  uint8_t ones[GG_CC_PAYLOAD_MAX];
  memset(ones, 0xFF, sizeof(ones));
  ggCcTxPacket(csn, ones, GG_CC_PAYLOAD_MAX);
}

static inline void ggCcCwStop(uint8_t csn) {
  ggCcIdle(csn);
  ggCcCwActive = false;
}

// ===== sub-GHz TX helper (single burst from profile) =======================
static uint32_t ggCcTxCount = 0;

static inline bool ggCcTxOnce(uint8_t csn, const GgCcProfile& p) {
  return ggCcTxPacket(csn, p.payload, p.plLen);
}
