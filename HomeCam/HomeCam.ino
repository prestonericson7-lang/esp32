/* ============================================================================
   HomeCam  —  ESP32-S3-CAM (OV3660, N16R8)  pan/tilt security cam
   ----------------------------------------------------------------------------
   ONE FILE. Only libraries that ship with the ESP32 board package. Nothing to
   download. Open in Arduino IDE, set the board options (see bottom of file),
   put in your WiFi name + password on the two lines just below, and flash.

   Open it on your phone (same WiFi) at:  http://homecam.local   (or the IP it
   prints to the Serial Monitor at 115200 baud if .local doesn't resolve).

   Hardware (wiring table is in the chat / bottom of this file):
     - 2x MG90S servos   : pan = GPIO1 , tilt = GPIO42
     - 2x HLK-LD2410C     : LEFT  TX -> GPIO41 ,  RIGHT TX -> GPIO21
     - Common ground between the ESP32, the servo supply, and the radars.
   ========================================================================== */

#include "esp_camera.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include "esp_http_server.h"
#include "driver/ledc.h"

/* ===========================================================================
   >>>>>>>>>>>>>>>>>>>>>>>  PUT YOUR WIFI INFO HERE  <<<<<<<<<<<<<<<<<<<<<<<<<<
   Edit the two lines right below. Keep the quotes.
   =========================================================================== */
const char* WIFI_SSID = "click for virus";         //  <-- your WiFi network name
const char* WIFI_PASS = "PUT_YOUR_PASSWORD_HERE";  //  <-- your WiFi password
/* =========================================================================== */

const char* MDNS_HOST = "homecam";   // -> http://homecam.local   (change if you like)

/* --------------------------------------------------------------------------
   PIN WIRING  (change a number here if you ever re-wire; nothing else to touch)
   -------------------------------------------------------------------------- */
#define PAN_SERVO_PIN     1    // MG90S pan  signal  (orange wire)
#define TILT_SERVO_PIN    42   // MG90S tilt signal  (orange wire)
#define RADAR_LEFT_RX     41   // LEFT  LD2410C  "TX" pin -> this ESP pin
#define RADAR_RIGHT_RX    21   // RIGHT LD2410C  "TX" pin -> this ESP pin

/* Mount-orientation flags. TILT is positional: flip TILT_INVERT if up/down is
   reversed. PAN is continuous: PAN_INVERT swaps which way left/right spins.     */
#define PAN_INVERT        false
#define TILT_INVERT       true    // your tilt read reversed; set back to false if it flips

/* Servo travel limits + centre (degrees). Tighten if the mount binds.         */
#define PAN_MIN_DEG       10
#define PAN_MAX_DEG       170
#define PAN_CENTER_DEG    90
#define TILT_MIN_DEG      40
#define TILT_MAX_DEG      140
#define TILT_CENTER_DEG   90

/* Pulse range for the POSITIONAL tilt servo (0..180 deg).                      */
#define SERVO_MIN_US      500
#define SERVO_MAX_US      2500

/* ---- PAN is a CONTINUOUS-ROTATION (360) servo: the pulse sets SPEED, not an
   angle.  1500us = stop; the further from 1500, the faster it spins.  Tune:
     - jog too fast / too slow?            change PAN_JOG_US
     - guard sweep too fast?               change PAN_SWEEP_US
     - pan CREEPS while it should be idle? nudge PAN_STOP_US by +/-10..30us until
                                           it sits dead still (servo trim).      */
#define PAN_STOP_US       1500    // neutral / STOP pulse (trim here if it creeps)
#define PAN_JOG_US        180     // manual jog speed: offset from stop (bigger = faster)
#define PAN_SWEEP_US      130     // guard-mode step speed (gentler)
#define PAN_CMD_MS        300     // a jog keeps pan moving this long; button release => stop

/* Boot-zero dead reckoning: position is estimated by integrating commanded
   motion from the power-on angle (defined as 0 deg). Hard soft-limits then make
   it physically impossible for the pan to wind away or be spun up by repeated
   radar hits. CALIBRATE PAN_DPS_PER_US once (jog a known ~90 deg, time it, and
   adjust until the on-screen angle matches reality). This only affects limit/
   centring accuracy -- the anti-runaway guarantee holds at any value.          */
#define PAN_DPS_PER_US    0.25f   // estimated degrees/sec per microsecond of offset
#define PAN_LIMIT_DEG     100     // HARD limit: never drive past +/- this from zero
#define PAN_SWEEP_RANGE   70      // guard sweep half-range from zero (< PAN_LIMIT_DEG)
#define PAN_HOME_TOL      3       // "centred" tolerance for return-to-zero (deg)

/* Guard uses STEP-SCAN: move a short step, then STOP and listen. The radars are
   read only while stopped, so the camera never chases its own motion and a
   noisy/continuous detection nudges it one step at most -- never spins it up.  */
#define GUARD_MOVE_MS     500     // drive this long each step
#define GUARD_DWELL_MS    450     // then stop & read the radars this long

/* Onboard WS2812 status LED (GPIO48). Set to 0 to disable completely.         */
#define USE_STATUS_LED    1
#define STATUS_LED_PIN    48

/* Default streaming resolution. FRAMESIZE_VGA (640x480) = smooth on a phone.
   Bump to FRAMESIZE_SVGA for more detail if your WiFi is strong.              */
#define DEFAULT_FRAMESIZE FRAMESIZE_VGA

/* ============================  CAMERA PIN MAP  =============================
   This is YOUR board (ESP32-S3-CAM OV3660). Do not change unless you swap
   boards. (Verified against the board silkscreen.)                           */
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     15
#define SIOD_GPIO_NUM     4
#define SIOC_GPIO_NUM     5
#define Y9_GPIO_NUM       16
#define Y8_GPIO_NUM       17
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       10
#define Y4_GPIO_NUM       8
#define Y3_GPIO_NUM       9
#define Y2_GPIO_NUM       11
#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     7
#define PCLK_GPIO_NUM     13

/* ----------------------------------------------------------------------------
   Tunables for behaviour
   ---------------------------------------------------------------------------- */
