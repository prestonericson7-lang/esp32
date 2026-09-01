#pragma once
// ############################################################################
//  gr_espnow.h  -  KODI  /  ESP-NOW link
//
//  EXACT same GtLinkMsg protocol as gg_espnow.h (Node 0), gs_espnow.h
//  (Node 1), and gb_espnow.h (Node 3). boardType = 2 (R40T).
//  25 commands (0-24), 6 backbone expansion commands (19-24).
//
//  Auto-discovery via broadcast heartbeats. Tracks 3 peers (N0, N1, N3).
//  Shimmed for Arduino-ESP32 2.x and 3.x.
// ############################################################################

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "gr_config.h"

// ---- protocol (MUST match gg_espnow.h and gs_espnow.h EXACTLY) ------------
#define GT_LINK_MAGIC_0  'G'
#define GT_LINK_MAGIC_1  'T'
#define GT_LINK_HB_MS    2000
#define GT_LINK_TIMEOUT  10000
#define GT_HDR_SIZE      7

enum GtLinkCmd : uint8_t {
  GT_CMD_HEARTBEAT    = 0,
  GT_CMD_FLOOD        = 1,
  GT_CMD_STOP         = 2,
  GT_CMD_SYNC         = 3,
  GT_CMD_BUTTON       = 4,
  GT_CMD_TELEMETRY    = 5,
  GT_CMD_ALERT        = 6,
  GT_CMD_DISPLAY      = 7,
  GT_CMD_AUDIO        = 8,
  GT_CMD_NEURAL_STATE = 9,
  GT_CMD_NEURAL_CMD   = 10,
  // ---- new commands for 3-node mesh ----
  GT_CMD_CAPTURE_DATA       = 11,
  GT_CMD_CAPTURE_META       = 12,
  GT_CMD_SORT_STATUS        = 13,
  GT_CMD_R40T_STATUS        = 14,
  GT_CMD_BUTTON_ROUTE       = 15,
  GT_CMD_MODE_CHANGE        = 16,
  GT_CMD_CLOCK_SYNC         = 17,
  GT_CMD_BACKBONE_STATUS    = 18,
  // ---- backbone expansion (6 new) ----
  GT_CMD_BACKBONE_WIFI      = 19,
  GT_CMD_BACKBONE_DEAUTH    = 20,
  GT_CMD_BACKBONE_HISTORY   = 21,
  GT_CMD_BACKBONE_PROXIMITY = 22,
  GT_CMD_BACKBONE_BEACON    = 23,
  GT_CMD_BACKBONE_CLIENTS   = 24,
  // ---- RF TX from SD via Node 0 ----
  GT_CMD_RF_TX_REQUEST      = 25,
  // ---- Learner state persistence (S3 ↔ R40T) ----
  GT_CMD_LEARNER_SAVE       = 26,
  GT_CMD_LEARNER_LOAD       = 27,
};

