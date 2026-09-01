// ############################################################################
//  KODI NODE 0 — Radio Brain
//  ESP32U running PN532 RFID + 2x CC1101 Sub-GHz radios
//  Receives commands from Node 1 (ESP32R40T) via ESP-NOW
//  Debug output on Serial (USB)
//
//  WIRING:
//  PN532 (SPI mode, DIP: 1=OFF 2=ON):
//    VCC→3.3V  GND→GND  SCK→18  MISO→19  MOSI→23  SS→5
//  CC1101-A:
//    VCC→3.3V  GND→GND  SCK→14  MISO→12  MOSI→13  CSN→25  GDO0→4
//  CC1101-B:
//    VCC→3.3V  GND→GND  SCK→14(shared)  MISO→12  MOSI→13  CSN→27  GDO0→2
//  LEDs:
//    Red→GPIO16  Green→GPIO17  (each with ~220R to GND)
//  Add 330uF cap on 3.3V rail near modules
// ############################################################################

#include <SPI.h>
#include <esp_now.h>
#include <WiFi.h>

// ═══════════════════════════════════════════════════════════
//  PIN DEFINITIONS
// ═══════════════════════════════════════════════════════════

#define PN_SCK    18
#define PN_MISO   19
#define PN_MOSI   23
#define PN_SS      5

#define CC_SCK    14
#define CC_MISO   12
#define CC_MOSI   13
#define CC_CS_A   25
#define CC_CS_B   27

#define PIN_RED   16
#define PIN_GREEN 17

// ═══════════════════════════════════════════════════════════
//  CC1101 REGISTERS
// ═══════════════════════════════════════════════════════════

#define R_IOCFG2   0x00
#define R_IOCFG1   0x01
#define R_IOCFG0   0x02
#define R_FIFOTHR  0x03
#define R_SYNC1    0x04
#define R_SYNC0    0x05
#define R_PKTLEN   0x06
#define R_PKTCTRL1 0x07
#define R_PKTCTRL0 0x08
#define R_ADDR     0x09
#define R_CHANNR   0x0A
#define R_FSCTRL1  0x0B
#define R_FSCTRL0  0x0C
#define R_FREQ2    0x0D
#define R_FREQ1    0x0E
#define R_FREQ0    0x0F
#define R_MDMCFG4  0x10
#define R_MDMCFG3  0x11
#define R_MDMCFG2  0x12
#define R_MDMCFG1  0x13
#define R_MDMCFG0  0x14
#define R_DEVIATN  0x15
#define R_MCSM2    0x16
#define R_MCSM1    0x17
#define R_MCSM0    0x18
#define R_FOCCFG   0x19
#define R_BSCFG    0x1A
#define R_AGCCTRL2 0x1B
#define R_AGCCTRL1 0x1C
#define R_AGCCTRL0 0x1D
#define R_FREND1   0x21
#define R_FREND0   0x22
#define R_FSCAL3   0x23
#define R_FSCAL2   0x24
#define R_FSCAL1   0x25
#define R_FSCAL0   0x26

#define CMD_SRES   0x30
#define CMD_SCAL   0x33
#define CMD_SRX    0x34
#define CMD_STX    0x35
#define CMD_SIDLE  0x36
#define CMD_SPWD   0x39
#define CMD_SFRX   0x3A
#define CMD_SFTX   0x3B

#define ST_VERSION   0x31
#define ST_RSSI      0x34
#define ST_MARCSTATE 0x35
#define ST_TXBYTES   0x3A
#define ST_RXBYTES   0x3B

#define REG_PATABLE 0x3E
#define REG_FIFO    0x3F

#define MS_IDLE  0x01
#define MS_TXUF  0x16

// ═══════════════════════════════════════════════════════════
//  CC1101 CONFIGURATION TABLES
// ═══════════════════════════════════════════════════════════

static const uint8_t ccRateRegs[][3] = {
  {0xF5, 0x83, 0x15},  // 0: 1.2 kbps
  {0xF6, 0x83, 0x15},  // 1: 2.4 kbps
  {0xC8, 0x93, 0x34},  // 2: 10 kbps
  {0xCA, 0x83, 0x35},  // 3: 38.4 kbps
  {0x5B, 0xF8, 0x47},  // 4: 100 kbps
  {0x2D, 0x3B, 0x62},  // 5: 250 kbps
};

static const uint8_t ccPaTable[] = {
  0x12, 0x0E, 0x1D, 0x34, 0x60, 0x84, 0xC8, 0xC0
};

// ═══════════════════════════════════════════════════════════
//  DATA STRUCTURES
// ═══════════════════════════════════════════════════════════

#define REC_MAX 2048

struct CcRecording {
  uint8_t  data[REC_MAX];
  uint16_t len;
  float    freqMHz;
  bool     valid;
};

struct JamState {
  float    freqMHz;
  uint16_t hopCount;
  bool     active;
};

struct BfState {
  uint8_t  csn;
  float    freqMHz;
  uint8_t  codeLen;
  uint32_t current;
  uint32_t total;
  uint16_t gapMs;
  uint32_t lastTx;
  bool     active;
};

struct CwState {
  uint8_t csn;
  bool    active;
};

struct ReplayState {
  uint8_t  csn;
  uint16_t pos;
  uint16_t count;
  uint16_t target;
  bool     active;
  bool     started;
};

struct TxPumpState {
  uint8_t  csn;
  uint8_t  payload[64];
  int      pLen;
  int      count;
  int      gap;
  int      sent;
  int      idx;
  uint32_t lastTx;
  bool     active;
};

// ═══════════════════════════════════════════════════════════
//  GLOBALS
// ═══════════════════════════════════════════════════════════

SPIClass ccSpi(HSPI);

bool pn532Ok   = false;
bool cc1101aOk = false;
bool cc1101bOk = false;

CcRecording ccRec;
bool ccRecording[2] = {false, false};

JamState   jamState   = {0, 0, false};
BfState    bfState    = {0, 0, 0, 0, 0, 0, 0, false};
CwState    cwState    = {0, false};
ReplayState rplState  = {0, 0, 0, 0, false, false};
TxPumpState txPumpSt  = {0, {0}, 0, 0, 0, 0, 0, 0, false};

bool rfidLoop = false;
uint32_t rfidLastPoll = 0;

uint32_t greenOffAt = 0;

char cmdBuf[2048];

// RFID crack engine
#define MAX_CRACK_KEYS 2048
uint8_t crackKeys[MAX_CRACK_KEYS][6];
int crackKeyCount = 0;

struct CrackState {
  bool active;
  uint8_t uid[10];
  uint8_t uidLen;
  uint8_t sak;
  int sector;
  int maxSector;
  int keyType;
  int keyIdx;
  int found;
  int priorityKey;
  bool sectorDone[40][2];
  uint32_t lastTry;
  uint32_t attempts;
};
CrackState crack = {false};

// ═══════════════════════════════════════════════════════════
//  PN532 — BIT-BANG SPI (LSB-first, Mode 0)
// ═══════════════════════════════════════════════════════════

uint8_t pnXfer(uint8_t val) {
  uint8_t rx = 0;
  for (int b = 0; b < 8; b++) {
    digitalWrite(PN_MOSI, (val >> b) & 1);
    delayMicroseconds(5);
    digitalWrite(PN_SCK, HIGH);
    delayMicroseconds(5);
    if (digitalRead(PN_MISO)) rx |= (1 << b);
    digitalWrite(PN_SCK, LOW);
    delayMicroseconds(5);
  }
  return rx;
}

