#pragma once
// ############################################################################
//  gs_config.h  —  KODI S3 NEURAL CO-PROCESSOR  /  master configuration
//
//  Board:  ESP32-S3-DevKitC-1 (onboard PCB antenna, NO U.FL jack)
//          Dual-core Xtensa LX7 @ 240 MHz, 320 KB SRAM, single-precision FPU
//          Onboard WS2812 NeoPixel on GPIO 48
//          Paired with ESP32-WROOM-32U main controller via ESP-NOW
//
//  This ESP has NO CC1101 radios, NO OLEDs, NO speaker.
//  All RF/display/audio lives on the main ESP. This is a pure neural
//  co-processor that observes, learns, and advises.
//
//  ---- NEURAL ENGINE LAYER ARCHITECTURE -----
//    L0  Telemetry Ingest    — multi-resolution ring buffers, per-metric history
//    L1  Signal Processing   — smoothing, derivatives, spectral, cross-correlation
//    L2  Feature Extraction  — raw, derived, temporal, statistical, contextual
//    L3  Pattern Recognition — sequences, state machines, periodicity, signatures
//    L4  Anomaly Detection   — Welford, isolation scoring, LOF, adaptive threshold
//    L5  Rule Engine         — 100+ weighted rules across 10 categories
//    L6  Behavior Learner    — user profiling, Markov chains, effectiveness tracking
//    L7  Risk Assessment     — thermal physics, detection model, HW stress, trajectory
//    L8  Decision Engine     — recommendations, auto-proposals, explanation generator
//    L9  Self-Monitor        — perf tracking, memory watch, rule effectiveness, diag
//
//  ---- ESP32-S3 PIN RULES ---
//   GPIO 0        -> BOOT strapping. Avoid.
//   GPIO 19, 20   -> USB D-/D+ (USB-CDC serial). Avoid.
//   GPIO 26..37   -> SPI flash/PSRAM (octal). NEVER use on S3.
//   GPIO 43, 44   -> UART0 TX/RX. Avoid.
//   GPIO 45, 46   -> Strapping pins. Avoid.
//   GPIO 4..18, 21, 38..42, 47 -> Proven safe general purpose.
//   GPIO 48       -> Onboard NeoPixel (already used).
//   ALL S3 GPIOs support INPUT_PULLUP.
// ############################################################################

#include <Arduino.h>

// ===== IDENTITY ==============================================================
#define GS_VERSION        "KODI S3-NN v2.0"
#define GS_BOARD_TYPE     1       // 0 = main ESP32U, 1 = S3 neural co-processor
#define GS_BUILD_DATE     __DATE__
#define GS_BUILD_TIME     __TIME__

// ===== GPIO — FIVE BUTTONS ===================================================
// GPIO 38-42: general-purpose, no strapping/USB/flash conflicts.
// Each button shorts pin to GND. Internal pull-up enabled.
#define GS_PIN_UP         38
#define GS_PIN_DOWN       39
#define GS_PIN_SELECT     40
#define GS_PIN_BACK       41
#define GS_PIN_SWAP       42

// ===== GPIO — Onboard RGB NeoPixel (WS2812) ==================================
#define GS_PIN_NEOPIXEL   48
#define GS_NUM_PIXELS     1

// ===== GPIO — LAFVIN Audio Codec Module (I2S + I2C) ==========================
// I2S bus for digital audio (speaker out + dual mic in)
#define GS_PIN_I2S_BCLK   4
#define GS_PIN_I2S_WS     5
#define GS_PIN_I2S_DIN    6     // S3 → codec (speaker data) — data INTO codec
#define GS_PIN_I2S_DOUT   7     // codec → S3 (mic data) — data OUT of codec
#define GS_PIN_I2S_MCLK   8     // master clock
// I2C control bus for codec registers
#define GS_PIN_CODEC_SCL  14
#define GS_PIN_CODEC_SDA  15
// PA enable: HIGH = speaker amplifier on
#define GS_PIN_PA_EN      16

// ===== INPUT FEEL ============================================================
#define GS_DEBOUNCE_MS    25
#define GS_LONGPRESS_MS   550
#define GS_REPEAT_MS      150
#define GS_NAV_DELAY_MS   380

