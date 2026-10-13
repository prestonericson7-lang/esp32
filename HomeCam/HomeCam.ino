/* ============================================================================
   HomeCam  —  ESP32-S3-CAM (OV3660, N16R8)  pan/tilt security cam
   FACE DETECTION + FACE RECOGNITION + SD LOGGING build
   ----------------------------------------------------------------------------
   One ESP32-S3-CAM: streams to your phone on your home WiFi at http://homecam.local,
   pan/tilt (continuous-rotation PAN servo + positional TILT servo), two LD2410C
   radars for the blind spots, a guard mode that scans + points at movement, and
   on-device face detection + recognition that logs people (enrolled name or
   "unknown") with snapshots to a micro-SD card.

   >>> REQUIRED Arduino IDE settings (face recognition will NOT compile otherwise) <<<
     Board            : ESP32S3 Dev Module
     ESP32 core       : esp32 by Espressif  **2.0.17**   (3.x REMOVED face recognition)
     PSRAM            : OPI PSRAM
     Flash Size       : 16MB (128Mb)
     Partition Scheme : Huge APP (3MB No OTA/1MB SPIFFS)
     USB CDC On Boot  : Enabled   (upload via the "TTL" USB-C port)
   No external libraries. The face models ship inside core 2.0.17.
   Put a (blank, FAT32) micro-SD card in the on-board slot for logging/snapshots.

   WIRING (your board, unchanged):
     PAN servo  signal -> GPIO1      TILT servo signal -> GPIO42
     LEFT  LD2410C TX  -> GPIO41     RIGHT LD2410C TX   -> GPIO21
     Servos 5V + radars VCC to 5V; ALL grounds common with the ESP.
   ========================================================================== */

#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include "FS.h"
#include "SD_MMC.h"
#include "driver/ledc.h"
#include <sys/time.h>
#include <list>
#include <map>
#include <vector>
#include "human_face_detect_msr01.hpp"
#include "human_face_detect_mnp01.hpp"
#include "face_recognition_tool.hpp"
#include "face_recognition_112_v1_s8.hpp"

/* =================== PUT YOUR WIFI HERE (lines 52-53) =================== */
const char* WIFI_SSID = "click for virus";
const char* WIFI_PASS = "PUT_YOUR_PASSWORD_HERE";
const char* MDNS_HOST  = "homecam";               // -> http://homecam.local

/* =================== PINS (your ESP32-S3-CAM board) =================== */
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  15
#define SIOD_GPIO_NUM   4
#define SIOC_GPIO_NUM   5
#define Y9_GPIO_NUM    16
#define Y8_GPIO_NUM    17
#define Y7_GPIO_NUM    18
#define Y6_GPIO_NUM    12
#define Y5_GPIO_NUM    10
#define Y4_GPIO_NUM     8
#define Y3_GPIO_NUM     9
#define Y2_GPIO_NUM    11
#define VSYNC_GPIO_NUM  6
#define HREF_GPIO_NUM   7
#define PCLK_GPIO_NUM  13

#define SD_CLK   39      // on-board micro-SD (1-bit SD_MMC)
#define SD_CMD   38
#define SD_D0    40
#define LED_PIN  48      // on-board WS2812 status LED (-1 to disable)

#define PAN_PIN       1  // continuous-rotation pan servo
#define TILT_PIN      42 // positional tilt servo
#define RADAR_L_RX    41 // LEFT  LD2410C TX
#define RADAR_R_RX    21 // RIGHT LD2410C TX

/* =================== TILT (positional) =================== */
#define TILT_MIN_DEG   40
#define TILT_MAX_DEG   140
#define TILT_CENTER    90
#define TILT_INVERT    true     // your tilt reads reversed; set false if it flips
#define SERVO_MIN_US   500
#define SERVO_MAX_US   2500
#define TILT_STEP_DEG  4
#define TILT_SLEW      2.0f

/* =================== PAN (continuous rotation: pulse = speed) =================== */
#define PAN_INVERT     false    // flip if "left" spins the camera right
#define PAN_STOP_US    1500     // neutral/stop pulse (nudge +/-10..30 if it creeps)
#define PAN_JOG_US     180      // manual jog speed offset from stop
#define PAN_SWEEP_US   130      // guard step speed
#define PAN_CMD_MS     300      // a jog keeps moving this long; release => stop
#define PAN_DPS_PER_US 0.25f    // est. deg/sec per us offset (calibrate for accurate limits)
#define PAN_LIMIT_DEG  100      // HARD soft-limit from boot-zero (anti wind-up)
#define PAN_SWEEP_RANGE 70      // guard sweep half-range from zero
#define PAN_HOME_TOL   3
#define GUARD_MOVE_MS  500      // guard step-scan: move this long
#define GUARD_DWELL_MS 450      // then stop & listen this long
#define SERVO_TICK_MS  20
#define RADAR_BLANK_MS 700      // ignore radar while/just after the head moves

/* =================== LEDC servo channels (timer 1; camera owns timer 0) ======= */
#define PAN_LEDC_CH    LEDC_CHANNEL_2
#define TILT_LEDC_CH   LEDC_CHANNEL_3
#define SERVO_TIMER    LEDC_TIMER_1

/* =================== FACE COLORS (BGR888) =================== */
#define C_RED    0x000000FF
#define C_GREEN  0x0000FF00
#define C_YELLOW 0x0000FFFF
#define C_CYAN   0x00FFFF00

/* =================== FACE AI =================== */
static FaceRecognition112V1S8 recognizer;
static HumanFaceDetectMSR01 s1(0.1F, 0.5F, 10, 0.2F);
static HumanFaceDetectMNP01 s2(0.5F, 0.3F, 5);
uint8_t *rgb = nullptr;                              // BGR888 work buffer (QVGA)