#define STEP_DEG            3        // degrees per button tap / hold-repeat
#define SERVO_TICK_MS       15       // how often servos step toward target
#define SERVO_SLEW_DEG      2.0f     // max degrees moved per servo tick (smoothness)
#define GUARD_TICK_MS       40       // guard-mode logic rate
#define GUARD_SWEEP_DEG     1.2f     // pan sweep speed per guard tick (~30 deg/s)
#define GUARD_HOLD_MS       4000     // hold & watch a radar hit this long
#define LOG_REPEAT_MS       2500     // throttle for "still moving" log lines
#define LOG_SIZE            40       // activity log ring-buffer length
#define LOG_JSON_MAX        25       // how many log lines sent to the web page

/* ----------------------------------------------------------------------------
   Globals
   ---------------------------------------------------------------------------- */
// Cross-task control values (written by web handlers, read by loop()).
// All are int/word sized => atomic single-word access on the ESP32-S3.
volatile int  g_mode       = 0;              // 0 = manual, 1 = guard
volatile int  tiltTarget   = TILT_CENTER_DEG;   // TILT is positional (an angle)

// PAN is a continuous-rotation servo: we command a spin DIRECTION, not an angle.
volatile int      panCmdDir   = 0;           // manual jog: -1 left, +1 right, 0 none
volatile uint32_t panCmdUntil = 0;           // manual jog stays active until this millis()
volatile int      g_panDir    = 0;           // what pan is doing right now (for the UI)

// loop()-only servo state
float tiltCur = TILT_CENTER_DEG;             // tilt current angle
// PAN dead-reckoning position (0 = boot angle) + guard step-scan state
float    panPosDeg       = 0;                // estimated pan angle from boot-zero
uint32_t lastPanMs       = 0;                // for position integration
volatile bool panHome    = false;            // "centre" -> drive back to zero
int      guardDir        = 1;                // current guard step direction
int      guardPhase      = 0;                // 0 = moving, 1 = dwell/listen
uint32_t guardPhaseUntil = 0;
uint32_t lastServoTick   = 0;

// LEDC servo channels (timer 1 so we never collide with the camera's timer 0).
#define PAN_LEDC_CH   LEDC_CHANNEL_2
#define TILT_LEDC_CH  LEDC_CHANNEL_3
#define SERVO_LEDC_TIMER LEDC_TIMER_1

/* ---- Radar ---------------------------------------------------------------- */
HardwareSerial RadarLeftSerial(1);   // UART1
HardwareSerial RadarRightSerial(2);  // UART2

struct Radar {
  HardwareSerial* ser;
  char            tag;          // 'L' or 'R'
  // frame parser
  uint8_t parseState;
  uint16_t payloadLen;
  uint16_t payloadIdx;
  uint8_t  payload[48];
  uint8_t  footerIdx;
  // decoded values
  volatile uint8_t  state;      // 0 none,1 moving,2 stationary,3 both
  volatile uint16_t movDist;    // cm
  volatile uint8_t  movEng;
  volatile uint16_t statDist;   // cm
  volatile uint8_t  statEng;
  volatile uint16_t detDist;    // cm
  volatile bool     present;
  volatile uint32_t lastFrameMs;
  volatile uint32_t frames;     // total valid frames decoded (0 = radar not wired/powered)
  // edge/log bookkeeping
  bool     prevPresent;
  uint32_t lastLogMs;
};

Radar radarL, radarR;

/* ---- Activity log (mutex-protected ring buffer) --------------------------- */
struct LogEntry {
  uint32_t t;        // millis()
  char     side;     // 'L','R','C'
  uint8_t  st;       // state
  uint16_t d;        // distance cm
};
LogEntry logBuf[LOG_SIZE];
int logHead = 0;     // index of next write
int logCount = 0;
SemaphoreHandle_t logMux;

/* ---- HTTP servers --------------------------------------------------------- */
httpd_handle_t camera_httpd = NULL;
httpd_handle_t stream_httpd = NULL;

/* ---- Stream / diagnostics ------------------------------------------------- */
volatile uint32_t g_servedFrames = 0;   // JPEG frames sent to clients (any endpoint)
volatile float    g_fps          = 0;   // frames/sec served (computed in loop())
volatile bool     g_psram        = false;
const char*       g_resLabel     = "VGA";

/* ============================================================================
   Status LED
   ========================================================================== */
inline void statusLED(uint8_t r, uint8_t g, uint8_t b) {
#if USE_STATUS_LED
  neopixelWrite(STATUS_LED_PIN, r, g, b);   // built into the ESP32 core (no lib)
#endif
}

/* ============================================================================
   Servos  (ESP-IDF LEDC driver, explicit timer/channel => no camera conflict)
   ========================================================================== */
static inline uint32_t usToDuty(int us) {
  if (us < SERVO_MIN_US) us = SERVO_MIN_US;
  if (us > SERVO_MAX_US) us = SERVO_MAX_US;
  // 14-bit resolution (ESP32-S3 LEDC max), 50 Hz => full scale 16384 == 20000 us
  return (uint32_t)(((uint64_t)us * 16384ULL) / 20000ULL);
}

void servoSetup() {
  ledc_timer_config_t tcfg;
  memset(&tcfg, 0, sizeof(tcfg));
  tcfg.speed_mode      = LEDC_LOW_SPEED_MODE;
  tcfg.timer_num       = SERVO_LEDC_TIMER;
  tcfg.duty_resolution = LEDC_TIMER_14_BIT;   // ESP32-S3 LEDC is 14-bit MAX (not 16)
  tcfg.freq_hz         = 50;
  tcfg.clk_cfg         = LEDC_AUTO_CLK;
  esp_err_t terr = ledc_timer_config(&tcfg);
  if (terr != ESP_OK) Serial.printf("[SERVO] timer config failed: 0x%x\n", terr);

  ledc_channel_config_t ccfg;
  memset(&ccfg, 0, sizeof(ccfg));
  ccfg.speed_mode = LEDC_LOW_SPEED_MODE;
  ccfg.timer_sel  = SERVO_LEDC_TIMER;
  ccfg.intr_type  = LEDC_INTR_DISABLE;
  ccfg.duty       = 0;
  ccfg.hpoint     = 0;

  ccfg.channel  = PAN_LEDC_CH;
  ccfg.gpio_num = PAN_SERVO_PIN;
  ledc_channel_config(&ccfg);

  ccfg.channel  = TILT_LEDC_CH;
  ccfg.gpio_num = TILT_SERVO_PIN;
  ledc_channel_config(&ccfg);
}

