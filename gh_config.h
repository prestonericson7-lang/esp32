#pragma once
// ############################################################################
//  gh_config.h  -  KODI NRF24  /  Hub node pin map + shared types
//
//  Board:  ESP32-WROOM-32U (CLASSIC ESP32, external antenna jack, 38-pin,
//          max GPIO 39, NO PSRAM).
//  Role:   HUB NODE - user interface controller.
//          Drives a 4x4 matrix keypad, two I2C OLEDs (status + UI), and a
//          speaker.  Has NO nRF24 radios - those live on Node 2.  The hub
//          talks to Node 2 over a serial link to command radio operations.
//
//  Display:
//    Status OLED  - SSD1306 128x64 I2C  (bus 0, SDA 21 / SCL 22)
//    UI OLED      - SSD1306 128x64 I2C  (bus 1, SDA 16 / SCL 17)
//
//  Input:  4x4 matrix keypad (rows 13/12/14/27, cols 26/25/33/32).
//  Audio:  mini speaker on GPIO 19, LEDC PWM channel 0.
//
//  ---- CLASSIC ESP32 PIN RULES ----------------------------------------------
//   * GPIO 6..11  -> on-board SPI FLASH. NEVER use them (board won't boot).
//   * GPIO 34..39 -> INPUT ONLY, no internal pull-up.
//   * Strapping pins 0,2,5,15: left FREE on the hub.
//   * GPIO 12 (MTDI): used for keypad R1 — drive LOW or burn eFuse
//     to lock VDD_SDIO at 3.3V so boot is safe.
//   * GPIO 1/3 = USB-serial console - left FREE.
// ############################################################################

#include <Arduino.h>

#define GG_VERSION "KODI NRF24 Edition"

// ===== Feature flags (set to 1 to enable, 0 to disable) ====================
#define GG_ENABLE_BLE_SPAM  1   // BLE multi-protocol spam (Apple/FastPair/Samsung/Swift/Names)
#define GG_ENABLE_PORTAL    1   // Evil Twin captive portal + credential harvest
#define GG_ENABLE_CRACK     1   // WPA2 handshake capture + dictionary crack
#define GG_ENABLE_DETECT    1   // Cop/surveillance scan + BLE tracker scan

// ===== Status OLED (SSD1306 128x64 I2C bus 0) ==============================
#define GG_PIN_SDA0       21
#define GG_PIN_SCL0       22
#define GG_OLED0_ADDR     0x3C       // auto-fallback to 0x3D at boot
#define GG_I2C0_HZ        400000UL

// ===== UI OLED (SSD1306 128x64 I2C bus 1) ==================================
#define GG_PIN_SDA1       16
#define GG_PIN_SCL1       17
#define GG_OLED1_ADDR     0x3C       // auto-fallback to 0x3D at boot
#define GG_I2C1_HZ        400000UL

// ===== 4x4 Matrix Keypad ===================================================
// Rows are outputs (active-low scan), columns are inputs (INPUT_PULLUP).
#define GG_PIN_KP_R0      13
#define GG_PIN_KP_R1      12
#define GG_PIN_KP_R2      14
#define GG_PIN_KP_R3      27

#define GG_PIN_KP_C0      26
#define GG_PIN_KP_C1      25
#define GG_PIN_KP_C2      33
#define GG_PIN_KP_C3      32

// ===== Speaker (LEDC PWM) ==================================================
// Small PC-style beeper on GPIO 19. Driven direct from the pin.
#define GG_PIN_SPK        19
#define GG_SPK_CH         0          // LEDC channel (used on ESP32 core 2.x)

// ===== Input feel ===========================================================
#define GG_DEBOUNCE_MS    25
#define GG_LONGPRESS_MS   550
#define GG_REPEAT_MS      150
#define GG_NAV_DELAY_MS   380        // held key: initial delay before auto-repeat

// ===== Radio limits =========================================================
#define GG_NRF_CH_MIN     0
#define GG_NRF_CH_MAX     125
#define GG_ADDR_WIDTH     5
#define GG_PAYLOAD_MAX    32
#define GG_PROFILE_SLOTS  8
#define GG_PAYLIB_SLOTS   8          // saved-payload library ("My payloads")

// ===== nRF dual-radio behaviour =============================================
enum GgNrfMode : uint8_t { GG_NM_PAIR = 0, GG_NM_DUALTX, GG_NM_HOTSWAP };
static const char* const GG_NM_NAME[] = { "PAIR", "DUAL-TX", "HOTSWAP" };

enum GgRate  : uint8_t { GG_DR_250K = 0, GG_DR_1M, GG_DR_2M };
static const char* const GG_RATE_NAME[] = { "250K", "1M", "2M" };
enum GgPower : uint8_t { GG_PWR_MIN = 0, GG_PWR_LOW, GG_PWR_HIGH, GG_PWR_MAX };
static const char* const GG_PWR_NAME[] = { "MIN", "LOW", "HIGH", "MAX" };

enum GgPayloadKind : uint8_t { GG_PL_HEX = 0, GG_PL_TEXT, GG_PL_COUNTER, GG_PL_RANDOM, GG_PL_ONES, GG_PL_ZEROS, GG_PL_ALT };
static const char* const GG_PL_NAME[] = { "HEX", "TEXT", "COUNT", "RAND", "0xFF", "0x00", "0xAA" };
#define GG_PL_COUNT 7

