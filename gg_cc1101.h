#pragma once
// ############################################################################
//  gg_cc1101.h  -  GHOST TX CC1101 EDITION  /  dual CC1101 driver (sub-GHz)
//
//  Two CC1101 on the shared HSPI bus, each with its own CSN. Raw SPI driver
//  with no external library dependency. Poll-based (no GDO pins wired).
//  26 MHz crystal assumed (standard CC1101 module).
// ############################################################################

#include <Arduino.h>
#include <SPI.h>
#include "gg_config.h"

// ===== CC1101 register addresses ============================================
#define CC1101_IOCFG2    0x00
#define CC1101_IOCFG1    0x01
#define CC1101_IOCFG0    0x02
#define CC1101_FIFOTHR   0x03
#define CC1101_SYNC1     0x04
#define CC1101_SYNC0     0x05
#define CC1101_PKTLEN    0x06
#define CC1101_PKTCTRL1  0x07
#define CC1101_PKTCTRL0  0x08
#define CC1101_ADDR      0x09
#define CC1101_CHANNR    0x0A
#define CC1101_FSCTRL1   0x0B
#define CC1101_FSCTRL0   0x0C
#define CC1101_FREQ2     0x0D
#define CC1101_FREQ1     0x0E
#define CC1101_FREQ0     0x0F
#define CC1101_MDMCFG4   0x10
#define CC1101_MDMCFG3   0x11
#define CC1101_MDMCFG2   0x12
#define CC1101_MDMCFG1   0x13
#define CC1101_MDMCFG0   0x14
#define CC1101_DEVIATN   0x15
#define CC1101_MCSM2     0x16
#define CC1101_MCSM1     0x17
#define CC1101_MCSM0     0x18
#define CC1101_FOCCFG    0x19
#define CC1101_BSCFG     0x1A
#define CC1101_AGCCTRL2  0x1B
#define CC1101_AGCCTRL1  0x1C
#define CC1101_AGCCTRL0  0x1D
#define CC1101_FREND1    0x22
#define CC1101_FREND0    0x23
#define CC1101_FSCAL3    0x24
#define CC1101_FSCAL2    0x25
#define CC1101_FSCAL1    0x26
#define CC1101_FSCAL0    0x27

// command strobes
#define CC1101_SRES      0x30
#define CC1101_SFSTXON   0x31
#define CC1101_SXOFF     0x32
#define CC1101_SCAL      0x33
#define CC1101_SRX       0x34
#define CC1101_STX       0x35
#define CC1101_SIDLE     0x36
#define CC1101_SWOR      0x38
#define CC1101_SPWD      0x39
#define CC1101_SFRX      0x3A
#define CC1101_SFTX      0x3B
#define CC1101_SNOP      0x3D

// status registers (read with burst bit to distinguish from strobes)
#define CC1101_PARTNUM   0x30
#define CC1101_VERSION   0x31
#define CC1101_RSSI_S    0x34
#define CC1101_MARCSTATE 0x35
#define CC1101_TXBYTES   0x3A
#define CC1101_RXBYTES   0x3B

// FIFO + PATABLE
#define CC1101_TXFIFO    0x3F
#define CC1101_RXFIFO    0x3F
#define CC1101_PATABLE   0x3E

// SPI header bits
#define CC1101_READ      0x80
#define CC1101_BURST     0x40

// MARCSTATE values
#define CC1101_MARC_IDLE   0x01
#define CC1101_MARC_RX     0x0D
#define CC1101_MARC_TX     0x13
#define CC1101_MARC_RXOVF  0x11
#define CC1101_MARC_TXUNF  0x16

// status byte masks (from SPI header response)
#define CC1101_ST_MASK   0x70
#define CC1101_ST_IDLE   0x00
#define CC1101_ST_RX     0x10
#define CC1101_ST_TX     0x20
#define CC1101_ST_TXUNF  0x70

// ===== lookup tables =======================================================

// PATABLE values for 433 MHz, indexed by GgCcPower enum
static const uint8_t GG_CC_PA_TABLE[] = {
  0x12, 0x0E, 0x1D, 0x34, 0x60, 0x84, 0xC8, 0xC0
};