// #############################################################################
//  NEURAL ENGINE SIZING — every buffer, window, and limit in the brain
// #############################################################################

// L0 — Telemetry Ingest
#define GS_TELEM_RING_SIZE    64     // main telemetry ring buffer depth
#define GS_METRIC_HIST_SIZE   128    // per-metric rolling sample history
#define GS_METRIC_COUNT       12     // tracked metrics (see GsMetricId)
#define GS_TELEM_MAX_AGE_MS   30000  // discard telemetry older than 30s

// L1 — Signal Processing
#define GS_SIG_EMA_ALPHA_FAST 0.3f
#define GS_SIG_EMA_ALPHA_SLOW 0.05f
#define GS_SIG_DERIV_WINDOW   8
#define GS_SIG_SPECTRAL_BINS  16
#define GS_SIG_XCORR_LAG_MAX  16
#define GS_SIG_FILTER_ORDER   4

// L2 — Features
#define GS_FEAT_RAW_COUNT     16
#define GS_FEAT_DERIVED_COUNT 24
#define GS_FEAT_TEMPORAL_COUNT 12
#define GS_FEAT_STAT_COUNT    16
#define GS_FEAT_CTX_COUNT     8
#define GS_FEAT_TOTAL         76

// L3 — Patterns
#define GS_PAT_SEQ_MAX        64
#define GS_PAT_SIGNATURE_MAX  16
#define GS_PAT_STATE_MAX      16
#define GS_PAT_PERIOD_MAX     128
#define GS_PAT_CLUSTER_MAX    8

// L4 — Anomaly
#define GS_ANOMALY_SIGMA      2.5f
#define GS_ANOMALY_METRICS    8
#define GS_ANOMALY_ISO_TREES  8
#define GS_ANOMALY_LOF_K      5
#define GS_ANOMALY_HISTORY    64

// L5 — Rules
#define GS_RULE_COUNT         120
#define GS_RULE_CHAIN_MAX     4
#define GS_ALERT_QUEUE_SIZE   16
#define GS_COOLDOWN_MS        15000
#define GS_COOLDOWN_FAST_MS   5000
#define GS_COOLDOWN_SLOW_MS   60000

// L6 — Learning
#define GS_LEARN_SLOTS        32
#define GS_MARKOV_STATES      32
#define GS_LEARN_SESSION_MAX  16
#define GS_LEARN_EFF_SLOTS    16
#define GS_LEARN_EMA_ALPHA    0.1f

// L7 — Risk
#define GS_RISK_PREDICT_STEPS 6
#define GS_RISK_PREDICT_DT    30.0f
#define GS_THERMAL_AMBIENT_C  25.0f
#define GS_THERMAL_MAX_C      85.0f
#define GS_THERMAL_TAU_S      45.0f

// L8 — Decision
#define GS_DECISION_QUEUE     8
#define GS_DECISION_EXPLAIN   80
#define GS_DECISION_CONF_MIN  0.3f

// L9 — Self-monitor
#define GS_SELFMON_WINDOW     100
#define GS_SELFMON_HEAP_WARN  20000

// Tick rate
#define GS_NEURAL_TICK_MS     250    // 4 Hz

// Voice engine sizing
#define GS_VOICE_MODEL_BUF      1024   // audio frame buffer for recognition
#define GS_VOICE_RECOG_WINDOW   3000   // recognition window ms (~3 seconds)
#define GS_VOICE_CONF_THRESH    0.6f   // minimum confidence for command match

// #############################################################################
//  NEURAL ENGINE STATE MACHINE
// #############################################################################
#define GS_NN_BOOT        0
#define GS_NN_IDLE        1
#define GS_NN_MONITORING  2
#define GS_NN_ALERT       3
#define GS_NN_ERROR       4
#define GS_NN_LEARNING    5
#define GS_NN_ANALYZING   6
#define GS_NN_ADVISING    7
#define GS_NN_STATE_COUNT 8

static const char* const GS_NN_STATE_NAMES[GS_NN_STATE_COUNT] = {
  "BOOT", "IDLE", "MONITOR", "ALERT", "ERROR", "LEARN", "ANALYZE", "ADVISE"
};

