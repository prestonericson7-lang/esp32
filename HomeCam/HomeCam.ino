/* ============================================================================
   HomeCam  —  ESP32-S3-CAM (OV3660, N16R8)  pan/tilt security cam  + SD logging
   ----------------------------------------------------------------------------
   COMPILES ON ANY CURRENT ESP32 CORE (2.x or 3.x) — no special headers, no
   library downloads, nothing to hunt. Open in Arduino IDE, add your WiFi below,
   flash. Reach it on your phone at  http://homecam.local

   What it does:
     - Live MJPEG video (one connection -> instant controls, no lag)
     - Pan (continuous servo, dead-reckoning + soft limits) + Tilt (positional)
     - 2x LD2410C radar for the blind spots, with a guard mode that step-scans
       and points at movement
     - SD CARD: logs every detection to /log.csv and saves a JPEG snapshot to
       /snaps on each new motion event + a manual Snapshot button. View the log
       in the web page; pull the card to see the snapshots.

   NOTE: true on-device FACE RECOGNITION is NOT here — the ESP32 face models
   only exist in a couple of old core versions and can't be added from the SD
   card. This logs WHEN/WHERE motion happens + saves the picture; recognizing
   WHO needs a real computer (Pi/PC) looking at these snapshots. This build is
   the solid, always-compiles foundation.

   Arduino IDE (Tools): Board "ESP32S3 Dev Module", PSRAM "OPI PSRAM",
   Flash 16MB, Partition "16M Flash (3MB APP/9.9MB FATFS)". Insert a FAT32 SD.

   WIRING (your pins): PAN sig->GPIO1  TILT sig->GPIO42  LEFT LD2410C TX->GPIO41
     RIGHT LD2410C TX->GPIO21   servos+radars 5V, all grounds common.
   ========================================================================== */

#include "esp_camera.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include "esp_http_server.h"
#include "driver/ledc.h"
#include "FS.h"
#include "SD_MMC.h"
#include <sys/time.h>

/* =================== PUT YOUR WIFI HERE (lines 47-48) =================== */
const char* WIFI_SSID = "click for virus";
const char* WIFI_PASS = "PUT_YOUR_PASSWORD_HERE";
const char* MDNS_HOST  = "homecam";               // -> http://homecam.local

/* =================== CAMERA PINS (your board) =================== */
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 15
#define SIOD_GPIO_NUM 4
#define SIOC_GPIO_NUM 5
#define Y9_GPIO_NUM 16
#define Y8_GPIO_NUM 17
#define Y7_GPIO_NUM 18
#define Y6_GPIO_NUM 12
#define Y5_GPIO_NUM 10
#define Y4_GPIO_NUM 8
#define Y3_GPIO_NUM 9
#define Y2_GPIO_NUM 11
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM 7
#define PCLK_GPIO_NUM 13

#define SD_CLK  39       // on-board micro-SD (1-bit SD_MMC)
#define SD_CMD  38
#define SD_D0   40
#define LED_PIN 48       // on-board WS2812 status LED

#define PAN_PIN     1    // continuous-rotation pan servo
#define TILT_PIN    42   // positional tilt servo
#define RADAR_L_RX  41   // LEFT  LD2410C TX
#define RADAR_R_RX  21   // RIGHT LD2410C TX

/* =================== TILT (positional) =================== */
#define TILT_MIN_DEG 40
#define TILT_MAX_DEG 140
#define TILT_CENTER  90
#define TILT_INVERT  true     // your tilt reads reversed; set false if it flips
#define SERVO_MIN_US 500
#define SERVO_MAX_US 2500
#define TILT_STEP    4
#define TILT_SLEW    2.0f