// Packed union struct — EXACT match to gg_espnow.h / gs_espnow.h
struct __attribute__((packed)) GtLinkMsg {
  uint8_t  magic[2];
  uint8_t  cmd;
  uint8_t  boardType;
  uint8_t  radioCount;
  uint8_t  attacking;
  uint8_t  screen;
  union {
    struct { uint8_t buttonId; } button;
    struct { uint8_t level; char msg[20]; } alert;
    struct { char line1[21]; char line2[21]; } display;
    struct { uint16_t freq; uint16_t durMs; } audio;
    struct {
      uint8_t  ccPresent;
      uint8_t  wifiActive;
      uint8_t  bleActive;
      uint8_t  ccJamming;
      float    ccFreqMHz;
      int8_t   ccRssi;
    } telemetry;
    struct {
      uint8_t  engineState;
      uint8_t  confidence;
      uint8_t  rulesFired;
    } neuralState;
    struct {
      uint8_t  commandId;
      uint8_t  param;
    } neuralCmd;
    struct {
      float    freqMHz;
      uint8_t  radioId;
      uint8_t  modulation;
      uint8_t  dataLen;
      uint8_t  data[200];
    } captureData;
    struct {
      float    freqMHz;
      uint8_t  deviceType;
      uint8_t  confidence;
      char     label[20];
    } captureMeta;
    struct {
      uint16_t queueDepth;
      uint8_t  sortingActive;
      uint16_t itemsSorted;
    } sortStatus;
    struct {
      uint8_t  screen;
      uint16_t sdFreeMb;
      uint16_t heapFree;
      uint8_t  speakerMuted;
    } r40tStatus;
    struct {
      uint8_t  targetNode;
    } buttonRoute;
    struct {
      uint8_t  newMode;
    } modeChange;
    struct {
      uint32_t millisOffset;
    } clockSync;
    struct {
      uint8_t  bleDeviceCount;
      uint8_t  trackersFound;
      uint8_t  trackersFlagged;
      uint8_t  meshHealth;
      uint8_t  load;
      uint8_t  newBleDevices;
      uint8_t  relayedCount;
      uint8_t  _pad;
    } backboneStatus;
    struct {
      uint8_t  apCount;
      uint8_t  apCountEver;
      int8_t   strongestRssi;
      uint8_t  busiestCh;
      uint8_t  openNets;
      uint8_t  hiddenNets;
      uint32_t envFingerprint;
      uint8_t  envChanged;
    } backboneWifi;
    struct {
      uint16_t deauthPerMin;
      uint16_t disassocPerMin;
      uint8_t  attackType;
      uint8_t  severity;
      uint8_t  targetChannel;
      uint8_t  stormActive;
    } backboneDeauth;
    struct {
      uint16_t totalUniqueBle;
      uint8_t  activeNow;
      uint8_t  newRatePer5m;
      uint8_t  phonesCount;
      uint8_t  trackersActive;
      uint8_t  avgDwellMin;
      uint16_t sessionMinutes;
    } backboneHistory;
    struct {
      uint8_t  zoneFar;
      uint8_t  zoneNear;
      uint8_t  zoneClose;
      uint8_t  zoneImmediate;
      int8_t   closestRssi;
      uint8_t  approaching;
    } backboneProximity;
    struct {
      uint8_t  beaconCount;
      uint8_t  iBeacons;
      uint8_t  eddystones;
      int8_t   nearestBeaconRssi;
    } backboneBeacon;
    struct {
      uint8_t  wifiClients;
      uint8_t  activeClients;
      uint8_t  probeCount;
    } backboneClients;
    struct {
      float    freqMHz;
      uint8_t  dataRate;
      uint8_t  dataLen;
      uint8_t  chunkIdx;
      uint8_t  totalChunks;
      uint8_t  repeats;
      uint8_t  _pad[3];
      uint8_t  data[200];
    } rfTxRequest;
    struct {
      uint8_t  chunkIdx;
      uint8_t  totalChunks;
      uint8_t  dataLen;
      uint8_t  data[200];
    } learnerData;
    uint8_t raw[212];
  };
};

// ---- link state ------------------------------------------------------------
static bool     grLinkReady     = false;
static uint32_t grLinkLastTx    = 0;

// peer tracking: Node 0 (boardType=0) and Node 1 (boardType=1)
struct GrPeer {
  uint8_t  mac[6];
  bool     found;
  bool     added;
  bool     up;
  uint32_t lastRx;
  uint8_t  radioCount;
  uint8_t  attacking;
  uint8_t  screen;
};
static GrPeer grPeer0 = {};  // Node 0
static GrPeer grPeer1 = {};  // Node 1
static GrPeer grPeer3 = {};  // Node 3 (Backbone)

// volatile flags from ESP-NOW callback — button events
static volatile bool grEnUp   = false;
static volatile bool grEnDown = false;
static volatile bool grEnSel  = false;
static volatile bool grEnBack = false;
static volatile bool grEnSwap = false;
static volatile bool grButtonRouteActive = false;

// neural state from Node 1
static volatile uint8_t grNnState      = 0;
static volatile uint8_t grNnConfidence = 0;
static volatile uint8_t grNnRulesFired = 0;

// alert from Node 1
static volatile uint8_t grLastAlertLvl = 0;
static char             grLastAlertMsg[21] = {};
static volatile bool    grNewAlert = false;

// behavior mode
static volatile uint8_t grBehaviorMode = 0;  // 0=HUNTER, 1=DEFENSIVE, 2=STEALTH

// capture data buffer (set by callback, consumed by main loop)
static volatile bool    grNewCapture     = false;
static float            grCapFreqMHz     = 0;
static uint8_t          grCapRadioId     = 0;
static uint8_t          grCapMod         = 0;
static uint8_t          grCapDataLen     = 0;
static uint8_t          grCapData[200]   = {};

