#pragma once
// ############################################################################
//  gh_buffer.h  -  KODI NRF24  /  Offline NVS buffer for bridge data
//
//  Stores up to 16 data chunks in NVS when the bridge to CC1101 is offline.
//  On reconnect, dumps all buffered entries via KB_DATA_CHUNK to kodi_node1
//  which writes them to SD card.
//
//  NVS keys: "buf0" through "buf15" (16 slots × up to 180 bytes each)
//  Bitmask key: "bufmask" (uint16_t tracking which slots have pending data)
// ############################################################################

#include <Arduino.h>
#include <Preferences.h>
#include "gh_config.h"
#include "kodi_bridge.h"

#define GH_BUF_SLOTS  16
#define GH_BUF_SIZE   180

#pragma pack(push, 1)
struct GhBufEntry {
  uint8_t  dataType;  // 0=scan, 1=rx, 2=cred, 3=log
  uint8_t  len;       // actual data length
  uint8_t  data[GH_BUF_SIZE];
};
#pragma pack(pop)

static uint16_t ghBufMask = 0;

static inline void ghBufInit() {
  extern Preferences ggNvs;
  ghBufMask = ggNvs.getUShort("bufmask", 0);
}

static inline int ghBufCount() {
  int n = 0;
  for (int i = 0; i < GH_BUF_SLOTS; i++)
    if (ghBufMask & (1 << i)) n++;
  return n;
}

static inline bool ghBufWrite(uint8_t dataType, const uint8_t* data, uint8_t len) {
  extern Preferences ggNvs;
  if (len > GH_BUF_SIZE) len = GH_BUF_SIZE;
  for (int i = 0; i < GH_BUF_SLOTS; i++) {
    if (!(ghBufMask & (1 << i))) {
      GhBufEntry e;
      e.dataType = dataType;
      e.len = len;
      memcpy(e.data, data, len);
      char k[6]; snprintf(k, sizeof(k), "buf%d", i);
      ggNvs.putBytes(k, &e, 2 + len);
      ghBufMask |= (1 << i);
      ggNvs.putUShort("bufmask", ghBufMask);
      Serial.printf("[buffer] slot %d written (%d bytes)\n", i, len);
      return true;
    }
  }
  Serial.println(F("[buffer] FULL — data dropped"));
  return false;
}

static inline void ghDataForward(uint8_t dataType, const uint8_t* data, uint8_t len,
                                  KbLinkState& bridge) {
  if (len == 0) return;
  if (kbIsLinked(bridge)) {
    KodiBridge msg;
    KbDataPayload dp = {};
    dp.dataType = dataType;
    dp.chunkIdx = 0;
    dp.totalChunks = 1;
    dp.len = (len > GH_BUF_SIZE) ? (uint8_t)GH_BUF_SIZE : len;
    memcpy(dp.data, data, dp.len);
    kbBuildDataChunk(msg, KB_UNIT_NRF24, bridge.seq++, dp);
    esp_now_send(bridge.peerMac, (uint8_t*)&msg, 6 + msg.len);
  } else {
    ghBufWrite(dataType, data, len);
  }
}

static inline void ghBufFlush(KbLinkState& bridge) {
  extern Preferences ggNvs;
  if (ghBufMask == 0) return;
  if (!kbIsLinked(bridge)) return;

  int total = ghBufCount();
  int seq = 0;

  for (int i = 0; i < GH_BUF_SLOTS; i++) {
    if (!(ghBufMask & (1 << i))) continue;
    char k[6]; snprintf(k, sizeof(k), "buf%d", i);
    GhBufEntry e;
    if (ggNvs.getBytes(k, &e, sizeof(e)) < 2) continue;

    if (e.len > GH_BUF_SIZE) e.len = GH_BUF_SIZE;

    KodiBridge msg;
    KbDataPayload dp = {};
    dp.dataType = e.dataType;
    dp.chunkIdx = seq++;
    dp.totalChunks = total;
    dp.len = e.len;
    memcpy(dp.data, e.data, e.len);
    kbBuildDataChunk(msg, KB_UNIT_NRF24, bridge.seq++, dp);

    esp_now_send(bridge.peerMac, (uint8_t*)&msg, 6 + msg.len);
    delay(20);

    ghBufMask &= ~(1 << i);
    ggNvs.putUShort("bufmask", ghBufMask);
    Serial.printf("[buffer] slot %d flushed\n", i);
  }

  KodiBridge endMsg;
  endMsg.magic = KB_MAGIC;
  endMsg.src = KB_UNIT_NRF24;
  endMsg.msgType = KB_MSG_DATA;
  endMsg.seq = bridge.seq++;
  endMsg.cmd = KB_DATA_END;
  endMsg.len = 0;
  esp_now_send(bridge.peerMac, (uint8_t*)&endMsg, 6);
  Serial.println(F("[buffer] flush complete"));
}
