#pragma once
// ############################################################################
//  gr_rf_tx.h  -  KODI  /  RF code TX via SD → ESP-NOW → Node 0 CC1101
//
//  Reads Flipper SubGhz .sub files from SD card, converts RAW timing data
//  to a packed OOK bitstream, and sends to Node 0 for CC1101 transmission.
//
//  .sub file format:
//    Frequency: 315000000       (Hz)
//    RAW_Data: 350 -1050 ...    (signed microsecond timings)
//
//  Timing → OOK bitstream conversion:
//    Positive values = RF ON  (1 bits)
//    Negative values = RF OFF (0 bits)
//    Quantized at 100us per bit (10 kbps data rate = GG_CR_10K)
//
//  ESP-NOW payload (GT_CMD_RF_TX_REQUEST):
//    freqMHz (float), dataRate (uint8_t), data[200], dataLen (uint8_t)
//    Sent to Node 0 which builds a GgCcRecording and calls ggCcReplay()
//
//  If the packed bitstream exceeds 200 bytes (1600 bits = 160ms at 10kbps),
//  multiple packets are sent sequentially with a short delay.
// ############################################################################

#include <Arduino.h>
#include <SD.h>
#include "gr_config.h"
#include "gr_sd.h"
#include "gr_espnow.h"

#define GR_RF_TX_BIT_US       100     // 100us per bit = 10 kbps
#define GR_RF_TX_MAX_TIMINGS  256     // max timing pairs to parse
#define GR_RF_TX_CHUNK_MAX    200     // max data bytes per ESP-NOW packet

// ---- parse .sub file from SD ------------------------------------------------
// Extracts frequency (Hz → MHz) and RAW_Data timing values from a .sub file.
// Returns true if parsing succeeded with at least 1 timing value.

struct GrRfSubFile {
  float    freqMHz;
  int16_t  timings[GR_RF_TX_MAX_TIMINGS];
  uint16_t timingCount;
  bool     valid;
};

static inline bool grRfTxParseSub(const char* path, GrRfSubFile& sub) {
  memset(&sub, 0, sizeof(sub));

  File f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.printf("[rf_tx] cannot open %s\n", path);
    return false;
  }

  size_t fSize = f.size();
  if (fSize > 4096) fSize = 4096;

  static char buf[4097];
  size_t rd = f.read((uint8_t*)buf, fSize);
  f.close();
  buf[rd] = '\0';

  // parse Frequency line
  char* freqLine = strstr(buf, "Frequency:");
  if (freqLine) {
    freqLine += 10; // skip "Frequency:"
    while (*freqLine == ' ') freqLine++;
    uint32_t freqHz = strtoul(freqLine, nullptr, 10);
    if (freqHz >= 300000000UL && freqHz <= 928000000UL) {
      sub.freqMHz = (float)freqHz / 1000000.0f;
    }
  }
  if (sub.freqMHz < 300.0f) {
    Serial.printf("[rf_tx] bad frequency in %s\n", path);
    return false;
  }

  // parse RAW_Data line
  char* rawLine = strstr(buf, "RAW_Data:");
  if (!rawLine) {
    Serial.printf("[rf_tx] no RAW_Data in %s\n", path);
    return false;
  }
  rawLine += 9; // skip "RAW_Data:"

  sub.timingCount = 0;
  char* p = rawLine;
  while (*p && sub.timingCount < GR_RF_TX_MAX_TIMINGS) {
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '\n' || *p == '\r') break;
    int32_t val = strtol(p, &p, 10);
    if (val == 0 && (*p != '-' && (*p < '0' || *p > '9'))) break;
    // clamp to int16_t range
    if (val > 32767) val = 32767;
    if (val < -32767) val = -32767;
    sub.timings[sub.timingCount++] = (int16_t)val;
  }

  if (sub.timingCount < 2) {
    Serial.printf("[rf_tx] too few timings (%d) in %s\n", sub.timingCount, path);
    return false;
  }

  sub.valid = true;
  return true;
}

