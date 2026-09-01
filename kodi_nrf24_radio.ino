// ############################################################################
//  kodi_nrf24_radio.ino  —  KODI NRF24  /  Radio Module
//
//  ESP32-WROOM-32U + 2x nRF24L01+ on shared VSPI bus.
//  Receives commands from the Hub (Node 1) via ESP-NOW.
//  Sends status/results back to the Hub via ESP-NOW.
//
//  Board:  "ESP32 Dev Module"
//  Serial: 115200
//  Libs:   RF24 by TMRh20
//
//  PIN MAP (proven wiring):
//    SPI shared:  SCK=18  MISO=19  MOSI=22
//    nRF-A:       CE=17   CSN=5
//    nRF-B:       CE=4    CSN=16
// ############################################################################

#include <SPI.h>
#include <RF24.h>
#include <WiFi.h>
#include <esp_now.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ===== PIN MAP ==============================================================
#define PIN_SCK       18
#define PIN_MISO      19
#define PIN_MOSI      22
#define PIN_NRFA_CE   17
#define PIN_NRFA_CSN  5
#define PIN_NRFB_CE   4
#define PIN_NRFB_CSN  16

// ===== ESP-NOW PROTOCOL (must match KODI NRF24 hub) =========================
enum CmdType : uint8_t {
  CMD_PING = 0,
  CMD_CONFIG,
  CMD_TX_SINGLE,
  CMD_TX_BURST,
  CMD_BEACON_START,
  CMD_BEACON_STOP,
  CMD_CARRIER_START,
  CMD_CARRIER_STOP,
  CMD_SCAN_START,
  CMD_RX_START,
  CMD_RX_STOP,
  CMD_STOP_ALL,
  CMD_NRF_JAM,
  CMD_NRF_JAM_STOP,
  CMD_KPAD_JAM,
  CMD_KPAD_JAM_STOP,
};

enum RadioMode : uint8_t {
  MODE_IDLE = 0, MODE_TX, MODE_BEACON, MODE_CARRIER,
  MODE_SCAN, MODE_RX, MODE_JAM, MODE_KPAD,
};
static const char* const MODE_NAME[] = {
  "IDLE","TX","BEACON","CARRIER","SCAN","RX","JAM","KPAD"
};