void servoWrite(ledc_channel_t ch, float angleDeg, bool invert) {
  if (invert) angleDeg = 180.0f - angleDeg;
  if (angleDeg < 0)   angleDeg = 0;
  if (angleDeg > 180) angleDeg = 180;
  int us = SERVO_MIN_US + (int)((angleDeg / 180.0f) * (SERVO_MAX_US - SERVO_MIN_US));
  ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, usToDuty(us));
  ledc_update_duty(LEDC_LOW_SPEED_MODE, ch);
}

// Raw microsecond write — used by the CONTINUOUS pan servo (pulse = SPEED).
void servoWriteRawUs(ledc_channel_t ch, int us) {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, usToDuty(us));
  ledc_update_duty(LEDC_LOW_SPEED_MODE, ch);
}

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ============================================================================
   Activity log
   ========================================================================== */
void logEvent(char side, uint8_t st, uint16_t d) {
  if (!logMux) return;
  if (xSemaphoreTake(logMux, pdMS_TO_TICKS(20)) == pdTRUE) {
    LogEntry& e = logBuf[logHead];
    e.t = millis(); e.side = side; e.st = st; e.d = d;
    logHead = (logHead + 1) % LOG_SIZE;
    if (logCount < LOG_SIZE) logCount++;
    xSemaphoreGive(logMux);
  }
}

void logClear() {
  if (!logMux) return;
  if (xSemaphoreTake(logMux, pdMS_TO_TICKS(20)) == pdTRUE) {
    logHead = 0; logCount = 0;
    xSemaphoreGive(logMux);
  }
}

/* ============================================================================
   LD2410 frame parser  (non-blocking, byte-at-a-time state machine)
   Data frame:  F4 F3 F2 F1 | len(2 LE) | payload[len] | F8 F7 F6 F5
   payload: 0x02,0xAA,state,movDist(2),movEng,statDist(2),statEng,detDist(2),0x55,chk
   ========================================================================== */
uint16_t radarDistance(const Radar& r) {
  if (r.state == 1) return r.movDist;
  if (r.state == 2) return r.statDist;
  if (r.state == 3) return r.movDist ? r.movDist : r.statDist;
  return r.detDist;
}

void radarDecode(Radar& r) {
  // Common fields (valid for both basic 0x02 and engineering 0x01 frames)
  if (r.payloadLen < 13) return;
  if (r.payload[0] != 0x02 && r.payload[0] != 0x01) return;
  if (r.payload[1] != 0xAA) return;

  r.state    = r.payload[2];
  r.movDist  = (uint16_t)r.payload[3]  | ((uint16_t)r.payload[4]  << 8);
  r.movEng   = r.payload[5];
  r.statDist = (uint16_t)r.payload[6]  | ((uint16_t)r.payload[7]  << 8);
  r.statEng  = r.payload[8];
  r.detDist  = (uint16_t)r.payload[9]  | ((uint16_t)r.payload[10] << 8);
  r.present  = (r.state != 0);
  r.lastFrameMs = millis();
  r.frames++;

  // Logging: appear / disappear edges, plus throttled "still moving"
  uint32_t now = millis();
  if (r.present && !r.prevPresent) {
    logEvent(r.tag, r.state, radarDistance(r));
    r.lastLogMs = now;
  } else if (!r.present && r.prevPresent) {
    logEvent(r.tag, 0, 0);               // cleared
  } else if (r.present && (r.state == 1 || r.state == 3)) {
    if (now - r.lastLogMs >= LOG_REPEAT_MS) {
      logEvent(r.tag, r.state, radarDistance(r));
      r.lastLogMs = now;
    }
  }
  r.prevPresent = r.present;
}

void radarFeed(Radar& r, uint8_t b) {
  switch (r.parseState) {
    case 0: r.parseState = (b == 0xF4) ? 1 : 0; break;
    case 1: r.parseState = (b == 0xF3) ? 2 : (b == 0xF4 ? 1 : 0); break;
    case 2: r.parseState = (b == 0xF2) ? 3 : (b == 0xF4 ? 1 : 0); break;
    case 3: r.parseState = (b == 0xF1) ? 4 : (b == 0xF4 ? 1 : 0); break;
    case 4: r.payloadLen = b; r.parseState = 5; break;                    // len low
    case 5:
      r.payloadLen |= ((uint16_t)b << 8);                                 // len high
      if (r.payloadLen == 0 || r.payloadLen > sizeof(r.payload)) { r.parseState = 0; }
      else { r.payloadIdx = 0; r.parseState = 6; }
      break;
    case 6:
      r.payload[r.payloadIdx++] = b;
      if (r.payloadIdx >= r.payloadLen) { r.footerIdx = 0; r.parseState = 7; }
      break;
    case 7: {
      static const uint8_t foot[4] = {0xF8, 0xF7, 0xF6, 0xF5};
      if (b == foot[r.footerIdx]) {
        r.footerIdx++;
        if (r.footerIdx >= 4) { radarDecode(r); r.parseState = 0; }
      } else {
        r.parseState = (b == 0xF4) ? 1 : 0;                               // resync
      }
      break;
    }
    default: r.parseState = 0; break;
  }
}

void radarPoll(Radar& r) {
  int guard = 0;
  while (r.ser->available() && guard++ < 256) {
    radarFeed(r, (uint8_t)r.ser->read());
  }
  // staleness: if no frame for >1.5s, treat as clear (radar unplugged / quiet)
  if (r.present && (millis() - r.lastFrameMs > 1500)) {
    r.present = false;
    if (r.prevPresent) { logEvent(r.tag, 0, 0); r.prevPresent = false; }
  }
}

