#pragma once
// ############################################################################
//  gh_nrf.h  -  KODI NRF24  /  Hub nRF STUB: ESP-NOW proxy to Radio Node
//
//  The Hub has NO nRF24L01+ radios. This header provides stub variables and
//  functions that the UI code references. All actual nRF operations are
//  dispatched to Node 2 over ESP-NOW.
//
//  Provides the same API surface as gg_nrf.h so screen handlers compile:
//    - ggNrfPresentA / ggNrfPresentB  (populated from Node 2 status)
//    - ggNrfBegin()  (no-op, returns true)
//    - ggNrfConfigTx/Rx, ggNrfTxOnce, ggNrfRxPoll  (send ESP-NOW commands)
//    - ggNrfCarrierOn/Off, ggNrfNoiseBegin/End/Blast  (send ESP-NOW commands)
//    - ggNrfScanChannel  (send ESP-NOW command)
//
//  The actual RF24 class is NOT included. No SPI.h dependency.
// ############################################################################

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "gh_config.h"
#include "kodi_bridge.h"
#include "gh_buffer.h"

// ---- Node 2 communication protocol ----------------------------------------
enum GhCmdType : uint8_t {
  GH_CMD_PING = 0,
  GH_CMD_CONFIG,
  GH_CMD_TX_SINGLE,
  GH_CMD_TX_BURST,
  GH_CMD_BEACON_START,
  GH_CMD_BEACON_STOP,
  GH_CMD_CARRIER_START,
  GH_CMD_CARRIER_STOP,
  GH_CMD_SCAN_START,
  GH_CMD_RX_START,
  GH_CMD_RX_STOP,
  GH_CMD_STOP_ALL,
  GH_CMD_NRF_JAM,
  GH_CMD_NRF_JAM_STOP,
  GH_CMD_KPAD_JAM,
  GH_CMD_KPAD_JAM_STOP,
};

enum GhRadioMode : uint8_t {
  GH_MODE_IDLE = 0, GH_MODE_TX, GH_MODE_BEACON, GH_MODE_CARRIER,
  GH_MODE_SCAN, GH_MODE_RX, GH_MODE_JAM, GH_MODE_KPAD,
};
static const char* const GH_MODE_NAME[] = {
  "IDLE","TX","BEACON","CARRIER","SCAN","RX","JAM","KPAD"
};

#pragma pack(push, 1)
struct GhNodeCmd {
  uint8_t  type;
  uint8_t  radio;          // 0=A, 1=B, 2=both
  uint8_t  channel;
  uint8_t  rate;
  uint8_t  power;
  uint8_t  addr[5];
  uint8_t  payloadLen;
  uint8_t  payload[32];
  uint16_t repeatMs;
  uint16_t burstN;
  uint8_t  autoAck;
  uint8_t  addrWidth;
  uint8_t  crc;
  uint8_t  retries;
};

struct GhNodeStatus {
  uint8_t  type;           // 0=status, 1=scan_hit, 2=rx_data
  bool     nrfAOk;
  bool     nrfBOk;
  uint8_t  mode;
  uint32_t txCount;
  uint32_t ackOk;
  uint32_t txFail;
  uint32_t rxCount;
  uint8_t  rxData[32];
  uint8_t  rxLen;
  uint8_t  rxPipe;
  uint8_t  activeChannel;
};
#pragma pack(pop)

// ---- ESP-NOW link state ----------------------------------------------------
static uint8_t ghRadioMac[6] = {0};
static bool    ghRadioLinked = false;
static uint32_t ghLastStatusMs = 0;
static GhNodeStatus ghStatus = {};
static volatile bool ghStatusPending = false;
static GhNodeStatus ghPendingStatus;
static portMUX_TYPE ghStatusMux = portMUX_INITIALIZER_UNLOCKED;

// Stub variables matching gg_nrf.h API
static bool ggNrfPresentA = false;
static bool ggNrfPresentB = false;
static bool ggFloodNrfActive = false;
static uint8_t ggFloodNrfPosA = 0;
static uint8_t ggFloodNrfPosB = 0;

// nRF scan results buffer (populated from radio node scan data)
static uint8_t gNrfScan[126];

// ---- KODI Bridge link state -------------------------------------------------
static KbLinkState ghBridge;
static volatile bool ghBridgePending = false;
static KodiBridge ghPendingBridge;
static portMUX_TYPE ghBridgeMux = portMUX_INITIALIZER_UNLOCKED;
static KbStatusPayload ghBridgeRemoteStatus = {};