// CRC length (matches RF24: off / 8-bit / 16-bit)
static const char* const GG_CRC_NAME[] = { "OFF", "8bit", "16bit" };

// ===== saveable transmit config =============================================
struct GgNrfProfile {
  char     name[13];
  uint8_t  channel;
  uint8_t  addr[GG_ADDR_WIDTH];
  uint8_t  rate, power, plKind, plLen;
  uint8_t  payload[GG_PAYLOAD_MAX];
  uint16_t repeatMs;
  uint8_t  autoAck;
  // advanced radio settings (to match other nRF24 gear exactly)
  uint8_t  addrWidth;   // 3..5 bytes
  uint8_t  crc;         // 0=off, 1=8-bit, 2=16-bit
  uint8_t  retries;     // auto-retransmit count 0..15
  // burst control
  uint16_t burstN;      // packets per SEND (0/1 = single shot)
  uint16_t burstGapMs;  // gap between burst packets, ms
};

// One saved entry in the "My payloads" library: the bytes AND how to send them
// (speed, power, channel, amount, gap). Loading one reproduces the exact send you
// configured, so "Load & send" fires that whole config from a saved payload.
struct GgPayload {
  uint8_t  kind;                  // GG_PL_HEX or GG_PL_TEXT (saved bytes are literal)
  uint8_t  len;                   // 1..GG_PAYLOAD_MAX
  uint8_t  bytes[GG_PAYLOAD_MAX];
  uint8_t  rate;                  // GgRate  (speed)
  uint8_t  power;                 // GgPower
  uint8_t  channel;               // 0..125
  uint16_t amount;                // packets per SEND (burstN)
  uint16_t gapMs;                 // ms between packets (burstGapMs)
};

struct GgSettings {
  uint8_t  volume;                // 0..10
  uint8_t  nrfMode;               // GgNrfMode
};

// ===== Wi-Fi TX settings ===================================================
enum GgWifiPwr : uint8_t { GG_WP_LOW=0, GG_WP_MED, GG_WP_HIGH, GG_WP_MAX };
static const char* const GG_WP_NAME[] = { "LOW", "MED", "HIGH", "MAX" };
enum GgWifiFrameSz : uint8_t { GG_WF_128=0, GG_WF_256 };
static const char* const GG_WF_NAME[] = { "128B", "256B" };

struct GgWifiTxCfg {
  uint8_t  channel;        // 1-14
  uint8_t  power;          // GgWifiPwr
  uint16_t pps;            // packets per second 10-1000
  uint16_t durationSec;    // 0 = continuous
  uint8_t  frameSize;      // GgWifiFrameSz (bigger = more airtime)
  uint8_t  payloadLen;     // 1-32
  uint8_t  payload[GG_PAYLOAD_MAX];
};

// ===== BLE TX settings =====================================================
enum GgBlePwr : uint8_t { GG_BP_LOW=0, GG_BP_MED, GG_BP_HIGH, GG_BP_MAX };
static const char* const GG_BP_NAME[] = { "LOW", "MED", "HIGH", "MAX" };
enum GgBleCh : uint8_t { GG_BC_ALL=0, GG_BC_37, GG_BC_38, GG_BC_39 };
static const char* const GG_BC_NAME[] = { "ALL", "37", "38", "39" };

struct GgBleTxCfg {
  uint16_t pps;            // packets per second 1-500
  uint16_t durationSec;    // 0 = continuous
  uint8_t  payloadLen;     // 1-26
  uint8_t  payload[31];
  uint8_t  power;          // GgBlePwr
  uint8_t  channels;       // GgBleCh
};

static inline GgNrfProfile ggDefaultNrf() {
  GgNrfProfile p{};
  strncpy(p.name, "NRF-A", sizeof(p.name));
  p.channel = 76;
  p.addr[0]='K'; p.addr[1]='O'; p.addr[2]='D'; p.addr[3]='I'; p.addr[4]='N';
  p.rate = GG_DR_1M; p.power = GG_PWR_LOW;
  p.plKind = GG_PL_TEXT;
  const char* h = "KODINRF"; p.plLen = strlen(h); memcpy(p.payload, h, p.plLen);
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
static inline GgWifiTxCfg ggDefaultWifiTx() {
  GgWifiTxCfg c{};
  c.channel = 1; c.power = GG_WP_MAX;
  c.pps = 200; c.durationSec = 0;
  c.frameSize = GG_WF_256;
  c.payloadLen = 7; memcpy(c.payload, "KODINRF", 7);
  return c;
}
static inline GgBleTxCfg ggDefaultBleTx() {
  GgBleTxCfg c{};
  c.pps = 100; c.durationSec = 0;
  c.payloadLen = 7; memcpy(c.payload, "KODINRF", 7);
  c.power = GG_BP_MAX; c.channels = GG_BC_ALL;
  return c;
}

// ===== Captured credentials (Evil Twin portal) ==============================
#define GG_CRED_MAX 8
struct GgCred {
  char user[33];
  char pass[33];
};