#pragma pack(push, 1)
struct NodeCmd {
  uint8_t  type;
  uint8_t  radio;
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

struct NodeStatus {
  uint8_t  type;          // 0=heartbeat, 1=tx_result, 2=rx_data
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

// ===== GLOBALS ==============================================================
static RF24 nrfA(PIN_NRFA_CE, PIN_NRFA_CSN);
static RF24 nrfB(PIN_NRFB_CE, PIN_NRFB_CSN);
static bool nrfAOk = false, nrfBOk = false;

static uint32_t txCount = 0, ackOk = 0, txFail = 0, rxCount = 0;
static uint8_t  currentMode = MODE_IDLE;
static uint8_t  activeChannel = 76;

// Beacon state
static bool     beaconOn = false;
static uint32_t beaconLast = 0;
static uint16_t beaconMs = 100;
static uint8_t  beaconPayload[32];
static uint8_t  beaconLen = 0;
static uint8_t  beaconRadio = 0;

// Burst state
static uint16_t burstLeft = 0;
static uint32_t burstNext = 0;
static uint16_t burstGapMs = 20;
static uint8_t  burstPayload[32];
static uint8_t  burstLen = 0;
static uint8_t  burstRadio = 0;

// RX state
static bool rxOn = false;

// Carrier state
static bool carrierOn = false;

// Jam state
static bool jamOn = false;
static uint8_t jamChLo = 0, jamChHi = 125;
static uint8_t jamPosA = 0, jamPosB = 63;
static uint32_t jamLastMs = 0;

// Scan state (continuous sweep, results sent per channel)
static bool scanOn = false;
static uint8_t scanCh = 0;
static uint32_t scanLastMs = 0;

// ESP-NOW
static uint8_t hubMac[6] = {0};
static bool    hubKnown = false;
static volatile bool cmdPending = false;
static NodeCmd  pendingCmd;
static portMUX_TYPE cmdMux = portMUX_INITIALIZER_UNLOCKED;

// ===== NRF HELPERS ==========================================================
static RF24& nrf(uint8_t r) { return (r == 1) ? nrfB : nrfA; }
static bool  nrfOk(uint8_t r) { return (r == 1) ? nrfBOk : nrfAOk; }

static void configRadio(RF24& radio, NodeCmd& c) {
  radio.stopListening();
  radio.setChannel(c.channel);
  switch (c.rate) {
    case 0: radio.setDataRate(RF24_250KBPS); break;
    case 2: radio.setDataRate(RF24_2MBPS);   break;
    default: radio.setDataRate(RF24_1MBPS);  break;
  }
  switch (c.power) {
    case 0: radio.setPALevel(RF24_PA_MIN);  break;
    case 1: radio.setPALevel(RF24_PA_LOW);  break;
    case 2: radio.setPALevel(RF24_PA_HIGH); break;
    default: radio.setPALevel(RF24_PA_MAX); break;
  }
  radio.setAutoAck(c.autoAck);
  uint8_t aw = c.addrWidth;
  if (aw < 3) aw = 3;
  if (aw > 5) aw = 5;
  radio.setAddressWidth(aw);
  switch (c.crc) {
    case 0: radio.disableCRC(); break;
    case 1: radio.setCRCLength(RF24_CRC_8); break;
    default: radio.setCRCLength(RF24_CRC_16); break;
  }
  radio.setRetries(5, c.retries & 0x0F);
  radio.openWritingPipe(c.addr);
  radio.openReadingPipe(1, c.addr);
  activeChannel = c.channel;
}

static bool txPacket(RF24& radio, uint8_t* data, uint8_t len) {
  radio.stopListening();
  bool ok = radio.write(data, len);
  txCount++;
  if (ok) ackOk++; else txFail++;
  return ok;
}

static void stopAll() {
  beaconOn = false;
  burstLeft = 0;
  carrierOn = false;
  jamOn = false;
  scanOn = false;
  currentMode = MODE_IDLE;
  if (rxOn) {
    if (nrfAOk) nrfA.stopListening();
    if (nrfBOk) nrfB.stopListening();
    rxOn = false;
  }
  if (nrfAOk) { nrfA.stopListening(); nrfA.powerDown(); }
  if (nrfBOk) { nrfB.stopListening(); nrfB.powerDown(); }
}

// ===== ESP-NOW CALLBACKS ====================================================
#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  const uint8_t* mac = info->src_addr;
#else
static void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
#endif
  if (len < 1 || len > (int)sizeof(NodeCmd)) return;
  if (!hubKnown) {
    memcpy(hubMac, mac, 6);
    esp_now_peer_info_t pi = {};
    memcpy(pi.peer_addr, mac, 6);
    pi.channel = 0;
    pi.encrypt = false;
    if (!esp_now_is_peer_exist(mac)) esp_now_add_peer(&pi);
    hubKnown = true;
    Serial.printf("[espnow] hub discovered: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }
  portENTER_CRITICAL(&cmdMux);
  memcpy(&pendingCmd, data, min((size_t)len, sizeof(NodeCmd)));
  cmdPending = true;
  portEXIT_CRITICAL(&cmdMux);
}

static void sendStatus(uint8_t type) {
  if (!hubKnown) return;
  NodeStatus st = {};
  st.type = type;
  st.nrfAOk = nrfAOk;
  st.nrfBOk = nrfBOk;
  st.mode = currentMode;
  st.txCount = txCount;
  st.ackOk = ackOk;
  st.txFail = txFail;
  st.rxCount = rxCount;
  st.activeChannel = activeChannel;
  esp_now_send(hubMac, (uint8_t*)&st, sizeof(st));
}

static void sendRxData(uint8_t* data, uint8_t len, uint8_t pipe) {
  if (!hubKnown) return;
  NodeStatus st = {};
  st.type = 2; // rx_data
  st.nrfAOk = nrfAOk;
  st.nrfBOk = nrfBOk;
  st.mode = currentMode;
  st.txCount = txCount;
  st.ackOk = ackOk;
  st.txFail = txFail;
  st.rxCount = rxCount;
  st.rxLen = min(len, (uint8_t)32);
  memcpy(st.rxData, data, st.rxLen);
  st.rxPipe = pipe;
  st.activeChannel = activeChannel;
  esp_now_send(hubMac, (uint8_t*)&st, sizeof(st));
}

// ===== COMMAND HANDLER ======================================================
static void handleCmd(NodeCmd& c) {
  Serial.printf("[cmd] type=%u radio=%u ch=%u\n", c.type, c.radio, c.channel);

  switch (c.type) {
    case CMD_PING:
      sendStatus(0);
      break;

    case CMD_CONFIG: {
      if (c.radio != 1 && nrfAOk) configRadio(nrfA, c);
      if (c.radio != 0 && nrfBOk) configRadio(nrfB, c);
      sendStatus(0);
    } break;

    case CMD_TX_SINGLE: {
      stopAll();
      currentMode = MODE_TX;
      if (c.radio != 1 && nrfAOk) { configRadio(nrfA, c); txPacket(nrfA, c.payload, c.payloadLen); }
      if (c.radio != 0 && nrfBOk) { configRadio(nrfB, c); txPacket(nrfB, c.payload, c.payloadLen); }
      sendStatus(1);
      currentMode = MODE_IDLE;
    } break;

    case CMD_TX_BURST: {
      stopAll();
      currentMode = MODE_TX;
      if (c.radio != 1 && nrfAOk) configRadio(nrfA, c);
      if (c.radio != 0 && nrfBOk) configRadio(nrfB, c);
      burstLeft = (c.burstN > 0) ? c.burstN : 10;
      burstGapMs = (c.repeatMs > 0) ? c.repeatMs : 20;
      memcpy(burstPayload, c.payload, c.payloadLen);
      burstLen = c.payloadLen;
      burstRadio = c.radio;
      burstNext = millis();
    } break;

    case CMD_BEACON_START: {
      stopAll();
      currentMode = MODE_BEACON;
      if (c.radio != 1 && nrfAOk) configRadio(nrfA, c);
      if (c.radio != 0 && nrfBOk) configRadio(nrfB, c);
      memcpy(beaconPayload, c.payload, c.payloadLen);
      beaconLen = c.payloadLen;
      beaconMs = (c.repeatMs > 0) ? c.repeatMs : 100;
      beaconRadio = c.radio;
      beaconOn = true;
      beaconLast = 0;
    } break;

    case CMD_BEACON_STOP:
      beaconOn = false;
      currentMode = MODE_IDLE;
      sendStatus(0);
      break;

    case CMD_CARRIER_START: {
      stopAll();
      currentMode = MODE_CARRIER;
      if (c.radio != 1 && nrfAOk) {
        nrfA.stopListening();
        nrfA.setChannel(c.channel);
        nrfA.setPALevel(RF24_PA_MAX);
        nrfA.startConstCarrier(RF24_PA_MAX, c.channel);
      }
      if (c.radio != 0 && nrfBOk) {
        nrfB.stopListening();
        nrfB.setChannel(c.channel);
        nrfB.setPALevel(RF24_PA_MAX);
        nrfB.startConstCarrier(RF24_PA_MAX, c.channel);
      }
      carrierOn = true;
      activeChannel = c.channel;
      sendStatus(0);
    } break;

    case CMD_CARRIER_STOP:
      if (carrierOn) {
        if (nrfAOk) nrfA.stopConstCarrier();
        if (nrfBOk) nrfB.stopConstCarrier();
        carrierOn = false;
      }
      currentMode = MODE_IDLE;
      sendStatus(0);
      break;

    case CMD_SCAN_START: {
      stopAll();
      currentMode = MODE_SCAN;
      scanOn = true;
      scanCh = 0;
      scanLastMs = millis();
      if (nrfAOk) nrfA.powerUp();
      if (nrfBOk) nrfB.powerUp();
      sendStatus(0);
    } break;

    case CMD_RX_START: {
      stopAll();
      currentMode = MODE_RX;
      if (c.radio != 1 && nrfAOk) { configRadio(nrfA, c); nrfA.startListening(); }
      if (c.radio != 0 && nrfBOk) { configRadio(nrfB, c); nrfB.startListening(); }
      rxOn = true;
      sendStatus(0);
    } break;

    case CMD_RX_STOP:
      if (rxOn) {
        if (nrfAOk) nrfA.stopListening();
        if (nrfBOk) nrfB.stopListening();
        rxOn = false;
      }
      currentMode = MODE_IDLE;
      sendStatus(0);
      break;

    case CMD_STOP_ALL:
      stopAll();
      sendStatus(0);
      break;

    case CMD_NRF_JAM: {
      stopAll();
      currentMode = MODE_JAM;
      jamChLo = 0; jamChHi = 125;
      jamPosA = 0; jamPosB = 63;
      if (nrfAOk) {
        nrfA.powerUp(); nrfA.setAutoAck(false);
        nrfA.setDataRate(RF24_2MBPS);
        nrfA.setPALevel(c.power <= 3 ? (rf24_pa_dbm_e)c.power : RF24_PA_MAX);
        nrfA.setChannel(jamPosA);
        nrfA.stopListening();
      }
      if (c.radio != 0 && nrfBOk) {
        nrfB.powerUp(); nrfB.setAutoAck(false);
        nrfB.setDataRate(RF24_2MBPS);
        nrfB.setPALevel(c.power <= 3 ? (rf24_pa_dbm_e)c.power : RF24_PA_MAX);
        nrfB.setChannel(jamPosB);
        nrfB.stopListening();
      }
      jamOn = true;
      sendStatus(0);
    } break;

    case CMD_NRF_JAM_STOP:
      jamOn = false;
      if (nrfAOk) nrfA.powerDown();
      if (nrfBOk) nrfB.powerDown();
      currentMode = MODE_IDLE;
      sendStatus(0);
      break;

    case CMD_KPAD_JAM: {
      stopAll();
      currentMode = MODE_KPAD;
      jamChLo = 2; jamChHi = 83;
      jamPosA = 2; jamPosB = 42;
      if (nrfAOk) {
        nrfA.powerUp(); nrfA.setAutoAck(false);
        nrfA.setDataRate(RF24_2MBPS);
        nrfA.setPALevel(c.power <= 3 ? (rf24_pa_dbm_e)c.power : RF24_PA_MAX);
        nrfA.setChannel(jamPosA);
        nrfA.stopListening();
      }
      if (c.radio != 0 && nrfBOk) {
        nrfB.powerUp(); nrfB.setAutoAck(false);
        nrfB.setDataRate(RF24_2MBPS);
        nrfB.setPALevel(c.power <= 3 ? (rf24_pa_dbm_e)c.power : RF24_PA_MAX);
        nrfB.setChannel(jamPosB);
        nrfB.stopListening();
      }
      jamOn = true;
      sendStatus(0);
    } break;

    case CMD_KPAD_JAM_STOP:
      jamOn = false;
      if (nrfAOk) nrfA.powerDown();
      if (nrfBOk) nrfB.powerDown();
      currentMode = MODE_IDLE;
      sendStatus(0);
      break;
  }
}

// ===== SETUP ================================================================
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n==== KODI NRF24 Edition - RADIO ===="));

  // Init SPI + force both CSN HIGH before begin()
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, -1);
  pinMode(PIN_NRFA_CSN, OUTPUT); digitalWrite(PIN_NRFA_CSN, HIGH);
  pinMode(PIN_NRFB_CSN, OUTPUT); digitalWrite(PIN_NRFB_CSN, HIGH);