bool pnWaitReady(uint16_t ms) {
  uint32_t start = millis();
  while ((millis() - start) < ms) {
    digitalWrite(PN_SS, LOW);
    delayMicroseconds(5);
    pnXfer(0x02);
    uint8_t st = pnXfer(0x00);
    digitalWrite(PN_SS, HIGH);
    if (st == 0x01) return true;
    delay(5);
  }
  return false;
}

void pnSendCmd(const uint8_t* cmd, uint8_t cmdLen) {
  uint8_t len = cmdLen + 1;
  uint8_t lcs = ~len + 1;
  uint8_t sum = 0xD4;
  for (int i = 0; i < cmdLen; i++) sum += cmd[i];
  uint8_t dcs = ~sum + 1;
  digitalWrite(PN_SS, LOW);
  delay(2);
  pnXfer(0x01);
  pnXfer(0x00); pnXfer(0x00); pnXfer(0xFF);
  pnXfer(len); pnXfer(lcs); pnXfer(0xD4);
  for (int i = 0; i < cmdLen; i++) pnXfer(cmd[i]);
  pnXfer(dcs); pnXfer(0x00);
  digitalWrite(PN_SS, HIGH);
}

bool pnReadAck() {
  const uint8_t expected[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
  digitalWrite(PN_SS, LOW);
  delay(1);
  pnXfer(0x03);
  bool ok = true;
  for (int i = 0; i < 6; i++)
    if (pnXfer(0x00) != expected[i]) ok = false;
  digitalWrite(PN_SS, HIGH);
  return ok;
}

int pnReadResp(uint8_t* buf, uint8_t bufLen) {
  digitalWrite(PN_SS, LOW);
  delay(1);
  pnXfer(0x03);
  pnXfer(0x00);
  uint8_t sc1 = pnXfer(0x00);
  uint8_t sc2 = pnXfer(0x00);
  if (sc1 != 0x00 || sc2 != 0xFF) { digitalWrite(PN_SS, HIGH); return -1; }
  uint8_t len = pnXfer(0x00);
  pnXfer(0x00);
  int n = (len < bufLen) ? len : bufLen;
  for (int i = 0; i < n; i++) buf[i] = pnXfer(0x00);
  for (int i = n; i < len; i++) pnXfer(0x00);
  pnXfer(0x00); pnXfer(0x00);
  digitalWrite(PN_SS, HIGH);
  return n;
}

bool pnCommand(const uint8_t* cmd, uint8_t cmdLen, uint8_t* resp, uint8_t respLen, int* outLen) {
  pnSendCmd(cmd, cmdLen);
  if (!pnWaitReady(100)) return false;
  if (!pnReadAck())      return false;
  if (!pnWaitReady(100)) return false;
  *outLen = pnReadResp(resp, respLen);
  return (*outLen > 0);
}

bool pnScanCardFull(uint8_t* uid, uint8_t* uidLen, uint8_t* sak);

bool pnScanCard(uint8_t* uid, uint8_t* uidLen) {
  uint8_t sak = 0;
  return pnScanCardFull(uid, uidLen, &sak);
}

bool pnScanCardFull(uint8_t* uid, uint8_t* uidLen, uint8_t* sak) {
  uint8_t scanCmd[] = {0x4A, 0x01, 0x00};
  uint8_t resp[32];
  int rLen = 0;
  if (pnCommand(scanCmd, 3, resp, sizeof(resp), &rLen)) {
    if (rLen >= 8 && resp[0] == 0xD5 && resp[1] == 0x4B && resp[2] > 0) {
      *sak = resp[6];
      *uidLen = resp[7];
      if (*uidLen > 0 && *uidLen <= 10) {
        memcpy(uid, &resp[8], *uidLen);
        return true;
      }
    }
  }
  return false;
}

bool pnMifareAuth(uint8_t block, uint8_t keyType, const uint8_t* key, const uint8_t* uid, uint8_t uidLen) {
  uint8_t cmd[14];
  cmd[0] = 0x40;
  cmd[1] = 0x01;
  cmd[2] = keyType;
  cmd[3] = block;
  memcpy(&cmd[4], key, 6);
  uint8_t authUidLen = (uidLen > 4) ? 4 : uidLen;
  memcpy(&cmd[10], uid, authUidLen);
  uint8_t resp[8];
  int rLen = 0;
  if (!pnCommand(cmd, 10 + authUidLen, resp, sizeof(resp), &rLen)) return false;
  return (rLen >= 3 && resp[0] == 0xD5 && resp[1] == 0x41 && resp[2] == 0x00);
}

int pnMifareRead(uint8_t block, uint8_t* data16) {
  uint8_t cmd[] = {0x40, 0x01, 0x30, block};
  uint8_t resp[24];
  int rLen = 0;
  if (!pnCommand(cmd, 4, resp, sizeof(resp), &rLen)) return -1;
  if (rLen >= 19 && resp[0] == 0xD5 && resp[1] == 0x41 && resp[2] == 0x00) {
    memcpy(data16, &resp[3], 16);
    return 16;
  }
  return -1;
}

void pnResetField() {
  uint8_t resp[8]; int rLen;
  uint8_t rfOff[] = {0x32, 0x01, 0x00};
  pnCommand(rfOff, 3, resp, sizeof(resp), &rLen);
  delay(5);
  uint8_t rfOn[] = {0x32, 0x01, 0x01};
  pnCommand(rfOn, 3, resp, sizeof(resp), &rLen);
}

// ═══════════════════════════════════════════════════════════
//  CC1101 — HARDWARE SPI DRIVER (HSPI, 4 MHz)
// ═══════════════════════════════════════════════════════════

void ccSelect(uint8_t csn) {
  ccSpi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  digitalWrite(csn, LOW);
  uint32_t t = micros();
  while (digitalRead(CC_MISO) && (micros() - t) < 50);
}

void ccDeselect(uint8_t csn) {
  digitalWrite(csn, HIGH);
  ccSpi.endTransaction();
}

void ccWriteReg(uint8_t csn, uint8_t addr, uint8_t val) {
  ccSelect(csn);
  ccSpi.transfer(addr);
  ccSpi.transfer(val);
  ccDeselect(csn);
}

uint8_t ccReadReg(uint8_t csn, uint8_t addr) {
  ccSelect(csn);
  ccSpi.transfer(addr | 0x80);
  uint8_t val = ccSpi.transfer(0);
  ccDeselect(csn);
  return val;
}

void ccWriteBurst(uint8_t csn, uint8_t addr, const uint8_t* data, int len) {
  ccSelect(csn);
  ccSpi.transfer(addr | 0x40);
  for (int i = 0; i < len; i++) ccSpi.transfer(data[i]);
  ccDeselect(csn);
}

void ccReadBurst(uint8_t csn, uint8_t addr, uint8_t* data, int len) {
  ccSelect(csn);
  ccSpi.transfer(addr | 0x80 | 0x40);
  for (int i = 0; i < len; i++) data[i] = ccSpi.transfer(0);
  ccDeselect(csn);
}

uint8_t ccStrobe(uint8_t csn, uint8_t cmd) {
  ccSelect(csn);
  uint8_t st = ccSpi.transfer(cmd);
  ccDeselect(csn);
  return st;
}

uint8_t ccReadStatus(uint8_t csn, uint8_t addr) {
  ccSelect(csn);
  ccSpi.transfer(addr | 0xC0);
  uint8_t val = ccSpi.transfer(0);
  ccDeselect(csn);
  return val;
}

// ═══════════════════════════════════════════════════════════
//  CC1101 — RADIO OPERATIONS
// ═══════════════════════════════════════════════════════════

void ccReset(uint8_t csn) {
  digitalWrite(csn, HIGH); delayMicroseconds(30);
  digitalWrite(csn, LOW);  delayMicroseconds(10);
  digitalWrite(csn, HIGH); delay(1);
  ccStrobe(csn, CMD_SRES);
  delay(2);
}

void ccIdle(uint8_t csn) {
  ccStrobe(csn, CMD_SIDLE);
  for (int i = 0; i < 200; i++) {
    if ((ccReadStatus(csn, ST_MARCSTATE) & 0x1F) == MS_IDLE) return;
    delayMicroseconds(100);
  }
}

void ccFlushTx(uint8_t csn)  { ccIdle(csn); ccStrobe(csn, CMD_SFTX); }
void ccFlushRx(uint8_t csn)  { ccIdle(csn); ccStrobe(csn, CMD_SFRX); }

void ccCalibrate(uint8_t csn) {
  ccIdle(csn);
  ccStrobe(csn, CMD_SCAL);
  for (int i = 0; i < 200; i++) {
    if ((ccReadStatus(csn, ST_MARCSTATE) & 0x1F) == MS_IDLE) return;
    delayMicroseconds(100);
  }
}

void ccSetFreq(uint8_t csn, float freqMHz) {
  uint32_t f = (uint32_t)(freqMHz * 65536.0f / 26.0f + 0.5f);
  ccWriteReg(csn, R_FREQ2, (f >> 16) & 0xFF);
  ccWriteReg(csn, R_FREQ1, (f >>  8) & 0xFF);
  ccWriteReg(csn, R_FREQ0,  f        & 0xFF);
}

uint8_t ccGetVersion(uint8_t csn) {
  ccReset(csn);
  uint8_t ver = ccReadStatus(csn, ST_VERSION);
  return (ver != 0x00 && ver != 0xFF) ? ver : 0;
}

int8_t ccGetRssi(uint8_t csn) {
  uint8_t raw = ccReadStatus(csn, ST_RSSI);
  int16_t r = (raw >= 128) ? (raw - 256) : raw;
  return (int8_t)(r / 2 - 74);
}

void ccInit(uint8_t csn, float freqMHz, uint8_t mod, uint8_t rate, uint8_t power, uint8_t pktLen, uint8_t sync1, uint8_t sync0) {
  ccReset(csn);
  ccWriteReg(csn, R_IOCFG2,  0x29);
  ccWriteReg(csn, R_IOCFG1,  0x2E);
  ccWriteReg(csn, R_IOCFG0,  0x06);
  ccWriteReg(csn, R_FIFOTHR, 0x07);
  ccWriteReg(csn, R_PKTLEN,  pktLen);
  ccWriteReg(csn, R_PKTCTRL1,0x08);
  ccWriteReg(csn, R_PKTCTRL0,0x04);
  ccWriteReg(csn, R_ADDR,    0x00);
  ccWriteReg(csn, R_CHANNR,  0x00);
  ccWriteReg(csn, R_FSCTRL1, 0x06);
  ccWriteReg(csn, R_FSCTRL0, 0x00);
  ccSetFreq(csn, freqMHz);
  if (rate > 5) rate = 1;
  ccWriteReg(csn, R_MDMCFG4, ccRateRegs[rate][0]);
  ccWriteReg(csn, R_MDMCFG3, ccRateRegs[rate][1]);
  uint8_t mdm2 = 0x02;
  switch (mod) {
    case 0: mdm2 |= 0x30; break;
    case 1: mdm2 |= 0x00; break;
    case 2: mdm2 |= 0x10; break;
    case 3: mdm2 |= 0x70; break;
  }
  ccWriteReg(csn, R_MDMCFG2, mdm2);
  ccWriteReg(csn, R_MDMCFG1, 0x22);
  ccWriteReg(csn, R_MDMCFG0, 0xF8);
  ccWriteReg(csn, R_DEVIATN, ccRateRegs[rate][2]);
  ccWriteReg(csn, R_MCSM2,   0x07);
  ccWriteReg(csn, R_MCSM1,   0x30);
  ccWriteReg(csn, R_MCSM0,   0x18);
  ccWriteReg(csn, R_FOCCFG,  0x16);
  ccWriteReg(csn, R_BSCFG,   0x6C);
  ccWriteReg(csn, R_AGCCTRL2,0x43);
  ccWriteReg(csn, R_AGCCTRL1,0x40);
  ccWriteReg(csn, R_AGCCTRL0,0x91);
  ccWriteReg(csn, R_FREND1,  0x56);
  ccWriteReg(csn, R_FREND0, (mod == 0) ? 0x11 : 0x10);
  ccWriteReg(csn, R_FSCAL3,  0xE9);
  ccWriteReg(csn, R_FSCAL2,  0x2A);
  ccWriteReg(csn, R_FSCAL1,  0x00);
  ccWriteReg(csn, R_FSCAL0,  0x1F);
  ccWriteReg(csn, R_SYNC1, sync1);
  ccWriteReg(csn, R_SYNC0, sync0);
  if (power > 7) power = 7;
  if (mod == 0) {
    uint8_t pa[2] = {0x00, ccPaTable[power]};
    ccWriteBurst(csn, REG_PATABLE, pa, 2);
  } else {
    ccWriteBurst(csn, REG_PATABLE, &ccPaTable[power], 1);
  }
  ccFlushRx(csn); ccFlushTx(csn);
  ccCalibrate(csn); ccIdle(csn);
}

bool ccTxPacket(uint8_t csn, const uint8_t* data, uint8_t len) {
  ccIdle(csn); ccFlushTx(csn);
  ccWriteReg(csn, R_PKTLEN, len);
  ccWriteBurst(csn, REG_FIFO, data, len);
  ccStrobe(csn, CMD_STX);
  uint32_t t = millis();
  while (millis() - t < 200) {
    uint8_t ms = ccReadStatus(csn, ST_MARCSTATE) & 0x1F;
    if (ms == MS_IDLE) return true;
    if (ms == MS_TXUF) { ccFlushTx(csn); return false; }
    delayMicroseconds(100);
  }
  ccIdle(csn);
  return false;
}

void ccRxBegin(uint8_t csn) {
  ccIdle(csn); ccFlushRx(csn);
  ccStrobe(csn, CMD_SRX);
}

int ccRxRead(uint8_t csn, uint8_t* buf, int maxLen) {
  uint8_t rxb = ccReadStatus(csn, ST_RXBYTES);
  if (rxb & 0x80) { ccFlushRx(csn); ccStrobe(csn, CMD_SRX); return -1; }
  if (rxb == 0) return 0;
  uint8_t pktLen = ccReadReg(csn, R_PKTLEN);
  if (rxb < pktLen) return 0;
  int n = (pktLen < maxLen) ? pktLen : maxLen;
  ccReadBurst(csn, REG_FIFO, buf, n);
  if (rxb > pktLen) { ccFlushRx(csn); ccStrobe(csn, CMD_SRX); }
  return n;
}

void ccRecordBegin(uint8_t csn, float freqMHz) {
  ccReset(csn);
  ccWriteReg(csn, R_PKTLEN,  0xFF);
  ccWriteReg(csn, R_PKTCTRL0,0x02);
  ccWriteReg(csn, R_MDMCFG2, 0x30);
  ccSetFreq(csn, freqMHz);
  ccWriteReg(csn, R_MDMCFG4, 0xF6);
  ccWriteReg(csn, R_MDMCFG3, 0x83);
  ccWriteReg(csn, R_AGCCTRL2,0x07);
  ccWriteReg(csn, R_MCSM1,   0x30);
  ccWriteReg(csn, R_MCSM0,   0x18);
  ccWriteReg(csn, R_FOCCFG,  0x16);
  ccWriteReg(csn, R_BSCFG,   0x6C);
  ccWriteReg(csn, R_FREND1,  0x56);
  ccWriteReg(csn, R_FREND0,  0x11);
  ccWriteReg(csn, R_FSCAL3,  0xE9);
  ccWriteReg(csn, R_FSCAL2,  0x2A);
  ccWriteReg(csn, R_FSCAL1,  0x00);
  ccWriteReg(csn, R_FSCAL0,  0x1F);
  uint8_t pa = 0x00;
  ccWriteBurst(csn, REG_PATABLE, &pa, 1);
  ccFlushRx(csn);
  ccStrobe(csn, CMD_SRX);
  ccRec.len = 0;
  ccRec.freqMHz = freqMHz;
  ccRec.valid = false;
}

bool ccRecordPoll(uint8_t csn) {
  uint8_t rxb = ccReadStatus(csn, ST_RXBYTES);
  if (rxb & 0x80) { ccFlushRx(csn); ccStrobe(csn, CMD_SRX); return false; }
  if (rxb == 0) return false;
  if (ccRec.len == 0) {
    int8_t rssi = ccGetRssi(csn);
    if (rssi < -75) {
      ccFlushRx(csn);
      return false;
    }
  }
  int avail = rxb;
  if (ccRec.len + avail > REC_MAX) avail = REC_MAX - ccRec.len;
  if (avail > 0) {
    ccReadBurst(csn, REG_FIFO, &ccRec.data[ccRec.len], avail);
    ccRec.len += avail;
  }
  if (ccRec.len >= REC_MAX) { ccRec.valid = true; return true; }
  return (ccRec.len > 0);
}

void ccReplayBegin(uint8_t csn, uint16_t count) {
  ccReset(csn);
  ccWriteReg(csn, R_PKTCTRL0,0x02);
  ccWriteReg(csn, R_MDMCFG2, 0x30);
  ccSetFreq(csn, ccRec.freqMHz);
  ccWriteReg(csn, R_MDMCFG4, 0xF6);
  ccWriteReg(csn, R_MDMCFG3, 0x83);
  ccWriteReg(csn, R_MCSM1,   0x00);
  ccWriteReg(csn, R_MCSM0,   0x18);
  ccWriteReg(csn, R_FREND0,  0x11);
  ccWriteReg(csn, R_FSCAL3,  0xE9);
  ccWriteReg(csn, R_FSCAL2,  0x2A);
  ccWriteReg(csn, R_FSCAL1,  0x00);
  ccWriteReg(csn, R_FSCAL0,  0x1F);
  uint8_t pa[2] = {0x00, 0xC0};
  ccWriteBurst(csn, REG_PATABLE, pa, 2);
  ccFlushTx(csn);
  int chunk = (ccRec.len < 62) ? ccRec.len : 62;
  ccWriteBurst(csn, REG_FIFO, ccRec.data, chunk);
  rplState.csn = csn;
  rplState.pos = chunk;
  rplState.count = 0;
  rplState.target = count;
  rplState.active = true;
  rplState.started = false;
  ccStrobe(csn, CMD_STX);
  rplState.started = true;
}

// ═══════════════════════════════════════════════════════════
//  HELPERS
// ═══════════════════════════════════════════════════════════

uint8_t hexNib(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return 0xFF;
}

int hexDecode(const char* hex, uint8_t* out, int maxLen) {
  int len = 0;
  while (hex[0] && hex[1] && len < maxLen) {
    uint8_t hi = hexNib(hex[0]), lo = hexNib(hex[1]);
    if (hi > 15 || lo > 15) break;
    out[len++] = (hi << 4) | lo;
    hex += 2;
  }
  return len;
}

void hexEncode(char* out, const uint8_t* data, int len) {
  static const char hx[] = "0123456789ABCDEF";
  for (int i = 0; i < len; i++) {
    out[i*2]   = hx[data[i] >> 4];
    out[i*2+1] = hx[data[i] & 0x0F];
  }
  out[len*2] = '\0';
}

uint8_t getCSN(char r) { return (r == 'B' || r == 'b') ? CC_CS_B : CC_CS_A; }
int     getRIdx(char r) { return (r == 'B' || r == 'b') ? 1 : 0; }

void ledFlashGreen() {
  digitalWrite(PIN_GREEN, HIGH);
  greenOffAt = millis() + 1500;
}

// ═══════════════════════════════════════════════════════════
//  ESP-NOW COMMUNICATION
// ═══════════════════════════════════════════════════════════

static const uint8_t espBroadcast[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

#define ESP_MSG_SLOTS 4
#define ESP_MSG_SIZE  512
char espMsgSlots[ESP_MSG_SLOTS][ESP_MSG_SIZE];
volatile int espMsgHead = 0;
volatile int espMsgTail = 0;

static char espAssembly[2048];
static int  espAssemblyPos = 0;

char espTxBuf[1024];

void onEspRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len <= 0) return;
  bool more = (len == 250 && data[249] == 0x01);
  int copyLen = more ? 249 : len;
  if (espAssemblyPos + copyLen < (int)sizeof(espAssembly) - 1) {
    memcpy(espAssembly + espAssemblyPos, data, copyLen);
    espAssemblyPos += copyLen;
  }
  if (!more) {
    espAssembly[espAssemblyPos] = '\0';
    int next = (espMsgHead + 1) % ESP_MSG_SLOTS;
    if (next != espMsgTail) {
      strlcpy(espMsgSlots[espMsgHead], espAssembly, ESP_MSG_SIZE);
      espMsgHead = next;
    }
    espAssemblyPos = 0;
  }
}