/* =================== CAMERA SHARED FRAME =================== */
uint8_t *jpgBuf = nullptr;  size_t jpgLen = 0;  volatile uint32_t frameNo = 0;
SemaphoreHandle_t frameMux, cmdMux, sdMux;   // sdMux: SD is written from BOTH cores

/* =================== STATE =================== */
bool sdOk = false, recogOn = true, flipOn = false;
bool enrollPending = false;  String enrollName;  uint32_t enrollDeadline = 0;
float fps = 0;
volatile int  g_mode = 0;                            // 0 = manual, 1 = guard
int lastFaces = 0, lastId = -1, lastSim = 0;  String lastName = "";
std::map<int, uint32_t> lastSeenLog;
uint32_t lastUnknownSnap = 0, snapCount = 0;
String statusJson = "{}";
std::list<String> cmdQueue;                          // heavy cmds run by loop()

/* =================== ACTIVITY LOG (ring, for the web page) =================== */
struct LogEntry { uint32_t t; char kind[12]; char name[22]; int d; };
#define LOG_SIZE 30
LogEntry logBuf[LOG_SIZE];  int logHead = 0, logCount = 0;

/* =================== SERVO / GUARD STATE =================== */
volatile int  tiltTarget   = TILT_CENTER;
volatile int  panCmdDir    = 0;                      // manual jog: -1 L, +1 R, 0 none
volatile uint32_t panCmdUntil = 0;
volatile int  g_panDir     = 0;                      // what pan is doing (UI)
volatile bool panHome      = false;
float tiltCur = TILT_CENTER;
float panPosDeg = 0;  uint32_t lastPanMs = 0;
int guardDir = 1, guardPhase = 0;  uint32_t guardPhaseUntil = 0;
uint32_t servoBusyUntil = 0;

/* =================== RADAR =================== */
HardwareSerial RadarLSer(1), RadarRSer(2);
struct Radar {
  char tag;  uint8_t buf[64]; int idx = 0;
  volatile int state = 0, movDist = 0, statDist = 0, detDist = 0;
  volatile uint32_t lastFrame = 0, frames = 0;
  bool ok()      { return lastFrame && millis() - lastFrame < 2000; }
  bool present() { return ok() && state != 0; }
  bool moving()  { return ok() && (state == 1 || state == 3); }
  int  dist()    { return state == 1 ? movDist : state == 2 ? statDist : detDist; }
  void feed(uint8_t b) {
    static const uint8_t H[4] = {0xF4, 0xF3, 0xF2, 0xF1};
    if (idx < 4) { if (b == H[idx]) buf[idx++] = b; else idx = (b == 0xF4) ? (buf[0] = b, 1) : 0; return; }
    buf[idx++] = b;
    if (idx < 6) return;
    int total = (buf[4] | (buf[5] << 8)) + 10;
    if (total > (int)sizeof(buf) || total < 17) { idx = 0; return; }
    if (idx < total) return;
    if (buf[total - 4] == 0xF8 && buf[total - 3] == 0xF7 && buf[total - 2] == 0xF6 && buf[total - 1] == 0xF5 && buf[7] == 0xAA) {
      state = buf[8]; movDist = buf[9] | (buf[10] << 8);
      statDist = buf[12] | (buf[13] << 8); detDist = buf[15] | (buf[16] << 8);
      lastFrame = millis(); frames++;
    }
    idx = 0;
  }
};
Radar radL{'L'}, radR{'R'};
volatile bool latchL = false, latchR = false;

httpd_handle_t webHttpd = NULL, streamHttpd = NULL;

/* =================== HELPERS =================== */
void led(uint8_t r, uint8_t g, uint8_t b) {
#if LED_PIN >= 0
  neopixelWrite(LED_PIN, r, g, b);
#endif
}
String cleanName(String n) {
  String o; for (char c : n) if (isalnum(c) || c == ' ' || c == '_' || c == '-') o += c;
  o.trim(); if (o.length() > 20) o = o.substring(0, 20); return o;
}
String timeStamp() {
  time_t now = time(nullptr);
  if (now > 1600000000) { struct tm t; localtime_r(&now, &t); char b[24]; strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &t); return String(b); }
  return "uptime " + String(millis() / 1000) + "s";
}
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* =================== LOG (web ring + SD /log.csv) =================== */
void logEvent(const String &kind, const String &name, int dist) {
  xSemaphoreTake(frameMux, portMAX_DELAY);
  LogEntry &e = logBuf[logHead];
  e.t = millis(); strlcpy(e.kind, kind.c_str(), sizeof(e.kind));
  strlcpy(e.name, name.c_str(), sizeof(e.name)); e.d = dist;
  logHead = (logHead + 1) % LOG_SIZE; if (logCount < LOG_SIZE) logCount++;
  xSemaphoreGive(frameMux);
  Serial.printf("[EVENT] %s %s %dcm\n", kind.c_str(), name.c_str(), dist);
  if (!sdOk) return;
  xSemaphoreTake(sdMux, portMAX_DELAY);
  File f = SD_MMC.open("/log.csv", FILE_APPEND);
  if (f) { f.printf("%s,%s,%s,%d\n", timeStamp().c_str(), kind.c_str(), name.c_str(), dist); f.close(); }
  xSemaphoreGive(sdMux);
}

