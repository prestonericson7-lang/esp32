#pragma once
// ############################################################################
//  gg_espnow.h  -  GHOST TX CC1101 EDITION  /  ESP-NOW link receiver
//
//  Receives button events and commands from the ESP32-S3 co-processor
//  via ESP-NOW. Protocol matches gs_espnow.h on the S3 side exactly.
//
//  Button IDs: 0=UP, 1=DOWN, 2=SELECT, 3=BACK, 4=SWAP
// ############################################################################

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "gg_config.h"

#define GT_LINK_MAGIC_0  'G'
#define GT_LINK_MAGIC_1  'T'
#define GT_LINK_HB_MS    2000
#define GT_HDR_SIZE      7       // magic(2)+cmd+boardType+radioCount+attacking+screen

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
  GT_CMD_CAPTURE_DATA  = 11,
  GT_CMD_CAPTURE_META  = 12,
  GT_CMD_SORT_STATUS   = 13,
  GT_CMD_R40T_STATUS   = 14,
  GT_CMD_BUTTON_ROUTE  = 15,
  GT_CMD_MODE_CHANGE   = 16,
  GT_CMD_CLOCK_SYNC    = 17,
  GT_CMD_BACKBONE_STATUS    = 18,
  // ---- backbone expansion (6 new) ----
  GT_CMD_BACKBONE_WIFI      = 19,
  GT_CMD_BACKBONE_DEAUTH    = 20,
  GT_CMD_BACKBONE_HISTORY   = 21,
  GT_CMD_BACKBONE_PROXIMITY = 22,
  GT_CMD_BACKBONE_BEACON    = 23,
  GT_CMD_BACKBONE_CLIENTS   = 24,
  // ---- RF TX from SD via R40T ----
  GT_CMD_RF_TX_REQUEST      = 25,
  GT_CMD_LEARNER_SAVE       = 26,
  GT_CMD_LEARNER_LOAD       = 27,
};

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
    uint8_t raw[212];
  };
};

// volatile flags set from ISR context — read and cleared by ggInputPoll()
static volatile bool ggEnUp   = false;
static volatile bool ggEnDown = false;
static volatile bool ggEnSel  = false;
static volatile bool ggEnBack = false;
static volatile bool ggEnSwap = false;

// neural state received from S3 (GT_CMD_NEURAL_STATE)
static volatile uint8_t ggNnState      = 0;
static volatile uint8_t ggNnConfidence = 0;
static volatile uint8_t ggNnRulesFired = 0;

// last alert received from S3 (GT_CMD_ALERT)
static volatile uint8_t ggLastAlertLvl = 0;
static char             ggLastAlertMsg[21] = {};
static volatile bool    ggNewAlert = false;

// audio request from S3 (GT_CMD_AUDIO)
static volatile uint16_t ggAudioReqFreq = 0;
static volatile uint16_t ggAudioReqMs   = 0;
static volatile bool     ggAudioReqPend = false;

// voice neural command from S3 (GT_CMD_NEURAL_CMD, voice-triggered)
static volatile uint8_t ggVoiceNeuralCmd = 0xFF;

// R40T status (GT_CMD_R40T_STATUS from Node 2)
static volatile uint8_t  ggR40tScreen     = 0;
static volatile uint16_t ggR40tSdFreeMb   = 0;
static volatile uint16_t ggR40tHeapFree   = 0;
static volatile bool     ggR40tMuted      = false;

// button routing (GT_CMD_BUTTON_ROUTE)
static volatile uint8_t  ggButtonTarget   = 0;  // 0=main, 2=R40T
static volatile bool     ggR40tCtrlMode   = false;

// Node 3 (Backbone) peer tracking
static bool     ggLinkBackboneUp      = false;
static uint32_t ggLinkBackboneLastRx  = 0;
static uint8_t  ggLinkBackboneMac[6]  = {};
static bool     ggLinkBackboneFound   = false;
static bool     ggLinkBackboneAdded   = false;