/* ============================================================================
   Guard STEP-SCAN + servo motion.
   PAN (continuous servo) uses boot-zero dead reckoning + hard soft-limits so it
   can never wind away, and reads the radars ONLY while stopped (dwell) so it
   never chases its own motion or gets spun up by a noisy/continuous detection.
   ========================================================================== */
void panControlTick(uint32_t now) {
  uint32_t dt = now - lastPanMs;
  if (dt > 120) dt = 120;                 // cap after any stall so position can't jump
  lastPanMs = now;

  int dir   = 0;
  int offUs = PAN_JOG_US;

  if (g_mode == 1) {
    // GUARD: step-scan — move a bounded step, then STOP and listen.
    offUs = PAN_SWEEP_US;
    if (now >= guardPhaseUntil) {
      if (guardPhase == 0) {                       // done moving -> dwell & listen
        guardPhase = 1; guardPhaseUntil = now + GUARD_DWELL_MS;
      } else {                                     // done listening -> choose next step
        guardPhase = 0; guardPhaseUntil = now + GUARD_MOVE_MS;
        bool l = radarL.present && (radarL.state == 1 || radarL.state == 3);
        bool r = radarR.present && (radarR.state == 1 || radarR.state == 3);
        if (l && !r)      guardDir = -1;           // movement left  -> step left
        else if (r && !l) guardDir = +1;           // movement right -> step right
        else if (l && r)  guardDir = (((int)radarL.movEng + radarL.statEng) >=
                                      ((int)radarR.movEng + radarR.statEng)) ? -1 : +1;
        else {                                     // nothing moving -> sweep, reverse at range
          if (panPosDeg >=  PAN_SWEEP_RANGE) guardDir = -1;
          else if (panPosDeg <= -PAN_SWEEP_RANGE) guardDir = +1;
        }
      }
    }
    dir = (guardPhase == 0) ? guardDir : 0;        // move in move-phase, stop during dwell
  } else if (panHome) {
    // return to boot-zero (centre)
    float ap = panPosDeg < 0 ? -panPosDeg : panPosDeg;
    if (ap <= PAN_HOME_TOL) { panHome = false; dir = 0; }
    else dir = (panPosDeg > 0) ? -1 : +1;
  } else {
    // MANUAL jog while a recent button command is live; release => stop
    dir = (now < panCmdUntil) ? panCmdDir : 0;
  }

  // Hard soft-limits from boot-zero: never drive past +/- PAN_LIMIT_DEG (anti wind-up).
  if (dir < 0 && panPosDeg <= -PAN_LIMIT_DEG) dir = 0;
  if (dir > 0 && panPosDeg >=  PAN_LIMIT_DEG) dir = 0;

  // Integrate the dead-reckoning position, then drive the servo.
  if (dir != 0) panPosDeg += dir * (offUs * PAN_DPS_PER_US) * (dt / 1000.0f);
  g_panDir = dir;
  int us = PAN_STOP_US + (dir == 0 ? 0 : (PAN_INVERT ? -dir : dir) * offUs);
  servoWriteRawUs(PAN_LEDC_CH, us);
}

void servoTick(uint32_t now) {
  // TILT: positional — slew the current angle toward the target
  float tt = (float)tiltTarget;
  if (tiltCur < tt)      tiltCur = min(tt, tiltCur + SERVO_SLEW_DEG);
  else if (tiltCur > tt) tiltCur = max(tt, tiltCur - SERVO_SLEW_DEG);
  servoWrite(TILT_LEDC_CH, tiltCur, TILT_INVERT);

  // PAN: continuous rotation — position-tracked, soft-limited, step-scan guard
  panControlTick(now);
}

/* ============================================================================
   Camera init
   ========================================================================== */
bool initCamera() {
  camera_config_t config;
  memset(&config, 0, sizeof(config));
  config.ledc_channel = LEDC_CHANNEL_0;   // camera owns ch0 / timer0
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk  = XCLK_GPIO_NUM;
  config.pin_pclk  = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href  = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn  = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  // Size the frame-buffer pool for the LARGEST resolution the UI can select
  // (SVGA). esp_camera_init allocates the pool once; set_framesize() only
  // down-switches the sensor, so every res button (QVGA/CIF/VGA/SVGA) then
  // stays within this allocation. The no-PSRAM branch below forces QVGA.
  config.frame_size   = FRAMESIZE_SVGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode    = CAMERA_GRAB_LATEST;
  config.fb_location  = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count     = 2;

  g_psram = psramFound();
  if (!g_psram) {
    // Fallback if PSRAM isn't detected. On an N16R8 this means "OPI PSRAM" was
    // NOT selected in Tools > PSRAM -> tiny single-buffer feed until you fix it.
    config.frame_size   = FRAMESIZE_QVGA;
    config.fb_location  = CAMERA_FB_IN_DRAM;
    config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_count     = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] init FAILED 0x%x — check the camera ribbon is seated.\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    // OV3660 modules image upside-down & a touch washed-out by default.
    if (s->id.PID == OV3660_PID) {
      s->set_vflip(s, 1);
      s->set_brightness(s, 1);
      s->set_saturation(s, -2);
    }
    s->set_framesize(s, FRAMESIZE_QVGA);   // start small + fast for smooth real-time; bump via UI
  }
  g_resLabel = "QVGA";
  Serial.printf("[CAM] ready | PSRAM: %s | start res: %s | fb_count: %d\n",
                g_psram ? "YES" : "NO  <-- set Tools>PSRAM>OPI PSRAM and re-flash!",
                g_resLabel, config.fb_count);
  return true;
}