// data-rate register values, indexed by GgCcRate enum
//                                   1.2k   2.4k   10k    38.4k  100k   250k
static const uint8_t GG_CC_RATE4[] = { 0xF5, 0xF6, 0xC8, 0xCA, 0x5B, 0x2D }; // MDMCFG4
static const uint8_t GG_CC_RATE3[] = { 0x83, 0x83, 0x93, 0x83, 0xF8, 0x3B }; // MDMCFG3
static const uint8_t GG_CC_DEVI[]  = { 0x15, 0x15, 0x34, 0x35, 0x47, 0x62 }; // DEVIATN

// modulation format bits for MDMCFG2[6:4]
static const uint8_t GG_CC_MOD_BITS[] = { 0x30, 0x00, 0x10, 0x70 }; // ASK 2FSK GFSK MSK

// ===== HSPI bus + radio globals =============================================

static SPIClass ggCcSpi(HSPI);
static const SPISettings ggCcSpiSettings(4000000, MSBFIRST, SPI_MODE0);

static const uint8_t ggCcA = GG_PIN_CCA_CSN;
static const uint8_t ggCcB = GG_PIN_CCB_CSN;
static const uint8_t ggCcC = GG_PIN_CCC_CSN;
static const uint8_t ggCcD = GG_PIN_CCD_CSN;
static bool ggCcPresentA = false;
static bool ggCcPresentB = false;
static bool ggCcPresentC = false;
static bool ggCcPresentD = false;

// ===== 1. SPI register access ==============================================

static inline void ggCcSpiBegin() {
  ggCcSpi.begin(GG_PIN_CC_SCK, GG_PIN_CC_MISO, GG_PIN_CC_MOSI, -1);
  pinMode(GG_PIN_CCA_CSN, OUTPUT); digitalWrite(GG_PIN_CCA_CSN, HIGH);
  pinMode(GG_PIN_CCB_CSN, OUTPUT); digitalWrite(GG_PIN_CCB_CSN, HIGH);
  pinMode(GG_PIN_CCC_CSN, OUTPUT); digitalWrite(GG_PIN_CCC_CSN, HIGH);
  pinMode(GG_PIN_CCD_CSN, OUTPUT); digitalWrite(GG_PIN_CCD_CSN, HIGH);
}

static inline void ggCcSelect(uint8_t csn) {
  digitalWrite(csn, LOW);
  uint32_t t = micros();
  while (digitalRead(GG_PIN_CC_MISO) && (micros() - t < 1000));
}

static inline void ggCcDeselect(uint8_t csn) { digitalWrite(csn, HIGH); }

static inline void ggCcWriteReg(uint8_t csn, uint8_t addr, uint8_t val) {
  ggCcSpi.beginTransaction(ggCcSpiSettings);
  ggCcSelect(csn);
  ggCcSpi.transfer(addr);
  ggCcSpi.transfer(val);
  ggCcDeselect(csn);
  ggCcSpi.endTransaction();
}

static inline uint8_t ggCcReadReg(uint8_t csn, uint8_t addr) {
  ggCcSpi.beginTransaction(ggCcSpiSettings);
  ggCcSelect(csn);
  ggCcSpi.transfer(addr | CC1101_READ);
  uint8_t v = ggCcSpi.transfer(0x00);
  ggCcDeselect(csn);
  ggCcSpi.endTransaction();
  return v;
}

static inline void ggCcWriteBurst(uint8_t csn, uint8_t addr,
                                   const uint8_t* data, uint8_t len) {
  ggCcSpi.beginTransaction(ggCcSpiSettings);
  ggCcSelect(csn);
  ggCcSpi.transfer(addr | CC1101_BURST);
  for (uint8_t i = 0; i < len; i++) ggCcSpi.transfer(data[i]);
  ggCcDeselect(csn);
  ggCcSpi.endTransaction();
}

static inline void ggCcReadBurst(uint8_t csn, uint8_t addr,
                                  uint8_t* data, uint8_t len) {
  ggCcSpi.beginTransaction(ggCcSpiSettings);
  ggCcSelect(csn);
  ggCcSpi.transfer(addr | CC1101_READ | CC1101_BURST);
  for (uint8_t i = 0; i < len; i++) data[i] = ggCcSpi.transfer(0x00);
  ggCcDeselect(csn);
  ggCcSpi.endTransaction();
}

static inline uint8_t ggCcStrobe(uint8_t csn, uint8_t cmd) {
  ggCcSpi.beginTransaction(ggCcSpiSettings);
  ggCcSelect(csn);
  uint8_t st = ggCcSpi.transfer(cmd);
  ggCcDeselect(csn);
  ggCcSpi.endTransaction();
  return st;
}

