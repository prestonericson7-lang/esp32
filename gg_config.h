#pragma once
// ############################################################################
//  gg_config.h  -  KODI RF CONTROLLER  /  pin map + shared types
//
//  Board:  Lonely Binary "Gold Edition" ESP32 - ESP32-WROOM-32U (CLASSIC ESP32,
//          external antenna jack, 38-pin, max GPIO 39, NO PSRAM).
//
//  Radios: FOUR CC1101 (sub-GHz) on shared HSPI bus (SCK13/MOSI14/MISO18)
//          ESP32 WiFi + BLE (built-in)
//          NO nRF24 on this tower — nRF is on the separate NRF tower,
//          reached via ESP-NOW.
//
//  Screens: TWO SSD1306 128x64 I2C OLEDs
//           OLED1: Wire  (SDA=21, SCL=22) at 0x3C
//           OLED2: Wire1 (SDA=16, SCL=17) at 0x3C
//  Input:   Buttons on ESP32-S3 co-processor, forwarded via ESP-NOW
//  Audio:   ONE speaker on LEDC PWM
//  LEDs:    THREE status LEDs (RED, GREEN, BLUE)
// ############################################################################

#include <Arduino.h>

#define GG_VERSION "KODI RF v2.0"

// ===== Shared HSPI bus — all 4 CC1101 radios ===============================
#define GG_PIN_CC_SCK   13
#define GG_PIN_CC_MOSI  14
#define GG_PIN_CC_MISO  18
#define GG_PIN_CC_GDO0  19          // not wired

// ===== CC1101 chip selects (active LOW) ====================================
#define GG_PIN_CCA_CSN   5         // Radio A
#define GG_PIN_CCB_CSN   4         // Radio B
#define GG_PIN_CCC_CSN  27         // Radio C
#define GG_PIN_CCD_CSN  25         // Radio D

// ===== DUAL OLEDs (SSD1306 128x64 I2C) ====================================
#define GG_PIN_SDA    21            // OLED1 on Wire
#define GG_PIN_SCL    22
#define GG_OLED_ADDR  0x3C

#define GG_PIN_SDA2   16            // OLED2 on Wire1
#define GG_PIN_SCL2   17
#define GG_OLED2_ADDR 0x3C

#define GG_I2C_HZ     400000UL

// ===== Buttons — NONE on main ESP ==========================================
// All input via ESP-NOW from S3 co-processor (gg_espnow.h).
// Defines kept for struct compatibility only — never read from GPIO.
#define GG_PIN_UP      34
#define GG_PIN_DOWN    35
#define GG_PIN_SELECT  36
#define GG_PIN_BACK    39

// ===== Speaker (single, LEDC PWM) ==========================================
#define GG_PIN_SPK     32
#define GG_SPK_CH      0           // LEDC channel (core 2.x)

// ===== THREE STATUS LEDs ===================================================
#define GG_PIN_LED_TX    33        // RED
#define GG_PIN_LED_RX    26        // GREEN
#define GG_PIN_LED_STAT  15        // BLUE

// ===== Input feel (used by ESP-NOW button events) ==========================
#define GG_DEBOUNCE_MS   25
#define GG_LONGPRESS_MS  550
#define GG_REPEAT_MS     150
#define GG_NAV_DELAY_MS  380

// ===== nRF Radio limits (stub — no nRF hardware) ===========================
#define GG_NRF_CH_MIN    0
#define GG_NRF_CH_MAX    125
#define GG_ADDR_WIDTH    5
#define GG_PAYLOAD_MAX   32
#define GG_PROFILE_SLOTS 8
#define GG_PAYLIB_SLOTS  8

// ===== nRF dual-radio behaviour (stub — menus still compile) ===============
enum GgNrfMode : uint8_t { GG_NM_PAIR = 0, GG_NM_DUALTX, GG_NM_HOTSWAP };
static const char* const GG_NM_NAME[] = { "PAIR", "DUAL-TX", "HOTSWAP" };