/* ============================================================================
   Web page (minimal, mobile-first). Stored in flash.
   ========================================================================== */
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>HomeCam</title>
<style>
:root{--bg:#0d0f14;--card:#171a21;--acc:#2f81f7;--txt:#e6e6e6;--mut:#8b94a3;--warn:#d29922}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--txt);font-family:system-ui,Arial,sans-serif}
header{padding:10px 14px;font-weight:700;font-size:18px;display:flex;justify-content:space-between;align-items:center}
#wrap{max-width:720px;margin:0 auto;padding:0 10px 30px}
.card{background:var(--card);border-radius:12px;padding:12px;margin:10px 0}
#vwrap{position:relative;border-radius:10px;overflow:hidden;background:#000;aspect-ratio:4/3}
#vwrap img{position:absolute;inset:0;width:100%;height:100%;object-fit:contain;background:#000}
#vid2{opacity:0}
#hud{position:absolute;top:6px;left:6px;z-index:2;display:flex;gap:6px;align-items:center;background:rgba(0,0,0,.55);color:#fff;font:12px/1 monospace;padding:5px 8px;border-radius:8px;pointer-events:none}
#fps{color:#2ea043;font-weight:700}
#bars{display:inline-flex;gap:2px;align-items:flex-end;height:13px}
#bars i{width:3px;background:#555;border-radius:1px}
#psw{position:absolute;bottom:6px;left:6px;right:6px;z-index:2;background:#b62324;color:#fff;font:12px/1.35 sans-serif;padding:7px 9px;border-radius:8px;display:none}
.seg{display:flex;gap:6px;margin-top:8px}
.seg button{flex:1;padding:9px 4px;font-size:13px;line-height:1.2}
.seg button.sel{background:var(--acc);border-color:var(--acc);color:#fff}
.pad{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;max-width:270px;margin:4px auto 0}
button{background:#222733;color:var(--txt);border:1px solid #2b3240;border-radius:10px;padding:16px;font-size:18px;cursor:pointer;touch-action:none;-webkit-user-select:none;user-select:none}
button:active{background:var(--acc)}
.wide{width:100%;padding:14px;font-size:16px}
.row{display:flex;gap:8px;flex-wrap:wrap}
.pill{flex:1;min-width:120px;background:#222733;border-radius:10px;padding:10px;text-align:center}
.dot{display:inline-block;width:10px;height:10px;border-radius:50%;background:#444;margin-right:6px;vertical-align:middle}
.on{background:var(--warn);box-shadow:0 0 8px var(--warn)}
#guardBtn.g{background:var(--warn);border-color:var(--warn);color:#000}
#log{max-height:220px;overflow:auto;white-space:pre-wrap;font:13px/1.5 monospace;color:var(--mut)}
.mut{color:var(--mut);font-size:13px}
a{color:var(--acc)}
</style></head><body>
<header><span>&#127968; HomeCam</span><span id=st class=mut>&hellip;</span></header>
<div id=wrap>
 <div class=card>
  <div id=vwrap>
   <img id=vid alt="camera"><img id=vid2 alt="">
   <div id=hud><span id=bars></span><span id=fps>--</span> fps<span id=res></span></div>
   <div id=psw>&#9888; PSRAM is OFF &mdash; set Tools &gt; PSRAM &gt; <b>OPI PSRAM</b> in Arduino IDE and re-flash. The feed stays slow until you do.</div>
  </div>
  <div class=seg>
   <button data-r=qvga class=sel>QVGA<br><span class=mut>fast</span></button>
   <button data-r=vga>VGA<br><span class=mut>balanced</span></button>
   <button data-r=svga>SVGA<br><span class=mut>sharp</span></button>
  </div>
  <div class=seg>
   <button id=mSnap class=sel>Snapshot<br><span class=mut>iPhone-smooth</span></button>
   <button id=mStream>Stream<br><span class=mut>MJPEG</span></button>
  </div>
 </div>
 <div class=card>
  <div class=pad>
   <span></span><button id=bu>&#9650;</button><span></span>
   <button id=bl>&#9664;</button><button id=bc>&#9632;</button><button id=br>&#9654;</button>
   <span></span><button id=bd>&#9660;</button><span></span>
  </div>
  <div class=mut style="text-align:center;margin-top:8px">Pan <span id=pan>&#9632;</span> &middot; Tilt <span id=tilt>90</span>&deg;</div>
 </div>
 <div class=card>
  <button id=guardBtn class=wide>Guard Mode: OFF</button>
  <div class=row style="margin-top:10px">
   <div class=pill><span class=dot id=ldot></span>Left<br><span id=lv class=mut>&mdash;</span></div>
   <div class=pill><span class=dot id=rdot></span>Right<br><span id=rv class=mut>&mdash;</span></div>
  </div>
  <div style="margin-top:10px"><a href="/capture" target=_blank>&#128247; Snapshot</a></div>
 </div>
 <div class=card>
  <div style="display:flex;justify-content:space-between"><b>Activity Log</b><span id=clr class=mut style="cursor:pointer">clear</span></div>
  <div id=log>&mdash;</div>
 </div>
</div>
<script>
var H=location.hostname;
var imgs=[document.getElementById('vid'),document.getElementById('vid2')];
var cur=0, mode='snap', busy=false, gen=0, lastReq=0;
var FRAME_MIN_MS=66;   // cap the snapshot rate (~15fps) so the feed CANNOT starve the controls
function capURL(){return 'http://'+H+':81/capture?x='+Date.now();}
function pump(){
 if(mode!=='snap'||busy) return;
 var wait=FRAME_MIN_MS-(Date.now()-lastReq); if(wait<0) wait=0;
 setTimeout(doPump, wait);
}
function doPump(){
 if(mode!=='snap'||busy) return;
 busy=true; lastReq=Date.now(); var g=++gen; var nxt=imgs[1-cur];
 nxt.onload=function(){ busy=false; if(g!==gen||mode!=='snap') return;
  nxt.style.opacity=1; imgs[cur].style.opacity=0; cur=1-cur; pump(); };
 nxt.onerror=function(){ busy=false; if(mode==='snap') setTimeout(pump,200); };
 nxt.src=capURL();
}
function setMode(m){
 gen++; busy=false; mode=m;
 document.getElementById('mSnap').className=(m==='snap'?'sel':'');
 document.getElementById('mStream').className=(m==='mjpeg'?'sel':'');
 if(m==='mjpeg'){ imgs[1].style.opacity=0; imgs[0].style.opacity=1; imgs[0].src='http://'+H+':81/stream'; }
 else { imgs[0].src=''; imgs[1].src=''; cur=0; imgs[0].style.opacity=0; imgs[1].style.opacity=0; pump(); }
}
document.getElementById('mSnap').onclick=function(){setMode('snap');};
document.getElementById('mStream').onclick=function(){setMode('mjpeg');};
var rb=document.querySelectorAll('.seg button[data-r]');
for(var ri=0;ri<rb.length;ri++){rb[ri].onclick=function(){
 for(var j=0;j<rb.length;j++)rb[j].className='';
 this.className='sel'; cmd('res='+this.getAttribute('data-r'));};}
function cmd(q){fetch('/cmd?'+q).catch(function(){});}
function hold(id,q){var el=document.getElementById(id),iv=null;
 function go(){cmd(q);}
 el.addEventListener('pointerdown',function(e){e.preventDefault();go();iv=setInterval(go,160);});
 function stop(){if(iv){clearInterval(iv);iv=null;}}
 el.addEventListener('pointerup',stop);el.addEventListener('pointerleave',stop);el.addEventListener('pointercancel',stop);}
hold('bu','move=up');hold('bd','move=down');hold('bl','move=left');hold('br','move=right');
document.getElementById('bc').onclick=function(){cmd('center=1');};
document.getElementById('guardBtn').onclick=function(){cmd('guard='+(document.getElementById('guardBtn').classList.contains('g')?0:1));};
document.getElementById('clr').onclick=function(){cmd('clearlog=1');};
function stxt(s){return s==1?'moving':s==2?'still':s==3?'moving':'clear';}
function upd(p,o){var d=document.getElementById(p+'dot'),v=document.getElementById(p+'v');
 if(!o||!o.rx){d.className='dot';v.style.color='#e5534b';v.textContent='no data ✗';return;}
 v.style.color='';
 if(o.present){d.className='dot on';v.textContent=stxt(o.st)+' '+o.d+'cm';}else{d.className='dot';v.textContent='clear';}}
function ago(t,now){var s=Math.max(0,Math.round((now-t)/1000));return s<1?'now':(s<60?s+'s':Math.floor(s/60)+'m')+' ago';}
function bars(rssi){var n=rssi>=-60?4:rssi>=-70?3:rssi>=-80?2:1;var s='';for(var k=0;k<4;k++){var ht=4+k*3;s+='<i style="height:'+ht+'px;background:'+(k<n?'#2ea043':'#555')+'"></i>';}return s;}
function poll(){fetch('/status').then(function(r){return r.json();}).then(function(j){
 document.getElementById('st').textContent=(j.mode?'GUARD':'manual')+' · '+j.ip;
 document.getElementById('pan').textContent=(j.panspin<0?'◄':(j.panspin>0?'►':'■'))+' '+(j.panpos!=null?j.panpos+'°':'');
 document.getElementById('tilt').textContent=j.tilt;
 document.getElementById('fps').textContent=(j.fps!=null?j.fps:'--');
 document.getElementById('res').textContent=j.res?(' '+j.res):'';
 document.getElementById('bars').innerHTML=bars(j.rssi==null?-100:j.rssi);
 document.getElementById('psw').style.display=(j.psram===false?'block':'none');
 var gb=document.getElementById('guardBtn');gb.textContent='Guard Mode: '+(j.mode?'ON':'OFF');gb.className='wide'+(j.mode?' g':'');
 upd('l',j.left);upd('r',j.right);
 var h='';
 if(j.log&&j.log.length){for(var i=0;i<j.log.length;i++){var e=j.log[i];
  h+=ago(e.t,j.now)+'  '+(e.side=='L'?'◄ LEFT ':(e.side=='R'?'RIGHT ►':'CAM    '))+'  '+stxt(e.st)+(e.st?(' @ '+e.d+'cm'):'')+'\n';}}
 else h='no activity yet';
 document.getElementById('log').textContent=h;
}).catch(function(){});}
setMode('snap');
setInterval(poll,1000);poll();
</script></body></html>)HTML";

/* ============================================================================
   HTTP handlers
   ========================================================================== */
#define PART_BOUNDARY "homecamframe"
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY     = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART         = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

esp_err_t index_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "identity");
  return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

esp_err_t capture_handler(httpd_req_t* req) {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { httpd_resp_send_500(req); return ESP_FAIL; }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  esp_err_t res = httpd_resp_send(req, (const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  if (res == ESP_OK) g_servedFrames++;
  return res;
}

esp_err_t stream_handler(httpd_req_t* req) {
  camera_fb_t* fb = NULL;
  esp_err_t res = ESP_OK;
  char part[72];

  res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "X-Framerate", "30");

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) { res = ESP_FAIL; break; }

    if (res == ESP_OK)
      res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (res == ESP_OK) {
      size_t hl = snprintf(part, sizeof(part), STREAM_PART, fb->len);
      res = httpd_resp_send_chunk(req, part, hl);
    }
    if (res == ESP_OK)
      res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);

    esp_camera_fb_return(fb);
    fb = NULL;
    if (res != ESP_OK) break;   // client closed the connection
    g_servedFrames++;
  }
  return res;
}

esp_err_t status_handler(httpd_req_t* req) {
  // Snapshot the log under the mutex, then build JSON outside the lock.
  LogEntry tmp[LOG_JSON_MAX];
  int n = 0;
  if (logMux && xSemaphoreTake(logMux, pdMS_TO_TICKS(30)) == pdTRUE) {
    int count = logCount < LOG_JSON_MAX ? logCount : LOG_JSON_MAX;
    for (int i = 0; i < count; i++) {
      int idx = (logHead - 1 - i + LOG_SIZE * 2) % LOG_SIZE;  // newest first
      tmp[n++] = logBuf[idx];
    }
    xSemaphoreGive(logMux);
  }

  String out;
  out.reserve(1600);
  out  = "{\"mode\":"; out += g_mode;
  out += ",\"panspin\":"; out += g_panDir;      // -1 left, 0 stop, +1 right (continuous pan)
  out += ",\"panpos\":";  out += (int)panPosDeg; // estimated pan angle from boot-zero
  out += ",\"tilt\":"; out += (int)tiltCur;
  out += ",\"now\":";  out += (uint32_t)millis();
  out += ",\"ip\":\""; out += WiFi.localIP().toString(); out += "\"";
  out += ",\"fps\":";   out += (int)(g_fps + 0.5f);
  out += ",\"rssi\":";  out += (int)WiFi.RSSI();
  out += ",\"psram\":"; out += g_psram ? "true" : "false";
  out += ",\"res\":\""; out += g_resLabel; out += "\"";

  out += ",\"left\":{\"present\":";  out += radarL.present ? "true" : "false";
  out += ",\"st\":"; out += radarL.state; out += ",\"d\":"; out += radarDistance(radarL);
  out += ",\"rx\":"; out += radarL.frames; out += "}";
  out += ",\"right\":{\"present\":"; out += radarR.present ? "true" : "false";
  out += ",\"st\":"; out += radarR.state; out += ",\"d\":"; out += radarDistance(radarR);
  out += ",\"rx\":"; out += radarR.frames; out += "}";

  out += ",\"log\":[";
  for (int i = 0; i < n; i++) {
    if (i) out += ",";
    out += "{\"t\":";   out += tmp[i].t;
    out += ",\"side\":\""; out += tmp[i].side; out += "\"";
    out += ",\"st\":";  out += tmp[i].st;
    out += ",\"d\":";   out += tmp[i].d; out += "}";
  }
  out += "]}";

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, out.c_str(), out.length());
}