static const uint8_t GR_BROADCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// ---- backbone state from Node 3 --------------------------------------------
// status (cmd 18)
static volatile uint8_t  grBbBleCount   = 0;
static volatile uint8_t  grBbTrackers   = 0;
static volatile uint8_t  grBbFlagged    = 0;
static volatile uint8_t  grBbMeshHealth = 0;
static volatile uint8_t  grBbLoad       = 0;
static volatile uint8_t  grBbNewBle     = 0;

// wifi environment (cmd 19)
static volatile uint8_t  grBbApCount       = 0;
static volatile uint8_t  grBbApCountEver   = 0;
static volatile int8_t   grBbStrongestRssi = 0;
static volatile uint8_t  grBbBusiestCh     = 0;
static volatile uint8_t  grBbOpenNets      = 0;
static volatile uint8_t  grBbHiddenNets    = 0;
static volatile uint32_t grBbEnvFingerprint = 0;
static volatile bool     grBbEnvChanged     = false;

// deauth detection (cmd 20)
static volatile uint16_t grBbDeauthPerMin    = 0;
static volatile uint16_t grBbDisassocPerMin  = 0;
static volatile uint8_t  grBbAttackType      = 0;
static volatile uint8_t  grBbAttackSeverity  = 0;
static volatile uint8_t  grBbAttackChannel   = 0;
static volatile bool     grBbStormActive     = false;

// BLE history (cmd 21)
static volatile uint16_t grBbTotalUniqueBle  = 0;
static volatile uint8_t  grBbActiveNow       = 0;
static volatile uint8_t  grBbNewRate         = 0;
static volatile uint8_t  grBbPhones          = 0;
static volatile uint8_t  grBbTrackersActive  = 0;
static volatile uint8_t  grBbAvgDwell        = 0;

// proximity (cmd 22)
static volatile uint8_t  grBbZoneFar       = 0;
static volatile uint8_t  grBbZoneNear      = 0;
static volatile uint8_t  grBbZoneClose     = 0;
static volatile uint8_t  grBbZoneImmediate = 0;
static volatile int8_t   grBbClosestRssi   = -127;
static volatile uint8_t  grBbApproaching   = 0;

// beacons (cmd 23)
static volatile uint8_t  grBbBeaconCount   = 0;
static volatile uint8_t  grBbIBeacons      = 0;
static volatile uint8_t  grBbEddystones    = 0;
static volatile int8_t   grBbNearBeaconRssi = -127;

// wifi clients (cmd 24)
static volatile uint8_t  grBbWifiClients   = 0;
static volatile uint8_t  grBbActiveClients = 0;
static volatile uint8_t  grBbProbeCount    = 0;

// ---- learner state reassembly from Node 1 -----------------------------------
static uint8_t  grLearnerBuf[2048] = {};
static uint16_t grLearnerBufLen = 0;
static uint8_t  grLearnerChunksRx = 0;
static uint8_t  grLearnerChunksTotal = 0;
static bool     grLearnerSavePending = false;
static bool     grLearnerLoadRequested = false;

// ---- pick peer by boardType ------------------------------------------------
static GrPeer* grPeerByType(uint8_t bt) {
  if (bt == 0) return &grPeer0;
  if (bt == 1) return &grPeer1;
  if (bt == 3) return &grPeer3;
  return nullptr;
}