static inline uint8_t ggCcReadStatus(uint8_t csn, uint8_t addr) {
  ggCcSpi.beginTransaction(ggCcSpiSettings);
  ggCcSelect(csn);
  ggCcSpi.transfer(addr | CC1101_READ | CC1101_BURST);
  uint8_t v = ggCcSpi.transfer(0x00);
  ggCcDeselect(csn);
  ggCcSpi.endTransaction();
  return v;
}

// ===== 2. Radio control ====================================================

static inline void ggCcReset(uint8_t csn) {
  ggCcDeselect(csn);
  delayMicroseconds(5);
  ggCcSelect(csn);
  delayMicroseconds(10);
  ggCcDeselect(csn);
  delayMicroseconds(45);
  ggCcStrobe(csn, CC1101_SRES);
  delay(5);
  // wait for MISO LOW (chip ready after reset)
  digitalWrite(csn, LOW);
  uint32_t t = micros();
  while (digitalRead(GG_PIN_CC_MISO) && (micros() - t < 5000));
  digitalWrite(csn, HIGH);
  delay(2);
}

static inline void ggCcIdle(uint8_t csn) {
  ggCcStrobe(csn, CC1101_SIDLE);
  uint8_t tries = 200;
  while (tries-- && (ggCcReadStatus(csn, CC1101_MARCSTATE) & 0x1F) != CC1101_MARC_IDLE)
    delayMicroseconds(100);
}

static inline void ggCcPowerDown(uint8_t csn) {
  ggCcIdle(csn);
  ggCcStrobe(csn, CC1101_SPWD);
}

static inline void ggCcFlushTx(uint8_t csn) { ggCcIdle(csn); ggCcStrobe(csn, CC1101_SFTX); }
static inline void ggCcFlushRx(uint8_t csn) { ggCcIdle(csn); ggCcStrobe(csn, CC1101_SFRX); }

static inline void ggCcCalibrate(uint8_t csn) {
  ggCcIdle(csn);
  ggCcStrobe(csn, CC1101_SCAL);
  uint8_t tries = 200;
  while (tries-- && (ggCcReadStatus(csn, CC1101_MARCSTATE) & 0x1F) != CC1101_MARC_IDLE)
    delayMicroseconds(100);
}

static inline void ggCcSetFreq(uint8_t csn, float freqMHz) {
  uint32_t f = (uint32_t)(freqMHz * 65536.0f / 26.0f + 0.5f);
  ggCcWriteReg(csn, CC1101_FREQ2, (f >> 16) & 0xFF);
  ggCcWriteReg(csn, CC1101_FREQ1, (f >> 8)  & 0xFF);
  ggCcWriteReg(csn, CC1101_FREQ0,  f        & 0xFF);
}

static inline void ggCcSetModulation(uint8_t csn, uint8_t mod) {
  uint8_t m = mod & 0x03;
  uint8_t mdm2 = ggCcReadReg(csn, CC1101_MDMCFG2);
  ggCcWriteReg(csn, CC1101_MDMCFG2, (mdm2 & 0x8F) | GG_CC_MOD_BITS[m]);
  ggCcWriteReg(csn, CC1101_FREND0, (m == GG_CM_ASK) ? 0x11 : 0x10);
}

static inline void ggCcSetDataRate(uint8_t csn, uint8_t rate) {
  uint8_t r = (rate > 5) ? 5 : rate;
  ggCcWriteReg(csn, CC1101_MDMCFG4, GG_CC_RATE4[r]);
  ggCcWriteReg(csn, CC1101_MDMCFG3, GG_CC_RATE3[r]);
  ggCcWriteReg(csn, CC1101_DEVIATN,  GG_CC_DEVI[r]);
}

static inline void ggCcSetPower(uint8_t csn, uint8_t power) {
  uint8_t pa = GG_CC_PA_TABLE[power & 0x07];
  if (ggCcReadReg(csn, CC1101_FREND0) & 0x01) {
    uint8_t pt[2] = { 0x00, pa };
    ggCcWriteBurst(csn, CC1101_PATABLE, pt, 2);
  } else {
    ggCcWriteReg(csn, CC1101_PATABLE, pa);
  }
}

