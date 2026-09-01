#pragma once
// ############################################################################
//  gg_store.h  -  KODI RF CONTROLLER  /  NVS persistence
//
//  Saves settings, live radio configs (nRF + CC1101), profile banks, saved
//  payloads, WiFi/BLE TX configs, and captured credentials to flash.
// ############################################################################

#include <Arduino.h>
#include <Preferences.h>
#include "gg_config.h"

static Preferences ggNvs;

static inline void ggStoreBegin() { ggNvs.begin("kodirf", false); }

// ---- settings + live configs ----
static inline void ggSaveSettings(const GgSettings& s) { ggNvs.putBytes("set", &s, sizeof(s)); }
static inline bool ggLoadSettings(GgSettings& s) { return ggNvs.getBytes("set", &s, sizeof(s)) == sizeof(s); }

static inline void ggSaveLive(const GgNrfProfile& a, const GgNrfProfile& b) {
  ggNvs.putBytes("nrfA", &a, sizeof(a));
  ggNvs.putBytes("nrfB", &b, sizeof(b));
}
static inline bool ggLoadLive(GgNrfProfile& a, GgNrfProfile& b) {
  bool ok = ggNvs.getBytes("nrfA", &a, sizeof(a)) == sizeof(a);
  ok &= ggNvs.getBytes("nrfB", &b, sizeof(b)) == sizeof(b);
  return ok;
}

// ---- CC1101 live configs ----
static inline void ggSaveCcLive(const GgCcProfile& a, const GgCcProfile& b) {
  ggNvs.putBytes("ccA", &a, sizeof(a));
  ggNvs.putBytes("ccB", &b, sizeof(b));
}
static inline bool ggLoadCcLive(GgCcProfile& a, GgCcProfile& b) {
  bool ok = ggNvs.getBytes("ccA", &a, sizeof(a)) == sizeof(a);
  ok &= ggNvs.getBytes("ccB", &b, sizeof(b)) == sizeof(b);
  return ok;
}

// ---- nRF profile bank (8 slots) ----
static inline uint8_t ggNrfMask() { return ggNvs.getUChar("nmask", 0); }
static inline bool ggNrfSlotUsed(uint8_t i) { return i < GG_PROFILE_SLOTS && (ggNrfMask() & (1 << i)); }
static inline bool ggNrfProfLoad(uint8_t i, GgNrfProfile& o) {
  if (!ggNrfSlotUsed(i)) return false;
  char k[6]; snprintf(k, sizeof(k), "n%u", i);
  return ggNvs.getBytes(k, &o, sizeof(o)) == sizeof(o);
}
static inline void ggNrfProfSave(uint8_t i, const GgNrfProfile& p) {
  if (i >= GG_PROFILE_SLOTS) return;
  char k[6]; snprintf(k, sizeof(k), "n%u", i);
  ggNvs.putBytes(k, &p, sizeof(p));
  ggNvs.putUChar("nmask", ggNrfMask() | (1 << i));
}
static inline void ggNrfProfErase(uint8_t i) {
  if (i >= GG_PROFILE_SLOTS) return;
  char k[6]; snprintf(k, sizeof(k), "n%u", i);
  ggNvs.remove(k); ggNvs.putUChar("nmask", ggNrfMask() & ~(1 << i));
}

// ---- CC1101 profile bank (8 slots) ----
static inline uint8_t ggCcMask() { return ggNvs.getUChar("cmask", 0); }
static inline bool ggCcSlotUsed(uint8_t i) { return i < GG_CC_PROFILE_SLOTS && (ggCcMask() & (1 << i)); }
static inline bool ggCcProfLoad(uint8_t i, GgCcProfile& o) {
  if (!ggCcSlotUsed(i)) return false;
  char k[6]; snprintf(k, sizeof(k), "c%u", i);
  return ggNvs.getBytes(k, &o, sizeof(o)) == sizeof(o);
}
static inline void ggCcProfSave(uint8_t i, const GgCcProfile& p) {
  if (i >= GG_CC_PROFILE_SLOTS) return;
  char k[6]; snprintf(k, sizeof(k), "c%u", i);
  ggNvs.putBytes(k, &p, sizeof(p));
  ggNvs.putUChar("cmask", ggCcMask() | (1 << i));
}
static inline void ggCcProfErase(uint8_t i) {
  if (i >= GG_CC_PROFILE_SLOTS) return;
  char k[6]; snprintf(k, sizeof(k), "c%u", i);
  ggNvs.remove(k); ggNvs.putUChar("cmask", ggCcMask() & ~(1 << i));
}

