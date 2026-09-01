#pragma once
// ############################################################################
//  gg_detect.h  -  GHOST TX GOLD  /  passive surveillance + tracker scan
//
//  Two independent scan modes, each using ONE radio so full power goes to it:
//
//    * COP SCAN (WiFi + BLE)  - async WiFi + BLE scans looking for
//      surveillance SSIDs and BLE device names (Flock, Axon, etc.)
//
//    * TRACKER SCAN (BLE only)  - background BLE scan looking for known
//      tracker company IDs (AirTag, Tile, SmartTag, Chipolo, etc.)
//
//  Both are non-blocking so the UI stays responsive. Neither transmits.
// ############################################################################

#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// ===== tracker database =====================================================
struct GgTracker { uint16_t company; const char* name; };
static const GgTracker GG_TRACKERS[] = {
  {0x004C,"AirTag/FindMy"}, {0x0075,"SmartTag"}, {0x03DA,"Tile"},
  {0x0157,"Chipolo"}, {0x0087,"Garmin"}, {0x018E,"Pebblebee"}, {0x05A7,"Nut"}
};
static const int GG_TRACKERS_N = sizeof(GG_TRACKERS)/sizeof(GG_TRACKERS[0]);

// ===== surveillance SSID watchwords (WiFi) ==================================
static const char* const GG_WIFI_WATCH[] = {
  "flock","axon","genetec","verkada","penlink","motorola","alpr","surveil",
  "vigilant","shotspot","clearview","palantir","cellebrite","stingray",
  "hikvision","dahua","avigilon"
};
static const int GG_WIFI_WATCH_N = sizeof(GG_WIFI_WATCH)/sizeof(GG_WIFI_WATCH[0]);

// ===== surveillance BLE watchwords ==========================================
static const char* const GG_BLE_WATCH[] = {
  "flock","axon","genetec","verkada","motorola","alpr","surveil",
  "vigilant","hikvision","dahua","avigilon"
};
static const int GG_BLE_WATCH_N = sizeof(GG_BLE_WATCH)/sizeof(GG_BLE_WATCH[0]);

// ===== COP SCAN (WiFi + BLE dual surveillance detection) ==================
static volatile uint32_t ggCopWifiSeen=0, ggCopWifiHit=0;
static volatile uint32_t ggCopBleSeen=0,  ggCopBleHit=0;
static char     ggCopLast[22] = "";
static volatile bool ggCopNewHit = false;
static bool     ggCopRunning = false;
static bool     ggCopBleUp   = false;
static BLEScan* ggCopBleScan = nullptr;

// BLE callback for cop scan — checks device names against watchwords
class GgCopBleCB : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) override {
    ggCopBleSeen = ggCopBleSeen + 1;
    String nm = dev.getName();
    if (nm.length() < 2) return;
    char low[40]; int j=0;
    for (; j<(int)nm.length() && j<39; j++) low[j]=(char)tolower((unsigned char)nm[j]);
    low[j]=0;
    for (int k=0;k<GG_BLE_WATCH_N;k++){
      if (strstr(low, GG_BLE_WATCH[k])) {
        ggCopBleHit = ggCopBleHit + 1; ggCopNewHit = true;
        snprintf(ggCopLast,sizeof(ggCopLast),"BLE:%.17s", nm.c_str());
        break;
      }
    }
  }
};
static GgCopBleCB ggCopBleCb;