// #############################################################################
//  ALERT LEVELS
// #############################################################################
#define GS_ALERT_DEBUG       0
#define GS_ALERT_INFO        1
#define GS_ALERT_WARNING     2
#define GS_ALERT_CRITICAL    3
#define GS_ALERT_EMERGENCY   4
#define GS_ALERT_LEVEL_COUNT 5

static const char* const GS_ALERT_LEVEL_NAMES[GS_ALERT_LEVEL_COUNT] = {
  "DEBUG", "INFO", "WARN", "CRIT", "EMERG"
};

// #############################################################################
//  RULE CATEGORIES — 10 deep analysis dimensions
// #############################################################################
#define GS_CAT_THERMAL        0
#define GS_CAT_TACTICAL       1
#define GS_CAT_SAFETY         2
#define GS_CAT_HEALTH         3
#define GS_CAT_BEHAVIORAL     4
#define GS_CAT_COUNTER_INTEL  5
#define GS_CAT_POWER          6
#define GS_CAT_ENVIRONMENT    7
#define GS_CAT_OPTIMIZATION   8
#define GS_CAT_COORDINATION   9
#define GS_CAT_COUNT          10

static const char* const GS_CAT_NAMES[GS_CAT_COUNT] = {
  "THERMAL", "TACTICAL", "SAFETY", "HEALTH", "BEHAVIOR",
  "COUNTER", "POWER", "ENV", "OPTIMIZE", "COORD"
};

static const float GS_CAT_WEIGHTS[GS_CAT_COUNT] = {
  1.0f, 0.7f, 1.0f, 0.9f, 0.4f,
  0.8f, 0.5f, 0.3f, 0.3f, 0.4f
};

// #############################################################################
//  ATTACK MODE IDs — complete Ghost TX capability map (44 modes)
// #############################################################################
#define GS_ATK_NONE           0

// CC1101 sub-GHz (1-8)
#define GS_ATK_CC_TX          1
#define GS_ATK_CC_RX          2
#define GS_ATK_CC_SCAN        3
#define GS_ATK_CC_RSSI        4
#define GS_ATK_CC_JAM         5
#define GS_ATK_CC_REPLAY      6
#define GS_ATK_CC_BRUTE       7
#define GS_ATK_CC_CW          8

// WiFi 2.4 GHz (9-24)
#define GS_ATK_WIFI_BEACON    9
#define GS_ATK_WIFI_DEAUTH    10
#define GS_ATK_WIFI_SPAM      11
#define GS_ATK_WIFI_RICK      12
#define GS_ATK_WIFI_AUTH      13
#define GS_ATK_WIFI_FUZZ      14
#define GS_ATK_WIFI_QUIET     15
#define GS_ATK_WIFI_CLONE     16
#define GS_ATK_WIFI_FARM      17
#define GS_ATK_WIFI_DDET      18
#define GS_ATK_WIFI_PROBE     19
#define GS_ATK_WIFI_EVIL      20
#define GS_ATK_WIFI_CRACK     21
#define GS_ATK_WIFI_GEO       22
#define GS_ATK_WIFI_HEAT      23
#define GS_ATK_WIFI_HIDDEN    24

// BLE 2.4 GHz (25-27)
#define GS_ATK_BLE_TX         25
#define GS_ATK_BLE_FLOOD      26
#define GS_ATK_BLE_SPAM       27

// Combo & advanced (28-39)
#define GS_ATK_FULL_JAM       28
#define GS_ATK_KPAD_BUST      29
#define GS_ATK_RAD_JAM        30
#define GS_ATK_DRONE_DROP     31
#define GS_ATK_SPK_JAM        32
#define GS_ATK_LOCK_BUZZ      33
#define GS_ATK_BLACKOUT       34
#define GS_ATK_CHAIN          35
#define GS_ATK_PROG_JAM       36
#define GS_ATK_AUTO_TGT       37
#define GS_ATK_CAM_JAM        38
#define GS_ATK_WIFI_BLE       39

// Detection & recon (40-43)
#define GS_ATK_COP_SCAN       40
#define GS_ATK_TRACK_SCAN     41
#define GS_ATK_NET_AUDIT      42
#define GS_ATK_SIG_METER      43

#define GS_ATK_COUNT          44