esp_err_t cmd_handler(httpd_req_t* req) {
  char  val[24];
  size_t qlen = httpd_req_get_url_query_len(req) + 1;
  if (qlen > 1 && qlen < 128) {
    char* q = (char*)malloc(qlen);
    if (q && httpd_req_get_url_query_str(req, q, qlen) == ESP_OK) {

      if (httpd_query_key_value(q, "move", val, sizeof(val)) == ESP_OK) {
        g_mode = 0; panHome = false;              // manual control cancels guard + homing
        uint32_t mnow = millis();
        if (!strcmp(val, "up"))    tiltTarget = clampi(tiltTarget + STEP_DEG, TILT_MIN_DEG, TILT_MAX_DEG);
        if (!strcmp(val, "down"))  tiltTarget = clampi(tiltTarget - STEP_DEG, TILT_MIN_DEG, TILT_MAX_DEG);
        if (!strcmp(val, "left"))  { panCmdDir = -1; panCmdUntil = mnow + PAN_CMD_MS; }  // jog pan
        if (!strcmp(val, "right")) { panCmdDir = +1; panCmdUntil = mnow + PAN_CMD_MS; }
      }
      if (httpd_query_key_value(q, "center", val, sizeof(val)) == ESP_OK) {
        g_mode = 0; panCmdDir = 0; panCmdUntil = 0;
        panHome = true;                           // pan returns to boot-zero
        tiltTarget = TILT_CENTER_DEG;             // tilt centres
      }
      if (httpd_query_key_value(q, "guard", val, sizeof(val)) == ESP_OK) {
        g_mode = (val[0] == '1') ? 1 : 0;
      }
      if (httpd_query_key_value(q, "clearlog", val, sizeof(val)) == ESP_OK) {
        logClear();
      }
      if (httpd_query_key_value(q, "res", val, sizeof(val)) == ESP_OK) {
        sensor_t* s = esp_camera_sensor_get();
        if (s) {
          if      (!strcmp(val, "qvga")) { s->set_framesize(s, FRAMESIZE_QVGA); g_resLabel = "QVGA"; }
          else if (!strcmp(val, "cif"))  { s->set_framesize(s, FRAMESIZE_CIF);  g_resLabel = "CIF";  }
          else if (!strcmp(val, "vga"))  { s->set_framesize(s, FRAMESIZE_VGA);  g_resLabel = "VGA";  }
          else if (!strcmp(val, "svga") && g_psram) { s->set_framesize(s, FRAMESIZE_SVGA); g_resLabel = "SVGA"; }
        }
      }
    }
    if (q) free(q);
  }
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, "ok", 2);
}