// ---- handle received message -----------------------------------------------
static void grLinkHandleRx(const uint8_t* mac, const uint8_t* raw, int len) {
  if (len < 7) return;
  const GtLinkMsg* m = (const GtLinkMsg*)raw;
  if (m->magic[0] != GT_LINK_MAGIC_0 || m->magic[1] != GT_LINK_MAGIC_1) return;
  if (m->boardType == GR_BOARD_TYPE) return;  // ignore our own

  GrPeer* p = grPeerByType(m->boardType);
  if (!p) return;

  p->lastRx = millis();
  p->radioCount = m->radioCount;
  p->attacking  = m->attacking;
  p->screen     = m->screen;

  if (!p->found) {
    memcpy(p->mac, mac, 6);
    p->found = true;
    Serial.printf("[link] Node %d discovered: %02X:%02X:%02X:%02X:%02X:%02X\n",
      m->boardType, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }

  switch (m->cmd) {
    case GT_CMD_HEARTBEAT:
      break;

    case GT_CMD_BUTTON:
      if (len < GT_HDR_SIZE + 1) break;
      if (!grButtonRouteActive) break;
      switch (m->button.buttonId) {
        case 0: grEnUp   = true; break;
        case 1: grEnDown = true; break;
        case 2: grEnSel  = true; break;
        case 3: grEnBack = true; break;
        case 4: grEnSwap = true; break;
      }
      break;

    case GT_CMD_BUTTON_ROUTE:
      if (len < GT_HDR_SIZE + 1) break;
      grButtonRouteActive = (m->buttonRoute.targetNode == 2);
      Serial.printf("[link] button route -> Node %d\n", m->buttonRoute.targetNode);
      break;

    case GT_CMD_NEURAL_STATE:
      if (len < GT_HDR_SIZE + 3) break;
      grNnState      = m->neuralState.engineState;
      grNnConfidence = m->neuralState.confidence;
      grNnRulesFired = m->neuralState.rulesFired;
      break;

    case GT_CMD_ALERT:
      if (len < GT_HDR_SIZE + 21) break;
      grLastAlertLvl = m->alert.level;
      memcpy(grLastAlertMsg, m->alert.msg, 20);
      grLastAlertMsg[20] = '\0';
      grNewAlert = true;
      break;

    case GT_CMD_MODE_CHANGE:
      if (len < GT_HDR_SIZE + 1) break;
      grBehaviorMode = m->modeChange.newMode;
      Serial.printf("[link] mode change: %d\n", m->modeChange.newMode);
      break;

    case GT_CMD_CAPTURE_DATA:
      if (len < GT_HDR_SIZE + 7) break;
      if (m->captureData.dataLen > 0 && m->captureData.dataLen <= 200
          && len >= GT_HDR_SIZE + 7 + m->captureData.dataLen) {
        grCapFreqMHz  = m->captureData.freqMHz;
        grCapRadioId  = m->captureData.radioId;
        grCapMod      = m->captureData.modulation;
        grCapDataLen  = m->captureData.dataLen;
        memcpy((void*)grCapData, m->captureData.data, m->captureData.dataLen);
        grNewCapture  = true;
      }
      break;

    case GT_CMD_CLOCK_SYNC:
      break;

    case GT_CMD_BACKBONE_STATUS:
      if (len < GT_HDR_SIZE + 8) break;
      grBbBleCount   = m->backboneStatus.bleDeviceCount;
      grBbTrackers   = m->backboneStatus.trackersFound;
      grBbFlagged    = m->backboneStatus.trackersFlagged;
      grBbMeshHealth = m->backboneStatus.meshHealth;
      grBbLoad       = m->backboneStatus.load;
      grBbNewBle     = m->backboneStatus.newBleDevices;
      break;

    case GT_CMD_BACKBONE_WIFI:
      if (len < GT_HDR_SIZE + 11) break;
      grBbApCount        = m->backboneWifi.apCount;
      grBbApCountEver    = m->backboneWifi.apCountEver;
      grBbStrongestRssi  = m->backboneWifi.strongestRssi;
      grBbBusiestCh      = m->backboneWifi.busiestCh;
      grBbOpenNets       = m->backboneWifi.openNets;
      grBbHiddenNets     = m->backboneWifi.hiddenNets;
      grBbEnvFingerprint = m->backboneWifi.envFingerprint;
      grBbEnvChanged     = m->backboneWifi.envChanged != 0;
      break;

    case GT_CMD_BACKBONE_DEAUTH:
      if (len < GT_HDR_SIZE + 8) break;
      grBbDeauthPerMin   = m->backboneDeauth.deauthPerMin;
      grBbDisassocPerMin = m->backboneDeauth.disassocPerMin;
      grBbAttackType     = m->backboneDeauth.attackType;
      grBbAttackSeverity = m->backboneDeauth.severity;
      grBbAttackChannel  = m->backboneDeauth.targetChannel;
      grBbStormActive    = m->backboneDeauth.stormActive != 0;
      break;

    case GT_CMD_BACKBONE_HISTORY:
      if (len < GT_HDR_SIZE + 9) break;
      grBbTotalUniqueBle = m->backboneHistory.totalUniqueBle;
      grBbActiveNow      = m->backboneHistory.activeNow;
      grBbNewRate         = m->backboneHistory.newRatePer5m;
      grBbPhones          = m->backboneHistory.phonesCount;
      grBbTrackersActive  = m->backboneHistory.trackersActive;
      grBbAvgDwell        = m->backboneHistory.avgDwellMin;
      break;

    case GT_CMD_BACKBONE_PROXIMITY:
      if (len < GT_HDR_SIZE + 6) break;
      grBbZoneFar       = m->backboneProximity.zoneFar;
      grBbZoneNear      = m->backboneProximity.zoneNear;
      grBbZoneClose     = m->backboneProximity.zoneClose;
      grBbZoneImmediate = m->backboneProximity.zoneImmediate;
      grBbClosestRssi   = m->backboneProximity.closestRssi;
      grBbApproaching   = m->backboneProximity.approaching;
      break;

    case GT_CMD_BACKBONE_BEACON:
      if (len < GT_HDR_SIZE + 4) break;
      grBbBeaconCount    = m->backboneBeacon.beaconCount;
      grBbIBeacons       = m->backboneBeacon.iBeacons;
      grBbEddystones     = m->backboneBeacon.eddystones;
      grBbNearBeaconRssi = m->backboneBeacon.nearestBeaconRssi;
      break;

    case GT_CMD_BACKBONE_CLIENTS:
      if (len < GT_HDR_SIZE + 3) break;
      grBbWifiClients   = m->backboneClients.wifiClients;
      grBbActiveClients = m->backboneClients.activeClients;
      grBbProbeCount    = m->backboneClients.probeCount;
      break;

    case GT_CMD_LEARNER_SAVE: {
      if (len < GT_HDR_SIZE + 3) break;
      uint8_t idx = m->learnerData.chunkIdx;
      uint8_t total = m->learnerData.totalChunks;
      uint8_t dLen = m->learnerData.dataLen;
      if (dLen > 200) dLen = 200;
      if (idx == 0) {
        memset(grLearnerBuf, 0, sizeof(grLearnerBuf));
        grLearnerBufLen = 0;
        grLearnerChunksRx = 0;
        grLearnerChunksTotal = total;
      }
      uint16_t offset = (uint16_t)idx * 200;
      if (offset + dLen <= sizeof(grLearnerBuf)) {
        memcpy(grLearnerBuf + offset, m->learnerData.data, dLen);
        grLearnerBufLen = offset + dLen;
        grLearnerChunksRx++;
      }
      if (grLearnerChunksRx >= grLearnerChunksTotal) {
        grLearnerSavePending = true;
        Serial.printf("[link] learner state received (%u bytes)\n", grLearnerBufLen);
      }
    } break;

    case GT_CMD_LEARNER_LOAD: {
      if (len < GT_HDR_SIZE + 3) break;
      if (m->learnerData.chunkIdx == 0xFF) {
        grLearnerLoadRequested = true;
        Serial.println(F("[link] learner state load requested by S3"));
      }
    } break;

    default:
      break;
  }
}

// ---- receive callback (ESP32 core version safe) ----------------------------
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
static void grLinkOnRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  grLinkHandleRx(info->src_addr, data, len);
}
#else
static void grLinkOnRecv(const uint8_t* mac, const uint8_t* data, int len) {
  grLinkHandleRx(mac, data, len);
}
#endif