// ---- convert timing values to packed OOK bitstream --------------------------
// Positive timing = bits set to 1 (RF on)
// Negative timing = bits set to 0 (RF off)
// Resolution: GR_RF_TX_BIT_US microseconds per bit

struct GrRfBitstream {
  uint8_t  data[512];
  uint16_t len;         // bytes used
  uint16_t totalBits;
};

static inline bool grRfTxConvert(const GrRfSubFile& sub, GrRfBitstream& bs) {
  memset(&bs, 0, sizeof(bs));

  uint16_t bitPos = 0;
  for (uint16_t i = 0; i < sub.timingCount; i++) {
    int16_t t = sub.timings[i];
    bool on = (t > 0);
    uint16_t absUs = (uint16_t)(on ? t : -t);

    // cap inter-code gaps to avoid bloating the bitstream with trailing zeros
    if (!on && absUs > 5000) absUs = 5000;

    // round to nearest number of bits
    uint16_t nBits = (absUs + GR_RF_TX_BIT_US / 2) / GR_RF_TX_BIT_US;
    if (nBits == 0) nBits = 1;

    for (uint16_t b = 0; b < nBits; b++) {
      if (bitPos / 8 >= 512) goto done;
      if (on) {
        bs.data[bitPos / 8] |= (0x80 >> (bitPos % 8));
      }
      bitPos++;
    }
  }
done:
  bs.totalBits = bitPos;
  bs.len = (bitPos + 7) / 8;
  return (bs.len > 0);
}

// ---- send bitstream to Node 0 via ESP-NOW -----------------------------------
// Splits into chunks of GR_RF_TX_CHUNK_MAX bytes if needed.
// Each chunk is sent as GT_CMD_RF_TX_REQUEST.

static inline bool grRfTxSendToNode0(float freqMHz, const GrRfBitstream& bs,
                                      uint8_t repeats) {
  if (!grLinkReady || !grPeer0.up) {
    Serial.println(F("[rf_tx] Node 0 not connected"));
    return false;
  }

  uint16_t pos = 0;
  uint8_t chunkIdx = 0;
  uint16_t remaining = bs.len;

  while (remaining > 0) {
    uint8_t chunkLen = (remaining > GR_RF_TX_CHUNK_MAX)
                       ? GR_RF_TX_CHUNK_MAX : (uint8_t)remaining;

    GtLinkMsg m{};
    m.magic[0]  = GT_LINK_MAGIC_0;
    m.magic[1]  = GT_LINK_MAGIC_1;
    m.cmd       = GT_CMD_RF_TX_REQUEST;
    m.boardType = GR_BOARD_TYPE;

    m.rfTxRequest.freqMHz   = freqMHz;
    m.rfTxRequest.dataRate   = 2;  // GG_CR_10K = 10 kbps
    m.rfTxRequest.dataLen    = chunkLen;
    m.rfTxRequest.chunkIdx   = chunkIdx;
    m.rfTxRequest.totalChunks = (uint8_t)((bs.len + GR_RF_TX_CHUNK_MAX - 1) / GR_RF_TX_CHUNK_MAX);
    m.rfTxRequest.repeats    = repeats;
    memcpy(m.rfTxRequest.data, &bs.data[pos], chunkLen);

    size_t msgSize = GT_HDR_SIZE + 12 + chunkLen;
    grLinkSendMsg(m, msgSize);

    Serial.printf("[rf_tx] chunk %d/%d sent (%d bytes) freq=%.2fMHz\n",
                  chunkIdx + 1, m.rfTxRequest.totalChunks, chunkLen, freqMHz);

    pos += chunkLen;
    remaining -= chunkLen;
    chunkIdx++;

    if (remaining > 0) delay(20);
  }

  return true;
}

// ---- high-level: read .sub from SD and transmit -----------------------------
// path: full SD path to .sub file
// repeats: how many times Node 0 should replay the signal (1 = once)