enum GgRate  : uint8_t { GG_DR_250K = 0, GG_DR_1M, GG_DR_2M };
static const char* const GG_RATE_NAME[] = { "250K", "1M", "2M" };
enum GgPower : uint8_t { GG_PWR_MIN = 0, GG_PWR_LOW, GG_PWR_HIGH, GG_PWR_MAX };
static const char* const GG_PWR_NAME[] = { "MIN", "LOW", "HIGH", "MAX" };

enum GgPayloadKind : uint8_t { GG_PL_HEX = 0, GG_PL_TEXT, GG_PL_COUNTER, GG_PL_RANDOM, GG_PL_ONES, GG_PL_ZEROS, GG_PL_ALT };
static const char* const GG_PL_NAME[] = { "HEX", "TEXT", "COUNT", "RAND", "0xFF", "0x00", "0xAA" };
#define GG_PL_COUNT 7

static const char* const GG_CRC_NAME[] = { "OFF", "8bit", "16bit" };

// ===== nRF saveable transmit config (stub — NVS compat) ====================
struct GgNrfProfile {
  char     name[13];
  uint8_t  channel;
  uint8_t  addr[GG_ADDR_WIDTH];
  uint8_t  rate, power, plKind, plLen;
  uint8_t  payload[GG_PAYLOAD_MAX];
  uint16_t repeatMs;
  uint8_t  autoAck;
  uint8_t  addrWidth;
  uint8_t  crc;
  uint8_t  retries;
  uint16_t burstN;
  uint16_t burstGapMs;
};

struct GgPayload {
  uint8_t  kind;
  uint8_t  len;
  uint8_t  bytes[GG_PAYLOAD_MAX];
  uint8_t  rate;
  uint8_t  power;
  uint8_t  channel;
  uint16_t amount;
  uint16_t gapMs;
};

struct GgSettings {
  uint8_t  volume;
  uint8_t  nrfMode;
};

// ===== CC1101 configuration ================================================
enum GgCcFreqBand : uint8_t { GG_CC_315=0, GG_CC_433, GG_CC_868, GG_CC_915 };
static const char* const GG_CC_FREQ_NAME[] = { "315MHz", "433MHz", "868MHz", "915MHz" };
static const float GG_CC_FREQ_VAL[] = { 315.0f, 433.92f, 868.0f, 915.0f };

enum GgCcMod : uint8_t { GG_CM_ASK=0, GG_CM_2FSK, GG_CM_GFSK, GG_CM_MSK };
static const char* const GG_CC_MOD_NAME[] = { "ASK/OOK", "2-FSK", "GFSK", "MSK" };

enum GgCcRate : uint8_t { GG_CR_1K=0, GG_CR_2K, GG_CR_10K, GG_CR_38K, GG_CR_100K, GG_CR_250K };
static const char* const GG_CC_RATE_NAME[] = { "1.2k", "2.4k", "10k", "38.4k", "100k", "250k" };

enum GgCcPower : uint8_t { GG_CP_M30=0, GG_CP_M20, GG_CP_M15, GG_CP_M10, GG_CP_0, GG_CP_5, GG_CP_7, GG_CP_10 };
static const char* const GG_CC_PWR_NAME[] = { "-30dBm", "-20dBm", "-15dBm", "-10dBm", "0dBm", "+5dBm", "+7dBm", "+10dBm" };

#define GG_CC_PAYLOAD_MAX 61

struct GgCcProfile {
  char     name[13];
  uint8_t  freqBand;
  float    freqMHz;
  uint8_t  modulation;
  uint8_t  dataRate;
  uint8_t  power;
  uint8_t  plLen;
  uint8_t  payload[GG_CC_PAYLOAD_MAX];
  uint16_t burstN;
  uint16_t burstGapMs;
  uint8_t  syncWord[2];
  uint8_t  pktFormat;
};

#define GG_CC_REC_MAX 512
struct GgCcRecording {
  uint8_t  data[GG_CC_REC_MAX];
  uint16_t len;
  float    freqMHz;
  uint8_t  modulation;
  uint8_t  dataRate;
  bool     valid;
};