bool espMsgAvail() { return espMsgHead != espMsgTail; }

const char* espMsgRead() {
  if (!espMsgAvail()) return nullptr;
  const char* msg = espMsgSlots[espMsgTail];
  espMsgTail = (espMsgTail + 1) % ESP_MSG_SLOTS;
  return msg;
}

volatile bool espSendDone = true;

void onEspSent(const esp_now_send_info_t *info, esp_now_send_status_t status) {
  espSendDone = true;
}

void espWaitSend() {
  uint32_t t = millis();
  while (!espSendDone && millis() - t < 100) delay(1);
}

void espSend(const char* msg) {
  int len = strlen(msg);
  espWaitSend();
  if (len <= 249) {
    espSendDone = false;
    esp_err_t err = esp_now_send(espBroadcast, (const uint8_t*)msg, len);
    if (err != ESP_OK) { espSendDone = true; delay(5); }
    return;
  }
  int off = 0;
  while (off < len) {
    int remain = len - off;
    espWaitSend();
    if (remain > 249) {
      uint8_t pkt[250];
      memcpy(pkt, msg + off, 249);
      pkt[249] = 0x01;
      espSendDone = false;
      esp_err_t err = esp_now_send(espBroadcast, pkt, 250);
      if (err != ESP_OK) { espSendDone = true; delay(5); }
      off += 249;
    } else {
      espSendDone = false;
      esp_err_t err = esp_now_send(espBroadcast, (const uint8_t*)(msg + off), remain);
      if (err != ESP_OK) { espSendDone = true; delay(5); }
      off += remain;
    }
  }
}