/* =================== PAN (continuous: pulse = speed) =================== */
#define PAN_INVERT   false    // flip if "left" spins the camera right
#define PAN_STOP_US  1500     // neutral/STOP (nudge +/-10..30 if it creeps)
#define PAN_JOG_US   180      // manual jog speed
#define PAN_SWEEP_US 130      // guard step speed
#define PAN_CMD_MS   300      // jog keeps moving this long; release => stop
#define PAN_DPS_PER_US 0.25f
#define PAN_LIMIT_DEG 100     // hard soft-limit (anti wind-up)
#define PAN_SWEEP_RANGE 70
#define PAN_HOME_TOL 3
#define GUARD_MOVE_MS 500
#define GUARD_DWELL_MS 450
#define SERVO_TICK_MS 20
#define RADAR_BLANK_MS 700
#define SNAP_COOLDOWN_MS 15000  // min time between auto snapshots per side

#define PAN_LEDC_CH  LEDC_CHANNEL_2
#define TILT_LEDC_CH LEDC_CHANNEL_3
#define SERVO_TIMER  LEDC_TIMER_1

/* =================== GLOBALS =================== */
volatile int  g_mode = 0;                 // 0 = manual, 1 = guard
volatile int  tiltTarget = TILT_CENTER;
volatile int  panCmdDir = 0;              // -1 L, +1 R, 0 none
volatile uint32_t panCmdUntil = 0;
volatile int  g_panDir = 0;
volatile bool panHome = false;
float tiltCur = TILT_CENTER, panPosDeg = 0;
uint32_t lastPanMs = 0, lastServoTick = 0, lastGuardTick = 0;
int guardDir = 1, guardPhase = 0;  uint32_t guardPhaseUntil = 0, servoBusyUntil = 0;

volatile float g_fps = 0;  volatile uint32_t g_servedFrames = 0;
volatile bool g_psram = false;
bool sdOk = false;  uint32_t snapCount = 0;
SemaphoreHandle_t sdMux, logMux;
volatile bool wantSnapManual = false;

httpd_handle_t webHttpd = NULL, streamHttpd = NULL;

HardwareSerial RadarLSer(1), RadarRSer(2);
struct Radar {
  char tag; uint8_t buf[64]; int idx = 0;
  volatile int state = 0, movDist = 0, statDist = 0, detDist = 0;
  volatile uint32_t lastFrame = 0, frames = 0;
  bool prevPresent = false; uint32_t lastSnap = 0;
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

struct LogEntry { uint32_t t; char kind[12]; int d; };
#define LOG_SIZE 30
LogEntry logBuf[LOG_SIZE]; int logHead = 0, logCount = 0;

/* =================== HELPERS =================== */
void led(uint8_t r, uint8_t g, uint8_t b) {
#if LED_PIN >= 0
  neopixelWrite(LED_PIN, r, g, b);
#endif
}
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
String timeStamp() {
  time_t now = time(nullptr);
  if (now > 1600000000) { struct tm t; localtime_r(&now, &t); char b[24]; strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &t); return String(b); }
  return "up" + String(millis() / 1000) + "s";
}

/* =================== SD LOG + SNAPSHOT =================== */
void logEvent(const String &kind, int dist) {
  xSemaphoreTake(logMux, portMAX_DELAY);
  LogEntry &e = logBuf[logHead];
  e.t = millis(); strlcpy(e.kind, kind.c_str(), sizeof(e.kind)); e.d = dist;
  logHead = (logHead + 1) % LOG_SIZE; if (logCount < LOG_SIZE) logCount++;
  xSemaphoreGive(logMux);
  Serial.printf("[EVENT] %s %dcm\n", kind.c_str(), dist);
  if (!sdOk) return;
  xSemaphoreTake(sdMux, portMAX_DELAY);
  File f = SD_MMC.open("/log.csv", FILE_APPEND);
  if (f) { f.printf("%s,%s,%d\n", timeStamp().c_str(), kind.c_str(), dist); f.close(); }
  xSemaphoreGive(sdMux);
}
bool saveSnapshot(const char *prefix) {
  if (!sdOk) return false;
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return false;
  bool ok = false;
  xSemaphoreTake(sdMux, portMAX_DELAY);
  char path[48]; snprintf(path, sizeof(path), "/snaps/%s_%05lu.jpg", prefix, (unsigned long)snapCount++);
  File f = SD_MMC.open(path, FILE_WRITE);
  if (f) { f.write(fb->buf, fb->len); f.close(); ok = true; }
  xSemaphoreGive(sdMux);
  esp_camera_fb_return(fb);
  if (ok) Serial.printf("[SD] saved %s\n", path);
  return ok;
}