bool saveSnapshot(const char *prefix) {
  if (!sdOk) return false;
  uint8_t *b = nullptr; size_t len = 0;
  xSemaphoreTake(frameMux, portMAX_DELAY);
  if (jpgBuf && jpgLen) { b = (uint8_t *)ps_malloc(jpgLen); if (b) { memcpy(b, jpgBuf, jpgLen); len = jpgLen; } }
  xSemaphoreGive(frameMux);                 // release the frame lock before the slow SD write
  if (!b) return false;
  bool ok = false;
  xSemaphoreTake(sdMux, portMAX_DELAY);
  char path[48]; snprintf(path, sizeof(path), "/snaps/%s_%05lu.jpg", prefix, (unsigned long)snapCount++);
  File f = SD_MMC.open(path, FILE_WRITE);
  if (f) { f.write(b, len); f.close(); ok = true; }
  xSemaphoreGive(sdMux);
  free(b);
  return ok;
}

/* =================== FACE DATABASE on SD (/faces.db) =================== */
String listJson() {
  String s = "[";
  std::vector<face_info_t> ids = recognizer.get_enrolled_ids();
  for (size_t i = 0; i < ids.size(); i++) {
    if (i) s += ",";
    s += "{\"id\":" + String(ids[i].id) + ",\"n\":\"" + String(ids[i].name.c_str()) + "\"}";
  }
  return s + "]";
}
void saveDB() {
  if (!sdOk) return;
  std::vector<face_info_t> ids = recognizer.get_enrolled_ids();
  xSemaphoreTake(sdMux, portMAX_DELAY);
  File f = SD_MMC.open("/faces.db", FILE_WRITE);
  if (!f) { xSemaphoreGive(sdMux); Serial.println("DB save failed"); return; }
  f.write((const uint8_t *)"FDB1", 4);
  uint16_t n = ids.size(); f.write((uint8_t *)&n, 2);
  for (auto &fi : ids) {
    dl::Tensor<float> &e = recognizer.get_face_emb(fi.id);
    uint16_t len = e.get_size();
    uint8_t nl = min((int)fi.name.size(), 31);
    f.write(&nl, 1); f.write((const uint8_t *)fi.name.c_str(), nl);
    f.write((uint8_t *)&len, 2); f.write((uint8_t *)e.get_element_ptr(), len * sizeof(float));
  }
  f.close();
  xSemaphoreGive(sdMux);
}
void loadDB() {
  if (!sdOk) return;
  File f = SD_MMC.open("/faces.db", FILE_READ);
  if (!f) return;
  char magic[4]; uint16_t n = 0;
  if (f.read((uint8_t *)magic, 4) == 4 && !memcmp(magic, "FDB1", 4) && f.read((uint8_t *)&n, 2) == 2) {
    for (int i = 0; i < n; i++) {
      uint8_t nl; char name[32] = {0}; uint16_t len;
      if (f.read(&nl, 1) != 1 || nl > 31) break;
      f.read((uint8_t *)name, nl);
      if (f.read((uint8_t *)&len, 2) != 2 || len == 0 || len > 2048) break;
      float *b = (float *)heap_caps_malloc(len * sizeof(float), MALLOC_CAP_SPIRAM);
      if (!b || f.read((uint8_t *)b, len * sizeof(float)) != (int)(len * sizeof(float))) break;
      dl::Tensor<float> t; t.set_element(b).set_shape({len}).set_auto_free(false);
      recognizer.enroll_id(t, std::string(name), false);
    }
  }
  f.close();
  Serial.printf("Loaded %d faces from SD\n", recognizer.get_enrolled_id_num());
}

/* =================== SERVOS =================== */
static inline uint32_t usToDuty(int us) {
  if (us < SERVO_MIN_US) us = SERVO_MIN_US;
  if (us > SERVO_MAX_US) us = SERVO_MAX_US;
  return (uint32_t)(((uint64_t)us * 16384ULL) / 20000ULL);   // 14-bit @ 50Hz (ESP32-S3 max)
}
void servoWriteRawUs(ledc_channel_t ch, int us) {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, usToDuty(us));
  ledc_update_duty(LEDC_LOW_SPEED_MODE, ch);
}
void servoWriteAngle(ledc_channel_t ch, float deg, bool invert) {
  if (invert) deg = 180.0f - deg;
  if (deg < 0) deg = 0; if (deg > 180) deg = 180;
  servoWriteRawUs(ch, SERVO_MIN_US + (int)((deg / 180.0f) * (SERVO_MAX_US - SERVO_MIN_US)));
}
void servoSetup() {
  ledc_timer_config_t t; memset(&t, 0, sizeof(t));
  t.speed_mode = LEDC_LOW_SPEED_MODE; t.timer_num = SERVO_TIMER;
  t.duty_resolution = LEDC_TIMER_14_BIT; t.freq_hz = 50; t.clk_cfg = LEDC_AUTO_CLK;
  if (ledc_timer_config(&t) != ESP_OK) Serial.println("[SERVO] timer cfg failed");
  ledc_channel_config_t c; memset(&c, 0, sizeof(c));
  c.speed_mode = LEDC_LOW_SPEED_MODE; c.timer_sel = SERVO_TIMER; c.intr_type = LEDC_INTR_DISABLE; c.duty = 0; c.hpoint = 0;
  c.channel = PAN_LEDC_CH;  c.gpio_num = PAN_PIN;  ledc_channel_config(&c);
  c.channel = TILT_LEDC_CH; c.gpio_num = TILT_PIN; ledc_channel_config(&c);
}