// ---- CC1101 bridge result state (set by KB_MSG_DATA handler, read by screens) --
static bool     gCcJamUp          = false;
static bool     gCcRecUp          = false;
static bool     gDualJamUp        = false;
static uint8_t  gCcTxResult       = 0;
static uint32_t gCcTxResultMs     = 0;
static int8_t   gCcScanRssi[64];
static uint8_t  gCcScanSteps      = 0;
static bool     gCcScanReady      = false;
static char     gCcRfidUid[21]    = "";
static uint8_t  gCcRfidLen        = 0;
static bool     gCcRfidReady      = false;
static uint8_t  gCcRecResult      = 0;
static uint32_t gCcRecResultMs    = 0;
static uint8_t  gCcReplayResult   = 0;
static uint32_t gCcReplayResultMs = 0;
static uint8_t  gCcRxResult       = 0;
static uint32_t gCcRxResultMs     = 0;
static uint8_t  gCcBfResult       = 0;
static uint32_t gCcBfResultMs     = 0;
static uint8_t  gCcCrackResult    = 0;
static uint32_t gCcCrackResultMs  = 0;
static bool     gBridgeScanPending = false;
static uint32_t gBridgeScanMs      = 0;
static uint8_t  gBridgeScanSeq     = 0;

static inline void ghBridgeSend(KodiBridge& msg) {
  if (ghBridge.linked) {
    esp_now_send(ghBridge.peerMac, (uint8_t*)&msg, 6 + msg.len);
  } else {
    uint8_t bc[6]; memset(bc, 0xFF, 6);
    esp_now_send(bc, (uint8_t*)&msg, 6 + msg.len);
  }
}

static inline void ghBridgeReply(uint8_t seq, uint8_t cmd, uint8_t result) {
  KodiBridge rsp;
  rsp.magic = KB_MAGIC; rsp.src = KB_UNIT_NRF24;
  rsp.msgType = KB_MSG_DATA; rsp.seq = seq;
  rsp.cmd = cmd; rsp.payload[0] = result; rsp.len = 1;
  ghBridgeSend(rsp);
}

static void ghBridgeHandleCmd(KodiBridge& msg);