static inline void ggCopScanBegin() {
  ggCopWifiSeen=0; ggCopWifiHit=0; ggCopBleSeen=0; ggCopBleHit=0;
  ggCopLast[0]=0; ggCopNewHit=false;
  WiFi.mode(WIFI_OFF);
  delay(50);
  WiFi.mode(WIFI_STA); WiFi.disconnect(); delay(30);
  wifi_country_t cty = { "XX", 1, 14, 78, WIFI_COUNTRY_POLICY_MANUAL };
  esp_wifi_set_country(&cty);
  WiFi.scanNetworks(true, true);
  if (ESP.getFreeHeap() < 60000) { ggCopRunning = true; return; }
  BLEDevice::init(""); delay(50);
  ggCopBleScan = BLEDevice::getScan();
  ggCopBleScan->setAdvertisedDeviceCallbacks(&ggCopBleCb, true);
  ggCopBleScan->setActiveScan(false);
  ggCopBleScan->setInterval(160); ggCopBleScan->setWindow(80);
  ggCopBleScan->start(0, nullptr, false);
  ggCopBleUp = true;
  ggCopRunning = true;
}
static inline void ggCopScanEnd() {
  if (ggCopBleUp) {
    if (ggCopBleScan) ggCopBleScan->stop();
    BLEDevice::deinit(false);
    ggCopBleUp = false; ggCopBleScan = nullptr;
  }
  if (ggCopRunning) { WiFi.scanDelete(); ggCopRunning=false; }
  WiFi.mode(WIFI_OFF);
}
static inline void ggCopScanPoll() {
  // WiFi SSID check
  int wc = WiFi.scanComplete();
  if (wc >= 0) {
    ggCopWifiSeen = ggCopWifiSeen + wc;
    for (int i=0;i<wc;i++){
      String ss = WiFi.SSID(i);
      char low[40]; int j=0; for (; j<(int)ss.length() && j<39; j++) low[j]=(char)tolower((unsigned char)ss[j]); low[j]=0;
      for (int k=0;k<GG_WIFI_WATCH_N;k++){
        if (strstr(low, GG_WIFI_WATCH[k])) {
          ggCopWifiHit = ggCopWifiHit + 1; ggCopNewHit=true;
          snprintf(ggCopLast,sizeof(ggCopLast),"%.21s", ss.c_str()); break;
        }
      }
    }
    WiFi.scanDelete();
    WiFi.scanNetworks(true, true);
  } else if (wc == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true, true);
  }
  // BLE hits arrive via callback — no polling needed
}

// ===== TRACKER SCAN (BLE tracker detection) ================================
static volatile uint32_t ggTrkBleSeen=0, ggTrkTrackCnt=0;
static char     ggTrkLast[22] = "";
static volatile bool ggTrkNewHit = false;
static bool     ggTrkBleReady = false;
static BLEScan* ggTrkScan = nullptr;

class GgTrkCB : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) override {
    ggTrkBleSeen = ggTrkBleSeen + 1;
    if (!dev.haveManufacturerData()) return;
    String md = dev.getManufacturerData();
    if (md.length() < 2) return;
    uint16_t company = (uint16_t)((uint8_t)md[0] | ((uint8_t)md[1] << 8));
    // Apple 0x004C: only flag FindMy/AirTag (OF type 0x12), not every iPhone
    if (company == 0x004C) {
      if (md.length() < 3 || (uint8_t)md[2] != 0x12) return;
    }
    for (int k=0;k<GG_TRACKERS_N;k++){
      if (company == GG_TRACKERS[k].company) {
        ggTrkTrackCnt = ggTrkTrackCnt + 1; ggTrkNewHit = true;
        snprintf(ggTrkLast,sizeof(ggTrkLast),"%.21s", GG_TRACKERS[k].name);
        break;
      }
    }
  }
};
static GgTrkCB ggTrkCb;

static inline void ggTrkScanBegin() {
  ggTrkBleSeen=0; ggTrkTrackCnt=0; ggTrkLast[0]=0; ggTrkNewHit=false;
  if (!ggTrkBleReady) {
    if (ESP.getFreeHeap() < 60000) return;
    BLEDevice::init(""); delay(50); ggTrkScan = BLEDevice::getScan(); ggTrkBleReady = true;
  }
  ggTrkScan->setAdvertisedDeviceCallbacks(&ggTrkCb, true);
  ggTrkScan->setActiveScan(false);
  ggTrkScan->setInterval(120); ggTrkScan->setWindow(100);
  ggTrkScan->start(0, nullptr, false);
}
static inline void ggTrkScanEnd() {
  if (ggTrkBleReady) { if (ggTrkScan) ggTrkScan->stop(); BLEDevice::deinit(false); ggTrkBleReady=false; ggTrkScan=nullptr; }
}