/* =================== SERVOS =================== */
static inline uint32_t usToDuty(int us) {
  if (us < SERVO_MIN_US) us = SERVO_MIN_US; if (us > SERVO_MAX_US) us = SERVO_MAX_US;
  return (uint32_t)(((uint64_t)us * 16384ULL) / 20000ULL);     // 14-bit @ 50Hz
}
void servoRawUs(ledc_channel_t ch, int us) {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, usToDuty(us)); ledc_update_duty(LEDC_LOW_SPEED_MODE, ch);
}
void servoAngle(ledc_channel_t ch, float deg, bool invert) {
  if (invert) deg = 180.0f - deg; if (deg < 0) deg = 0; if (deg > 180) deg = 180;
  servoRawUs(ch, SERVO_MIN_US + (int)((deg / 180.0f) * (SERVO_MAX_US - SERVO_MIN_US)));
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

/* =================== GUARD + PAN CONTROL =================== */
int guardPanDir(uint32_t now) {
  if (now >= guardPhaseUntil) {
    if (guardPhase == 0) { guardPhase = 1; guardPhaseUntil = now + GUARD_DWELL_MS; }
    else {
      guardPhase = 0; guardPhaseUntil = now + GUARD_MOVE_MS;
      bool l = radL.moving(), r = radR.moving();
      if (l && !r) guardDir = -1; else if (r && !l) guardDir = +1;
      else if (l && r) guardDir = (radL.dist() <= radR.dist()) ? -1 : +1;
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
  servoRawUs(PAN_LEDC_CH, PAN_STOP_US + (dir == 0 ? 0 : (PAN_INVERT ? -dir : dir) * offUs));
}
void servoTick(uint32_t now) {
  float tt = (float)tiltTarget;
  if (tiltCur < tt) tiltCur = min(tt, tiltCur + TILT_SLEW); else if (tiltCur > tt) tiltCur = max(tt, tiltCur - TILT_SLEW);
  servoAngle(TILT_LEDC_CH, tiltCur, TILT_INVERT);
  panControlTick(now);
}

/* =================== RADAR POLL (+ log + snapshot on new motion) =============== */
void radarPoll() {
  int g = 0; while (RadarLSer.available() && g++ < 256) radL.feed(RadarLSer.read());
  g = 0;     while (RadarRSer.available() && g++ < 256) radR.feed(RadarRSer.read());
  if (millis() > servoBusyUntil) {
    uint32_t now = millis();
    bool a = radL.present(), b = radR.present();
    if (a && !latchL) { logEvent("motion-L", radL.dist()); if (now - radL.lastSnap > SNAP_COOLDOWN_MS) { radL.lastSnap = now; saveSnapshot("left"); } }
    if (b && !latchR) { logEvent("motion-R", radR.dist()); if (now - radR.lastSnap > SNAP_COOLDOWN_MS) { radR.lastSnap = now; saveSnapshot("right"); } }
    latchL = a; latchR = b;
  }
  if (wantSnapManual) { wantSnapManual = false; logEvent(saveSnapshot("snap") ? "snapshot" : "snap-fail", 0); }
}

/* =================== COMMANDS (servo instant; snapshot deferred) =============== */
void handleCmd(const String &cRaw) {
  String c = cRaw; c.trim();
  if (c == "left")   { g_mode = 0; panHome = false; panCmdDir = -1; panCmdUntil = millis() + PAN_CMD_MS; }
  else if (c == "right") { g_mode = 0; panHome = false; panCmdDir = +1; panCmdUntil = millis() + PAN_CMD_MS; }
  else if (c == "up")    { g_mode = 0; tiltTarget = clampi(tiltTarget + TILT_STEP, TILT_MIN_DEG, TILT_MAX_DEG); }
  else if (c == "down")  { g_mode = 0; tiltTarget = clampi(tiltTarget - TILT_STEP, TILT_MIN_DEG, TILT_MAX_DEG); }
  else if (c == "center"){ g_mode = 0; panCmdDir = 0; panCmdUntil = 0; panHome = true; tiltTarget = TILT_CENTER; }
  else if (c == "guard1"){ g_mode = 1; }
  else if (c == "guard0"){ g_mode = 0; }
  else if (c == "snap")  { wantSnapManual = true; }
  else if (c == "clearlog") { xSemaphoreTake(logMux, portMAX_DELAY); logHead = 0; logCount = 0; xSemaphoreGive(logMux); }
}

/* =================== WEB PAGE =================== */
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no"><title>HomeCam</title>
<style>:root{--bg:#0d0f14;--card:#171a21;--acc:#2f81f7;--txt:#e6e6e6;--mut:#8b94a3;--warn:#d29922}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--txt);font-family:system-ui,Arial,sans-serif}
header{padding:10px 14px;font-weight:700;font-size:18px;display:flex;justify-content:space-between;align-items:center}
#wrap{max-width:720px;margin:0 auto;padding:0 10px 30px}.card{background:var(--card);border-radius:12px;padding:12px;margin:10px 0}
#vid{width:100%;border-radius:10px;background:#000;display:block;aspect-ratio:4/3;object-fit:contain}
.pad{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;max-width:270px;margin:4px auto 0}
button{background:#222733;color:var(--txt);border:1px solid #2b3240;border-radius:10px;padding:15px;font-size:17px;cursor:pointer;touch-action:none;-webkit-user-select:none;user-select:none}
button:active{background:var(--acc)}.wide{width:100%;padding:13px}
.row{display:flex;gap:8px;flex-wrap:wrap}.pill{flex:1;min-width:110px;background:#222733;border-radius:10px;padding:10px;text-align:center}
.dot{display:inline-block;width:10px;height:10px;border-radius:50%;background:#444;margin-right:6px}.on{background:var(--warn);box-shadow:0 0 8px var(--warn)}
#gB.g{background:var(--warn);border-color:var(--warn);color:#000}
#log{max-height:200px;overflow:auto;white-space:pre-wrap;font:13px/1.5 monospace;color:var(--mut)}
.mut{color:var(--mut);font-size:13px}a{color:var(--acc)}</style></head><body>
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
  <button id=gB class=wide>Guard Mode: OFF</button>
  <div class=row style="margin-top:10px">
   <div class=pill><span class=dot id=ld></span>Left<br><span id=lv class=mut>&mdash;</span></div>
   <div class=pill><span class=dot id=rd></span>Right<br><span id=rv class=mut>&mdash;</span></div></div>
  <div class=row style="margin-top:10px"><button id=snap class=wide>&#128247; Save Snapshot to SD</button></div>
  <div class=mut style="margin-top:8px">SD card: <span id=sd>?</span> &middot; <a href="/log" target=_blank>view log</a></div>
 </div>
 <div class=card>
  <div style="display:flex;justify-content:space-between"><b>Activity</b><span id=clr class=mut style="cursor:pointer">clear</span></div>
  <div id=log>&mdash;</div></div>
</div>
<script>
var H=location.hostname,$=function(i){return document.getElementById(i);},vid=$('vid');
function startStream(){vid.src='http://'+H+':81/stream?x='+Date.now();}
vid.onerror=function(){setTimeout(startStream,1500);};
function cmd(q){fetch('/cmd?c='+encodeURIComponent(q)).catch(function(){});}
function hold(id,q){var el=$(id),iv=null;function go(){cmd(q);}
 el.addEventListener('pointerdown',function(e){e.preventDefault();go();iv=setInterval(go,160);});
 function s(){if(iv){clearInterval(iv);iv=null;}}
 el.addEventListener('pointerup',s);el.addEventListener('pointerleave',s);el.addEventListener('pointercancel',s);}
hold('bu','up');hold('bd','down');hold('bl','left');hold('br','right');
$('bc').onclick=function(){cmd('center');};
$('gB').onclick=function(){cmd($('gB').classList.contains('g')?'guard0':'guard1');};
$('snap').onclick=function(){cmd('snap');};
$('clr').onclick=function(){cmd('clearlog');};
function up(p,o){var d=$(p+'d'),v=$(p+'v');if(!o||!o.rx){d.className='dot';v.style.color='#e5534b';v.textContent='no data';return;}
 v.style.color='';if(o.present){d.className='dot on';v.textContent=(o.moving?'moving ':'still ')+o.d+'cm';}else{d.className='dot';v.textContent='clear';}}
function ago(t,n){var s=Math.max(0,Math.round((n-t)/1000));return s<1?'now':(s<60?s+'s':Math.floor(s/60)+'m')+' ago';}
function poll(){fetch('/status').then(function(r){return r.json();}).then(function(j){
 $('st').textContent=(j.mode?'GUARD':'manual')+' · '+j.ip;
 $('pan').textContent=(j.panspin<0?'◄':(j.panspin>0?'►':'■'))+' '+j.panpos+'°';
 $('tilt').textContent=j.tilt;$('fps').textContent=j.fps;$('sd').textContent=j.sd?'ready':'no card';
 var g=$('gB');g.textContent='Guard Mode: '+(j.mode?'ON':'OFF');g.className='wide'+(j.mode?' g':'');
 up('l',j.left);up('r',j.right);
 var h='';if(j.log&&j.log.length){for(var i=0;i<j.log.length;i++){var e=j.log[i];h+=ago(e.t,j.now)+'  '+e.k+(e.d?' '+e.d+'cm':'')+'\n';}}else h='no activity yet';
 $('log').textContent=h;
}).catch(function(){});}
startStream();setInterval(poll,800);poll();
</script></body></html>)HTML";

/* =================== WEB HANDLERS =================== */
static esp_err_t indexHandler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}
static String urlDecode(const char *s) {
  String o; for (; *s; s++) {
    if (*s == '%' && s[1] && s[2]) { char h[3] = {s[1], s[2], 0}; o += (char)strtol(h, nullptr, 16); s += 2; }
    else if (*s == '+') o += ' '; else o += *s; }
  return o;
}
static esp_err_t cmdHandler(httpd_req_t *req) {
  char q[128], v[80];
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK && httpd_query_key_value(q, "c", v, sizeof(v)) == ESP_OK)
    handleCmd(urlDecode(v));
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, "ok", 2);
}
static esp_err_t statusHandler(httpd_req_t *req) {
  String logj = "[";
  xSemaphoreTake(logMux, portMAX_DELAY);
  int cnt = logCount < LOG_SIZE ? logCount : LOG_SIZE;
  for (int i = 0; i < cnt; i++) { int idx = (logHead - 1 - i + LOG_SIZE * 2) % LOG_SIZE;
    if (i) logj += ","; logj += "{\"t\":" + String(logBuf[idx].t) + ",\"k\":\"" + String(logBuf[idx].kind) + "\",\"d\":" + String(logBuf[idx].d) + "}"; }
  xSemaphoreGive(logMux);
  logj += "]";
  bool wc = WiFi.status() == WL_CONNECTED;
  String js = "{\"ip\":\"" + (wc ? WiFi.localIP().toString() : String("connecting")) + "\",\"mode\":" + g_mode +
    ",\"panspin\":" + g_panDir + ",\"panpos\":" + (int)panPosDeg + ",\"tilt\":" + (int)tiltCur + ",\"fps\":" + String(g_fps, 1) +
    ",\"sd\":" + (sdOk ? 1 : 0) +
    ",\"left\":{\"rx\":" + radL.frames + ",\"present\":" + (radL.present() ? "true" : "false") + ",\"moving\":" + (radL.moving() ? "true" : "false") + ",\"d\":" + radL.dist() + "}" +
    ",\"right\":{\"rx\":" + radR.frames + ",\"present\":" + (radR.present() ? "true" : "false") + ",\"moving\":" + (radR.moving() ? "true" : "false") + ",\"d\":" + radR.dist() + "}" +
    ",\"now\":" + (uint32_t)millis() + ",\"log\":" + logj + "}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, js.c_str(), js.length());
}
static esp_err_t logHandler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/plain");
  if (!sdOk) return httpd_resp_send(req, "No SD card", HTTPD_RESP_USE_STRLEN);
  xSemaphoreTake(sdMux, portMAX_DELAY);
  File f = SD_MMC.open("/log.csv", FILE_READ);
  if (!f) { xSemaphoreGive(sdMux); return httpd_resp_send(req, "Log empty", HTTPD_RESP_USE_STRLEN); }
  if (f.size() > 16384) f.seek(f.size() - 16384);
  httpd_resp_sendstr_chunk(req, "time,event,dist_cm\n");
  char buf[1024]; int n; while ((n = f.read((uint8_t *)buf, sizeof(buf))) > 0) httpd_resp_send_chunk(req, buf, n);
  f.close(); xSemaphoreGive(sdMux);
  return httpd_resp_send_chunk(req, NULL, 0);
}
static esp_err_t streamHandler(httpd_req_t *req) {
  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frm");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  char hdr[96];
  while (true) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) { vTaskDelay(20 / portTICK_PERIOD_MS); continue; }
    int hl = snprintf(hdr, sizeof(hdr), "\r\n--frm\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", (unsigned)fb->len);
    esp_err_t r = httpd_resp_send_chunk(req, hdr, hl);
    if (r == ESP_OK) r = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    if (r != ESP_OK) break;
    g_servedFrames++;
  }
  return ESP_OK;
}
void startServers() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.max_uri_handlers = 8; cfg.lru_purge_enable = true;
  httpd_uri_t u[] = {
    {"/", HTTP_GET, indexHandler, NULL}, {"/status", HTTP_GET, statusHandler, NULL},
    {"/cmd", HTTP_GET, cmdHandler, NULL}, {"/log", HTTP_GET, logHandler, NULL}};
  if (httpd_start(&webHttpd, &cfg) == ESP_OK) for (auto &x : u) httpd_register_uri_handler(webHttpd, &x);
  cfg.server_port = 81; cfg.ctrl_port = 32769;
  httpd_uri_t st = {"/stream", HTTP_GET, streamHandler, NULL};
  if (httpd_start(&streamHttpd, &cfg) == ESP_OK) httpd_register_uri_handler(streamHttpd, &st);
}