  // Init radios
  nrfAOk = nrfA.begin(&SPI);
  if (nrfAOk) nrfAOk = nrfA.isChipConnected();
  if (nrfAOk) { nrfA.setAutoAck(false); nrfA.stopListening(); nrfA.powerDown(); }

  nrfBOk = nrfB.begin(&SPI);
  if (nrfBOk) nrfBOk = nrfB.isChipConnected();
  if (nrfBOk) { nrfB.setAutoAck(false); nrfB.stopListening(); nrfB.powerDown(); }

  Serial.printf("[radio] nRF-A=%s  nRF-B=%s\n", nrfAOk ? "OK" : "--", nrfBOk ? "OK" : "--");
  if (!nrfAOk && !nrfBOk) Serial.println(F("[radio] neither nRF answered — check wiring + 10uF cap"));

  // Init ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  Serial.printf("[espnow] MAC: %s\n", WiFi.macAddress().c_str());

  if (esp_now_init() != ESP_OK) {
    Serial.println(F("[espnow] init FAILED"));
  } else {
    esp_now_register_recv_cb(onRecv);
    // Add broadcast peer so hub can discover us
    esp_now_peer_info_t bc = {};
    memset(bc.peer_addr, 0xFF, 6);
    bc.channel = 0;
    bc.encrypt = false;
    esp_now_add_peer(&bc);
    Serial.println(F("[espnow] ready, waiting for hub..."));
  }
}

// ===== LOOP =================================================================
void loop() {
  // Handle pending command
  if (cmdPending) {
    NodeCmd localCmd;
    portENTER_CRITICAL(&cmdMux);
    memcpy(&localCmd, &pendingCmd, sizeof(NodeCmd));
    cmdPending = false;
    portEXIT_CRITICAL(&cmdMux);
    handleCmd(localCmd);
  }

  // Beacon TX
  if (beaconOn && millis() - beaconLast >= beaconMs) {
    beaconLast = millis();
    if (beaconRadio != 1 && nrfAOk) txPacket(nrfA, beaconPayload, beaconLen);
    if (beaconRadio != 0 && nrfBOk) txPacket(nrfB, beaconPayload, beaconLen);
  }

  // Burst TX
  if (burstLeft > 0 && millis() >= burstNext) {
    burstNext = millis() + burstGapMs;
    if (burstRadio != 1 && nrfAOk) txPacket(nrfA, burstPayload, burstLen);
    if (burstRadio != 0 && nrfBOk) txPacket(nrfB, burstPayload, burstLen);
    burstLeft--;
    if (burstLeft == 0) {
      currentMode = MODE_IDLE;
      sendStatus(1);
    }
  }

  // RX poll
  if (rxOn) {
    uint8_t pipe;
    if (nrfAOk && nrfA.available(&pipe)) {
      uint8_t buf[32]; uint8_t len = nrfA.getPayloadSize();
      nrfA.read(buf, len);
      rxCount++;
      sendRxData(buf, len, pipe);
    }
    if (nrfBOk && nrfB.available(&pipe)) {
      uint8_t buf[32]; uint8_t len = nrfB.getPayloadSize();
      nrfB.read(buf, len);
      rxCount++;
      sendRxData(buf, len, pipe);
    }
  }

  // Jam pump: blast random noise, hop channels
  if (jamOn && millis() - jamLastMs >= 1) {
    jamLastMs = millis();
    uint8_t noise[32];
    for (int i = 0; i < 32; i++) noise[i] = random(256);
    if (nrfAOk) {
      nrfA.writeFast(noise, 32);
      nrfA.txStandBy();
      jamPosA++;
      if (jamPosA > jamChHi) jamPosA = jamChLo;
      nrfA.setChannel(jamPosA);
    }
    if (nrfBOk) {
      nrfB.writeFast(noise, 32);
      nrfB.txStandBy();
      jamPosB++;
      if (jamPosB > jamChHi) jamPosB = jamChLo;
      nrfB.setChannel(jamPosB);
    }
  }

  // Continuous scan: sweep one channel per tick, loop forever
  if (scanOn) {
    uint8_t hits = 0;
    if (nrfAOk) {
      nrfA.setChannel(scanCh);
      nrfA.startListening();
      delayMicroseconds(800);
      nrfA.stopListening();
      if (nrfA.testRPD()) hits++;
    }
    if (nrfBOk) {
      nrfB.setChannel(scanCh);
      nrfB.startListening();
      delayMicroseconds(800);
      nrfB.stopListening();
      if (nrfB.testRPD()) hits++;
    }
    NodeStatus st = {};
    st.type = 2;
    st.nrfAOk = nrfAOk; st.nrfBOk = nrfBOk;
    st.mode = MODE_SCAN;
    st.rxData[0] = scanCh;
    st.rxData[1] = hits;
    st.rxLen = 2;
    st.activeChannel = scanCh;
    if (hubKnown) esp_now_send(hubMac, (uint8_t*)&st, sizeof(st));
    scanCh++;
    if (scanCh > 125) scanCh = 0;
  }

  // Heartbeat to hub every 500ms
  static uint32_t lastHb = 0;
  if (millis() - lastHb >= 500) {
    lastHb = millis();
    sendStatus(0);
  }

  delay(1);
}