static const char* const GS_ATK_NAMES[GS_ATK_COUNT] = {
  "NONE",
  "CC:TX", "CC:RX", "CC:SCAN", "CC:RSSI", "CC:JAM",
  "CC:REPLAY", "CC:BRUTE", "CC:CW",
  "W:BEACON", "W:DEAUTH", "W:SPAM", "W:RICK", "W:AUTH",
  "W:FUZZ", "W:QUIET", "W:CLONE", "W:FARM", "W:DDET",
  "W:PROBE", "W:EVIL", "W:CRACK", "W:GEO", "W:HEAT", "W:HIDDEN",
  "B:TX", "B:FLOOD", "B:SPAM",
  "FULL_JAM", "KPAD_BUST", "RAD_JAM", "DRONE_DRP",
  "SPK_JAM", "LOCK_BUZZ", "BLACKOUT", "CHAIN",
  "PROG_JAM", "AUTO_TGT", "CAM_JAM", "W+B",
  "COP_SCAN", "TRACK_SCN", "NET_AUDIT", "SIG_METER"
};

// Attack classification flags
#define GS_ATKF_ACTIVE_TX    0x01
#define GS_ATKF_SUBGHZ       0x02
#define GS_ATKF_WIFI         0x04
#define GS_ATKF_BLE          0x08
#define GS_ATKF_MULTI_BAND   0x10
#define GS_ATKF_PASSIVE      0x20
#define GS_ATKF_HIGH_PPS     0x40
#define GS_ATKF_STEALTH      0x80

static const uint8_t GS_ATK_FLAGS[GS_ATK_COUNT] = {
  0x00,
  0x03, 0x22, 0x22, 0x22, 0x43, 0x03, 0x43, 0x03,
  0x45, 0x45, 0x45, 0x45, 0x45, 0x45, 0x85, 0x05,
  0x45, 0x24, 0x24, 0x05, 0x04, 0x24, 0x24, 0x24,
  0x09, 0x49, 0x49,
  0x5F, 0x53, 0x53, 0x53, 0x03, 0x03, 0x5F, 0x1F,
  0x53, 0x45, 0x45, 0x1D,
  0x22, 0x28, 0x24, 0x22
};

// Thermal heat generation coefficients per attack mode (normalized watts)
static const float GS_ATK_THERMAL[GS_ATK_COUNT] = {
  0.0f,
  0.3f, 0.1f, 0.15f, 0.05f, 1.2f, 0.4f, 0.8f, 0.5f,
  0.6f, 0.9f, 0.7f, 0.7f, 0.85f, 0.5f, 0.4f, 0.5f,
  0.8f, 0.1f, 0.1f, 0.7f, 0.3f, 0.1f, 0.1f, 0.15f,
  0.4f, 0.9f, 0.85f,
  1.8f, 1.3f, 1.1f, 1.0f, 0.6f, 0.5f, 2.0f, 1.5f,
  1.2f, 0.8f, 0.9f, 1.4f,
  0.05f, 0.05f, 0.1f, 0.05f
};

// Detection risk coefficients per attack mode (0.0–1.0)
static const float GS_ATK_DETECT[GS_ATK_COUNT] = {
  0.0f,
  0.3f, 0.05f, 0.1f, 0.02f, 0.8f, 0.4f, 0.6f, 0.5f,
  0.7f, 0.95f, 0.8f, 0.8f, 0.9f, 0.6f, 0.3f, 0.5f,
  0.85f, 0.1f, 0.1f, 0.7f, 0.2f, 0.1f, 0.1f, 0.15f,
  0.5f, 0.85f, 0.8f,
  1.0f, 0.7f, 0.6f, 0.5f, 0.4f, 0.35f, 1.0f, 0.9f,
  0.75f, 0.8f, 0.9f, 0.95f,
  0.02f, 0.02f, 0.05f, 0.02f
};

// Hardware stress coefficients per attack mode (0.0–1.0)
static const float GS_ATK_HW_STRESS[GS_ATK_COUNT] = {
  0.0f,
  0.2f, 0.05f, 0.1f, 0.03f, 0.7f, 0.3f, 0.5f, 0.4f,
  0.4f, 0.6f, 0.5f, 0.5f, 0.6f, 0.4f, 0.3f, 0.3f,
  0.6f, 0.05f, 0.05f, 0.5f, 0.8f, 0.05f, 0.05f, 0.1f,
  0.3f, 0.6f, 0.5f,
  0.9f, 0.7f, 0.6f, 0.5f, 0.3f, 0.4f, 1.0f, 0.8f,
  0.6f, 0.5f, 0.6f, 0.7f,
  0.02f, 0.02f, 0.05f, 0.02f
};