/* =================== GUARD + PAN CONTROL (continuous pan, dead reckoning) ===== */
int guardPanDir(uint32_t now) {
  if (now >= guardPhaseUntil) {
    if (guardPhase == 0) { guardPhase = 1; guardPhaseUntil = now + GUARD_DWELL_MS; }   // move -> dwell
    else {                                                                              // dwell -> next move
      guardPhase = 0; guardPhaseUntil = now + GUARD_MOVE_MS;
      bool l = radL.moving(), r = radR.moving();                 // radar read ONLY while stopped
      if (l && !r)      guardDir = -1;
      else if (r && !l) guardDir = +1;
      else if (l && r)  guardDir = (radL.dist() <= radR.dist()) ? -1 : +1;
      else { if (panPosDeg >= PAN_SWEEP_RANGE) guardDir = -1; else if (panPosDeg <= -PAN_SWEEP_RANGE) guardDir = +1; }
    }
  }
  return (guardPhase == 0) ? guardDir : 0;
}
void panControlTick(uint32_t now) {
  uint32_t dt = now - lastPanMs; if (dt > 120) dt = 120; lastPanMs = now;
  int dir = 0, offUs = PAN_JOG_US;
  if (g_mode == 1) { offUs = PAN_SWEEP_US; dir = guardPanDir(now); }
  else if (panHome) { float ap = panPosDeg < 0 ? -panPosDeg : panPosDeg;
    if (ap <= PAN_HOME_TOL) { panHome = false; dir = 0; } else dir = (panPosDeg > 0) ? -1 : +1; }
  else dir = (now < panCmdUntil) ? panCmdDir : 0;
  if (dir < 0 && panPosDeg <= -PAN_LIMIT_DEG) dir = 0;
  if (dir > 0 && panPosDeg >=  PAN_LIMIT_DEG) dir = 0;
  if (dir != 0) { panPosDeg += dir * (offUs * PAN_DPS_PER_US) * (dt / 1000.0f); servoBusyUntil = now + RADAR_BLANK_MS; }
  g_panDir = dir;
  servoWriteRawUs(PAN_LEDC_CH, PAN_STOP_US + (dir == 0 ? 0 : (PAN_INVERT ? -dir : dir) * offUs));
}
void servoTick(uint32_t now) {
  float tt = (float)tiltTarget;
  if (tiltCur < tt) tiltCur = min(tt, tiltCur + TILT_SLEW); else if (tiltCur > tt) tiltCur = max(tt, tiltCur - TILT_SLEW);
  servoWriteAngle(TILT_LEDC_CH, tiltCur, TILT_INVERT);
  panControlTick(now);
}

/* =================== RADAR POLL (+ guard logging) =================== */
void readRadars() {
  while (RadarLSer.available()) radL.feed(RadarLSer.read());
  while (RadarRSer.available()) radR.feed(RadarRSer.read());
  if (millis() > servoBusyUntil) {                 // ignore while the head moves
    bool a = radL.present(), b = radR.present();
    if (a && !latchL) logEvent("radar-L", String(radL.dist()) + "cm", radL.dist());
    if (b && !latchR) logEvent("radar-R", String(radR.dist()) + "cm", radR.dist());
    latchL = a; latchR = b;
  }
}

/* =================== COMMANDS =================== */
// servo/guard/recog are applied instantly here; heavy ones (face/SD) queue to loop()
void handleCmd(const String &cRaw) {
  String c = cRaw; c.trim();
  if (c == "left")  { g_mode = 0; panHome = false; panCmdDir = -1; panCmdUntil = millis() + PAN_CMD_MS; return; }
  if (c == "right") { g_mode = 0; panHome = false; panCmdDir = +1; panCmdUntil = millis() + PAN_CMD_MS; return; }
  if (c == "up")    { g_mode = 0; tiltTarget = clampi(tiltTarget + TILT_STEP_DEG, TILT_MIN_DEG, TILT_MAX_DEG); return; }
  if (c == "down")  { g_mode = 0; tiltTarget = clampi(tiltTarget - TILT_STEP_DEG, TILT_MIN_DEG, TILT_MAX_DEG); return; }
  if (c == "center"){ g_mode = 0; panCmdDir = 0; panCmdUntil = 0; panHome = true; tiltTarget = TILT_CENTER; return; }
  if (c == "guard1"){ g_mode = 1; return; }
  if (c == "guard0"){ g_mode = 0; return; }
  if (c == "recog") { recogOn = !recogOn; return; }
  // everything else runs in loop()
  xSemaphoreTake(cmdMux, portMAX_DELAY);
  if (cmdQueue.size() < 16) cmdQueue.push_back(c);
  xSemaphoreGive(cmdMux);
}
void runHeavyCmd(String c) {
  if (c.startsWith("E,") || c == "E") {                 // enroll
    String n = cleanName(c.length() > 2 ? c.substring(2) : "");
    if (n == "") n = "Person " + String(recognizer.get_enrolled_id_num() + 1);
    enrollName = n; enrollPending = true; enrollDeadline = millis() + 10000;
    logEvent("enroll?", n, 0);
  } else if (c.startsWith("X,")) {                       // delete id
    int id = c.substring(2).toInt(); recognizer.delete_id(id, false); saveDB();
    logEvent("delete", "#" + String(id), 0);
  } else if (c == "CLR") { recognizer.clear_id(false); saveDB(); logEvent("clear-all", "", 0); }
  else if (c == "P")     { logEvent(saveSnapshot("snap") ? "snapshot" : "snap-fail", "", 0); }
  else if (c == "FLIP")  { flipOn = !flipOn; sensor_t *s = esp_camera_sensor_get(); s->set_vflip(s, flipOn); s->set_hmirror(s, flipOn); }
  else if (c == "clearlog") { xSemaphoreTake(frameMux, portMAX_DELAY); logHead = 0; logCount = 0; xSemaphoreGive(frameMux); }
}