// ---- init ------------------------------------------------------------------
static inline bool grLinkBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println(F("[link] ESP-NOW init FAILED"));
    return false;
  }
  esp_now_register_recv_cb(grLinkOnRecv);

  esp_now_peer_info_t bc{};
  memcpy(bc.peer_addr, GR_BROADCAST, 6);
  bc.channel = 0;
  bc.encrypt = false;
  esp_now_add_peer(&bc);

  grLinkReady = true;
  Serial.print(F("[link] ESP-NOW ready CH1, MAC: "));
  Serial.println(WiFi.macAddress());
  return true;
}

// ---- add peer as unicast when discovered -----------------------------------
static inline void grLinkAddPeer(GrPeer* p) {
  if (p->added || !p->found) return;
  esp_now_peer_info_t pi{};
  memcpy(pi.peer_addr, p->mac, 6);
  pi.channel = 0;
  pi.encrypt = false;
  esp_now_add_peer(&pi);
  p->added = true;
}

// ---- send helpers ----------------------------------------------------------
static inline void grLinkSendMsg(const GtLinkMsg& m, size_t sz) {
  if (!grLinkReady) return;
  esp_now_send(GR_BROADCAST, (const uint8_t*)&m, sz);
  grLinkLastTx = millis();
}

static inline void grLinkSendHeartbeat() {
  GtLinkMsg m{};
  m.magic[0]   = GT_LINK_MAGIC_0;
  m.magic[1]   = GT_LINK_MAGIC_1;
  m.cmd        = GT_CMD_HEARTBEAT;
  m.boardType  = GR_BOARD_TYPE;
  m.radioCount = 0;
  m.attacking  = 0;
  m.screen     = 0;
  grLinkSendMsg(m, GT_HDR_SIZE);
}