void espSendFmt(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(espTxBuf, sizeof(espTxBuf), fmt, ap);
  va_end(ap);
  espSend(espTxBuf);
}

void espNowSetup() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_now_init() != ESP_OK) {
    Serial.println(F("ESP-NOW init FAILED"));
    return;
  }
  esp_now_register_recv_cb(onEspRecv);
  esp_now_register_send_cb(onEspSent);
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, espBroadcast, 6);
  peer.channel = 0;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
  Serial.println(F("ESP-NOW: OK"));
}

// ═══════════════════════════════════════════════════════════
//  COMMAND HANDLER
// ═══════════════════════════════════════════════════════════

void sendReply(const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  espSend(buf);
  Serial.print("[TX] "); Serial.println(buf);
}

void processCommand(char* cmd) {
  Serial.print("[RX] "); Serial.println(cmd);

  // RFID_KEYADD handled before tokenizer (needs raw comma-separated string)
  if (strncmp(cmd, "RFID_KEYADD,", 12) == 0) {
    int added = 0;
    char* p = cmd + 12;
    while (*p && crackKeyCount < MAX_CRACK_KEYS) {
      char* comma = strchr(p, ',');
      int len = comma ? (comma - p) : (int)strlen(p);
      if (len == 12) {
        char tmp[13]; memcpy(tmp, p, 12); tmp[12] = '\0';
        hexDecode(tmp, crackKeys[crackKeyCount], 6);
        crackKeyCount++;
        added++;
      }
      if (!comma) break;
      p = comma + 1;
    }
    sendReply("RK,ADD,%d,%d", added, crackKeyCount);
    return;
  }

  char* tok[16];
  int nt = 0;
  tok[nt++] = cmd;
  for (int i = 0; cmd[i] && nt < 16; i++) {
    if (cmd[i] == ',') { cmd[i] = '\0'; tok[nt++] = &cmd[i+1]; }
  }

  // --- PING ---
  if (strcmp(tok[0], "PING") == 0) {
    ledFlashGreen();
    sendReply("PONG");
  }
  // --- STATUS ---
  else if (strcmp(tok[0], "STATUS") == 0) {
    sendReply("ST,%d,%d,%d", pn532Ok?1:0, cc1101aOk?1:0, cc1101bOk?1:0);
  }
  // --- RFID single scan ---
  else if (strcmp(tok[0], "RFID") == 0) {
    if (!pn532Ok) { sendReply("RF,ERR"); return; }
    uint8_t uid[10]; uint8_t uidLen = 0;
    if (pnScanCard(uid, &uidLen)) {
      ledFlashGreen();
      char hex[21];
      hexEncode(hex, uid, uidLen);
      sendReply("RF,%d,%s", uidLen, hex);
    } else {
      sendReply("RF,0");
    }
  }
  // --- RFID loop start/stop ---
  else if (strcmp(tok[0], "RFID_LOOP") == 0) {
    rfidLoop = true; rfidLastPoll = 0;
    sendReply("RL,1");
  }
  else if (strcmp(tok[0], "RFID_STOP") == 0) {
    rfidLoop = false;
    sendReply("RL,0");
  }
  // --- CC_INIT: radio,freq,mod,rate,power,pktlen,sync1,sync0 ---
  else if (strcmp(tok[0], "CC_INIT") == 0 && nt >= 9) {
    uint8_t csn = getCSN(tok[1][0]);
    if (txPumpSt.active && txPumpSt.csn == csn) txPumpSt.active = false;
    float freq = atof(tok[2]);
    uint8_t mod = atoi(tok[3]), rate = atoi(tok[4]), pwr = atoi(tok[5]);
    uint8_t pktLen = atoi(tok[6]);
    uint8_t s1 = (uint8_t)strtol(tok[7], NULL, 16);
    uint8_t s0 = (uint8_t)strtol(tok[8], NULL, 16);
    ccInit(csn, freq, mod, rate, pwr, pktLen, s1, s0);
    sendReply("CI,OK");
  }
  // --- CC_TX: radio,hexdata,count,gap ---
  else if (strcmp(tok[0], "CC_TX") == 0 && nt >= 5) {
    uint8_t csn = getCSN(tok[1][0]);
    uint8_t payload[64];
    int pLen = hexDecode(tok[2], payload, 61);
    int count = atoi(tok[3]);
    int gap = atoi(tok[4]);
    if (count <= 1) {
      int sent = ccTxPacket(csn, payload, pLen) ? 1 : 0;
      ledFlashGreen();
      sendReply("CT,%d", sent);
    } else {
      txPumpSt.csn = csn;
      memcpy(txPumpSt.payload, payload, pLen);
      txPumpSt.pLen = pLen;
      txPumpSt.count = count;
      txPumpSt.gap = (gap < 1) ? 1 : gap;
      txPumpSt.sent = ccTxPacket(csn, payload, pLen) ? 1 : 0;
      txPumpSt.idx = 1;
      txPumpSt.lastTx = millis();
      txPumpSt.active = true;
      ledFlashGreen();
    }
  }
  // --- CC_RX: radio ---
  else if (strcmp(tok[0], "CC_RX") == 0 && nt >= 2) {
    uint8_t csn = getCSN(tok[1][0]);
    ccRxBegin(csn);
    sendReply("CR,ON");
  }
  // --- CC_RXPOLL: radio ---
  else if (strcmp(tok[0], "CC_RXPOLL") == 0 && nt >= 2) {
    uint8_t csn = getCSN(tok[1][0]);
    uint8_t buf[64];
    int n = ccRxRead(csn, buf, 61);
    if (n > 0) {
      ledFlashGreen();
      char hex[123];
      hexEncode(hex, buf, n);
      sendReply("RD,%d,%s", n, hex);
    } else {
      sendReply("RD,0");
    }
  }
  // --- CC_RXSTOP: radio ---
  else if (strcmp(tok[0], "CC_RXSTOP") == 0 && nt >= 2) {
    uint8_t csn = getCSN(tok[1][0]);
    ccIdle(csn);
    sendReply("CR,OFF");
  }
  // --- CC_RSSI: radio ---
  else if (strcmp(tok[0], "CC_RSSI") == 0 && nt >= 2) {
    uint8_t csn = getCSN(tok[1][0]);
    sendReply("RS,%d", ccGetRssi(csn));
  }
  // --- CC_FREQ: radio,freq ---
  else if (strcmp(tok[0], "CC_FREQ") == 0 && nt >= 3) {
    uint8_t csn = getCSN(tok[1][0]);
    ccIdle(csn);
    ccSetFreq(csn, atof(tok[2]));
    ccCalibrate(csn);
    sendReply("CF,OK");
  }
  // --- CC_REC: radio,freq ---
  else if (strcmp(tok[0], "CC_REC") == 0 && nt >= 3) {
    uint8_t csn = getCSN(tok[1][0]);
    int idx = getRIdx(tok[1][0]);
    ccRecordBegin(csn, atof(tok[2]));
    ccRecording[idx] = true;
    sendReply("RE,ON");
  }
  // --- CC_RECPOLL: radio ---
  else if (strcmp(tok[0], "CC_RECPOLL") == 0 && nt >= 2) {
    sendReply("RP,%d", ccRec.len);
  }
  // --- CC_RECSTOP ---
  else if (strcmp(tok[0], "CC_RECSTOP") == 0 && nt >= 2) {
    uint8_t csn = getCSN(tok[1][0]);
    int idx = getRIdx(tok[1][0]);
    ccIdle(csn);
    ccRecording[idx] = false;
    ccRec.valid = (ccRec.len > 0);
    sendReply("RE,OFF,%d", ccRec.len);
  }
  // --- CC_RECGET: retrieve recorded data in chunks ---
  else if (strcmp(tok[0], "CC_RECGET") == 0) {
    if (!ccRec.valid || ccRec.len == 0) { sendReply("RG,0"); return; }
    sendReply("RG,%d,%.2f", ccRec.len, ccRec.freqMHz);
    delay(5);
    int chunkBytes = 80;
    for (int off = 0; off < ccRec.len; off += chunkBytes) {
      int n = min(chunkBytes, (int)(ccRec.len - off));
      int pos = snprintf(espTxBuf, sizeof(espTxBuf), "RD,%d,%d,", off, n);
      hexEncode(espTxBuf + pos, &ccRec.data[off], n);
      espSend(espTxBuf);
      delay(5);
    }
    espSend("RD,DONE");
  }
  // --- CC_RECSET: freq --- start new recording load
  else if (strcmp(tok[0], "CC_RECSET") == 0 && nt >= 2) {
    ccRec.freqMHz = atof(tok[1]);
    ccRec.len = 0;
    ccRec.valid = false;
    sendReply("RS,OK");
  }
  // --- CC_RECADD: hexdata --- append chunk to recording
  else if (strcmp(tok[0], "CC_RECADD") == 0 && nt >= 2) {
    int added = hexDecode(tok[1], ccRec.data + ccRec.len, REC_MAX - ccRec.len);
    ccRec.len += added;
    sendReply("RA,%d", ccRec.len);
  }
  // --- CC_RECDONE --- finalize loaded recording
  else if (strcmp(tok[0], "CC_RECDONE") == 0) {
    ccRec.valid = (ccRec.len > 0);
    sendReply("RS,DONE,%d", ccRec.len);
  }
  // --- CC_REPLAY: radio,count ---
  else if (strcmp(tok[0], "CC_REPLAY") == 0 && nt >= 2) {
    if (!ccRec.valid) { sendReply("RY,ERR"); return; }
    uint8_t csn = getCSN(tok[1][0]);
    uint16_t cnt = (nt >= 3) ? atoi(tok[2]) : 1;
    ccReplayBegin(csn, cnt);
    sendReply("RY,ON");
  }
  // --- CC_REPLAYSTOP ---
  else if (strcmp(tok[0], "CC_REPLAYSTOP") == 0) {
    if (rplState.active) {
      ccIdle(rplState.csn);
      rplState.active = false;
    }
    sendReply("RY,OFF");
  }
  // --- CC_SCAN: startFreq,endFreq,steps ---
  else if (strcmp(tok[0], "CC_SCAN") == 0 && nt >= 4) {
    float startF = atof(tok[1]), endF = atof(tok[2]);
    int steps = atoi(tok[3]);
    if (steps < 2) steps = 2;
    if (steps > 128) steps = 128;
    uint8_t csn = CC_CS_A;
    ccIdle(csn);
    ccWriteReg(csn, R_PKTCTRL0, 0x02);
    ccWriteReg(csn, R_MDMCFG2,  0x30);
    ccWriteReg(csn, R_AGCCTRL2, 0x07);
    ccWriteReg(csn, R_MCSM1,    0x30);
    ccWriteReg(csn, R_MCSM0,    0x18);
    ccWriteReg(csn, R_FOCCFG,   0x16);
    ccWriteReg(csn, R_BSCFG,    0x6C);
    int pos = snprintf(espTxBuf, sizeof(espTxBuf), "SC,%d", steps);
    float step = (endF - startF) / (steps - 1);
    for (int i = 0; i < steps; i++) {
      ccIdle(csn);
      ccSetFreq(csn, startF + i * step);
      ccStrobe(csn, CMD_SRX);
      delayMicroseconds(800);
      pos += snprintf(espTxBuf + pos, sizeof(espTxBuf) - pos, ",%d", ccGetRssi(csn));
    }
    espSend(espTxBuf);
    ccIdle(csn);
  }
  // --- CC_JAM: freq ---
  else if (strcmp(tok[0], "CC_JAM") == 0 && nt >= 2) {
    float freq = atof(tok[1]);
    jamState.freqMHz = freq;
    jamState.hopCount = 0;
    jamState.active = true;
    for (int r = 0; r < 2; r++) {
      uint8_t csn = r ? CC_CS_B : CC_CS_A;
      ccReset(csn);
      ccWriteReg(csn, R_IOCFG0,  0x06);
      ccWriteReg(csn, R_PKTLEN,  61);
      ccWriteReg(csn, R_PKTCTRL0,0x04);
      ccWriteReg(csn, R_FSCTRL1, 0x06);
      ccSetFreq(csn, freq);
      ccWriteReg(csn, R_MDMCFG4, 0x2D);
      ccWriteReg(csn, R_MDMCFG3, 0x3B);
      ccWriteReg(csn, R_MDMCFG2, 0x02);
      ccWriteReg(csn, R_DEVIATN, 0x77);
      ccWriteReg(csn, R_MCSM1,   0x00);
      ccWriteReg(csn, R_MCSM0,   0x18);
      ccWriteReg(csn, R_FREND0,  0x10);
      uint8_t pa = 0xC0;
      ccWriteBurst(csn, REG_PATABLE, &pa, 1);
      ccCalibrate(csn);
    }
    sendReply("JM,ON");
  }
  // --- CC_JAMSTOP ---
  else if (strcmp(tok[0], "CC_JAMSTOP") == 0) {
    jamState.active = false;
    ccIdle(CC_CS_A); ccIdle(CC_CS_B);
    sendReply("JM,OFF");
  }
  // --- CC_BF: radio,freq,codelen,gap ---
  else if (strcmp(tok[0], "CC_BF") == 0 && nt >= 5) {
    uint8_t csn = getCSN(tok[1][0]);
    bfState.csn = csn;
    bfState.freqMHz = atof(tok[2]);
    bfState.codeLen = constrain(atoi(tok[3]), 1, 4);
    bfState.gapMs = atoi(tok[4]);
    bfState.current = 0;
    bfState.total = 1;
    for (int i = 0; i < bfState.codeLen; i++) bfState.total *= 256;
    bfState.lastTx = 0;
    bfState.active = true;
    ccInit(csn, bfState.freqMHz, 0, 1, 7, bfState.codeLen, 0xD3, 0x91);
    sendReply("BF,ON");
  }
  // --- CC_BFPOLL ---
  else if (strcmp(tok[0], "CC_BFPOLL") == 0) {
    if (!bfState.active) { sendReply("BP,0,100"); return; }
    uint32_t pct = (bfState.current * 100) / bfState.total;
    sendReply("BP,%lu,%lu", bfState.current, pct);
  }
  // --- CC_BFSTOP ---
  else if (strcmp(tok[0], "CC_BFSTOP") == 0) {
    bfState.active = false;
    ccIdle(bfState.csn);
    sendReply("BF,OFF");
  }
  // --- CC_CW: radio,freq ---
  else if (strcmp(tok[0], "CC_CW") == 0 && nt >= 3) {
    uint8_t csn = getCSN(tok[1][0]);
    ccReset(csn);
    ccWriteReg(csn, R_PKTCTRL0,0x02);
    ccWriteReg(csn, R_MDMCFG2, 0x02);
    ccSetFreq(csn, atof(tok[2]));
    ccWriteReg(csn, R_MDMCFG4, 0x2D);
    ccWriteReg(csn, R_MDMCFG3, 0x3B);
    ccWriteReg(csn, R_DEVIATN, 0x62);
    ccWriteReg(csn, R_MCSM1,   0x00);
    ccWriteReg(csn, R_MCSM0,   0x18);
    ccWriteReg(csn, R_FREND0,  0x10);
    ccWriteReg(csn, R_SYNC1,   0xFF);
    ccWriteReg(csn, R_SYNC0,   0xFF);
    uint8_t pa = 0xC0;
    ccWriteBurst(csn, REG_PATABLE, &pa, 1);
    ccFlushTx(csn);
    uint8_t fill[62]; memset(fill, 0xFF, 62);
    ccWriteBurst(csn, REG_FIFO, fill, 62);
    cwState.csn = csn;
    cwState.active = true;
    ccStrobe(csn, CMD_STX);
    sendReply("CW,ON");
  }
  // --- CC_CWSTOP ---
  else if (strcmp(tok[0], "CC_CWSTOP") == 0) {
    cwState.active = false;
    ccIdle(cwState.csn);
    sendReply("CW,OFF");
  }
  // --- CC_VER: radio ---
  else if (strcmp(tok[0], "CC_VER") == 0 && nt >= 2) {
    uint8_t csn = getCSN(tok[1][0]);
    uint8_t ver = ccReadStatus(csn, ST_VERSION);
    sendReply("CV,%02X", ver);
  }
  // --- RFID_KEYCLR: clear key buffer ---
  else if (strcmp(tok[0], "RFID_KEYCLR") == 0) {
    crackKeyCount = 0;
    sendReply("RK,CLR");
  }
  // --- RFID_CRACK: start cracking ---
  else if (strcmp(tok[0], "RFID_CRACK") == 0) {
    if (!pn532Ok) { sendReply("RC,ERR,NPN"); return; }
    if (crackKeyCount == 0) { sendReply("RC,ERR,NOKEYS"); return; }
    uint8_t uid[10]; uint8_t uidLen = 0; uint8_t sak = 0;
    if (!pnScanCardFull(uid, &uidLen, &sak)) { sendReply("RC,ERR,NOCARD"); return; }
    crack.active = true;
    memcpy(crack.uid, uid, uidLen);
    crack.uidLen = uidLen;
    crack.sak = sak;
    crack.sector = 0;
    crack.keyType = 0;
    crack.keyIdx = -1;
    crack.found = 0;
    crack.priorityKey = -1;
    crack.attempts = 0;
    crack.lastTry = 0;
    if (sak == 0x18) crack.maxSector = 40;
    else crack.maxSector = 16;
    memset(crack.sectorDone, 0, sizeof(crack.sectorDone));
    char hex[21];
    hexEncode(hex, uid, uidLen);
    sendReply("RC,ON,%d,%s,%02X,%d", uidLen, hex, sak, crack.maxSector);
    rfidLoop = false;
  }
  // --- RFID_CRACKSTOP ---
  else if (strcmp(tok[0], "RFID_CRACKSTOP") == 0) {
    crack.active = false;
    sendReply("RC,OFF,%d", crack.found);
  }
  // --- RFID_CRACKPOLL ---
  else if (strcmp(tok[0], "RFID_CRACKPOLL") == 0) {
    if (!crack.active) { sendReply("RP,0,0,0"); return; }
    int total = crack.maxSector * 2 * crackKeyCount;
    sendReply("RP,%lu,%d,%d", crack.attempts, total, crack.found);
  }
  // --- RFID_AUTH: single key test block,type(A/B),keyhex ---
  else if (strcmp(tok[0], "RFID_AUTH") == 0 && nt >= 4) {
    if (!pn532Ok) { sendReply("RA,ERR"); return; }
    uint8_t block = atoi(tok[1]);
    uint8_t kt = (tok[2][0] == 'B' || tok[2][0] == 'b') ? 0x61 : 0x60;
    uint8_t key[6];
    hexDecode(tok[3], key, 6);
    uint8_t uid[10]; uint8_t uidLen = 0;
    if (!pnScanCard(uid, &uidLen)) { sendReply("RA,NOCARD"); return; }
    if (pnMifareAuth(block, kt, key, uid, uidLen)) {
      uint8_t data[16];
      if (pnMifareRead(block, data) == 16) {
        char hex[33];
        hexEncode(hex, data, 16);
        sendReply("RA,OK,%s", hex);
      } else {
        sendReply("RA,OK,NOREAD");
      }
    } else {
      sendReply("RA,FAIL");
    }
  }
  // --- Unknown ---
  else {
    sendReply("ERR,UNKNOWN");
  }
}