static inline void ggCcSetSyncWord(uint8_t csn, uint8_t sw0, uint8_t sw1) {
  ggCcWriteReg(csn, CC1101_SYNC1, sw1);
  ggCcWriteReg(csn, CC1101_SYNC0, sw0);
}

static inline void ggCcInit(uint8_t csn, const GgCcProfile& p) {
  ggCcReset(csn);

  ggCcWriteReg(csn, CC1101_IOCFG2,   0x29);
  ggCcWriteReg(csn, CC1101_IOCFG1,   0x2E);
  ggCcWriteReg(csn, CC1101_IOCFG0,   0x06);
  ggCcWriteReg(csn, CC1101_FIFOTHR,  0x07);

  uint8_t pLen = p.plLen;
  if (pLen < 1)                pLen = 1;
  if (pLen > GG_CC_PAYLOAD_MAX) pLen = GG_CC_PAYLOAD_MAX;
  ggCcWriteReg(csn, CC1101_PKTLEN,   pLen);
  ggCcWriteReg(csn, CC1101_PKTCTRL1, 0x08);  // CRC autoflush, no status append
  ggCcWriteReg(csn, CC1101_PKTCTRL0, (p.pktFormat == 2) ? 0x32 : 0x04); // async serial or fixed+CRC
  ggCcWriteReg(csn, CC1101_ADDR,     0x00);
  ggCcWriteReg(csn, CC1101_CHANNR,   0x00);
  ggCcWriteReg(csn, CC1101_FSCTRL1,  0x06);
  ggCcWriteReg(csn, CC1101_FSCTRL0,  0x00);

  ggCcSetFreq(csn, p.freqMHz);

  uint8_t r = (p.dataRate > 5) ? 5 : p.dataRate;
  ggCcWriteReg(csn, CC1101_MDMCFG4, GG_CC_RATE4[r]);
  ggCcWriteReg(csn, CC1101_MDMCFG3, GG_CC_RATE3[r]);

  uint8_t m = p.modulation & 0x03;
  ggCcWriteReg(csn, CC1101_MDMCFG2, GG_CC_MOD_BITS[m] | 0x02); // mod + 16/16 sync
  ggCcWriteReg(csn, CC1101_MDMCFG1, 0x22);
  ggCcWriteReg(csn, CC1101_MDMCFG0, 0xF8);
  ggCcWriteReg(csn, CC1101_DEVIATN,  GG_CC_DEVI[r]);

  ggCcWriteReg(csn, CC1101_MCSM2,   0x07);
  ggCcWriteReg(csn, CC1101_MCSM1,   0x30);  // stay in RX after RX, idle after TX
  ggCcWriteReg(csn, CC1101_MCSM0,   0x18);  // autocal IDLE->RX/TX
  ggCcWriteReg(csn, CC1101_FOCCFG,  0x16);
  ggCcWriteReg(csn, CC1101_BSCFG,   0x6C);
  ggCcWriteReg(csn, CC1101_AGCCTRL2,0x43);
  ggCcWriteReg(csn, CC1101_AGCCTRL1,0x40);
  ggCcWriteReg(csn, CC1101_AGCCTRL0,0x91);

  bool ask = (m == GG_CM_ASK);
  ggCcWriteReg(csn, CC1101_FREND1, 0x56);
  ggCcWriteReg(csn, CC1101_FREND0, ask ? 0x11 : 0x10);
  ggCcWriteReg(csn, CC1101_FSCAL3, 0xE9);
  ggCcWriteReg(csn, CC1101_FSCAL2, 0x2A);
  ggCcWriteReg(csn, CC1101_FSCAL1, 0x00);
  ggCcWriteReg(csn, CC1101_FSCAL0, 0x1F);

  ggCcSetSyncWord(csn, p.syncWord[0], p.syncWord[1]);

  uint8_t pa = GG_CC_PA_TABLE[p.power & 0x07];
  if (ask) {
    uint8_t pt[2] = { 0x00, pa };
    ggCcWriteBurst(csn, CC1101_PATABLE, pt, 2);
  } else {
    ggCcWriteReg(csn, CC1101_PATABLE, pa);
  }

  ggCcStrobe(csn, CC1101_SFRX);
  ggCcStrobe(csn, CC1101_SFTX);
  ggCcStrobe(csn, CC1101_SCAL);
  delay(1);
  ggCcIdle(csn);
}

// ===== 3. TX / RX operations ===============================================

