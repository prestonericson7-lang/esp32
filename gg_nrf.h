#pragma once
// ############################################################################
//  gg_nrf.h  -  STUB for CC1101-ONLY tower (no nRF24 hardware)
//
//  Provides empty types and no-op functions so the UI code compiles.
//  ggNrfPresentA/B are always false, so all nRF code paths are dead.
//  The RF24 library is NOT needed with this stub.
// ############################################################################

#include <Arduino.h>
#include <SPI.h>
#include "gg_config.h"

enum rf24_pa_dbm_e  : uint8_t { RF24_PA_MIN = 0, RF24_PA_LOW = 1, RF24_PA_HIGH = 2, RF24_PA_MAX = 3 };
enum rf24_datarate_e : uint8_t { RF24_1MBPS = 0, RF24_2MBPS = 1, RF24_250KBPS = 2 };
enum rf24_crclength_e : uint8_t { RF24_CRC_DISABLED = 0, RF24_CRC_8 = 1, RF24_CRC_16 = 2 };

struct RF24 {
  RF24(uint8_t, uint8_t) {}
  bool begin(SPIClass*) { return false; }
  bool isChipConnected() { return false; }
  void setAutoAck(bool) {}
  void stopListening() {}
  void startListening() {}
  void powerDown() {}
  void powerUp() {}
  void setChannel(uint8_t) {}
  void setPALevel(int) {}
  void setDataRate(rf24_datarate_e) {}
  void setCRCLength(rf24_crclength_e) {}
  void setPayloadSize(uint8_t) {}
  void setAddressWidth(uint8_t) {}
  void setRetries(uint8_t, uint8_t) {}
  void openWritingPipe(const uint8_t*) {}
  void openReadingPipe(uint8_t, const uint8_t*) {}
  bool write(const void*, uint8_t) { return false; }
  bool writeFast(const void*, uint8_t, bool) { return false; }
  void txStandBy() {}
  void flush_tx() {}
  void flush_rx() {}
  void read(void*, uint8_t) {}
  uint8_t getPayloadSize() { return 0; }
  bool available(uint8_t* p = nullptr) { (void)p; return false; }
  bool testRPD() { return false; }
  void startConstCarrier(rf24_pa_dbm_e, uint8_t) {}
  void stopConstCarrier() {}
};

static RF24 ggNrfA(0, 0);
static RF24 ggNrfB(0, 0);
static bool ggNrfPresentA = false;
static bool ggNrfPresentB = false;

static inline rf24_datarate_e ggMapRate(uint8_t) { return RF24_1MBPS; }
static inline uint8_t ggMapPwr(uint8_t p) { return p & 0x03; }
static inline rf24_crclength_e ggMapCrc(uint8_t) { return RF24_CRC_DISABLED; }
static inline uint8_t ggClampAW(uint8_t w) { return w<3?3 : w>5?5 : w; }

static inline bool ggNrfBegin(SPIClass&) { return false; }
static inline void ggNrfRecheck() {}
static inline void ggNrfConfigTx(RF24&, const GgNrfProfile&) {}
static inline void ggNrfConfigRx(RF24&, const GgNrfProfile&) {}
static inline uint8_t ggNrfBuildPayload(const GgNrfProfile& p, uint8_t* out, uint32_t) {
  uint8_t len = p.plLen; if (len < 1) len = 1; if (len > GG_PAYLOAD_MAX) len = GG_PAYLOAD_MAX;
  memset(out, 0, len); return len;
}
static inline bool ggNrfTxOnce(RF24&, const GgNrfProfile&, uint32_t) { return false; }
static inline void ggNrfCarrierOn(RF24&, const GgNrfProfile&) {}
static inline void ggNrfCarrierOff(RF24&) {}
static inline void ggNrfNoiseBegin(RF24&, uint8_t) {}
static inline void ggNrfNoiseBlast(RF24&) {}
static inline void ggNrfNoiseEnd(RF24&) {}
static inline void ggNrfScanPass(RF24&, uint8_t*) {}
static inline bool ggNrfRxPoll(RF24&, uint8_t*, uint8_t*, uint8_t*) { return false; }