// ═══════════════════════════════════════════════════════════
//  BACKGROUND PUMPS
// ═══════════════════════════════════════════════════════════

void rfidPump() {
  if (!rfidLoop || !pn532Ok) return;
  if (millis() - rfidLastPoll < 200) return;
  rfidLastPoll = millis();
  uint8_t uid[10]; uint8_t uidLen = 0;
  if (pnScanCard(uid, &uidLen)) {
    ledFlashGreen();
    int pos = snprintf(espTxBuf, sizeof(espTxBuf), "!RF,%d,", uidLen);
    hexEncode(espTxBuf + pos, uid, uidLen);
    espSend(espTxBuf);
  }
}

void recordPump() {
  for (int r = 0; r < 2; r++) {
    if (!ccRecording[r]) continue;
    uint8_t csn = r ? CC_CS_B : CC_CS_A;
    ccRecordPoll(csn);
    if (ccRec.len >= REC_MAX) {
      ccIdle(csn);
      ccRecording[r] = false;
      ccRec.valid = true;
      espSendFmt("!REC_FULL,%d", ccRec.len);
    }
  }
}

void replayPump() {
  if (!rplState.active) return;
  uint8_t txb = ccReadStatus(rplState.csn, ST_TXBYTES) & 0x7F;
  if (txb > 48) return;
  int remain = ccRec.len - rplState.pos;
  int chunk = (remain < 32) ? remain : 32;
  if (chunk > 0) {
    ccWriteBurst(rplState.csn, REG_FIFO, &ccRec.data[rplState.pos], chunk);
    rplState.pos += chunk;
  }
  if (rplState.pos >= ccRec.len) {
    rplState.count++;
    if (rplState.count >= rplState.target) {
      ccIdle(rplState.csn);
      rplState.active = false;
      ledFlashGreen();
      espSendFmt("!RY_DONE,%d", rplState.count);
    } else {
      rplState.pos = 0;
    }
  }
}