static inline bool ggCcTxPacket(uint8_t csn, const uint8_t* data, uint8_t len) {
  if (len < 1 || len > GG_CC_PAYLOAD_MAX) return false;
  ggCcIdle(csn);
  ggCcStrobe(csn, CC1101_SFTX);
  ggCcWriteReg(csn, CC1101_PKTLEN, len);
  ggCcWriteBurst(csn, CC1101_TXFIFO, data, len);
  ggCcStrobe(csn, CC1101_STX);
  uint32_t t = millis();
  while (millis() - t < 200) {
    uint8_t st = ggCcReadStatus(csn, CC1101_MARCSTATE) & 0x1F;
    if (st == CC1101_MARC_IDLE) return true;
    if (st == CC1101_MARC_TXUNF) { ggCcStrobe(csn, CC1101_SFTX); return false; }
    delayMicroseconds(100);
  }
  ggCcIdle(csn);
  return false;
}

static inline void ggCcRxBegin(uint8_t csn) {
  ggCcIdle(csn);
  ggCcStrobe(csn, CC1101_SFRX);
  ggCcStrobe(csn, CC1101_SRX);
}

static inline bool ggCcRxAvailable(uint8_t csn) {
  uint8_t rxb = ggCcReadStatus(csn, CC1101_RXBYTES);
  if (rxb & 0x80) {
    ggCcIdle(csn);
    ggCcStrobe(csn, CC1101_SFRX);
    ggCcStrobe(csn, CC1101_SRX);
    return false;
  }
  return (rxb & 0x7F) >= ggCcReadReg(csn, CC1101_PKTLEN);
}

static inline uint8_t ggCcRxRead(uint8_t csn, uint8_t* buf, uint8_t maxLen) {
  uint8_t rxb = ggCcReadStatus(csn, CC1101_RXBYTES);
  if (rxb & 0x80) {
    ggCcIdle(csn);
    ggCcStrobe(csn, CC1101_SFRX);
    return 0;
  }
  rxb &= 0x7F;
  if (rxb == 0) return 0;
  uint8_t n = (rxb < maxLen) ? rxb : maxLen;
  ggCcReadBurst(csn, CC1101_RXFIFO, buf, n);
  if (n < rxb) {
    ggCcIdle(csn);
    ggCcStrobe(csn, CC1101_SFRX);
  }
  return n;
}

static inline int8_t ggCcGetRssi(uint8_t csn) {
  int16_t r = (int8_t)ggCcReadStatus(csn, CC1101_RSSI_S);
  return (int8_t)(r / 2 - 74);
}

static inline uint8_t ggCcGetState(uint8_t csn) {
  return ggCcReadStatus(csn, CC1101_MARCSTATE) & 0x1F;
}

// ===== 4. Radio detection + management =====================================

static inline void ggCcIsolateCs(uint8_t target) {
  if (target != GG_PIN_CCA_CSN) digitalWrite(GG_PIN_CCA_CSN, HIGH);
  if (target != GG_PIN_CCB_CSN) digitalWrite(GG_PIN_CCB_CSN, HIGH);
  if (target != GG_PIN_CCC_CSN) digitalWrite(GG_PIN_CCC_CSN, HIGH);
  if (target != GG_PIN_CCD_CSN) digitalWrite(GG_PIN_CCD_CSN, HIGH);
}

static inline bool ggCcDetectOne(uint8_t csn, const char* label) {
  ggCcIsolateCs(csn);
  ggCcReset(csn);
  delay(50);

  static const uint8_t retryDelays[] = { 10, 25, 50 };
  for (int attempt = 0; attempt < 3; attempt++) {
    uint8_t v = ggCcReadStatus(csn, CC1101_VERSION);
    Serial.printf("[cc1101] %s attempt %d: VERSION=0x%02X\n", label, attempt + 1, v);
    if (v != 0x00 && v != 0xFF) {
      Serial.printf("[cc1101] %s detected (CS=%d)\n", label, csn);
      return true;
    }
    if (attempt < 2) {
      delay(retryDelays[attempt]);
      ggCcIsolateCs(csn);
      ggCcReset(csn);
      delay(retryDelays[attempt + 1]);
    }
  }
  Serial.printf("[cc1101] %s NOT found (CS=%d)\n", label, csn);
  return false;
}