// ---- saved-payload library (8 slots) ----
static inline uint8_t ggPayMask() { return ggNvs.getUChar("pmask", 0); }
static inline bool ggPaySlotUsed(uint8_t i) { return i < GG_PAYLIB_SLOTS && (ggPayMask() & (1 << i)); }
static inline bool ggPayLoad(uint8_t i, GgPayload& o) {
  if (!ggPaySlotUsed(i)) return false;
  char k[6]; snprintf(k, sizeof(k), "p%u", i);
  return ggNvs.getBytes(k, &o, sizeof(o)) == sizeof(o);
}
static inline void ggPaySave(uint8_t i, const GgPayload& p) {
  if (i >= GG_PAYLIB_SLOTS) return;
  char k[6]; snprintf(k, sizeof(k), "p%u", i);
  ggNvs.putBytes(k, &p, sizeof(p));
  ggNvs.putUChar("pmask", ggPayMask() | (1 << i));
}
static inline void ggPayErase(uint8_t i) {
  if (i >= GG_PAYLIB_SLOTS) return;
  char k[6]; snprintf(k, sizeof(k), "p%u", i);
  ggNvs.remove(k); ggNvs.putUChar("pmask", ggPayMask() & ~(1 << i));
}

// ---- CC1101 signal recording (1 slot) ----
static inline void ggSaveCcRec(const GgCcRecording& r) { ggNvs.putBytes("ccrec", &r, sizeof(r)); }
static inline bool ggLoadCcRec(GgCcRecording& r) {
  if (ggNvs.getBytes("ccrec", &r, sizeof(r)) != sizeof(r)) return false;
  return r.valid;
}
static inline void ggClearCcRec() { GgCcRecording r{}; r.valid = false; ggNvs.putBytes("ccrec", &r, sizeof(r)); }

// ---- WiFi TX config ----
static inline void ggSaveWifiTx(const GgWifiTxCfg& c) { ggNvs.putBytes("wftx", &c, sizeof(c)); }
static inline bool ggLoadWifiTx(GgWifiTxCfg& c) { return ggNvs.getBytes("wftx", &c, sizeof(c)) == sizeof(c); }

// ---- BLE TX config ----
static inline void ggSaveBleTx(const GgBleTxCfg& c) { ggNvs.putBytes("bltx", &c, sizeof(c)); }
static inline bool ggLoadBleTx(GgBleTxCfg& c) { return ggNvs.getBytes("bltx", &c, sizeof(c)) == sizeof(c); }

// ---- Captured credentials (Evil Twin) ----
static inline void ggSaveCreds(const GgCred* creds, int count) {
  if (count < 0) count = 0;
  if (count > GG_CRED_MAX) count = GG_CRED_MAX;
  ggNvs.putUChar("ccnt", (uint8_t)count);
  for (int i = 0; i < count; i++) {
    char k[6]; snprintf(k, sizeof(k), "d%u", i);
    ggNvs.putBytes(k, &creds[i], sizeof(GgCred));
  }
}
static inline int ggLoadCreds(GgCred* creds, int maxN) {
  uint8_t cnt = ggNvs.getUChar("ccnt", 0);
  if (cnt > (uint8_t)maxN) cnt = (uint8_t)maxN;
  for (int i = 0; i < cnt; i++) {
    char k[6]; snprintf(k, sizeof(k), "d%u", i);
    if (ggNvs.getBytes(k, &creds[i], sizeof(GgCred)) != sizeof(GgCred)) return i;
  }
  return cnt;
}
static inline void ggClearCreds() {
  uint8_t cnt = ggNvs.getUChar("ccnt", 0);
  if (cnt > GG_CRED_MAX) cnt = GG_CRED_MAX;
  for (int i = 0; i < cnt; i++) {
    char k[6]; snprintf(k, sizeof(k), "d%u", i);
    ggNvs.remove(k);
  }
  ggNvs.putUChar("ccnt", 0);
}