void startServers() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 8;
  config.lru_purge_enable = true;

  // Control / UI server on port 80
  config.server_port = 80;
  config.ctrl_port   = 32768;
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_uri_t u_index   = { "/",        HTTP_GET, index_handler,   NULL };
    httpd_uri_t u_cmd     = { "/cmd",     HTTP_GET, cmd_handler,     NULL };
    httpd_uri_t u_status  = { "/status",  HTTP_GET, status_handler,  NULL };
    httpd_uri_t u_capture = { "/capture", HTTP_GET, capture_handler, NULL };
    httpd_register_uri_handler(camera_httpd, &u_index);
    httpd_register_uri_handler(camera_httpd, &u_cmd);
    httpd_register_uri_handler(camera_httpd, &u_status);
    httpd_register_uri_handler(camera_httpd, &u_capture);
    Serial.println("[WEB] control server on :80");
  }

  // Dedicated MJPEG stream server on port 81 (so the stream can't block the UI)
  config.server_port = 81;
  config.ctrl_port   = 32769;
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_uri_t u_stream = { "/stream",  HTTP_GET, stream_handler,  NULL };
    httpd_uri_t u_scap   = { "/capture", HTTP_GET, capture_handler, NULL };
    httpd_register_uri_handler(stream_httpd, &u_stream);
    httpd_register_uri_handler(stream_httpd, &u_scap);   // snapshot-pump uses :81/capture
    Serial.println("[WEB] stream server on :81");
  }
}

/* ============================================================================
   WiFi
   ========================================================================== */
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                 // keeps the stream snappy
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WiFi] connecting to \"%s\" ", WIFI_SSID);

  uint32_t start = millis();
  bool blink = false;
  while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) {
    delay(300);
    Serial.print(".");
    blink = !blink;
    statusLED(0, 0, blink ? 40 : 0);    // blink blue while connecting
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] connected. IP: ");
    Serial.println(WiFi.localIP());
    Serial.printf("[WiFi] signal RSSI: %d dBm (closer to 0 = stronger; below -80 is weak)\n", WiFi.RSSI());
    statusLED(0, 30, 0);                 // green = connected
  } else {
    Serial.println("[WiFi] FAILED — check SSID/password at the top of the sketch.");
    statusLED(40, 0, 0);                 // red = no wifi
  }
}