static inline void grLinkSendR40tStatus(uint8_t screen, uint16_t sdFreeMb,
                                         uint16_t heapFree, bool muted) {
  GtLinkMsg m{};
  m.magic[0]   = GT_LINK_MAGIC_0;
  m.magic[1]   = GT_LINK_MAGIC_1;
  m.cmd        = GT_CMD_R40T_STATUS;
  m.boardType  = GR_BOARD_TYPE;
  m.r40tStatus.screen       = screen;
  m.r40tStatus.sdFreeMb     = sdFreeMb;
  m.r40tStatus.heapFree     = heapFree;
  m.r40tStatus.speakerMuted = muted ? 1 : 0;
  grLinkSendMsg(m, GT_HDR_SIZE + 6);
}

static inline void grLinkSendButtonRoute(uint8_t target) {
  GtLinkMsg m{};
  m.magic[0]   = GT_LINK_MAGIC_0;
  m.magic[1]   = GT_LINK_MAGIC_1;
  m.cmd        = GT_CMD_BUTTON_ROUTE;
  m.boardType  = GR_BOARD_TYPE;
  m.buttonRoute.targetNode = target;
  grLinkSendMsg(m, GT_HDR_SIZE + 1);
}

// ---- send learner state back to Node 1 (chunked) --------------------------
static inline void grLinkSendLearnerState(const uint8_t* data, size_t len) {
  if (!grLinkReady || len == 0 || len > 2048) return;
  uint8_t totalChunks = (len + 199) / 200;
  for (uint8_t i = 0; i < totalChunks; i++) {
    GtLinkMsg m{};
    m.magic[0] = GT_LINK_MAGIC_0;
    m.magic[1] = GT_LINK_MAGIC_1;
    m.cmd = GT_CMD_LEARNER_LOAD;
    m.boardType = GR_BOARD_TYPE;
    m.learnerData.chunkIdx = i;
    m.learnerData.totalChunks = totalChunks;
    uint16_t offset = (uint16_t)i * 200;
    uint8_t chunkLen = (offset + 200 <= len) ? 200 : (len - offset);
    m.learnerData.dataLen = chunkLen;
    memcpy(m.learnerData.data, data + offset, chunkLen);
    grLinkSendMsg(m, GT_HDR_SIZE + 3 + chunkLen);
    delay(15);
  }
  Serial.printf("[link] learner state sent to S3 (%u bytes, %u chunks)\n",
                (unsigned)len, totalChunks);
}

// ---- pump: heartbeat + timeout + peer management ---------------------------
static uint32_t grLinkLastHb = 0;
static inline void grLinkPump() {
  if (!grLinkReady) return;
  uint32_t now = millis();

  if (grPeer0.found && !grPeer0.added) grLinkAddPeer(&grPeer0);
  if (grPeer1.found && !grPeer1.added) grLinkAddPeer(&grPeer1);
  if (grPeer3.found && !grPeer3.added) grLinkAddPeer(&grPeer3);

  if (now - grLinkLastHb >= GT_LINK_HB_MS) {
    grLinkLastHb = now;
    grLinkSendHeartbeat();
  }

  // update link status
  bool was0 = grPeer0.up, was1 = grPeer1.up, was3 = grPeer3.up;
  grPeer0.up = grPeer0.found && (now - grPeer0.lastRx < GT_LINK_TIMEOUT);
  grPeer1.up = grPeer1.found && (now - grPeer1.lastRx < GT_LINK_TIMEOUT);
  grPeer3.up = grPeer3.found && (now - grPeer3.lastRx < GT_LINK_TIMEOUT);
  if (grPeer0.up && !was0) Serial.println(F("[link] Node 0 CONNECTED"));
  if (!grPeer0.up && was0) Serial.println(F("[link] Node 0 LOST"));
  if (grPeer1.up && !was1) Serial.println(F("[link] Node 1 CONNECTED"));
  if (!grPeer1.up && was1) Serial.println(F("[link] Node 1 LOST"));
  if (grPeer3.up && !was3) Serial.println(F("[link] Node 3 CONNECTED"));
  if (!grPeer3.up && was3) Serial.println(F("[link] Node 3 LOST"));
}
