#pragma once
// ############################################################################
//  gs_espnow.h  —  GHOST TX S3 NEURAL CO-PROCESSOR  /  ESP-NOW link
//
//  ESP-NOW communication between the S3 co-processor (board=1) and the
//  main ESP32-WROOM-32U (board=0).
//
//  MUST use the EXACT same protocol as gt_espnow.h on the main ESP:
//    Magic:    'G','T'
//    Commands: 0-9 (GtLinkCmd enum)
//    Struct:   packed union GtLinkMsg
//
//  Auto-discovery via broadcast heartbeats → unicast once partner found.
//  Shimmed for Arduino-ESP32 2.x and 3.x (callback signature changed).
// ############################################################################

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "gs_config.h"
#include "gs_telemetry.h"

// ---- protocol (MUST match gt_espnow.h on the main ESP) --------------------
#define GT_LINK_MAGIC_0  'G'
#define GT_LINK_MAGIC_1  'T'
#define GT_LINK_HB_MS    2000     // S3 sends heartbeat every 2s
#define GT_LINK_TIMEOUT  10000    // partner lost after 10s
#define GT_HDR_SIZE      7       // magic(2)+cmd+boardType+radioCount+attacking+screen

enum GtLinkCmd : uint8_t {
  GT_CMD_HEARTBEAT    = 0,
  GT_CMD_FLOOD        = 1,
  GT_CMD_STOP         = 2,
  GT_CMD_SYNC         = 3,
  GT_CMD_BUTTON       = 4,   // S3→main: button press (all 5)
  GT_CMD_TELEMETRY    = 5,   // main→S3: radio state snapshot
  GT_CMD_ALERT        = 6,   // S3→main: neural alert
  GT_CMD_DISPLAY      = 7,   // S3→main: text for OLED2
  GT_CMD_AUDIO        = 8,   // S3→main: play beep
  GT_CMD_NEURAL_STATE = 9,   // S3→main: neural engine status
  GT_CMD_NEURAL_CMD   = 10,  // main→S3: neural command request
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
  // ---- RF TX from SD via Node 0 ----
  GT_CMD_RF_TX_REQUEST      = 25,
  // ---- Learner state persistence (S3 ↔ R40T) ----
  GT_CMD_LEARNER_SAVE       = 26,
  GT_CMD_LEARNER_LOAD       = 27,
};