// Backbone status (GT_CMD_BACKBONE_STATUS from Node 3)
static volatile uint8_t  ggBbBleCount   = 0;
static volatile uint8_t  ggBbTrackers   = 0;
static volatile uint8_t  ggBbFlagged    = 0;
static volatile uint8_t  ggBbMeshHealth = 0;
static volatile uint8_t  ggBbLoad       = 0;

// RF TX request from R40T (GT_CMD_RF_TX_REQUEST)
static volatile bool     ggRfTxPending     = false;
static GgCcRecording     ggRfTxRec         = {};
static volatile uint8_t  ggRfTxRepeats     = 1;
static uint8_t           ggRfTxChunksRcvd  = 0;
static uint8_t           ggRfTxTotalChunks = 0;

static bool     ggLinkReady  = false;
static bool     ggLinkUp     = false;  // S3 link
static uint32_t ggLinkLastRx = 0;
static uint8_t  ggLinkPeerMac[6] = {};
static bool     ggLinkPeerFound = false;
static bool     ggLinkPeerAdded = false;

// Node 2 (R40T) peer tracking
static bool     ggLinkR40tUp      = false;
static uint32_t ggLinkR40tLastRx  = 0;
static uint8_t  ggLinkR40tMac[6]  = {};
static bool     ggLinkR40tFound   = false;
static bool     ggLinkR40tAdded   = false;