/* =================== WEB PAGE =================== */
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no"><title>HomeCam</title>
<style>
:root{--bg:#0d0f14;--card:#171a21;--acc:#2f81f7;--txt:#e6e6e6;--mut:#8b94a3;--warn:#d29922}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--txt);font-family:system-ui,Arial,sans-serif}
header{padding:10px 14px;font-weight:700;font-size:18px;display:flex;justify-content:space-between;align-items:center}
#wrap{max-width:720px;margin:0 auto;padding:0 10px 30px}.card{background:var(--card);border-radius:12px;padding:12px;margin:10px 0}
#vid{width:100%;border-radius:10px;background:#000;display:block;aspect-ratio:4/3;object-fit:contain}
.pad{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;max-width:270px;margin:4px auto 0}
button{background:#222733;color:var(--txt);border:1px solid #2b3240;border-radius:10px;padding:14px;font-size:16px;cursor:pointer;touch-action:none;-webkit-user-select:none;user-select:none}
button:active{background:var(--acc)}.wide{width:100%;padding:13px}
.row{display:flex;gap:8px;flex-wrap:wrap}.pill{flex:1;min-width:110px;background:#222733;border-radius:10px;padding:10px;text-align:center}
.dot{display:inline-block;width:10px;height:10px;border-radius:50%;background:#444;margin-right:6px}.on{background:var(--warn);box-shadow:0 0 8px var(--warn)}
#gBtn.g{background:var(--warn);border-color:var(--warn);color:#000}#rBtn.g{background:#2ea043;border-color:#2ea043;color:#000}
#log{max-height:200px;overflow:auto;white-space:pre-wrap;font:13px/1.5 monospace;color:var(--mut)}
.mut{color:var(--mut);font-size:13px}a{color:var(--acc)}input{padding:10px;border-radius:8px;border:1px solid #2b3240;background:#0d0f14;color:#fff;width:55%}
ul{list-style:none;padding:0;margin:8px 0}li{display:flex;justify-content:space-between;align-items:center;padding:4px 0}
.x{background:#5a1f1f;border-color:#7a2a2a;padding:6px 10px}
</style></head><body>
<header><span>&#127968; HomeCam</span><span id=st class=mut>&hellip;</span></header>
<div id=wrap>
 <div class=card><img id=vid alt="camera"></div>
 <div class=card>
  <div class=pad><span></span><button id=bu>&#9650;</button><span></span>
   <button id=bl>&#9664;</button><button id=bc>&#9632;</button><button id=br>&#9654;</button>
   <span></span><button id=bd>&#9660;</button><span></span></div>
  <div class=mut style="text-align:center;margin-top:8px">Pan <span id=pan>&#9632;</span> &middot; Tilt <span id=tilt>90</span>&deg; &middot; <span id=fps>0</span>fps</div>
 </div>
 <div class=card>
  <button id=gBtn class=wide>Guard Mode: OFF</button>
  <div class=row style="margin-top:10px">
   <div class=pill><span class=dot id=ld></span>Left<br><span id=lv class=mut>&mdash;</span></div>
   <div class=pill><span class=dot id=rd></span>Right<br><span id=rv class=mut>&mdash;</span></div>
  </div>
 </div>
 <div class=card>
  <div class=big id=who style="font-size:20px;font-weight:700">No face</div>
  <div class=row style="margin-top:8px">
   <input id=nm placeholder="name to enroll"><button id=enr>Enroll</button>
   <button id=rBtn>Recog</button><button id=snap>&#128247; Snap</button><button id=flip>Flip</button>
  </div>
  <ul id=faces></ul>
  <div class=mut>SD: <span id=sd>?</span> &middot; <a href="/log" target=_blank>full log</a> &middot; <a href="#" id=clrAll style="color:#e5534b">clear all faces</a></div>
 </div>
 <div class=card>
  <div style="display:flex;justify-content:space-between"><b>Activity</b><span id=clr class=mut style="cursor:pointer">clear</span></div>
  <div id=log>&mdash;</div>
 </div>
</div>
<script>
var H=location.hostname,$=function(i){return document.getElementById(i);};
var vid=$('vid');
function startStream(){vid.src='http://'+H+':81/stream?x='+Date.now();}
vid.onerror=function(){setTimeout(startStream,1500);};
function cmd(q){fetch('/cmd?c='+encodeURIComponent(q)).catch(function(){});}
function hold(id,q){var el=$(id),iv=null;function go(){cmd(q);}
 el.addEventListener('pointerdown',function(e){e.preventDefault();go();iv=setInterval(go,160);});
 function s(){if(iv){clearInterval(iv);iv=null;}}
 el.addEventListener('pointerup',s);el.addEventListener('pointerleave',s);el.addEventListener('pointercancel',s);}
hold('bu','up');hold('bd','down');hold('bl','left');hold('br','right');
$('bc').onclick=function(){cmd('center');};
$('gBtn').onclick=function(){cmd($('gBtn').classList.contains('g')?'guard0':'guard1');};
$('rBtn').onclick=function(){cmd('recog');};
$('snap').onclick=function(){cmd('P');};
$('flip').onclick=function(){cmd('FLIP');};
$('enr').onclick=function(){cmd('E,'+$('nm').value);$('nm').value='';};
$('clrAll').onclick=function(e){e.preventDefault();if(confirm('Delete ALL enrolled faces?'))cmd('CLR');};
$('clr').onclick=function(){cmd('clearlog');};
function up(p,o){var d=$(p+'d'),v=$(p+'v');if(!o||!o.rx){d.className='dot';v.style.color='#e5534b';v.textContent='no data';return;}
 v.style.color='';if(o.present){d.className='dot on';v.textContent=(o.moving?'moving ':'still ')+o.d+'cm';}else{d.className='dot';v.textContent='clear';}}
function ago(t,n){var s=Math.max(0,Math.round((n-t)/1000));return s<1?'now':(s<60?s+'s':Math.floor(s/60)+'m')+' ago';}
function poll(){fetch('/status').then(function(r){return r.json();}).then(function(j){
 $('st').textContent=(j.mode?'GUARD':'manual')+' · '+j.ip;
 $('pan').textContent=(j.panspin<0?'◄':(j.panspin>0?'►':'■'))+' '+j.panpos+'°';
 $('tilt').textContent=j.tilt;$('fps').textContent=j.fps;
 var g=$('gBtn');g.textContent='Guard Mode: '+(j.mode?'ON':'OFF');g.className='wide'+(j.mode?' g':'');
 var rb=$('rBtn');rb.className=j.recog?'g':'';rb.textContent=j.recog?'Recog ON':'Detect';
 $('who').textContent=j.faces?(j.name?j.name+' '+j.sim+'%':'Unknown face'):'No face';
 $('sd').textContent=j.sd?'ok':'no card';
 up('l',j.left);up('r',j.right);
 $('faces').innerHTML=(j.list||[]).map(function(f){return '<li>#'+f.id+' '+f.n+' <button class=x onclick="cmd(\'X,'+f.id+'\')">x</button></li>';}).join('')||'<li class=mut>none enrolled</li>';
 var h='';if(j.log&&j.log.length){for(var i=0;i<j.log.length;i++){var e=j.log[i];h+=ago(e.t,j.now)+'  '+e.k+(e.n?' '+e.n:'')+(e.d?' '+e.d+'cm':'')+'\n';}}else h='no activity yet';
 $('log').textContent=h;
}).catch(function(){});}
startStream();setInterval(poll,700);poll();
</script></body></html>)HTML";

/* =================== WEB HANDLERS =================== */
static esp_err_t indexHandler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}
static esp_err_t statusHandler(httpd_req_t *req) {
  xSemaphoreTake(frameMux, portMAX_DELAY);
  String s = statusJson;
  xSemaphoreGive(frameMux);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, s.c_str(), s.length());
}
static String urlDecode(const char *s) {
  String o;
  for (; *s; s++) {
    if (*s == '%' && s[1] && s[2]) { char h[3] = {s[1], s[2], 0}; o += (char)strtol(h, nullptr, 16); s += 2; }
    else if (*s == '+') o += ' '; else o += *s;
  }
  return o;
}
static esp_err_t cmdHandler(httpd_req_t *req) {
  char q[128], v[100];
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK && httpd_query_key_value(q, "c", v, sizeof(v)) == ESP_OK)
    handleCmd(urlDecode(v));
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, "ok", 2);
}
static uint8_t *copyLatest(size_t &len) {
  uint8_t *out = nullptr; len = 0;
  xSemaphoreTake(frameMux, portMAX_DELAY);
  if (jpgBuf && jpgLen) { out = (uint8_t *)ps_malloc(jpgLen); if (out) { memcpy(out, jpgBuf, jpgLen); len = jpgLen; } }
  xSemaphoreGive(frameMux);
  return out;
}
static esp_err_t jpgHandler(httpd_req_t *req) {
  size_t len; uint8_t *b = copyLatest(len);
  if (!b) { httpd_resp_send_500(req); return ESP_FAIL; }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  esp_err_t r = httpd_resp_send(req, (const char *)b, len); free(b); return r;
}
static esp_err_t streamHandler(httpd_req_t *req) {
  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frm");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  uint32_t sent = 0;
  while (true) {
    if (frameNo == sent) { vTaskDelay(10 / portTICK_PERIOD_MS); continue; }
    size_t len; uint8_t *b = copyLatest(len);
    if (!b) { vTaskDelay(20 / portTICK_PERIOD_MS); continue; }
    sent = frameNo;
    char hdr[96]; int hl = snprintf(hdr, sizeof(hdr), "\r\n--frm\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", (unsigned)len);
    esp_err_t r = httpd_resp_send_chunk(req, hdr, hl);
    if (r == ESP_OK) r = httpd_resp_send_chunk(req, (const char *)b, len);
    free(b);
    if (r != ESP_OK) break;
  }
  return ESP_OK;
}
static esp_err_t logHandler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/plain");
  if (!sdOk) return httpd_resp_send(req, "No SD card", HTTPD_RESP_USE_STRLEN);
  File f = SD_MMC.open("/log.csv", FILE_READ);
  if (!f) return httpd_resp_send(req, "Log empty", HTTPD_RESP_USE_STRLEN);
  if (f.size() > 16384) f.seek(f.size() - 16384);
  char buf[1024];
  httpd_resp_sendstr_chunk(req, "time,event,name,dist_cm\n");
  int n; while ((n = f.read((uint8_t *)buf, sizeof(buf))) > 0) httpd_resp_send_chunk(req, buf, n);
  f.close();
  return httpd_resp_send_chunk(req, NULL, 0);
}
void startServers() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.max_uri_handlers = 10; cfg.stack_size = 8192;
  httpd_uri_t u[] = {
    {"/", HTTP_GET, indexHandler, NULL}, {"/status", HTTP_GET, statusHandler, NULL},
    {"/cmd", HTTP_GET, cmdHandler, NULL}, {"/jpg", HTTP_GET, jpgHandler, NULL},
    {"/log", HTTP_GET, logHandler, NULL}};
  if (httpd_start(&webHttpd, &cfg) == ESP_OK) for (auto &x : u) httpd_register_uri_handler(webHttpd, &x);
  cfg.server_port = 81; cfg.ctrl_port = 32769;
  httpd_uri_t st = {"/stream", HTTP_GET, streamHandler, NULL};
  if (httpd_start(&streamHttpd, &cfg) == ESP_OK) httpd_register_uri_handler(streamHttpd, &st);
}