// ---- ESP-NOW receive callback -----------------------------------------------
#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void ghOnRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  const uint8_t* mac = info->src_addr;
#else
static void ghOnRecv(const uint8_t* mac, const uint8_t* data, int len) {
#endif
  if (len < 1) return;

  // Bridge message: first byte 0xBD
  if (data[0] == KB_MAGIC && len >= 6 && len <= (int)KB_MSG_MAX) {
    if (!ghBridge.linked) {
      memcpy(ghBridge.peerMac, mac, 6);
      esp_now_peer_info_t pi = {};
      memcpy(pi.peer_addr, mac, 6);
      pi.channel = 0; pi.encrypt = false;
      if (!esp_now_is_peer_exist(mac)) esp_now_add_peer(&pi);
      ghBridge.linked = true;
      Serial.printf("[bridge] CC1101 linked: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    ghBridge.lastRxMs = millis();
    portENTER_CRITICAL(&ghBridgeMux);
    memcpy(&ghPendingBridge, data, min((size_t)len, sizeof(KodiBridge)));
    ghBridgePending = true;
    portEXIT_CRITICAL(&ghBridgeMux);
    return;
  }

  // Internal message from radio node (GhNodeStatus)
  if (len < (int)sizeof(GhNodeStatus)) return;
  if (len > (int)sizeof(GhNodeStatus)) return;
  if (!ghRadioLinked) {
    memcpy(ghRadioMac, mac, 6);
    esp_now_peer_info_t pi = {};
    memcpy(pi.peer_addr, mac, 6);
    pi.channel = 0; pi.encrypt = false;
    if (!esp_now_is_peer_exist(mac)) esp_now_add_peer(&pi);
    ghRadioLinked = true;
    Serial.printf("[espnow] Node 2 linked: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }
  portENTER_CRITICAL(&ghStatusMux);
  memcpy(&ghPendingStatus, data, min((size_t)len, sizeof(GhNodeStatus)));
  ghStatusPending = true;
  portEXIT_CRITICAL(&ghStatusMux);
}

static inline void ghSendCmd(GhNodeCmd& c) {
  if (!ghRadioLinked) {
    uint8_t bc[6]; memset(bc, 0xFF, 6);
    esp_now_send(bc, (uint8_t*)&c, sizeof(c));
  } else {
    esp_now_send(ghRadioMac, (uint8_t*)&c, sizeof(c));
  }
}

static inline GhNodeCmd ghBuildCmd(uint8_t type, const GgNrfProfile& p) {
  GhNodeCmd c = {};
  c.type = type;
  c.channel = p.channel;
  c.rate = p.rate;
  c.power = p.power;
  memcpy(c.addr, p.addr, 5);
  c.payloadLen = p.plLen;
  memcpy(c.payload, p.payload, p.plLen);
  c.repeatMs = p.repeatMs;
  c.burstN = p.burstN;
  c.autoAck = p.autoAck;
  c.addrWidth = p.addrWidth;
  c.crc = p.crc;
  c.retries = p.retries;
  return c;
}

// ---- ESP-NOW init ----------------------------------------------------------
static inline void ghEspNowBegin() {
  kbInitLink(ghBridge);
  ghBufInit();
  if (esp_now_init() != ESP_OK) {
    Serial.println(F("[espnow] init FAILED"));
    return;
  }
  esp_now_register_recv_cb(ghOnRecv);
  esp_now_peer_info_t bc = {};
  memset(bc.peer_addr, 0xFF, 6);
  bc.channel = 0; bc.encrypt = false;
  esp_now_add_peer(&bc);
  GhNodeCmd c = {}; c.type = GH_CMD_PING;
  esp_now_send(bc.peer_addr, (uint8_t*)&c, sizeof(c));
  Serial.println(F("[espnow] pinging for Node 2 + bridge..."));
}

static inline void ghPollStatus() {
  // Process internal radio status (copy to local first — callback runs on core 0)
  if (ghStatusPending) {
    GhNodeStatus localStatus;
    portENTER_CRITICAL(&ghStatusMux);
    memcpy(&localStatus, (const void*)&ghPendingStatus, sizeof(GhNodeStatus));
    ghStatusPending = false;
    portEXIT_CRITICAL(&ghStatusMux);
    ghStatus = localStatus;
    ghLastStatusMs = millis();
    ggNrfPresentA = ghStatus.nrfAOk;
    ggNrfPresentB = ghStatus.nrfBOk;
    if (localStatus.type == 2 && localStatus.rxLen > 0 && localStatus.rxLen <= 32) {
      // Scan results: rxData[0] = channel, rxData[1] = hit count
      if (localStatus.mode == 4 && localStatus.rxLen >= 2 && localStatus.rxData[0] <= 125) {
        uint8_t ch = localStatus.rxData[0];
        uint8_t hits = localStatus.rxData[1];
        if (hits > 0 && gNrfScan[ch] < 255) gNrfScan[ch]++;
      }
      uint8_t dt = (localStatus.mode == 4) ? 0 : 1;
      ghDataForward(dt, localStatus.rxData, localStatus.rxLen, ghBridge);
    }
  }

  // Process bridge messages
  if (ghBridgePending) {
    KodiBridge localBridge;
    portENTER_CRITICAL(&ghBridgeMux);
    memcpy(&localBridge, (const void*)&ghPendingBridge, sizeof(KodiBridge));
    ghBridgePending = false;
    portEXIT_CRITICAL(&ghBridgeMux);
    ghBridgeHandleCmd(localBridge);
  }

  // Bridge link timeout (10s silence → unlinked)
  if (ghBridge.linked && millis() - ghBridge.lastRxMs > 10000) {
    ghBridge.linked = false;
    Serial.println(F("[bridge] CC1101 link lost"));
  }

  // Radio ping (existing)
  static uint32_t lastRadioPing = 0;
  if (!ghRadioLinked && millis() - lastRadioPing >= 2000) {
    lastRadioPing = millis();
    uint8_t bc[6]; memset(bc, 0xFF, 6);
    GhNodeCmd c = {}; c.type = GH_CMD_PING;
    esp_now_send(bc, (uint8_t*)&c, sizeof(c));
  }

  // Bridge ping/discovery (broadcast every 2s when unlinked, every 5s when linked)
  uint32_t bridgeInterval = ghBridge.linked ? 5000 : 2000;
  if (millis() - ghBridge.lastPingMs >= bridgeInterval) {
    ghBridge.lastPingMs = millis();
    KodiBridge ping;
    kbBuildPing(ping, KB_UNIT_NRF24, ghBridge.seq++);
    ghBridgeSend(ping);
  }

  // Flush offline buffer when bridge is connected
  static uint32_t lastFlush = 0;
  if (kbIsLinked(ghBridge) && ghBufMask != 0 && millis() - lastFlush >= 3000) {
    lastFlush = millis();
    ghBufFlush(ghBridge);
  }

  // Send periodic nRF scan snapshots back to bridge requester (every 2s)
  if (gBridgeScanPending && millis() - gBridgeScanMs >= 2000) {
    gBridgeScanMs = millis();
    if (kbIsLinked(ghBridge)) {
      KodiBridge rsp;
      rsp.magic = KB_MAGIC;
      rsp.src = KB_UNIT_NRF24;
      rsp.msgType = KB_MSG_DATA;
      rsp.seq = gBridgeScanSeq;
      rsp.cmd = KB_NRF_SCAN;
      rsp.len = 126;
      memcpy(rsp.payload, gNrfScan, 126);
      ghBridgeSend(rsp);
    }
  }
}

// ---- Bridge command handler --------------------------------------------------
static void ghBridgeHandleCmd(KodiBridge& msg) {
  if (msg.msgType == KB_MSG_ACK) {
    return;
  }

  if (msg.msgType == KB_MSG_STATUS) {
    if (msg.len >= sizeof(KbStatusPayload)) {
      memcpy(&ghBridgeRemoteStatus, msg.payload, sizeof(KbStatusPayload));
    }
    return;
  }

  if (msg.msgType == KB_MSG_CMD) {
    // Reply with ACK
    KodiBridge ack;
    kbBuildAck(ack, KB_UNIT_NRF24, msg.seq, msg.cmd);
    ghBridgeSend(ack);

    switch (msg.cmd) {
      case KB_PING:
        return;

      case KB_STATUS_REQ: {
        KbStatusPayload st = {};
        st.unit = KB_UNIT_NRF24;
        st.radioAOk = ggNrfPresentA;
        st.radioBOk = ggNrfPresentB;
        st.mode = ghStatus.mode;
        st.txCount = ghStatus.txCount;
        st.rxCount = ghStatus.rxCount;
        st.activeChannel = ghStatus.activeChannel;
        KodiBridge reply;
        kbBuildStatus(reply, KB_UNIT_NRF24, ghBridge.seq++, st);
        ghBridgeSend(reply);
        return;
      }

      case KB_NRF_TX: {
        if (msg.len < sizeof(KbNrfPayload)) return;
        KbNrfPayload nrf;
        memcpy(&nrf, msg.payload, sizeof(KbNrfPayload));
        GhNodeCmd c = {};
        c.type = GH_CMD_TX_SINGLE;
        c.radio = nrf.radio;
        c.channel = nrf.channel;
        c.rate = nrf.rate;
        c.power = nrf.power;
        memcpy(c.addr, nrf.addr, 5);
        c.payloadLen = nrf.payloadLen;
        memcpy(c.payload, nrf.payload, min((uint8_t)32, nrf.payloadLen));
        c.repeatMs = nrf.repeatMs;
        c.burstN = nrf.burstN;
        c.autoAck = nrf.autoAck;
        c.addrWidth = nrf.addrWidth;
        c.crc = nrf.crc;
        c.retries = nrf.retries;
        ghSendCmd(c);
        ghBridgeReply(msg.seq, KB_NRF_TX, 1);
        Serial.println(F("[bridge] → NRF TX"));
        return;
      }

      case KB_NRF_JAM: {
        if (msg.len < sizeof(KbNrfPayload)) return;
        KbNrfPayload nrf;
        memcpy(&nrf, msg.payload, sizeof(KbNrfPayload));
        GhNodeCmd c = {};
        c.type = GH_CMD_NRF_JAM;
        c.radio = nrf.radio;
        c.channel = nrf.channel;
        c.rate = nrf.rate;
        c.power = nrf.power;
        ghSendCmd(c);
        ghBridgeReply(msg.seq, KB_NRF_JAM, 1);
        Serial.println(F("[bridge] → NRF JAM"));
        return;
      }

      case KB_NRF_SCAN: {
        memset(gNrfScan, 0, sizeof(gNrfScan));
        GhNodeCmd c = {};
        c.type = GH_CMD_SCAN_START;
        c.radio = 0;
        if (msg.len >= sizeof(KbNrfPayload)) {
          KbNrfPayload nrf;
          memcpy(&nrf, msg.payload, sizeof(KbNrfPayload));
          c.channel = nrf.channel;
        }
        ghSendCmd(c);
        gBridgeScanPending = true;
        gBridgeScanMs = millis();
        gBridgeScanSeq = msg.seq;
        Serial.println(F("[bridge] → NRF SCAN (continuous)"));
        return;
      }

      case KB_NRF_BEACON: {
        if (msg.len < sizeof(KbNrfPayload)) return;
        KbNrfPayload nrf;
        memcpy(&nrf, msg.payload, sizeof(KbNrfPayload));
        GhNodeCmd c = {};
        c.type = GH_CMD_BEACON_START;
        c.radio = nrf.radio;
        c.channel = nrf.channel;
        c.rate = nrf.rate;
        c.power = nrf.power;
        memcpy(c.addr, nrf.addr, 5);
        c.payloadLen = nrf.payloadLen;
        memcpy(c.payload, nrf.payload, min((uint8_t)32, nrf.payloadLen));
        c.repeatMs = nrf.repeatMs;
        c.autoAck = nrf.autoAck;
        c.addrWidth = nrf.addrWidth;
        c.crc = nrf.crc;
        ghSendCmd(c);
        ghBridgeReply(msg.seq, KB_NRF_BEACON, 1);
        Serial.println(F("[bridge] → NRF BEACON"));
        return;
      }

      case KB_NRF_RX: {
        if (msg.len < sizeof(KbNrfPayload)) return;
        KbNrfPayload nrf;
        memcpy(&nrf, msg.payload, sizeof(KbNrfPayload));
        GhNodeCmd c = {};
        c.type = GH_CMD_RX_START;
        c.radio = nrf.radio;
        c.channel = nrf.channel;
        c.rate = nrf.rate;
        c.power = nrf.power;
        memcpy(c.addr, nrf.addr, 5);
        c.payloadLen = nrf.payloadLen;
        c.autoAck = nrf.autoAck;
        c.addrWidth = nrf.addrWidth;
        c.crc = nrf.crc;
        ghSendCmd(c);
        ghBridgeReply(msg.seq, KB_NRF_RX, 1);
        Serial.println(F("[bridge] → NRF RX"));
        return;
      }

      case KB_NRF_STOP: {
        GhNodeCmd c = {};
        c.type = GH_CMD_STOP_ALL;
        ghSendCmd(c);
        ggFloodNrfActive = false;
        gBridgeScanPending = false;
        ghBridgeReply(msg.seq, KB_NRF_STOP, 1);
        Serial.println(F("[bridge] → NRF STOP"));
        return;
      }

      case KB_NRF_CARRIER: {
        if (msg.len < sizeof(KbNrfPayload)) return;
        KbNrfPayload nrf;
        memcpy(&nrf, msg.payload, sizeof(KbNrfPayload));
        GhNodeCmd c = {};
        c.type = GH_CMD_CARRIER_START;
        c.radio = nrf.radio;
        c.channel = nrf.channel;
        ghSendCmd(c);
        ghBridgeReply(msg.seq, KB_NRF_CARRIER, 1);
        Serial.println(F("[bridge] → NRF CARRIER"));
        return;
      }

      case KB_NRF_KPAD_JAM: {
        GhNodeCmd c = {};
        c.type = GH_CMD_KPAD_JAM;
        if (msg.len >= sizeof(KbNrfPayload)) {
          KbNrfPayload nrf;
          memcpy(&nrf, msg.payload, sizeof(KbNrfPayload));
          c.radio = nrf.radio;
          c.channel = nrf.channel;
        }
        ghSendCmd(c);
        ghBridgeReply(msg.seq, KB_NRF_KPAD_JAM, 1);
        Serial.println(F("[bridge] → NRF KPAD JAM"));
        return;
      }

      case KB_DUAL_JAM: {
        // Start local nRF24 jam
        GhNodeCmd c = {};
        c.type = GH_CMD_NRF_JAM;
        c.radio = 2; // both radios
        if (msg.len >= sizeof(KbNrfPayload)) {
          KbNrfPayload nrf;
          memcpy(&nrf, msg.payload, sizeof(KbNrfPayload));
          c.channel = nrf.channel;
        }
        ghSendCmd(c);
        ggFloodNrfActive = true;
        ghBridgeReply(msg.seq, KB_DUAL_JAM, 1);
        Serial.println(F("[bridge] → DUAL JAM (nRF side)"));
        return;
      }

      case KB_DUAL_SCAN: {
        GhNodeCmd c = {};
        c.type = GH_CMD_SCAN_START;
        c.radio = 0;
        ghSendCmd(c);
        ghBridgeReply(msg.seq, KB_DUAL_SCAN, 1);
        Serial.println(F("[bridge] → DUAL SCAN (nRF side)"));
        return;
      }

      default:
        Serial.printf("[bridge] unknown cmd 0x%02X\n", msg.cmd);
        return;
    }
  }

  if (msg.msgType == KB_MSG_DATA) {
    KodiBridge ack;
    kbBuildAck(ack, KB_UNIT_NRF24, msg.seq, msg.cmd);
    ghBridgeSend(ack);

    switch (msg.cmd) {
      case KB_CC_TX:
        gCcTxResult   = (msg.len > 0 && msg.payload[0] == 1) ? 1 : 2;
        gCcTxResultMs = millis();
        break;

      case KB_CC_JAM:
        if (msg.len > 0 && msg.payload[0] == 0) gCcJamUp = false;
        break;

      case KB_DUAL_JAM:
        if (msg.len > 0 && msg.payload[0] == 0) gDualJamUp = false;
        break;

      case KB_CC_STOP:
        gCcJamUp = false;
        gCcRecUp = false;
        break;

      case KB_CC_SCAN:
      case KB_DUAL_SCAN:
        if (msg.len >= 4 && msg.payload[0] == 'S' && msg.payload[1] == 'C'
            && msg.payload[2] == ',') {
          const char* p = (const char*)msg.payload + 3;
          int steps = atoi(p);
          while (*p && *p != ',') p++;
          if (*p == ',') p++;
          memset(gCcScanRssi, 0, sizeof(gCcScanRssi));
          int idx = 0;
          while (*p && idx < 64 && idx < steps) {
            gCcScanRssi[idx] = (int8_t)atoi(p);
            idx++;
            while (*p && *p != ',') p++;
            if (*p == ',') p++;
          }
          gCcScanSteps = idx;
          gCcScanReady = true;
        }
        break;

      case KB_CC_REC:
        gCcRecResult   = (msg.len > 0 && msg.payload[0] == 1) ? 1 : 2;
        gCcRecResultMs = millis();
        if (msg.len > 0 && msg.payload[0] == 0) gCcRecUp = false;
        break;

      case KB_CC_REPLAY:
        gCcReplayResult   = (msg.len > 0 && msg.payload[0] == 1) ? 1 : 2;
        gCcReplayResultMs = millis();
        break;

      case KB_RFID_READ:
        if (msg.len >= 4 && msg.payload[0] == 'R' && msg.payload[1] == 'F'
            && msg.payload[2] == ',') {
          const char* p = (const char*)msg.payload + 3;
          gCcRfidLen = atoi(p);
          while (*p && *p != ',') p++;
          if (*p == ',') p++;
          strncpy(gCcRfidUid, p, 20);
          gCcRfidUid[20] = '\0';
          gCcRfidReady = true;
        } else {
          gCcRfidReady = false;
        }
        break;

      case KB_CC_RX:
        gCcRxResult   = (msg.len > 0 && msg.payload[0] == 1) ? 1 : 2;
        gCcRxResultMs = millis();
        break;

      case KB_CC_BF:
        gCcBfResult   = (msg.len > 0 && msg.payload[0] == 1) ? 1 : 2;
        gCcBfResultMs = millis();
        break;

      case KB_RFID_CRACK:
        gCcCrackResult   = (msg.len > 0 && msg.payload[0] == 1) ? 1 : 2;
        gCcCrackResultMs = millis();
        break;

      default: break;
    }
    Serial.printf("[bridge] DATA cmd=0x%02X len=%d\n", msg.cmd, msg.len);
    return;
  }
}

// ---- Stub nRF functions (proxy to Node 2 via ESP-NOW) ----------------------
static inline bool ggNrfBegin() { return true; }

static inline void ggNrfRecheck() {
  GhNodeCmd c = {}; c.type = GH_CMD_PING; ghSendCmd(c);
}

static inline void ggNrfConfigTx(int, const GgNrfProfile& p) {
  GhNodeCmd c = ghBuildCmd(GH_CMD_CONFIG, p); c.radio = 0; ghSendCmd(c);
}
static inline void ggNrfConfigRx(int, const GgNrfProfile& p) {
  GhNodeCmd c = ghBuildCmd(GH_CMD_CONFIG, p); c.radio = 1; ghSendCmd(c);
}

static inline bool ggNrfTxOnce(int, const GgNrfProfile& p, uint32_t cnt) {
  (void)cnt;
  GhNodeCmd c = ghBuildCmd(GH_CMD_TX_SINGLE, p); ghSendCmd(c);
  return true;
}

static inline bool ggNrfRxPoll(int, uint8_t* buf, uint8_t* len, uint8_t* pipe) {
  if (ghStatus.rxLen > 0) {
    memcpy(buf, ghStatus.rxData, ghStatus.rxLen);
    *len = ghStatus.rxLen; *pipe = ghStatus.rxPipe;
    ghStatus.rxLen = 0;
    return true;
  }
  return false;
}

static inline void ggNrfCarrierOn(int, uint8_t ch) {
  GgNrfProfile p = ggDefaultNrf(); p.channel = ch;
  GhNodeCmd c = ghBuildCmd(GH_CMD_CARRIER_START, p); ghSendCmd(c);
}
static inline void ggNrfCarrierOff(int) {
  GhNodeCmd c = {}; c.type = GH_CMD_CARRIER_STOP; ghSendCmd(c);
}

static inline void ggNrfScanChannel(int, uint8_t ch) {
  GgNrfProfile p = ggDefaultNrf(); p.channel = ch;
  GhNodeCmd c = ghBuildCmd(GH_CMD_SCAN_START, p); ghSendCmd(c);
}

static inline void ggNrfNoiseBegin(int radioIdx, uint8_t ch) {
  GgNrfProfile p = ggDefaultNrf(); p.channel = ch;
  GhNodeCmd c = ghBuildCmd(GH_CMD_NRF_JAM, p);
  c.radio = (uint8_t)radioIdx;
  ghSendCmd(c);
}
static inline void ggNrfNoiseEnd(int) {
  GhNodeCmd c = {}; c.type = GH_CMD_NRF_JAM_STOP; ghSendCmd(c);
}
static inline void ggNrfNoiseBlast(int) { }

static inline void ggNrfScanStart() {
  GhNodeCmd c = {}; c.type = GH_CMD_SCAN_START; c.radio = 0;
  ghSendCmd(c);
}

static inline void ggNrfSetPower(int radio, uint8_t pa) {
  GhNodeCmd c = {}; c.type = GH_CMD_CONFIG; c.radio = radio; c.power = pa;
  ghSendCmd(c);
}

static inline void ggNrfStopAll() {
  GhNodeCmd c = {}; c.type = GH_CMD_STOP_ALL; ghSendCmd(c);
  ggFloodNrfActive = false;
}

// WiFi channel to nRF channel helper (needed by combo attacks)
static inline uint8_t ggWifiChToNrf(uint8_t wch) {
  return (uint8_t)((wch - 1) * 5 + 12);
}

// nRF flood stubs for combo attacks - send jam command to Node 2
static uint8_t ggFloodNrfChLo = 0;
static uint8_t ggFloodNrfChHi = 125;

static inline void ggFloodNrfStart(uint8_t wifiCh) {
  uint8_t center = ggWifiChToNrf(wifiCh);
  ggFloodNrfChLo = (center > 3) ? center - 3 : 0;
  ggFloodNrfChHi = (center + 3 <= 125) ? center + 3 : 125;
  GgNrfProfile p = ggDefaultNrf(); p.channel = ggFloodNrfChLo;
  GhNodeCmd c = ghBuildCmd(GH_CMD_NRF_JAM, p);
  c.radio = 2; // both radios
  ghSendCmd(c);
  ggFloodNrfActive = true;
}
static inline void ggFloodNrfStop() {
  if (!ggFloodNrfActive) return;
  GhNodeCmd c = {}; c.type = GH_CMD_NRF_JAM_STOP; ghSendCmd(c);
  ggFloodNrfActive = false;
}
static inline void ggFloodNrfPump() { }

static inline void ggBleFloodNrfStart() {
  GgNrfProfile p = ggDefaultNrf(); p.channel = 0;
  GhNodeCmd c = ghBuildCmd(GH_CMD_NRF_JAM, p);
  c.radio = 2;
  ghSendCmd(c);
  ggFloodNrfActive = true;
}
