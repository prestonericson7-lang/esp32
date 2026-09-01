#pragma once
// ############################################################################
//  kodi_bridge.h  -  Cross-device ESP-NOW bridge protocol
//
//  Connects KODI CC1101 (kodi_node1) ↔ KODI NRF24 (hub) so either UI
//  can control the other system's radios across a building.
//
//  The bridge sits ON TOP of each system's existing internal ESP-NOW link.
//  Neither radio node (kodi_node0, kodi_nrf24_radio) needs this header.
//
//  Message format: 206 bytes max, fits in one ESP-NOW frame (250 byte limit).
//  First byte is always 0xBD to distinguish from internal messages.
//
//  IDENTICAL COPY in both kodi_nrf24_hub/ and kodi_node1/ directories.
// ############################################################################

#include <Arduino.h>

#define KB_MAGIC  0xBD

// Bridge unit IDs
#define KB_UNIT_CC1101  0
#define KB_UNIT_NRF24   1

// Message types
enum KbMsgType : uint8_t {
  KB_MSG_CMD    = 0x01,
  KB_MSG_ACK    = 0x02,
  KB_MSG_STATUS = 0x03,
  KB_MSG_DATA   = 0x04,
};

// Bridge commands
enum KbCmd : uint8_t {
  // Bidirectional
  KB_PING        = 0x01,
  KB_STATUS_REQ  = 0x02,

  // NRF24 hub → CC1101 (requesting CC1101 actions)
  KB_CC_TX       = 0x10,
  KB_CC_RX       = 0x11,
  KB_CC_JAM      = 0x12,
  KB_CC_STOP     = 0x13,
  KB_CC_SCAN     = 0x14,
  KB_CC_BF       = 0x15,
  KB_CC_REPLAY   = 0x16,
  KB_CC_REC      = 0x17,
  KB_RFID_READ   = 0x20,
  KB_RFID_CRACK  = 0x21,

  // CC1101 → NRF24 hub (requesting nRF24 actions)
  KB_NRF_TX       = 0x30,
  KB_NRF_JAM      = 0x31,
  KB_NRF_SCAN     = 0x32,
  KB_NRF_BEACON   = 0x33,
  KB_NRF_RX       = 0x34,
  KB_NRF_STOP     = 0x35,
  KB_NRF_CARRIER  = 0x36,
  KB_NRF_KPAD_JAM = 0x37,

  // Data forwarding (NRF24 → CC1101 SD card)
  KB_DATA_CHUNK  = 0x40,
  KB_DATA_END    = 0x41,

  // Combined ops
  KB_DUAL_JAM    = 0x50,
  KB_DUAL_SCAN   = 0x51,
};

// CC1101 TX/Jam payload: packed radio config for bridge commands
#pragma pack(push, 1)
struct KbCcPayload {
  uint8_t  radio;       // 0=A, 1=B, 2=both
  float    freq;        // MHz (e.g. 433.92)
  uint8_t  mod;         // 0=OOK, 1=2FSK, 2=GFSK, 3=4FSK, 4=MSK
  uint8_t  rate;        // data rate index
  uint8_t  power;       // TX power 0-7
  uint8_t  pktLen;      // payload length
  uint8_t  payload[64]; // payload data
  uint16_t burstN;      // number of transmissions
  uint16_t gapMs;       // gap between bursts
};

// nRF24 TX/Jam payload: packed radio config for bridge commands
struct KbNrfPayload {
  uint8_t  radio;       // 0=A, 1=B, 2=both
  uint8_t  channel;     // 0-125
  uint8_t  rate;        // 0=250K, 1=1M, 2=2M
  uint8_t  power;       // 0=MIN, 1=LOW, 2=HIGH, 3=MAX
  uint8_t  addr[5];     // pipe address
  uint8_t  payloadLen;  // 1-32
  uint8_t  payload[32]; // payload data
  uint16_t repeatMs;    // repeat interval
  uint16_t burstN;      // burst count
  uint8_t  autoAck;     // auto-ack on/off
  uint8_t  addrWidth;   // 3-5
  uint8_t  crc;         // 0=off, 1=8bit, 2=16bit
  uint8_t  retries;     // 0-15
};

// Status payload: sent with KB_MSG_STATUS
struct KbStatusPayload {
  uint8_t  unit;          // KB_UNIT_CC1101 or KB_UNIT_NRF24
  bool     radioAOk;
  bool     radioBOk;
  uint8_t  mode;          // current radio mode
  uint32_t txCount;
  uint32_t rxCount;
  uint8_t  activeChannel;
  uint8_t  extra[16];     // unit-specific status data
};