/* =================== SETUP =================== */
void setup() {
  Serial.begin(115200); delay(300);
  Serial.println("\n== HomeCam booting ==");
  led(10, 0, 10);
  sdMux = xSemaphoreCreateMutex(); logMux = xSemaphoreCreateMutex();

  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0; c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM; c.pin_d1 = Y3_GPIO_NUM; c.pin_d2 = Y4_GPIO_NUM; c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM; c.pin_d5 = Y7_GPIO_NUM; c.pin_d6 = Y8_GPIO_NUM; c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM; c.pin_pclk = PCLK_GPIO_NUM; c.pin_vsync = VSYNC_GPIO_NUM; c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM; c.pin_sccb_scl = SIOC_GPIO_NUM; c.pin_pwdn = PWDN_GPIO_NUM; c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_SVGA;         // fb pool sized for SVGA; live view set to VGA below
  c.jpeg_quality = 12; c.fb_count = 2; c.grab_mode = CAMERA_GRAB_LATEST;
  g_psram = psramFound();
  if (!g_psram) { c.frame_size = FRAMESIZE_QVGA; c.fb_location = CAMERA_FB_IN_DRAM; c.grab_mode = CAMERA_GRAB_WHEN_EMPTY; c.fb_count = 1; Serial.println("!! No PSRAM - set OPI PSRAM"); }
  else c.fb_location = CAMERA_FB_IN_PSRAM;
  if (esp_camera_init(&c) != ESP_OK) {
    Serial.println("!! Camera init failed - check ribbon");
    while (true) { led(50, 0, 0); delay(300); led(0, 0, 0); delay(300); }
  }
  sensor_t *s = esp_camera_sensor_get();
  if (s->id.PID == OV3660_PID) { s->set_vflip(s, 1); s->set_brightness(s, 1); s->set_saturation(s, -2); }
  s->set_framesize(s, FRAMESIZE_VGA);    // 640x480 live view (smooth); SD/stream share it
  Serial.printf("[CAM] ready, PSRAM:%s\n", g_psram ? "yes" : "NO");

  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
  sdOk = SD_MMC.begin("/sdcard", true);
  if (sdOk) { SD_MMC.mkdir("/snaps"); Serial.println("[SD] ready"); } else Serial.println("[SD] no card - logging/snaps off");

  servoSetup();
  servoRawUs(PAN_LEDC_CH, PAN_STOP_US);
  servoAngle(TILT_LEDC_CH, tiltCur, TILT_INVERT);
  panPosDeg = 0; lastPanMs = millis();

  RadarLSer.begin(256000, SERIAL_8N1, RADAR_L_RX, -1);
  RadarRSer.begin(256000, SERIAL_8N1, RADAR_R_RX, -1);

  WiFi.mode(WIFI_STA); WiFi.setSleep(false); WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WiFi] joining \"%s\" ", WIFI_SSID);
  uint32_t t0 = millis(); bool bl = false;
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) { delay(300); Serial.print("."); bl = !bl; led(0, 0, bl ? 40 : 0); }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) { Serial.print("[WiFi] IP "); Serial.println(WiFi.localIP());
    Serial.printf("[WiFi] RSSI %d dBm\n", WiFi.RSSI()); led(0, 30, 0); }
  else { Serial.println("[WiFi] FAILED - check SSID/password at top"); led(40, 0, 0); }
  if (MDNS.begin(MDNS_HOST)) { MDNS.addService("http", "tcp", 80); Serial.printf("[mDNS] http://%s.local\n", MDNS_HOST); }

  startServers();
  Serial.println("== HomeCam ready ==");
}

/* =================== LOOP =================== */
void loop() {
  radarPoll();
  uint32_t now = millis();
  if (now - lastServoTick >= SERVO_TICK_MS) { lastServoTick = now; servoTick(now); }

  static uint32_t lastFpsMs = 0, lastFrames = 0;
  if (now - lastFpsMs >= 1000) { uint32_t f = g_servedFrames; g_fps = (float)(f - lastFrames) * 1000.0f / (float)(now - lastFpsMs); lastFrames = f; lastFpsMs = now; }

  static uint32_t lastLed = 0;
  if (WiFi.status() == WL_CONNECTED && now - lastLed > 500) { lastLed = now;
    if (g_mode == 1) { bool hit = radL.present() || radR.present(); led(hit ? 60 : 30, hit ? 25 : 18, 0); } else led(0, 22, 0); }

  static uint32_t lastRadarDbg = 0;
  if (now - lastRadarDbg >= 2000) { lastRadarDbg = now;
    Serial.printf("[RADAR] L rx=%u st=%d %dcm | R rx=%u st=%d %dcm\n", radL.frames, radL.state, radL.dist(), radR.frames, radR.state, radR.dist()); }

  delay(2);
}