// #############################################################################
//  METRIC IDs — for per-metric signal processing and anomaly detection
// #############################################################################
enum GsMetricId : uint8_t {
  GS_MET_PKT_RATE     = 0,
  GS_MET_RSSI         = 1,
  GS_MET_FREQ         = 2,
  GS_MET_RADIO_COUNT  = 3,
  GS_MET_ATTACK_DUR   = 4,
  GS_MET_BAND_COUNT   = 5,
  GS_MET_THERMAL      = 6,
  GS_MET_DETECT       = 7,
  GS_MET_EFFECT       = 8,
  GS_MET_HEAP_FREE    = 9,
  GS_MET_LINK_QUALITY = 10,
  GS_MET_SWITCH_RATE  = 11,
};

static const char* const GS_MET_NAMES[GS_METRIC_COUNT] = {
  "pktRate", "rssi", "freq", "radios", "atkDur", "bands",
  "thermal", "detect", "effect", "heap", "linkQ", "switchR"
};

// #############################################################################
//  TELEMETRY SNAPSHOT — what the S3 knows about the main ESP at one instant
// #############################################################################
struct GsTelemetry {
  uint8_t  ccPresent;       // bitmask: bit0=A, bit1=B, bit2=C, bit3=D
  uint8_t  ccActive;        // bitmask: which radios actively TX
  uint8_t  wifiActive;      // 1 = WiFi TX attack running
  uint8_t  bleActive;       // 1 = BLE TX attack running
  uint8_t  ccJamming;       // 1 = CC1101 jam active
  float    ccFreqMHz;       // active frequency
  int8_t   ccRssi;          // last measured RSSI
  uint8_t  screen;          // current UI screen on main ESP
  uint8_t  radioCount;      // total radios detected
  uint8_t  attacking;       // 1 = any attack active
  uint8_t  attackMode;      // GS_ATK_* enum
  uint16_t pktCount;        // cumulative packet count
  uint32_t uptimeSec;       // main ESP estimated uptime
  uint32_t timestampMs;     // local millis() when snapshot was taken
  uint8_t  bandCount;       // how many bands active (0-3: subghz/wifi/ble)
  uint8_t  atkFlags;        // GS_ATKF_* flags for current mode
};

// #############################################################################
//  ALERT STRUCT — used by the alert queue
// #############################################################################
struct GsAlert {
  uint8_t  level;           // GS_ALERT_*
  uint8_t  ruleId;          // which rule generated this
  uint8_t  category;        // GS_CAT_*
  char     msg[21];         // display text (20 chars + null)
  uint32_t timestampMs;
  bool     sent;            // has this been transmitted to main ESP?
};

// #############################################################################
//  PATTERN SIGNATURE — a known operational sequence
// #############################################################################
struct GsPatternSig {
  uint8_t  modes[8];
  uint8_t  len;
  uint8_t  alertLevel;
  uint8_t  category;
  char     name[16];
  float    confidence;
};

// #############################################################################
//  RISK TRAJECTORY POINT
// #############################################################################
struct GsRiskPoint {
  float thermal;
  float detection;
  float hardware;
  float composite;
  float timeSec;
};

// #############################################################################
//  RECOMMENDATION — output from the decision engine
// #############################################################################
struct GsRecommendation {
  uint8_t  action;          // GS_REC_*
  uint8_t  priority;        // 0=low 1=med 2=high 3=urgent
  uint8_t  confidence;      // 0-100
  uint8_t  category;        // GS_CAT_*
  char     reason[40];
  uint32_t timestampMs;
  bool     acknowledged;
};

#define GS_REC_NONE           0
#define GS_REC_STOP_ATTACK    1
#define GS_REC_REDUCE_POWER   2
#define GS_REC_SWITCH_MODE    3
#define GS_REC_COOL_DOWN      4
#define GS_REC_CHECK_RADIO    5
#define GS_REC_CHANGE_FREQ    6
#define GS_REC_ADD_RADIO      7
#define GS_REC_COMBO_ATTACK   8
#define GS_REC_REDUCE_RATE    9
#define GS_REC_ROTATE_MODE    10
#define GS_REC_ABORT          11
#define GS_REC_INFO           12
#define GS_REC_COUNT          13