// Data chunk payload: for forwarding data to SD card
struct KbDataPayload {
  uint8_t  dataType;      // 0=scan, 1=rx, 2=cred, 3=log
  uint8_t  chunkIdx;      // chunk index (for multi-chunk data)
  uint8_t  totalChunks;   // total chunks in this transfer
  uint8_t  len;           // data length in this chunk
  uint8_t  data[180];     // chunk data
};
#pragma pack(pop)

// Main bridge message envelope
#pragma pack(push, 1)
struct KodiBridge {
  uint8_t  magic;         // KB_MAGIC (0xBD)
  uint8_t  src;           // KB_UNIT_CC1101 or KB_UNIT_NRF24
  uint8_t  msgType;       // KbMsgType
  uint8_t  seq;           // sequence number for ack matching
  uint8_t  cmd;           // KbCmd
  uint8_t  len;           // payload length (0-200)
  uint8_t  payload[200];  // command-specific data
};
#pragma pack(pop)

#define KB_MSG_MAX  sizeof(KodiBridge)  // 206 bytes, fits in ESP-NOW 250 limit

// Bridge link state (each side maintains this)
struct KbLinkState {
  uint8_t  peerMac[6];
  bool     linked;
  uint32_t lastRxMs;
  uint32_t lastPingMs;
  uint8_t  seq;
};

static inline void kbInitLink(KbLinkState& lk) {
  memset(&lk, 0, sizeof(lk));
}

static inline bool kbIsLinked(const KbLinkState& lk) {
  return lk.linked && (millis() - lk.lastRxMs < 10000);
}

static inline void kbBuildPing(KodiBridge& msg, uint8_t srcUnit, uint8_t seq) {
  msg.magic   = KB_MAGIC;
  msg.src     = srcUnit;
  msg.msgType = KB_MSG_CMD;
  msg.seq     = seq;
  msg.cmd     = KB_PING;
  msg.len     = 0;
}

static inline void kbBuildAck(KodiBridge& msg, uint8_t srcUnit, uint8_t seq, uint8_t cmd) {
  msg.magic   = KB_MAGIC;
  msg.src     = srcUnit;
  msg.msgType = KB_MSG_ACK;
  msg.seq     = seq;
  msg.cmd     = cmd;
  msg.len     = 0;
}

static inline void kbBuildStatus(KodiBridge& msg, uint8_t srcUnit, uint8_t seq,
                                  const KbStatusPayload& st) {
  msg.magic   = KB_MAGIC;
  msg.src     = srcUnit;
  msg.msgType = KB_MSG_STATUS;
  msg.seq     = seq;
  msg.cmd     = KB_STATUS_REQ;
  msg.len     = sizeof(KbStatusPayload);
  memcpy(msg.payload, &st, sizeof(KbStatusPayload));
}

static inline void kbBuildNrfCmd(KodiBridge& msg, uint8_t srcUnit, uint8_t seq,
                                  uint8_t cmd, const KbNrfPayload& nrf) {
  msg.magic   = KB_MAGIC;
  msg.src     = srcUnit;
  msg.msgType = KB_MSG_CMD;
  msg.seq     = seq;
  msg.cmd     = cmd;
  msg.len     = sizeof(KbNrfPayload);
  memcpy(msg.payload, &nrf, sizeof(KbNrfPayload));
}

static inline void kbBuildCcCmd(KodiBridge& msg, uint8_t srcUnit, uint8_t seq,
                                 uint8_t cmd, const KbCcPayload& cc) {
  msg.magic   = KB_MAGIC;
  msg.src     = srcUnit;
  msg.msgType = KB_MSG_CMD;
  msg.seq     = seq;
  msg.cmd     = cmd;
  msg.len     = sizeof(KbCcPayload);
  memcpy(msg.payload, &cc, sizeof(KbCcPayload));
}

static inline void kbBuildDataChunk(KodiBridge& msg, uint8_t srcUnit, uint8_t seq,
                                     const KbDataPayload& dp) {
  msg.magic   = KB_MAGIC;
  msg.src     = srcUnit;
  msg.msgType = KB_MSG_DATA;
  msg.seq     = seq;
  msg.cmd     = KB_DATA_CHUNK;
  msg.len     = 4 + dp.len;  // header(4) + data
  memcpy(msg.payload, &dp, 4 + dp.len);
}