/* =================== DRAW BOX =================== */
void drawBox(fb_data_t *fb, int x0, int y0, int x1, int y1, uint32_t col, const String &label) {
  x0 = constrain(x0, 0, fb->width - 1); x1 = constrain(x1, 0, fb->width - 1);
  y0 = constrain(y0, 0, fb->height - 1); y1 = constrain(y1, 0, fb->height - 1);
  int w = x1 - x0 + 1, h = y1 - y0 + 1;
  for (int t = 0; t < 2; t++) {
    if (w - 2 * t <= 0 || h - 2 * t <= 0) break;
    fb_gfx_drawFastHLine(fb, x0 + t, y0 + t, w - 2 * t, col);
    fb_gfx_drawFastHLine(fb, x0 + t, y1 - t, w - 2 * t, col);
    fb_gfx_drawFastVLine(fb, x0 + t, y0 + t, h - 2 * t, col);
    fb_gfx_drawFastVLine(fb, x1 - t, y0 + t, h - 2 * t, col);
  }
  if (label.length()) { int ty = y0 - 10; if (ty < 0) ty = y1 + 2; if (ty > fb->height - 9) ty = fb->height - 9;
    fb_gfx_print(fb, x0, ty, col, label.c_str()); }
}

/* =================== CONTROL TASK (core 0): radars + servos =================== */
void controlTask(void *) {
  uint32_t lastServo = 0;
  for (;;) {
    readRadars();
    uint32_t now = millis();
    if (now - lastServo >= SERVO_TICK_MS) { lastServo = now; servoTick(now); }
    // status LED: amber=guard, green=idle, white-ish on a face elsewhere set in loop
    static uint32_t lastLed = 0;
    if (now - lastLed > 400) { lastLed = now;
      if (g_mode == 1) { bool hit = radL.present() || radR.present(); led(hit ? 60 : 30, hit ? 25 : 18, 0); }
      else if (lastFaces) led(0, 25, 0); else led(0, 6, 2); }
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}

/* =================== SETUP =================== */
void setup() {
  Serial.begin(115200); delay(300);
  Serial.println("\n== HomeCam (face build) booting ==");
  led(10, 0, 10);
  frameMux = xSemaphoreCreateMutex(); cmdMux = xSemaphoreCreateMutex(); sdMux = xSemaphoreCreateMutex();

  if (!psramFound()) Serial.println("!! NO PSRAM - set Tools > PSRAM > OPI PSRAM");

  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0; c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM; c.pin_d1 = Y3_GPIO_NUM; c.pin_d2 = Y4_GPIO_NUM; c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM; c.pin_d5 = Y7_GPIO_NUM; c.pin_d6 = Y8_GPIO_NUM; c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM; c.pin_pclk = PCLK_GPIO_NUM; c.pin_vsync = VSYNC_GPIO_NUM; c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM; c.pin_sccb_scl = SIOC_GPIO_NUM; c.pin_pwdn = PWDN_GPIO_NUM; c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_RGB565;        // raw pixels for the face AI
  c.frame_size = FRAMESIZE_QVGA;            // 320x240 = the size the face models need
  c.fb_location = CAMERA_FB_IN_PSRAM; c.fb_count = 2; c.grab_mode = CAMERA_GRAB_LATEST;
  if (esp_camera_init(&c) != ESP_OK) {
    Serial.println("!! Camera init failed - check the ribbon cable");
    while (true) { led(50, 0, 0); delay(300); led(0, 0, 0); delay(300); }
  }
  sensor_t *s = esp_camera_sensor_get();
  if (s->id.PID == OV3660_PID) { s->set_vflip(s, 1); s->set_brightness(s, 1); s->set_saturation(s, -2); flipOn = true; }
  rgb = (uint8_t *)ps_malloc(320 * 240 * 3);

  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
  sdOk = SD_MMC.begin("/sdcard", true);
  if (sdOk) { SD_MMC.mkdir("/snaps"); Serial.println("SD OK"); } else Serial.println("SD not found (logging off)");

  servoSetup();
  servoWriteRawUs(PAN_LEDC_CH, PAN_STOP_US);            // pan STOP at boot
  servoWriteAngle(TILT_LEDC_CH, tiltCur, TILT_INVERT);  // tilt centre
  panPosDeg = 0; lastPanMs = millis();

  RadarLSer.begin(256000, SERIAL_8N1, RADAR_L_RX, -1);
  RadarRSer.begin(256000, SERIAL_8N1, RADAR_R_RX, -1);

  WiFi.mode(WIFI_STA); WiFi.setSleep(false); WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WiFi] joining \"%s\" ", WIFI_SSID);
  uint32_t t0 = millis(); bool bl = false;
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) { delay(300); Serial.print("."); bl = !bl; led(0, 0, bl ? 40 : 0); }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) { Serial.print("[WiFi] IP: "); Serial.println(WiFi.localIP());
    Serial.printf("[WiFi] RSSI %d dBm\n", WiFi.RSSI()); }
  else Serial.println("[WiFi] FAILED - check SSID/password at top of sketch");
  if (MDNS.begin(MDNS_HOST)) { MDNS.addService("http", "tcp", 80); Serial.printf("[mDNS] http://%s.local\n", MDNS_HOST); }

  loadDB();
  startServers();
  xTaskCreatePinnedToCore(controlTask, "control", 8192, NULL, 2, NULL, 0);
  Serial.println("== HomeCam ready ==");
}