void jamPump() {
  if (!jamState.active) return;
  float base = jamState.freqMHz;
  bool fired = false;
  for (int r = 0; r < 2; r++) {
    uint8_t csn = r ? CC_CS_B : CC_CS_A;
    uint8_t ms = ccReadStatus(csn, ST_MARCSTATE) & 0x1F;
    if (ms != MS_IDLE && ms != MS_TXUF) continue;
    if (ms == MS_TXUF) ccStrobe(csn, CMD_SFTX);
    uint8_t ch = ((uint16_t)jamState.hopCount * (r ? 13 : 7) + r * 17) & 0x1F;
    float f = base + (ch - 16) * 0.125f;
    ccSetFreq(csn, f);
    uint8_t noise[61];
    for (int i = 0; i < 61; i++) noise[i] = (uint8_t)random(256);
    ccWriteBurst(csn, REG_FIFO, noise, 61);
    ccStrobe(csn, CMD_STX);
    fired = true;
  }
  if (fired) jamState.hopCount++;
  ledFlashGreen();
}

void bfPump() {
  if (!bfState.active) return;
  if (millis() - bfState.lastTx < bfState.gapMs) return;
  uint8_t payload[4];
  uint32_t code = bfState.current;
  for (int i = bfState.codeLen - 1; i >= 0; i--) {
    payload[i] = code & 0xFF;
    code >>= 8;
  }
  ccTxPacket(bfState.csn, payload, bfState.codeLen);
  bfState.lastTx = millis();
  bfState.current++;
  if (bfState.current >= bfState.total) {
    bfState.active = false;
    ccIdle(bfState.csn);
    ledFlashGreen();
    espSend("!BF_DONE");
  }
}