static const char* const GS_REC_NAMES[GS_REC_COUNT] = {
  "NONE", "STOP", "PWR_DN", "SWITCH", "COOL", "CHK_RADIO",
  "CHG_FREQ", "ADD_RADIO", "COMBO", "RATE_DN", "ROTATE", "ABORT", "INFO"
};

// #############################################################################
//  DISPLAY PAGE TYPES
// #############################################################################
#define GS_DISP_BOOT         0
#define GS_DISP_STATUS       1
#define GS_DISP_RISK         2
#define GS_DISP_ALERT        3
#define GS_DISP_TELEMETRY    4
#define GS_DISP_RECOMMEND    5
#define GS_DISP_LEARNER      6
#define GS_DISP_PATTERN      7
#define GS_DISP_PAGE_COUNT   8

// #############################################################################
//  AUDIO ALERT PATTERNS
// #############################################################################
struct GsAudioPattern {
  uint16_t freqHz;
  uint16_t onMs;
  uint16_t offMs;
  uint8_t  repeats;
};

static const GsAudioPattern GS_AUDIO_PATTERNS[GS_ALERT_LEVEL_COUNT] = {
  {  800,  50,   0, 1 },   // DEBUG
  { 1200, 100,   0, 1 },   // INFO
  { 1800, 150, 100, 2 },   // WARNING
  { 2400, 200, 100, 3 },   // CRITICAL
  { 3000, 300, 150, 5 },   // EMERGENCY
};

// #############################################################################
//  OPERATIONAL STATE CLUSTER
// #############################################################################
struct GsOpCluster {
  float    centroid[4];     // [bandCount, thermalRisk, pktRate, duration]
  uint32_t memberCount;
  uint8_t  label;
  bool     valid;
};

// #############################################################################
//  MARKOV TRANSITION ENTRY
// #############################################################################
struct GsMarkovEntry {
  uint32_t transitions[GS_MARKOV_STATES];
  uint32_t totalOut;
};

// #############################################################################
//  SESSION PROFILE
// #############################################################################
struct GsSessionProfile {
  uint32_t startMs;
  uint32_t totalAttackMs;
  uint32_t longestAttackMs;
  uint16_t modeChanges;
  uint16_t alertsFired;
  uint8_t  peakBandCount;
  float    peakThermalRisk;
  float    avgEffectiveness;
  uint8_t  uniqueModesUsed;
  bool     valid;
};

// #############################################################################
//  SELF-MONITOR STATS
// #############################################################################
struct GsSelfMonStats {
  uint32_t tickCount;
  uint32_t avgTickUs;
  uint32_t maxTickUs;
  uint32_t minFreeHeap;
  uint32_t rulesEvaluated;
  uint32_t rulesFired;
  uint32_t alertsGenerated;
  uint32_t alertsDropped;
  uint32_t anomaliesDetected;
  uint32_t patternsMatched;
  uint32_t recommendations;
  float    ruleHitRate;
};

// #############################################################################
//  WELFORD ONLINE STATISTICS — used across multiple layers
// #############################################################################
struct GsWelford {
  float    mean;
  float    m2;
  uint32_t n;

  void reset() { mean = 0; m2 = 0; n = 0; }

  void update(float val) {
    n++;
    float delta = val - mean;
    mean += delta / (float)n;
    m2 += delta * (val - mean);
  }

  float variance() const {
    return (n >= 2) ? m2 / (float)(n - 1) : 0.0f;
  }

  float stddev() const {
    return (n >= 2) ? sqrtf(m2 / (float)(n - 1)) : 0.0f;
  }

  bool isAnomaly(float val, float sigma) const {
    if (n < 10) return false;
    float sd = stddev();
    if (sd < 0.001f) return false;
    return fabsf(val - mean) > sigma * sd;
  }

  float zScore(float val) const {
    if (n < 2) return 0.0f;
    float sd = stddev();
    if (sd < 0.001f) return 0.0f;
    return (val - mean) / sd;
  }
};