static inline bool ggCcBegin() {
  Serial.println(F("[cc1101] HSPI starting, detecting radios..."));
  ggCcSpiBegin();
  delay(50);

  ggCcPresentA = ggCcDetectOne(ggCcA, "A");
  ggCcPresentB = ggCcDetectOne(ggCcB, "B");
  ggCcPresentC = ggCcDetectOne(ggCcC, "C");
  ggCcPresentD = ggCcDetectOne(ggCcD, "D");

  if (ggCcPresentA) ggCcPowerDown(ggCcA);
  if (ggCcPresentB) ggCcPowerDown(ggCcB);
  if (ggCcPresentC) ggCcPowerDown(ggCcC);
  if (ggCcPresentD) ggCcPowerDown(ggCcD);

  int n = (ggCcPresentA?1:0)+(ggCcPresentB?1:0)+(ggCcPresentC?1:0)+(ggCcPresentD?1:0);
  Serial.printf("[cc1101] A=%s B=%s C=%s D=%s  (%d/4)\n",
    ggCcPresentA?"OK":"--", ggCcPresentB?"OK":"--",
    ggCcPresentC?"OK":"--", ggCcPresentD?"OK":"--", n);
  return n > 0;
}

// ===== 5. Signal recording / replay ========================================

static inline void ggCcRecordBegin(uint8_t csn, float freqMHz) {
  ggCcReset(csn);
  ggCcWriteReg(csn, CC1101_IOCFG2,   0x29);
  ggCcWriteReg(csn, CC1101_IOCFG1,   0x2E);
  ggCcWriteReg(csn, CC1101_IOCFG0,   0x06);
  ggCcWriteReg(csn, CC1101_FIFOTHR,  0x07);
  ggCcWriteReg(csn, CC1101_PKTLEN,   0xFF);
  ggCcWriteReg(csn, CC1101_PKTCTRL1, 0x00);
  ggCcWriteReg(csn, CC1101_PKTCTRL0, 0x02);  // infinite packet length, no CRC
  ggCcWriteReg(csn, CC1101_ADDR,     0x00);
  ggCcWriteReg(csn, CC1101_CHANNR,   0x00);
  ggCcWriteReg(csn, CC1101_FSCTRL1,  0x06);
  ggCcWriteReg(csn, CC1101_FSCTRL0,  0x00);
  ggCcSetFreq(csn, freqMHz);
  ggCcWriteReg(csn, CC1101_MDMCFG4, GG_CC_RATE4[GG_CR_2K]);
  ggCcWriteReg(csn, CC1101_MDMCFG3, GG_CC_RATE3[GG_CR_2K]);
  ggCcWriteReg(csn, CC1101_MDMCFG2, 0x30);   // ASK/OOK, no sync word
  ggCcWriteReg(csn, CC1101_MDMCFG1, 0x00);
  ggCcWriteReg(csn, CC1101_MDMCFG0, 0xF8);
  ggCcWriteReg(csn, CC1101_DEVIATN,  0x15);
  ggCcWriteReg(csn, CC1101_MCSM2,   0x07);
  ggCcWriteReg(csn, CC1101_MCSM1,   0x0C);  // stay in RX after packet
  ggCcWriteReg(csn, CC1101_MCSM0,   0x18);
  ggCcWriteReg(csn, CC1101_FOCCFG,  0x16);
  ggCcWriteReg(csn, CC1101_BSCFG,   0x6C);
  ggCcWriteReg(csn, CC1101_AGCCTRL2,0x07);  // max sensitivity for capture
  ggCcWriteReg(csn, CC1101_AGCCTRL1,0x00);
  ggCcWriteReg(csn, CC1101_AGCCTRL0,0x91);
  ggCcWriteReg(csn, CC1101_FREND1,  0x56);
  ggCcWriteReg(csn, CC1101_FREND0,  0x11);  // ASK PA index 1
  ggCcWriteReg(csn, CC1101_FSCAL3,  0xE9);
  ggCcWriteReg(csn, CC1101_FSCAL2,  0x2A);
  ggCcWriteReg(csn, CC1101_FSCAL1,  0x00);
  ggCcWriteReg(csn, CC1101_FSCAL0,  0x1F);
  ggCcStrobe(csn, CC1101_SRX);
}