static inline bool grRfTxFromSd(const char* path, uint8_t repeats) {
  GrRfSubFile sub;
  if (!grRfTxParseSub(path, sub)) return false;

  Serial.printf("[rf_tx] parsed %s: %.2fMHz, %d timings\n",
                path, sub.freqMHz, sub.timingCount);

  GrRfBitstream bs;
  if (!grRfTxConvert(sub, bs)) {
    Serial.printf("[rf_tx] conversion failed for %s\n", path);
    return false;
  }

  Serial.printf("[rf_tx] bitstream: %d bits, %d bytes\n", bs.totalBits, bs.len);

  return grRfTxSendToNode0(sub.freqMHz, bs, repeats);
}

// ---- batch TX: transmit all .sub files in a directory -----------------------
// Iterates all .sub files in the given SD directory and transmits each one.
// delayBetweenMs: milliseconds to wait between transmissions.

static bool     grRfBatchActive   = false;
static uint16_t grRfBatchTotal    = 0;
static uint16_t grRfBatchSent     = 0;
static char     grRfBatchDir[64]  = {};

static inline bool grRfBatchStart(const char* dirPath, uint8_t repeats,
                                   uint16_t delayBetweenMs) {
  if (!grSdReady) return false;

  File dir = SD.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    Serial.printf("[rf_tx] cannot open dir %s\n", dirPath);
    return false;
  }

  strncpy(grRfBatchDir, dirPath, sizeof(grRfBatchDir) - 1);
  grRfBatchTotal = grSdCountFiles(dirPath);
  grRfBatchSent = 0;
  grRfBatchActive = true;

  Serial.printf("[rf_tx] batch start: %s (%d files)\n", dirPath, grRfBatchTotal);

  File f = dir.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      const char* name = f.name();
      // check for .sub extension
      size_t nLen = strlen(name);
      if (nLen > 4 && strcmp(name + nLen - 4, ".sub") == 0) {
        char fullPath[128];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, name);

        grRfTxFromSd(fullPath, repeats);
        grRfBatchSent++;

        if (delayBetweenMs > 0) delay(delayBetweenMs);
      }
    }
    f = dir.openNextFile();
  }

  grRfBatchActive = false;
  Serial.printf("[rf_tx] batch complete: %d/%d sent\n", grRfBatchSent, grRfBatchTotal);
  return true;
}

// ---- list available protocol directories on SD ------------------------------
// Returns count of .sub files found in /credentials/rf_codes/fixed_codes/
// and its subdirectories. Useful for UI display.

struct GrRfCodeDir {
  char     name[24];
  char     path[64];
  uint16_t fileCount;
};

#define GR_RF_DIR_MAX 16

static GrRfCodeDir grRfDirs[GR_RF_DIR_MAX] = {};
static uint8_t     grRfDirCount = 0;

static inline void grRfTxScanDirs() {
  grRfDirCount = 0;
  const char* base = "/credentials/rf_codes/fixed_codes";

  File root = SD.open(base);
  if (!root || !root.isDirectory()) return;

  File sub = root.openNextFile();
  while (sub && grRfDirCount < GR_RF_DIR_MAX) {
    if (sub.isDirectory()) {
      GrRfCodeDir& d = grRfDirs[grRfDirCount];
      strncpy(d.name, sub.name(), sizeof(d.name) - 1);
      snprintf(d.path, sizeof(d.path), "%s/%s", base, sub.name());
      d.fileCount = grSdCountFiles(d.path);
      if (d.fileCount > 0) grRfDirCount++;
    }
    sub = root.openNextFile();
  }

  Serial.printf("[rf_tx] found %d protocol dirs\n", grRfDirCount);
  for (uint8_t i = 0; i < grRfDirCount; i++) {
    Serial.printf("[rf_tx]   %s: %d files\n", grRfDirs[i].name, grRfDirs[i].fileCount);
  }
}