// Packed union struct — EXACT match to gt_espnow.h's GtLinkMsg
struct __attribute__((packed)) GtLinkMsg {
  uint8_t  magic[2];       // 'G','T'
  uint8_t  cmd;            // GtLinkCmd
  uint8_t  boardType;      // 0=main, 1=S3
  uint8_t  radioCount;     // how many radios detected
  uint8_t  attacking;      // 1 = currently transmitting
  uint8_t  screen;         // current screen enum
  union {
    struct { uint8_t buttonId; } button;                      // GT_CMD_BUTTON
    struct { uint8_t level; char msg[20]; } alert;            // GT_CMD_ALERT
    struct { char line1[21]; char line2[21]; } display;       // GT_CMD_DISPLAY
    struct { uint16_t freq; uint16_t durMs; } audio;          // GT_CMD_AUDIO
    struct {
      uint8_t  ccPresent;
      uint8_t  wifiActive;
      uint8_t  bleActive;
      uint8_t  ccJamming;
      float    ccFreqMHz;
      int8_t   ccRssi;
    } telemetry;                                              // GT_CMD_TELEMETRY
    struct {
      uint8_t  engineState;
      uint8_t  confidence;
      uint8_t  rulesFired;
    } neuralState;                                            // GT_CMD_NEURAL_STATE
    struct {
      uint8_t  commandId;
      uint8_t  param;
    } neuralCmd;                                              // GT_CMD_NEURAL_CMD
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
static bool     gsLinkReady     = false;
static bool     gsLinkFound     = false;
static bool     gsLinkUp        = false;
static uint32_t gsLinkLastRx    = 0;
static uint32_t gsLinkLastTx    = 0;
static uint8_t  gsLinkPeerMac[6] = {};
static bool     gsLinkPeerAdded = false;

// Partner state from heartbeats
static uint8_t  gsPartnerRadios    = 0;
static bool     gsPartnerAttacking = false;
static uint8_t  gsPartnerScreen    = 0;

// Incoming command queue
static volatile uint8_t gsLinkInCmd = 0xFF;
static volatile uint8_t gsNeuralCmdIn = 0xFF;

// Link quality tracking
static uint32_t gsLinkRxCount    = 0;
static uint32_t gsLinkTxCount    = 0;
static uint32_t gsLinkRxErrors   = 0;
static float    gsLinkQuality    = 0;

// Button routing: 0=Node 0 (default), 2=R40T
static uint8_t  gsButtonTarget   = 0;

// Node 2 (R40T) peer tracking
static bool     gsLinkR40tFound  = false;
static bool     gsLinkR40tUp     = false;
static uint32_t gsLinkR40tLastRx = 0;
static uint8_t  gsLinkR40tMac[6] = {};
static bool     gsLinkR40tAdded  = false;

// Node 3 (Backbone) peer tracking
static bool     gsLinkBbFound  = false;
static bool     gsLinkBbUp     = false;
static uint32_t gsLinkBbLastRx = 0;
static uint8_t  gsLinkBbMac[6] = {};
static bool     gsLinkBbAdded  = false;

static const uint8_t GS_BROADCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// ---- convert incoming telemetry to GsTelemetry and feed neural engine ------
static void gsLinkIngestTelemetry(const GtLinkMsg* m) {
  GsTelemetry t = {};
  t.ccPresent   = m->telemetry.ccPresent;
  t.ccActive    = m->attacking ? m->telemetry.ccPresent : 0;
  t.wifiActive  = m->telemetry.wifiActive;
  t.bleActive   = m->telemetry.bleActive;
  t.ccJamming   = m->telemetry.ccJamming;
  t.ccFreqMHz   = m->telemetry.ccFreqMHz;
  t.ccRssi      = m->telemetry.ccRssi;
  t.screen      = m->screen;
  t.radioCount  = m->radioCount;
  t.attacking   = m->attacking;
  t.attackMode  = GS_ATK_NONE; // not in telemetry payload; derived from flags
  t.pktCount    = 0;           // not in this payload
  t.uptimeSec   = millis() / 1000;
  t.timestampMs = millis();

  // Derive attack mode from flags if attacking
  if (m->attacking) {
    if (m->telemetry.ccJamming)
      t.attackMode = GS_ATK_CC_JAM;
    else if (m->telemetry.wifiActive && m->telemetry.bleActive)
      t.attackMode = GS_ATK_WIFI_BLE;
    else if (m->telemetry.wifiActive)
      t.attackMode = GS_ATK_WIFI_BEACON;
    else if (m->telemetry.bleActive)
      t.attackMode = GS_ATK_BLE_TX;
    else
      t.attackMode = GS_ATK_CC_TX;
  }

  gsTelemIngest(t);
}

// ---- learner state reassembly from Node 2 -----------------------------------
static uint8_t  gsLearnerBuf[2048] = {};
static uint16_t gsLearnerBufLen = 0;
static uint8_t  gsLearnerChunksRx = 0;
static uint8_t  gsLearnerChunksTotal = 0;
static bool     gsLearnerLoaded = false;

static void gsLearnerLoadChunk(const GtLinkMsg* m) {
  uint8_t idx = m->learnerData.chunkIdx;
  uint8_t total = m->learnerData.totalChunks;
  uint8_t dLen = m->learnerData.dataLen;
  if (dLen > 200) dLen = 200;

  if (idx == 0) {
    memset(gsLearnerBuf, 0, sizeof(gsLearnerBuf));
    gsLearnerBufLen = 0;
    gsLearnerChunksRx = 0;
    gsLearnerChunksTotal = total;
  }

  uint16_t offset = (uint16_t)idx * 200;
  if (offset + dLen > sizeof(gsLearnerBuf)) return;

  memcpy(gsLearnerBuf + offset, m->learnerData.data, dLen);
  gsLearnerBufLen = offset + dLen;
  gsLearnerChunksRx++;

  if (gsLearnerChunksRx >= gsLearnerChunksTotal) {
    gsLearnerLoaded = true;
    Serial.printf("[link] learner state received (%u bytes, %u chunks)\n",
                  gsLearnerBufLen, gsLearnerChunksRx);
  }
}

// ---- handle received message -----------------------------------------------
static void gsLinkHandleRx(const uint8_t* mac, const uint8_t* raw, int len) {
  if (len < 7) return; // minimum: magic(2) + cmd + boardType + radioCount + attacking + screen
  const GtLinkMsg* m = (const GtLinkMsg*)raw;
  if (m->magic[0] != GT_LINK_MAGIC_0 || m->magic[1] != GT_LINK_MAGIC_1) {
    gsLinkRxErrors++;
    return;
  }
  if (m->boardType == GS_BOARD_TYPE) return; // ignore our own

  gsLinkRxCount++;

  // Track Node 0 (boardType=0) and Node 2 (boardType=2) separately
  if (m->boardType == 0) {
    gsLinkLastRx = millis();
    gsPartnerRadios    = m->radioCount;
    gsPartnerAttacking = (m->attacking != 0);
    gsPartnerScreen    = m->screen;
    if (!gsLinkFound) {
      memcpy(gsLinkPeerMac, mac, 6);
      gsLinkFound = true;
    }
  } else if (m->boardType == 2) {
    gsLinkR40tLastRx = millis();
    if (!gsLinkR40tFound) {
      memcpy(gsLinkR40tMac, mac, 6);
      gsLinkR40tFound = true;
      Serial.printf("[link] R40T discovered: %02X:%02X:%02X:%02X:%02X:%02X\n",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
  } else if (m->boardType == 3) {
    gsLinkBbLastRx = millis();
    if (!gsLinkBbFound) {
      memcpy(gsLinkBbMac, mac, 6);
      gsLinkBbFound = true;
      Serial.printf("[link] Backbone discovered: %02X:%02X:%02X:%02X:%02X:%02X\n",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
  }

  switch (m->cmd) {
    case GT_CMD_HEARTBEAT:
      break;

    case GT_CMD_TELEMETRY:
      if (len < GT_HDR_SIZE + 9) break;
      gsLinkIngestTelemetry(m);
      break;

    case GT_CMD_FLOOD:
    case GT_CMD_STOP:
    case GT_CMD_SYNC:
      gsLinkInCmd = m->cmd;
      break;

    case GT_CMD_NEURAL_CMD:
      if (len < GT_HDR_SIZE + 2) break;
      gsNeuralCmdIn = m->neuralCmd.commandId;
      Serial.printf("[link] neural cmd %d from main ESP\n", m->neuralCmd.commandId);
      break;

    case GT_CMD_BUTTON_ROUTE:
      if (len < GT_HDR_SIZE + 1) break;
      gsButtonTarget = m->buttonRoute.targetNode;
      Serial.printf("[link] button route -> Node %d\n", m->buttonRoute.targetNode);
      break;

    case GT_CMD_LEARNER_LOAD:
      if (len < GT_HDR_SIZE + 3) break;
      gsLearnerLoadChunk(m);
      break;

    default:
      break;
  }

  // Update link quality (EMA of success rate)
  float success = (gsLinkRxCount > 0) ?
    1.0f - (float)gsLinkRxErrors / (float)(gsLinkRxCount + gsLinkRxErrors) : 0;
  gsLinkQuality = gsLinkQuality * 0.95f + success * 0.05f;
}

// ---- receive callback (ESP32 core version safe) ----------------------------
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
static void gsLinkOnRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  gsLinkHandleRx(info->src_addr, data, len);
}
#else
static void gsLinkOnRecv(const uint8_t* mac, const uint8_t* data, int len) {
  gsLinkHandleRx(mac, data, len);
}
#endif

// ---- init ------------------------------------------------------------------
static inline bool gsLinkBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println(F("[link] ESP-NOW init FAILED"));
    return false;
  }
  esp_now_register_recv_cb(gsLinkOnRecv);

  esp_now_peer_info_t bc{};
  memcpy(bc.peer_addr, GS_BROADCAST, 6);
  bc.channel = 0;
  bc.encrypt = false;
  esp_now_add_peer(&bc);

  gsLinkReady = true;
  Serial.print(F("[link] ESP-NOW ready CH1, MAC: "));
  Serial.println(WiFi.macAddress());
  return true;
}

// ---- low-level send (ALWAYS broadcast — mesh needs every node to hear) -----
static inline void gsLinkSendMsg(const GtLinkMsg& m, size_t sz) {
  if (!gsLinkReady) return;
  esp_now_send(GS_BROADCAST, (const uint8_t*)&m, sz);
  gsLinkLastTx = millis();
  gsLinkTxCount++;
}

// ---- add partner as unicast peer -------------------------------------------
static inline void gsLinkAddPeer() {
  if (gsLinkPeerAdded || !gsLinkFound) return;
  esp_now_peer_info_t pi{};
  memcpy(pi.peer_addr, gsLinkPeerMac, 6);
  pi.channel = 0;
  pi.encrypt = false;
  esp_now_add_peer(&pi);
  gsLinkPeerAdded = true;
  Serial.printf("[link] partner: %02X:%02X:%02X:%02X:%02X:%02X\n",
    gsLinkPeerMac[0], gsLinkPeerMac[1], gsLinkPeerMac[2],
    gsLinkPeerMac[3], gsLinkPeerMac[4], gsLinkPeerMac[5]);
}

// ---- typed send helpers ----------------------------------------------------

static inline void gsLinkSendHeartbeat() {
  GtLinkMsg m{};
  m.magic[0]   = GT_LINK_MAGIC_0;
  m.magic[1]   = GT_LINK_MAGIC_1;
  m.cmd        = GT_CMD_HEARTBEAT;
  m.boardType  = GS_BOARD_TYPE;
  m.radioCount = 0;
  m.attacking  = 0;
  m.screen     = 0;
  gsLinkSendMsg(m, GT_HDR_SIZE);
}

static inline void gsLinkSendButton(uint8_t buttonId) {
  GtLinkMsg m{};
  m.magic[0]   = GT_LINK_MAGIC_0;
  m.magic[1]   = GT_LINK_MAGIC_1;
  m.cmd        = GT_CMD_BUTTON;
  m.boardType  = GS_BOARD_TYPE;
  m.button.buttonId = buttonId;
  gsLinkSendMsg(m, GT_HDR_SIZE + 1);
}

static inline void gsLinkSendAlert(uint8_t level, const char* msg) {
  GtLinkMsg m{};
  m.magic[0]   = GT_LINK_MAGIC_0;
  m.magic[1]   = GT_LINK_MAGIC_1;
  m.cmd        = GT_CMD_ALERT;
  m.boardType  = GS_BOARD_TYPE;
  m.alert.level = level;
  strncpy(m.alert.msg, msg, 20);
  gsLinkSendMsg(m, GT_HDR_SIZE + 21);
}

static inline void gsLinkSendDisplay(const char* l1, const char* l2) {
  GtLinkMsg m{};
  m.magic[0]   = GT_LINK_MAGIC_0;
  m.magic[1]   = GT_LINK_MAGIC_1;
  m.cmd        = GT_CMD_DISPLAY;
  m.boardType  = GS_BOARD_TYPE;
  strncpy(m.display.line1, l1, 21);
  strncpy(m.display.line2, l2, 21);
  gsLinkSendMsg(m, GT_HDR_SIZE + 42);
}

static inline void gsLinkSendAudio(uint16_t freqHz, uint16_t durMs) {
  GtLinkMsg m{};
  m.magic[0]   = GT_LINK_MAGIC_0;
  m.magic[1]   = GT_LINK_MAGIC_1;
  m.cmd        = GT_CMD_AUDIO;
  m.boardType  = GS_BOARD_TYPE;
  m.audio.freq  = freqHz;
  m.audio.durMs = durMs;
  gsLinkSendMsg(m, GT_HDR_SIZE + 4);
}

static inline void gsLinkSendNeuralState(uint8_t state, uint8_t confidence,
                                          uint8_t rulesFired) {
  GtLinkMsg m{};
  m.magic[0]   = GT_LINK_MAGIC_0;
  m.magic[1]   = GT_LINK_MAGIC_1;
  m.cmd        = GT_CMD_NEURAL_STATE;
  m.boardType  = GS_BOARD_TYPE;
  m.neuralState.engineState = state;
  m.neuralState.confidence  = confidence;
  m.neuralState.rulesFired  = rulesFired;
  gsLinkSendMsg(m, GT_HDR_SIZE + 3);
}

// ---- send neural command to main ESP (voice command dispatch) ----------------
static inline void gsLinkSendNeuralCmd(uint8_t cmdId) {
  GtLinkMsg m{};
  m.magic[0]   = GT_LINK_MAGIC_0;
  m.magic[1]   = GT_LINK_MAGIC_1;
  m.cmd        = GT_CMD_NEURAL_CMD;
  m.boardType  = GS_BOARD_TYPE;
  m.neuralCmd.commandId = cmdId;
  m.neuralCmd.param = 0;
  gsLinkSendMsg(m, GT_HDR_SIZE + 2);
  Serial.printf("[link] voice cmd %d sent to main ESP\n", cmdId);
}

// ---- add R40T as unicast peer ---------------------------------------------
static inline void gsLinkAddR40tPeer() {
  if (gsLinkR40tAdded || !gsLinkR40tFound) return;
  esp_now_peer_info_t pi{};
  memcpy(pi.peer_addr, gsLinkR40tMac, 6);
  pi.channel = 0;
  pi.encrypt = false;
  esp_now_add_peer(&pi);
  gsLinkR40tAdded = true;
  Serial.printf("[link] R40T paired: %02X:%02X:%02X:%02X:%02X:%02X\n",
    gsLinkR40tMac[0], gsLinkR40tMac[1], gsLinkR40tMac[2],
    gsLinkR40tMac[3], gsLinkR40tMac[4], gsLinkR40tMac[5]);
}

static inline void gsLinkAddBbPeer() {
  if (gsLinkBbAdded || !gsLinkBbFound) return;
  esp_now_peer_info_t pi{};
  memcpy(pi.peer_addr, gsLinkBbMac, 6);
  pi.channel = 0;
  pi.encrypt = false;
  esp_now_add_peer(&pi);
  gsLinkBbAdded = true;
  Serial.printf("[link] Backbone paired: %02X:%02X:%02X:%02X:%02X:%02X\n",
    gsLinkBbMac[0], gsLinkBbMac[1], gsLinkBbMac[2],
    gsLinkBbMac[3], gsLinkBbMac[4], gsLinkBbMac[5]);
}

// ---- pump: heartbeat + timeout + peer management ---------------------------
static inline void gsLinkPump() {
  if (!gsLinkReady) return;
  uint32_t now = millis();

  if (gsLinkFound && !gsLinkPeerAdded) gsLinkAddPeer();
  if (gsLinkR40tFound && !gsLinkR40tAdded) gsLinkAddR40tPeer();
  if (gsLinkBbFound && !gsLinkBbAdded) gsLinkAddBbPeer();

  // Send heartbeat
  if (now - gsLinkLastTx >= GT_LINK_HB_MS) {
    gsLinkSendHeartbeat();
  }

  // Node 0 link
  bool wasUp = gsLinkUp;
  gsLinkUp = gsLinkFound && (now - gsLinkLastRx < GT_LINK_TIMEOUT);
  if (gsLinkUp && !wasUp) Serial.println(F("[link] Node 0 CONNECTED"));
  if (!gsLinkUp && wasUp) Serial.println(F("[link] Node 0 LOST"));

  // R40T link
  bool wasR40t = gsLinkR40tUp;
  gsLinkR40tUp = gsLinkR40tFound && (now - gsLinkR40tLastRx < GT_LINK_TIMEOUT);
  if (gsLinkR40tUp && !wasR40t) Serial.println(F("[link] R40T CONNECTED"));
  if (!gsLinkR40tUp && wasR40t) Serial.println(F("[link] R40T LOST"));

  // Backbone link
  bool wasBb = gsLinkBbUp;
  gsLinkBbUp = gsLinkBbFound && (now - gsLinkBbLastRx < GT_LINK_TIMEOUT);
  if (gsLinkBbUp && !wasBb) Serial.println(F("[link] Backbone CONNECTED"));
  if (!gsLinkBbUp && wasBb) Serial.println(F("[link] Backbone LOST"));
}

// ---- pop incoming command --------------------------------------------------
static inline uint8_t gsLinkPopCmd() {
  uint8_t c = gsLinkInCmd;
  gsLinkInCmd = 0xFF;
  return c;
}

// ---- pop neural command from main ESP ------------------------------------
static inline uint8_t gsLinkPopNeuralCmd() {
  uint8_t c = gsNeuralCmdIn;
  gsNeuralCmdIn = 0xFF;
  return c;
}

// ---- send learner state to Node 2 for SD persistence -----------------------
static inline void gsLinkSendLearnerState(const uint8_t* data, size_t len) {
  if (!gsLinkReady || len == 0 || len > 2048) return;

  uint8_t totalChunks = (len + 199) / 200;
  for (uint8_t i = 0; i < totalChunks; i++) {
    GtLinkMsg m{};
    m.magic[0] = GT_LINK_MAGIC_0;
    m.magic[1] = GT_LINK_MAGIC_1;
    m.cmd = GT_CMD_LEARNER_SAVE;
    m.boardType = GS_BOARD_TYPE;
    m.learnerData.chunkIdx = i;
    m.learnerData.totalChunks = totalChunks;

    uint16_t offset = (uint16_t)i * 200;
    uint8_t chunkLen = (offset + 200 <= len) ? 200 : (len - offset);
    m.learnerData.dataLen = chunkLen;
    memcpy(m.learnerData.data, data + offset, chunkLen);

    gsLinkSendMsg(m, GT_HDR_SIZE + 3 + chunkLen);
    delay(15);
  }
  Serial.printf("[link] learner state sent (%u bytes, %u chunks) to R40T\n",
                (unsigned)len, totalChunks);
}

// ---- request learner state from Node 2 on boot ----------------------------
static inline void gsLinkRequestLearnerState() {
  GtLinkMsg m{};
  m.magic[0] = GT_LINK_MAGIC_0;
  m.magic[1] = GT_LINK_MAGIC_1;
  m.cmd = GT_CMD_LEARNER_LOAD;
  m.boardType = GS_BOARD_TYPE;
  m.learnerData.chunkIdx = 0xFF;
  m.learnerData.totalChunks = 0;
  m.learnerData.dataLen = 0;
  gsLinkSendMsg(m, GT_HDR_SIZE + 3);
  Serial.println(F("[link] learner state requested from R40T"));
}

// ---- query helpers ---------------------------------------------------------
static inline bool  gsLinkIsUp()       { return gsLinkUp; }
static inline float gsLinkGetQuality() { return gsLinkQuality; }