static void ggLinkHandleRx(const uint8_t* mac, const uint8_t* raw, int len) {
  if (len < 7) return;
  const GtLinkMsg* m = (const GtLinkMsg*)raw;
  if (m->magic[0] != GT_LINK_MAGIC_0 || m->magic[1] != GT_LINK_MAGIC_1) return;
  if (m->boardType == 0) return; // ignore our own (main=0)

  // track S3 (boardType=1) and R40T (boardType=2) separately
  if (m->boardType == 1) {
    ggLinkLastRx = millis();
    if (!ggLinkPeerFound) {
      memcpy(ggLinkPeerMac, mac, 6);
      ggLinkPeerFound = true;
    }
  } else if (m->boardType == 2) {
    ggLinkR40tLastRx = millis();
    if (!ggLinkR40tFound) {
      memcpy(ggLinkR40tMac, mac, 6);
      ggLinkR40tFound = true;
      Serial.printf("[link] R40T discovered: %02X:%02X:%02X:%02X:%02X:%02X\n",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
  } else if (m->boardType == 3) {
    ggLinkBackboneLastRx = millis();
    if (!ggLinkBackboneFound) {
      memcpy(ggLinkBackboneMac, mac, 6);
      ggLinkBackboneFound = true;
      Serial.printf("[link] Backbone discovered: %02X:%02X:%02X:%02X:%02X:%02X\n",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
  }

  switch (m->cmd) {
    case GT_CMD_HEARTBEAT:
      break;

    case GT_CMD_BUTTON:
      if (len < GT_HDR_SIZE + 1) break;
      if (m->button.buttonId == 4) {
        ggEnSwap = true;
      } else if (!ggR40tCtrlMode) {
        switch (m->button.buttonId) {
          case 0: ggEnUp   = true; break;
          case 1: ggEnDown = true; break;
          case 2: ggEnSel  = true; break;
          case 3: ggEnBack = true; break;
        }
      }
      break;

    case GT_CMD_NEURAL_STATE:
      if (len < GT_HDR_SIZE + 3) break;
      ggNnState      = m->neuralState.engineState;
      ggNnConfidence = m->neuralState.confidence;
      ggNnRulesFired = m->neuralState.rulesFired;
      break;

    case GT_CMD_ALERT:
      if (len < GT_HDR_SIZE + 21) break;
      ggLastAlertLvl = m->alert.level;
      memcpy(ggLastAlertMsg, m->alert.msg, 20);
      ggLastAlertMsg[20] = '\0';
      ggNewAlert = true;
      break;

    case GT_CMD_AUDIO:
      if (len < GT_HDR_SIZE + 4) break;
      ggAudioReqFreq = m->audio.freq;
      ggAudioReqMs   = m->audio.durMs;
      ggAudioReqPend = true;
      break;

    case GT_CMD_NEURAL_CMD:
      if (len < GT_HDR_SIZE + 2) break;
      ggVoiceNeuralCmd = m->neuralCmd.commandId;
      Serial.printf("[link] voice neural cmd %d from S3\n", m->neuralCmd.commandId);
      break;

    case GT_CMD_R40T_STATUS:
      if (len < GT_HDR_SIZE + 6) break;
      ggR40tScreen   = m->r40tStatus.screen;
      ggR40tSdFreeMb = m->r40tStatus.sdFreeMb;
      ggR40tHeapFree = m->r40tStatus.heapFree;
      ggR40tMuted    = m->r40tStatus.speakerMuted;
      break;

    case GT_CMD_BUTTON_ROUTE:
      if (len < GT_HDR_SIZE + 1) break;
      ggButtonTarget = m->buttonRoute.targetNode;
      ggR40tCtrlMode = (m->buttonRoute.targetNode == 2);
      Serial.printf("[link] button route -> Node %d\n", m->buttonRoute.targetNode);
      break;

    case GT_CMD_BACKBONE_STATUS:
      if (len < GT_HDR_SIZE + 8) break;
      ggBbBleCount   = m->backboneStatus.bleDeviceCount;
      ggBbTrackers   = m->backboneStatus.trackersFound;
      ggBbFlagged    = m->backboneStatus.trackersFlagged;
      ggBbMeshHealth = m->backboneStatus.meshHealth;
      ggBbLoad       = m->backboneStatus.load;
      break;

    case GT_CMD_RF_TX_REQUEST:
      if (len < GT_HDR_SIZE + 12) break;
      {
        uint8_t ci   = m->rfTxRequest.chunkIdx;
        uint8_t tc   = m->rfTxRequest.totalChunks;
        uint8_t dLen = m->rfTxRequest.dataLen;
        if (dLen > 200) break;

        if (ci == 0) {
          memset(&ggRfTxRec, 0, sizeof(ggRfTxRec));
          ggRfTxRec.freqMHz    = m->rfTxRequest.freqMHz;
          ggRfTxRec.modulation = 0; // GG_CM_ASK (OOK)
          ggRfTxRec.dataRate   = m->rfTxRequest.dataRate;
          ggRfTxRec.len        = 0;
          ggRfTxTotalChunks    = tc;
          ggRfTxChunksRcvd     = 0;
          ggRfTxRepeats        = m->rfTxRequest.repeats;
        }

        uint16_t offset = (uint16_t)ci * 200;
        if (offset + dLen <= GG_CC_REC_MAX) {
          memcpy(&ggRfTxRec.data[offset], m->rfTxRequest.data, dLen);
          if (offset + dLen > ggRfTxRec.len)
            ggRfTxRec.len = offset + dLen;
        }
        ggRfTxChunksRcvd++;

        if (ggRfTxChunksRcvd >= ggRfTxTotalChunks) {
          ggRfTxRec.valid = true;
          ggRfTxPending   = true;
          Serial.printf("[link] RF TX request: %.2fMHz %d bytes %dx\n",
                        ggRfTxRec.freqMHz, ggRfTxRec.len, ggRfTxRepeats);
        }
      }
      break;

    default:
      break;
  }
}

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
static void ggLinkOnRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  ggLinkHandleRx(info->src_addr, data, len);
}
#else
static void ggLinkOnRecv(const uint8_t* mac, const uint8_t* data, int len) {
  ggLinkHandleRx(mac, data, len);
}
#endif

static inline bool ggLinkBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println(F("[link] ESP-NOW init FAILED"));
    return false;
  }
  esp_now_register_recv_cb(ggLinkOnRecv);

  esp_now_peer_info_t bc{};
  memcpy(bc.peer_addr, (const uint8_t[]){0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, 6);
  bc.channel = 0;
  bc.encrypt = false;
  esp_now_add_peer(&bc);

  ggLinkReady = true;
  Serial.print(F("[link] ESP-NOW ready CH1, MAC: "));
  Serial.println(WiFi.macAddress());
  return true;
}

static inline void ggLinkAddPeer() {
  if (ggLinkPeerAdded || !ggLinkPeerFound) return;
  esp_now_peer_info_t pi{};
  memcpy(pi.peer_addr, ggLinkPeerMac, 6);
  pi.channel = 0;
  pi.encrypt = false;
  esp_now_add_peer(&pi);
  ggLinkPeerAdded = true;
  Serial.printf("[link] S3 paired: %02X:%02X:%02X:%02X:%02X:%02X\n",
    ggLinkPeerMac[0], ggLinkPeerMac[1], ggLinkPeerMac[2],
    ggLinkPeerMac[3], ggLinkPeerMac[4], ggLinkPeerMac[5]);
}

static uint8_t ggLinkRadioCount = 0;

static inline void ggLinkSendHeartbeat() {
  if (!ggLinkReady) return;
  GtLinkMsg m{};
  m.magic[0]   = GT_LINK_MAGIC_0;
  m.magic[1]   = GT_LINK_MAGIC_1;
  m.cmd        = GT_CMD_HEARTBEAT;
  m.boardType  = 0;
  m.radioCount = ggLinkRadioCount;
  m.attacking  = 0;
  m.screen     = 0;
  esp_now_send((const uint8_t[]){0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, (const uint8_t*)&m, GT_HDR_SIZE);
}

static inline void ggLinkSendNeuralCmd(uint8_t cmdId) {
  if (!ggLinkReady) return;
  GtLinkMsg m{};
  m.magic[0]   = GT_LINK_MAGIC_0;
  m.magic[1]   = GT_LINK_MAGIC_1;
  m.cmd        = GT_CMD_NEURAL_CMD;
  m.boardType  = 0;
  m.radioCount = ggLinkRadioCount;
  m.neuralCmd.commandId = cmdId;
  m.neuralCmd.param = 0;
  const uint8_t* dst = ggLinkPeerFound ? ggLinkPeerMac : (const uint8_t[]){0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  esp_now_send(dst, (const uint8_t*)&m, GT_HDR_SIZE + 2);
  Serial.printf("[link] neural cmd %d sent to S3\n", cmdId);
}

// ---- add R40T peer --------------------------------------------------------
static inline void ggLinkAddR40tPeer() {
  if (ggLinkR40tAdded || !ggLinkR40tFound) return;
  esp_now_peer_info_t pi{};
  memcpy(pi.peer_addr, ggLinkR40tMac, 6);
  pi.channel = 0;
  pi.encrypt = false;
  esp_now_add_peer(&pi);
  ggLinkR40tAdded = true;
  Serial.printf("[link] R40T paired: %02X:%02X:%02X:%02X:%02X:%02X\n",
    ggLinkR40tMac[0], ggLinkR40tMac[1], ggLinkR40tMac[2],
    ggLinkR40tMac[3], ggLinkR40tMac[4], ggLinkR40tMac[5]);
}

// ---- add Backbone peer ---------------------------------------------------
static inline void ggLinkAddBackbonePeer() {
  if (ggLinkBackboneAdded || !ggLinkBackboneFound) return;
  esp_now_peer_info_t pi{};
  memcpy(pi.peer_addr, ggLinkBackboneMac, 6);
  pi.channel = 0;
  pi.encrypt = false;
  esp_now_add_peer(&pi);
  ggLinkBackboneAdded = true;
  Serial.printf("[link] Backbone paired: %02X:%02X:%02X:%02X:%02X:%02X\n",
    ggLinkBackboneMac[0], ggLinkBackboneMac[1], ggLinkBackboneMac[2],
    ggLinkBackboneMac[3], ggLinkBackboneMac[4], ggLinkBackboneMac[5]);
}

// ---- send capture data to mesh (broadcast) --------------------------------
static inline void ggLinkSendCaptureData(float freqMHz, uint8_t radioId,
                                          uint8_t mod, const uint8_t* data,
                                          uint8_t len) {
  if (!ggLinkReady || len > 200) return;
  GtLinkMsg m{};
  m.magic[0]   = GT_LINK_MAGIC_0;
  m.magic[1]   = GT_LINK_MAGIC_1;
  m.cmd        = GT_CMD_CAPTURE_DATA;
  m.boardType  = 0;
  m.radioCount = ggLinkRadioCount;
  m.captureData.freqMHz    = freqMHz;
  m.captureData.radioId    = radioId;
  m.captureData.modulation = mod;
  m.captureData.dataLen    = len;
  memcpy(m.captureData.data, data, len);
  esp_now_send((const uint8_t[]){0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
               (const uint8_t*)&m, GT_HDR_SIZE + 7 + len);
}

static inline void ggLinkSendTelemetry(uint8_t wifiAct, uint8_t bleAct,
                                        uint8_t ccJam, float ccFreq,
                                        int8_t ccRssi, uint8_t attacking,
                                        uint8_t screen) {
  if (!ggLinkReady) return;
  GtLinkMsg m{};
  m.magic[0]   = GT_LINK_MAGIC_0;
  m.magic[1]   = GT_LINK_MAGIC_1;
  m.cmd        = GT_CMD_TELEMETRY;
  m.boardType  = 0;
  m.radioCount = ggLinkRadioCount;
  m.attacking  = attacking;
  m.screen     = screen;
  m.telemetry.ccPresent   = ggLinkRadioCount;
  m.telemetry.wifiActive  = wifiAct;
  m.telemetry.bleActive   = bleAct;
  m.telemetry.ccJamming   = ccJam;
  m.telemetry.ccFreqMHz   = ccFreq;
  m.telemetry.ccRssi      = ccRssi;
  esp_now_send((const uint8_t[]){0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
               (const uint8_t*)&m, GT_HDR_SIZE + 9);
}

static inline void ggLinkSendButtonRoute(uint8_t target) {
  if (!ggLinkReady) return;
  GtLinkMsg m{};
  m.magic[0]   = GT_LINK_MAGIC_0;
  m.magic[1]   = GT_LINK_MAGIC_1;
  m.cmd        = GT_CMD_BUTTON_ROUTE;
  m.boardType  = 0;
  m.buttonRoute.targetNode = target;
  esp_now_send((const uint8_t[]){0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
               (const uint8_t*)&m, GT_HDR_SIZE + 1);
}

static uint32_t ggLinkLastHb = 0;
static inline void ggLinkPump() {
  if (!ggLinkReady) return;
  uint32_t now = millis();
  if (ggLinkPeerFound && !ggLinkPeerAdded) ggLinkAddPeer();
  if (ggLinkR40tFound && !ggLinkR40tAdded) ggLinkAddR40tPeer();
  if (ggLinkBackboneFound && !ggLinkBackboneAdded) ggLinkAddBackbonePeer();
  if (now - ggLinkLastHb >= GT_LINK_HB_MS) { ggLinkLastHb = now; ggLinkSendHeartbeat(); }

  // S3 link
  bool wasUp = ggLinkUp;
  ggLinkUp = ggLinkPeerFound && (now - ggLinkLastRx < 10000);
  if (ggLinkUp && !wasUp) Serial.println(F("[link] S3 CONNECTED"));
  if (!ggLinkUp && wasUp) Serial.println(F("[link] S3 LOST"));

  // R40T link
  bool wasR40t = ggLinkR40tUp;
  ggLinkR40tUp = ggLinkR40tFound && (now - ggLinkR40tLastRx < 10000);
  if (ggLinkR40tUp && !wasR40t) Serial.println(F("[link] R40T CONNECTED"));
  if (!ggLinkR40tUp && wasR40t) Serial.println(F("[link] R40T LOST"));

  // Backbone link
  bool wasBb = ggLinkBackboneUp;
  ggLinkBackboneUp = ggLinkBackboneFound && (now - ggLinkBackboneLastRx < 10000);
  if (ggLinkBackboneUp && !wasBb) Serial.println(F("[link] BACKBONE CONNECTED"));
  if (!ggLinkBackboneUp && wasBb) Serial.println(F("[link] BACKBONE LOST"));
}