/* =================== LOOP (core 1): camera + face AI =================== */
void loop() {
  // run queued heavy commands
  while (true) {
    xSemaphoreTake(cmdMux, portMAX_DELAY);
    if (cmdQueue.empty()) { xSemaphoreGive(cmdMux); break; }
    String c = cmdQueue.front(); cmdQueue.pop_front();
    xSemaphoreGive(cmdMux);
    runHeavyCmd(c);
  }
  if (enrollPending && millis() > enrollDeadline) { enrollPending = false; logEvent("enroll-timeout", enrollName, 0); }

  uint32_t t0 = millis();
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { delay(5); return; }
  int w = fb->width, h = fb->height;
  bool ok = fmt2rgb888(fb->buf, fb->len, fb->format, rgb);
  esp_camera_fb_return(fb);
  if (!ok) return;

  std::list<dl::detect::result_t> &cand = s1.infer(rgb, {h, w, 3});
  std::list<dl::detect::result_t> &res  = s2.infer(rgb, {h, w, 3}, cand);

  fb_data_t fbd; fbd.width = w; fbd.height = h; fbd.bytes_per_pixel = 3; fbd.format = FB_BGR888; fbd.data = rgb;
  dl::Tensor<uint8_t> tensor; tensor.set_element(rgb).set_shape({h, w, 3}).set_auto_free(false);

  int faces = res.size(), bestArea = 0, topId = -1, topSim = 0; String topName = ""; int n = 0;
  bool snapUnknown = false;
  for (auto &r : res) {
    int x0 = r.box[0], y0 = r.box[1], x1 = r.box[2], y1 = r.box[3];
    int area = (x1 - x0) * (y1 - y0);
    uint32_t col = C_YELLOW; String label = "";
    if ((recogOn || enrollPending) && n < 3) {
      if (enrollPending && faces == 1) {
        int id = recognizer.enroll_id(tensor, r.keypoint, std::string(enrollName.c_str()), false);
        enrollPending = false;
        if (id >= 0) { saveDB(); logEvent("enrolled", enrollName, 0); saveSnapshot("enroll"); }
        else logEvent("enroll-fail", "", 0);
      }
      face_info_t fi = recognizer.recognize(tensor, r.keypoint);
      int sim = (int)(fi.similarity * 100);
      if (fi.id >= 0) {
        col = C_GREEN; label = String(fi.name.c_str()) + " " + sim + "%";
        uint32_t &ls = lastSeenLog[fi.id];
        if (ls == 0 || millis() - ls > 10000) logEvent("seen", String(fi.name.c_str()), sim);
        ls = millis();
        if (area > bestArea) { topId = fi.id; topSim = sim; topName = String(fi.name.c_str()); }
      } else {
        col = C_RED; label = "Unknown";
        if (millis() - lastUnknownSnap > 15000) { lastUnknownSnap = millis(); logEvent("unknown", "", sim); snapUnknown = true; }
        if (area > bestArea) { topId = -1; topSim = sim; topName = ""; }
      }
    }
    if (area > bestArea) bestArea = area;
    drawBox(&fbd, x0, y0, x1, y1, col, label);
    n++;
  }

  uint8_t *jpg = nullptr; size_t jl = 0;
  if (fmt2jpg(rgb, w * h * 3, w, h, PIXFORMAT_RGB888, 70, &jpg, &jl)) {
    xSemaphoreTake(frameMux, portMAX_DELAY);
    if (jpgBuf) free(jpgBuf);
    jpgBuf = jpg; jpgLen = jl; frameNo++;
    xSemaphoreGive(frameMux);
  }
  if (snapUnknown) saveSnapshot("unknown");

  fps = fps * 0.8f + 0.2f * (1000.0f / max<uint32_t>(1, millis() - t0));
  lastFaces = faces; lastId = topId; lastSim = topSim; lastName = topName;

  // build status JSON (served to the web page)
  static uint32_t lastStat = 0;
  if (millis() - lastStat > 300) {
    lastStat = millis();
    bool wc = WiFi.status() == WL_CONNECTED;
    String ip = wc ? WiFi.localIP().toString() : String("connecting");
    // activity log newest-first
    String logj = "[";
    xSemaphoreTake(frameMux, portMAX_DELAY);
    int cnt = logCount < LOG_SIZE ? logCount : LOG_SIZE;
    for (int i = 0; i < cnt; i++) {
      int idx = (logHead - 1 - i + LOG_SIZE * 2) % LOG_SIZE;
      if (i) logj += ",";
      logj += "{\"t\":" + String(logBuf[idx].t) + ",\"k\":\"" + String(logBuf[idx].kind) + "\",\"n\":\"" + String(logBuf[idx].name) + "\",\"d\":" + String(logBuf[idx].d) + "}";
    }
    xSemaphoreGive(frameMux);
    logj += "]";
    String js = "{\"ip\":\"" + ip + "\",\"mode\":" + g_mode + ",\"panspin\":" + g_panDir + ",\"panpos\":" + (int)panPosDeg +
                ",\"tilt\":" + (int)tiltCur + ",\"fps\":" + String(fps, 1) + ",\"recog\":" + (recogOn ? 1 : 0) +
                ",\"sd\":" + (sdOk ? 1 : 0) + ",\"faces\":" + faces + ",\"name\":\"" + topName + "\",\"sim\":" + topSim +
                ",\"left\":{\"rx\":" + radL.frames + ",\"present\":" + (radL.present() ? "true" : "false") + ",\"moving\":" + (radL.moving() ? "true" : "false") + ",\"d\":" + radL.dist() + "}" +
                ",\"right\":{\"rx\":" + radR.frames + ",\"present\":" + (radR.present() ? "true" : "false") + ",\"moving\":" + (radR.moving() ? "true" : "false") + ",\"d\":" + radR.dist() + "}" +
                ",\"now\":" + (uint32_t)millis() + ",\"list\":" + listJson() + ",\"log\":" + logj + "}";
    xSemaphoreTake(frameMux, portMAX_DELAY);
    statusJson = js;
    xSemaphoreGive(frameMux);
  }
}