static inline bool ggCcRecordPoll(uint8_t csn, GgCcRecording* rec) {
  uint8_t rxb = ggCcReadStatus(csn, CC1101_RXBYTES);
  if (rxb & 0x80) {
    ggCcStrobe(csn, CC1101_SIDLE);
    ggCcStrobe(csn, CC1101_SFRX);
    ggCcStrobe(csn, CC1101_SRX);
    return false;
  }
  rxb &= 0x7F;
  if (rxb == 0) return false;
  uint16_t space = GG_CC_REC_MAX - rec->len;
  if (space == 0) return false;
  uint8_t n = rxb;
  if ((uint16_t)n > space) n = (uint8_t)space;
  ggCcReadBurst(csn, CC1101_RXFIFO, &rec->data[rec->len], n);
  rec->len += n;
  rec->valid = true;
  return true;
}

static inline void ggCcReplay(uint8_t csn, const GgCcRecording* rec) {
  if (!rec->valid || rec->len == 0) return;
  ggCcReset(csn);
  ggCcWriteReg(csn, CC1101_IOCFG2,   0x29);
  ggCcWriteReg(csn, CC1101_IOCFG1,   0x2E);
  ggCcWriteReg(csn, CC1101_IOCFG0,   0x06);
  ggCcWriteReg(csn, CC1101_PKTCTRL1, 0x00);
  ggCcWriteReg(csn, CC1101_PKTCTRL0, 0x02);  // infinite length, no CRC
  ggCcWriteReg(csn, CC1101_FSCTRL1,  0x06);
  ggCcWriteReg(csn, CC1101_FSCTRL0,  0x00);
  ggCcSetFreq(csn, rec->freqMHz);
  uint8_t r = (rec->dataRate > 5) ? 5 : rec->dataRate;
  ggCcWriteReg(csn, CC1101_MDMCFG4, GG_CC_RATE4[r]);
  ggCcWriteReg(csn, CC1101_MDMCFG3, GG_CC_RATE3[r]);
  ggCcWriteReg(csn, CC1101_MDMCFG2, 0x30);   // ASK, no sync
  ggCcWriteReg(csn, CC1101_MDMCFG1, 0x00);   // min preamble (2 bytes)
  ggCcWriteReg(csn, CC1101_MDMCFG0, 0xF8);
  ggCcWriteReg(csn, CC1101_DEVIATN,  0x15);
  ggCcWriteReg(csn, CC1101_MCSM1,   0x00);   // idle after TX
  ggCcWriteReg(csn, CC1101_MCSM0,   0x18);
  ggCcWriteReg(csn, CC1101_FREND0,  0x11);
  ggCcWriteReg(csn, CC1101_FSCAL3,  0xE9);
  ggCcWriteReg(csn, CC1101_FSCAL2,  0x2A);
  ggCcWriteReg(csn, CC1101_FSCAL1,  0x00);
  ggCcWriteReg(csn, CC1101_FSCAL0,  0x1F);
  ggCcWriteReg(csn, CC1101_SYNC1,   0x00);
  ggCcWriteReg(csn, CC1101_SYNC0,   0x00);
  uint8_t pt[2] = { 0x00, 0xC0 };            // max power ASK
  ggCcWriteBurst(csn, CC1101_PATABLE, pt, 2);

  // pre-fill TX FIFO then stream remaining data
  ggCcStrobe(csn, CC1101_SFTX);
  uint16_t pos = 0;
  uint8_t fill = (rec->len > 62) ? 62 : (uint8_t)rec->len;
  ggCcWriteBurst(csn, CC1101_TXFIFO, rec->data, fill);
  pos = fill;
  ggCcStrobe(csn, CC1101_STX);

  while (pos < rec->len) {
    uint8_t st = ggCcStrobe(csn, CC1101_SNOP);
    if ((st & CC1101_ST_MASK) == CC1101_ST_TXUNF) break;
    uint8_t txb = ggCcReadStatus(csn, CC1101_TXBYTES) & 0x7F;
    if (txb < 32) {
      uint8_t avail = 62 - txb;
      uint16_t rem  = rec->len - pos;
      uint8_t n = (rem > (uint16_t)avail) ? avail : (uint8_t)rem;
      ggCcWriteBurst(csn, CC1101_TXFIFO, &rec->data[pos], n);
      pos += n;
    }
  }
  uint32_t t = millis();
  while ((ggCcStrobe(csn, CC1101_SNOP) & CC1101_ST_MASK) == CC1101_ST_TX) {
    if (millis() - t > 2000) break;
  }
  ggCcIdle(csn);
}