void cwPump() {
  if (!cwState.active) return;
  uint8_t txb = ccReadStatus(cwState.csn, ST_TXBYTES) & 0x7F;
  if (txb < 32) {
    uint8_t fill[32]; memset(fill, 0xFF, 32);
    ccWriteBurst(cwState.csn, REG_FIFO, fill, 32);
  }
}

void txPump() {
  if (!txPumpSt.active) return;
  if (millis() - txPumpSt.lastTx < (uint32_t)txPumpSt.gap) return;
  if (ccTxPacket(txPumpSt.csn, txPumpSt.payload, txPumpSt.pLen)) txPumpSt.sent++;
  ledFlashGreen();
  txPumpSt.idx++;
  txPumpSt.lastTx = millis();
  if (txPumpSt.idx >= txPumpSt.count) {
    txPumpSt.active = false;
    sendReply("CT,%d", txPumpSt.sent);
  }
}

void rfidCrackPump() {
  if (!crack.active) return;
  if (millis() - crack.lastTry < 50) return;
  crack.lastTry = millis();

  int nextKey = crack.keyIdx + 1;
  if (crack.keyIdx == -1 && crack.priorityKey >= 0) {
    nextKey = crack.priorityKey;
    crack.keyIdx = -2;
  } else if (crack.keyIdx == -2) {
    nextKey = 0;
  }

  if (nextKey >= crackKeyCount) {
    crack.keyType++;
    if (crack.keyType > 1) {
      crack.keyType = 0;
      crack.sector++;
      if (crack.sector >= crack.maxSector) {
        crack.active = false;
        ledFlashGreen();
        espSendFmt("!RC_DONE,%d", crack.found);
        return;
      }
    }
    crack.keyIdx = -1;
    return;
  }

  if (nextKey == crack.priorityKey && crack.keyIdx >= 0) {
    crack.keyIdx = nextKey;
    return;
  }

  if (crack.sectorDone[crack.sector][crack.keyType]) {
    crack.keyIdx = crackKeyCount;
    return;
  }

  crack.keyIdx = nextKey;
  crack.attempts++;

  pnResetField();

  uint8_t uid[10]; uint8_t uidLen = 0;
  if (!pnScanCard(uid, &uidLen)) return;

  uint8_t block;
  if (crack.sector < 32) block = crack.sector * 4;
  else block = 128 + (crack.sector - 32) * 16;
  uint8_t kt = crack.keyType ? 0x61 : 0x60;

  if (pnMifareAuth(block, kt, crackKeys[nextKey], uid, uidLen)) {
    crack.sectorDone[crack.sector][crack.keyType] = true;
    crack.found++;
    if (crack.priorityKey < 0) crack.priorityKey = nextKey;
    ledFlashGreen();

    uint8_t data[16];
    char dHex[33] = "";
    if (pnMifareRead(block, data) == 16) {
      hexEncode(dHex, data, 16);
    }

    char kHex[13];
    hexEncode(kHex, crackKeys[nextKey], 6);

    espSendFmt("!RK,%d,%c,%s,%s",
      crack.sector, crack.keyType ? 'B' : 'A', kHex, dHex);

    crack.keyIdx = crackKeyCount;
  }
}