#define GG_CC_PROFILE_SLOTS 8

// ===== Wi-Fi TX settings ===================================================
enum GgWifiPwr : uint8_t { GG_WP_LOW=0, GG_WP_MED, GG_WP_HIGH, GG_WP_MAX };
static const char* const GG_WP_NAME[] = { "LOW", "MED", "HIGH", "MAX" };
enum GgWifiFrameSz : uint8_t { GG_WF_128=0, GG_WF_256 };
static const char* const GG_WF_NAME[] = { "128B", "256B" };

struct GgWifiTxCfg {
  uint8_t  channel;
  uint8_t  power;
  uint16_t pps;
  uint16_t durationSec;
  uint8_t  frameSize;
  uint8_t  payloadLen;
  uint8_t  payload[GG_PAYLOAD_MAX];
};

// ===== BLE TX settings =====================================================
enum GgBlePwr : uint8_t { GG_BP_LOW=0, GG_BP_MED, GG_BP_HIGH, GG_BP_MAX };
static const char* const GG_BP_NAME[] = { "LOW", "MED", "HIGH", "MAX" };
enum GgBleCh : uint8_t { GG_BC_ALL=0, GG_BC_37, GG_BC_38, GG_BC_39 };
static const char* const GG_BC_NAME[] = { "ALL", "37", "38", "39" };

struct GgBleTxCfg {
  uint16_t pps;
  uint16_t durationSec;
  uint8_t  payloadLen;
  uint8_t  payload[31];
  uint8_t  power;
  uint8_t  channels;
};

static inline GgNrfProfile ggDefaultNrf() {
  GgNrfProfile p{};
  strncpy(p.name, "NRF-A", sizeof(p.name));
  p.channel = 76;
  p.addr[0]='K'; p.addr[1]='O'; p.addr[2]='D'; p.addr[3]='I'; p.addr[4]='X';
  p.rate = GG_DR_1M; p.power = GG_PWR_LOW;
  p.plKind = GG_PL_TEXT;
  const char* h = "KODI-RF"; p.plLen = strlen(h); memcpy(p.payload, h, p.plLen);
  p.repeatMs = 0; p.autoAck = 1;
  p.addrWidth = 5; p.crc = 2; p.retries = 15;
  p.burstN = 10; p.burstGapMs = 20;
  return p;
}
static inline GgSettings ggDefaultSettings() {
  GgSettings s{};
  s.volume = 6; s.nrfMode = GG_NM_PAIR;
  return s;
}
static inline GgCcProfile ggDefaultCc() {
  GgCcProfile p{};
  strncpy(p.name, "CC1101-A", sizeof(p.name));
  p.freqBand = GG_CC_433; p.freqMHz = 433.92f;
  p.modulation = GG_CM_ASK; p.dataRate = GG_CR_2K;
  p.power = GG_CP_0;
  const char* d = "KODI"; p.plLen = strlen(d); memcpy(p.payload, d, p.plLen);
  p.burstN = 10; p.burstGapMs = 50;
  p.syncWord[0] = 0xD3; p.syncWord[1] = 0x91;
  p.pktFormat = 0;
  return p;
}
static inline GgWifiTxCfg ggDefaultWifiTx() {
  GgWifiTxCfg c{};
  c.channel = 1; c.power = GG_WP_MAX;
  c.pps = 200; c.durationSec = 0;
  c.frameSize = GG_WF_256;
  c.payloadLen = 7; memcpy(c.payload, "KODI_RF", 7);
  return c;
}
static inline GgBleTxCfg ggDefaultBleTx() {
  GgBleTxCfg c{};
  c.pps = 100; c.durationSec = 0;
  c.payloadLen = 7; memcpy(c.payload, "KODI_RF", 7);
  c.power = GG_BP_MAX; c.channels = GG_BC_ALL;
  return c;
}

// ===== Captured credentials (Evil Twin portal) ==============================
#define GG_CRED_MAX 8
struct GgCred {
  char user[33];
  char pass[33];
};