/* ============================================================================
   setup / loop
   ========================================================================== */
void setup() {
  // NOTE: deliberately NO brownout-detector register poke here. On the
  // ESP32-S3 the classic WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0) trick is
  // unreliable and is reported to reset the board before setup() even runs.
  // A "Brownout detector was triggered" message means the 3.3V rail is dipping
  // on the camera/WiFi inrush -> cure it with a solid 5V supply + a bulk
  // capacitor on 5V (see the POWER / BROWNOUT notes at the bottom of this file).
  Serial.begin(115200);
  delay(300);
  Serial.println("\n== HomeCam booting ==");

  statusLED(20, 0, 20);                        // purple = booting

  logMux = xSemaphoreCreateMutex();

  // Servos first, then centre them.
  servoSetup();
  servoWriteRawUs(PAN_LEDC_CH, PAN_STOP_US);        // continuous pan: STOP at boot (no spinning)
  servoWrite(TILT_LEDC_CH, tiltCur, TILT_INVERT);   // positional tilt: centre
  panPosDeg = 0; lastPanMs = millis();              // this boot angle = zero reference

  // Radars (RX only; 256000 baud default for LD2410C).
  radarL = Radar(); radarR = Radar();
  radarL.ser = &RadarLeftSerial;  radarL.tag = 'L';
  radarR.ser = &RadarRightSerial; radarR.tag = 'R';
  RadarLeftSerial.setRxBufferSize(512);
  RadarRightSerial.setRxBufferSize(512);
  RadarLeftSerial.begin(256000, SERIAL_8N1, RADAR_LEFT_RX, -1);
  RadarRightSerial.begin(256000, SERIAL_8N1, RADAR_RIGHT_RX, -1);
  Serial.println("[RADAR] LD2410C serial up (left=GPIO41, right=GPIO21)");

  // Camera
  if (!initCamera()) {
    // Halt with a red LED so you get clear feedback instead of a silent fail.
    while (true) { statusLED(60, 0, 0); delay(400); statusLED(0, 0, 0); delay(400); }
  }

  connectWiFi();

  if (MDNS.begin(MDNS_HOST)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[mDNS] http://%s.local\n", MDNS_HOST);
  }

  startServers();

  Serial.println("== HomeCam ready ==");
}

void loop() {
  radarPoll(radarL);
  radarPoll(radarR);

  uint32_t now = millis();
  if (now - lastServoTick >= SERVO_TICK_MS) { lastServoTick = now; servoTick(now); }

  // Stream FPS counter: frames served in the last second (shown in the web HUD).
  static uint32_t lastFpsMs = 0, lastFrames = 0;
  if (now - lastFpsMs >= 1000) {
    uint32_t f = g_servedFrames;
    g_fps = (float)(f - lastFrames) * 1000.0f / (float)(now - lastFpsMs);
    lastFrames = f; lastFpsMs = now;
  }

  // Radar heartbeat on Serial: if rx counts stay 0, the LD2410 TX pin is not
  // wired to the ESP pin / the radar isn't powered. If rx climbs but state
  // stays 0, the radar works but sees nothing (placement/sensitivity).
  static uint32_t lastRadarDbg = 0;
  if (now - lastRadarDbg >= 2000) {
    lastRadarDbg = now;
    Serial.printf("[RADAR] L rx=%u state=%d %dcm | R rx=%u state=%d %dcm\n",
                  radarL.frames, radarL.state, radarDistance(radarL),
                  radarR.frames, radarR.state, radarDistance(radarR));
  }

  // Status LED: amber in guard mode, green otherwise (only when connected).
  static uint32_t lastLed = 0;
  if (WiFi.status() == WL_CONNECTED && now - lastLed > 500) {
    lastLed = now;
    if (g_mode == 1) {
      bool hit = radarL.present || radarR.present;
      statusLED(hit ? 60 : 35, hit ? 30 : 20, 0);   // amber, brighter on a hit
    } else {
      statusLED(0, 25, 0);                            // green
    }
  }

  delay(2);   // keep the watchdog happy, yield to WiFi/httpd tasks
}

/* ============================================================================
   ARDUINO IDE BOARD SETTINGS  (do this once)
   ----------------------------------------------------------------------------
   Tools menu:
     Board:            "ESP32S3 Dev Module"   (under esp32 by Espressif)
     USB CDC On Boot:  "Enabled"
     Flash Size:       "16MB (128Mb)"
     Partition Scheme: "16M Flash (3MB APP/9.9MB FATFS)"  (or any with >=3MB app)
     PSRAM:            "OPI PSRAM"            <-- IMPORTANT (this is the R8)
     Upload Mode:      "UART0 / Hardware CDC"
   Plug the USB-C cable into the port labelled "TTL" to upload.
   Open Serial Monitor at 115200 to see the IP address.
   ============================================================================

   POWER / "Brownout detector was triggered"  (boot loop, even with nothing
   else wired up)
   ----------------------------------------------------------------------------
   This is NOT a code bug and NOT your servos/radars -- it is the camera's
   current spike (OV3660 + WiFi) sagging the board's little 3.3V regulator
   below its safe voltage, so the chip resets itself over and over. Fix the
   power and it boots clean. In order of effectiveness:

     1) Power it from a REAL 5V supply: a 5V / 2A (or better) phone wall
        charger, NOT a PC/laptop USB port or hub, and use a SHORT, THICK,
        data-capable USB-C cable (thin "charge-only" cables cause this).
     2) Add a bulk capacitor: a 470uF-1000uF electrolytic between 5V and GND,
        as close to the board as possible. It soaks up the inrush spike.
        (Polarity matters: the striped leg is - and goes to GND.)
     3) Reseat the camera ribbon -- a loose brown FFC connector also throws
        this same error (and "camera probe failed").
     4) If you power the 5V pin from your own supply, keep its GND tied to the
        board GND, and you can leave the TTL USB plugged in just for Serial.

   Secondary software mitigation (only if good power still isn't enough):
     drop the camera clock on the "config.xclk_freq_hz = 20000000;" line to
     10000000 (10 MHz). Lower clock = less inrush, slightly lower frame rate.
   ========================================================================== */