// ═══════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(F("\n=== KODI NODE 0 — Radio Brain ==="));

  espNowSetup();

  pinMode(PN_SCK, OUTPUT); pinMode(PN_MOSI, OUTPUT);
  pinMode(PN_MISO, INPUT); pinMode(PN_SS, OUTPUT);
  digitalWrite(PN_SS, HIGH); digitalWrite(PN_SCK, LOW);

  pinMode(CC_CS_A, OUTPUT); pinMode(CC_CS_B, OUTPUT);
  digitalWrite(CC_CS_A, HIGH); digitalWrite(CC_CS_B, HIGH);

  pinMode(PIN_RED, OUTPUT); pinMode(PIN_GREEN, OUTPUT);
  digitalWrite(PIN_RED, LOW); digitalWrite(PIN_GREEN, LOW);

  ccSpi.begin(CC_SCK, CC_MISO, CC_MOSI, -1);

  // Check PN532
  Serial.print(F("PN532:    "));
  digitalWrite(PN_SS, LOW); delay(5); digitalWrite(PN_SS, HIGH); delay(10);
  uint8_t fwCmd[] = {0x02};
  uint8_t resp[12]; int rLen = 0;
  if (pnCommand(fwCmd, 1, resp, sizeof(resp), &rLen) &&
      rLen >= 6 && resp[0] == 0xD5 && resp[1] == 0x03) {
    Serial.printf("OK — PN5%02X FW %d.%d\n", resp[2], resp[3], resp[4]);
    pn532Ok = true;
    uint8_t samCmd[] = {0x14, 0x01, 0x00, 0x01};
    pnCommand(samCmd, 4, resp, sizeof(resp), &rLen);
  } else {
    Serial.println(F("NO RESPONSE"));
  }

  // Check CC1101-A
  Serial.print(F("CC1101-A: "));
  uint8_t verA = ccGetVersion(CC_CS_A);
  if (verA == 0x14) { Serial.println(F("OK")); cc1101aOk = true; }
  else { Serial.printf("FAIL (0x%02X)\n", verA); }

  // Check CC1101-B
  Serial.print(F("CC1101-B: "));
  uint8_t verB = ccGetVersion(CC_CS_B);
  if (verB == 0x14) { Serial.println(F("OK")); cc1101bOk = true; }
  else { Serial.printf("FAIL (0x%02X)\n", verB); }

  // Power down detected radios until configured
  if (cc1101aOk) { ccIdle(CC_CS_A); ccStrobe(CC_CS_A, CMD_SPWD); }
  if (cc1101bOk) { ccIdle(CC_CS_B); ccStrobe(CC_CS_B, CMD_SPWD); }

  int total = pn532Ok + cc1101aOk + cc1101bOk;
  Serial.printf("\n%d/3 modules alive. Listening via ESP-NOW.\n\n", total);
}

// ═══════════════════════════════════════════════════════════
//  LED STATE
// ═══════════════════════════════════════════════════════════

void ledUpdate() {
  static uint32_t lastLedCheck = 0;
  static bool lastRed = false;
  uint32_t now = millis();
  if (now - lastLedCheck < 100) return;
  lastLedCheck = now;

  bool redOn = false;
  if (jamState.active || bfState.active || cwState.active)
    redOn = true;
  else if (ccRecording[0] || ccRecording[1] || rfidLoop || crack.active)
    redOn = (now / 500) & 1;

  if (redOn != lastRed) {
    digitalWrite(PIN_RED, redOn ? HIGH : LOW);
    lastRed = redOn;
  }
}

// ═══════════════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════════════

void loop() {
  while (espMsgAvail()) {
    const char* msg = espMsgRead();
    strncpy(cmdBuf, msg, sizeof(cmdBuf) - 1);
    cmdBuf[sizeof(cmdBuf) - 1] = '\0';
    processCommand(cmdBuf);
    // Feed TX FIFOs between commands so queued messages don't cause underflow
    replayPump();
    cwPump();
  }

  // Priority 1: TX FIFO critical — 64-byte FIFO drains in ~5ms at high rates
  replayPump();
  cwPump();

  // Priority 2: TX active — need frequent refills but self-timed
  jamPump();
  bfPump();
  txPump();

  // Priority 3: RX — can buffer, no FIFO drain risk
  recordPump();

  // Priority 4: RFID (I2C) — separate operations from CC1101, never concurrent
  rfidPump();
  rfidCrackPump();

  // LED update
  ledUpdate();
  if (greenOffAt && millis() >= greenOffAt) {
    digitalWrite(PIN_GREEN, LOW);
    greenOffAt = 0;
  }

  yield();
}
