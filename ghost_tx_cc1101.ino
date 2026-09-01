// ############################################################################
//  ghost_tx_cc1101.ino  -  KODI RF CONTROLLER
//
//  Lonely Binary Gold Edition ESP32 (WROOM-32U, external antenna) +
//  4x CC1101 (sub-GHz) on shared HSPI bus +
//  2x SSD1306 OLED (Wire + Wire1) + 1 speaker + 3 LEDs.
//  Buttons via ESP-NOW from ESP32-S3 co-processor.
//
//  ---- BUILD ---------------------------------------------------------------
//  Board:   "ESP32 Dev Module"  (esp32 core). Classic ESP32, NOT S3.
//  Serial:  115200.
//  Libraries (Library Manager):
//     * Adafruit SSD1306  by Adafruit      (pulls in Adafruit GFX)
//  Wire / SPI / Preferences ship with the core.
//  CC1101 driver is built-in (gg_cc1101.h) - no external library needed.
//  nRF24 stub (gg_nrf.h) - no RF24 library needed, no nRF hardware.
//
//  HSPI (all 4 CC1101): SCK=13 / MOSI=14 / MISO=18
//  CS pins: A=5, B=4, C=27, D=25
// ############################################################################

#include <SPI.h>
#include <DNSServer.h>
#include <WebServer.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "gg_config.h"
#include "gg_leds.h"
#include "gg_audio.h"
#include "gg_espnow.h"
#include "gg_input.h"
#include "gg_nrf.h"
#include "gg_cc1101.h"
#include "gg_store.h"
#include "gg_oled2.h"
#include "gg_boot.h"
#include "gg_cc1101_tools.h"
#include "gg_ui.h"

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("==== KODI RF CONTROLLER (4-radio) ===="));
  randomSeed(micros());

  ggLedBegin();

  // NVS: settings + live configs
  ggStoreBegin();
  if (!ggLoadSettings(gSet)) { gSet = ggDefaultSettings(); Serial.println(F("[nvs] default settings")); }
  if (!ggLoadLive(gNrfA, gNrfB)) {
    gNrfA = ggDefaultNrf();
    gNrfB = ggDefaultNrf(); strncpy(gNrfB.name, "NRF-B", sizeof(gNrfB.name)); gNrfB.channel = 100; gNrfB.addr[4] = '2';
    Serial.println(F("[nvs] default nRF configs"));
  }
  if (gNrfA.plLen < 1 || gNrfA.plLen > GG_PAYLOAD_MAX) gNrfA.plLen = 8;
  if (gNrfB.plLen < 1 || gNrfB.plLen > GG_PAYLOAD_MAX) gNrfB.plLen = 8;

  // CC1101 live configs (A and B from NVS, C and D get defaults)
  if (!ggLoadCcLive(gCcA, gCcB)) {
    gCcA = ggDefaultCc();
    gCcB = ggDefaultCc(); strncpy(gCcB.name, "CC1101-B", sizeof(gCcB.name)); gCcB.freqMHz = 315.0f; gCcB.freqBand = GG_CC_315;
    Serial.println(F("[nvs] default CC1101 configs"));
  }
  if (gCcA.plLen < 1 || gCcA.plLen > GG_CC_PAYLOAD_MAX) gCcA.plLen = 5;
  if (gCcB.plLen < 1 || gCcB.plLen > GG_CC_PAYLOAD_MAX) gCcB.plLen = 5;

  if (!ggLoadWifiTx(gWifiTx)) { gWifiTx = ggDefaultWifiTx(); Serial.println(F("[nvs] default wifi tx")); }
  if (!ggLoadBleTx(gBleTx))   { gBleTx  = ggDefaultBleTx();  Serial.println(F("[nvs] default ble tx"));  }
  if (gWifiTx.channel < 1 || gWifiTx.channel > 14) gWifiTx.channel = 1;
  if (gWifiTx.pps < 10 || gWifiTx.pps > 1000) gWifiTx.pps = 200;
  if (gWifiTx.frameSize > GG_WF_256) gWifiTx.frameSize = GG_WF_256;
  if (gWifiTx.payloadLen < 1 || gWifiTx.payloadLen > 32) gWifiTx.payloadLen = 8;
  if (gBleTx.pps < 1 || gBleTx.pps > 500) gBleTx.pps = 100;
  if (gBleTx.payloadLen < 1 || gBleTx.payloadLen > 26) gBleTx.payloadLen = 8;
  if (gBleTx.power > GG_BP_MAX) gBleTx.power = GG_BP_MAX;
  if (gBleTx.channels > GG_BC_39) gBleTx.channels = GG_BC_ALL;

  // Input (via ESP-NOW) + audio + screens
  ggInputBegin();
  ggAudioBegin();
  ggSetVolume(gSet.volume);
  ggUiBegin();
  if (!ggOledOk) Serial.println(F("[oled] main SSD1306 not found (SDA=21 SCL=22)"));

  // Second OLED on Wire1 (SDA=16, SCL=17)
  ggOled2Begin();
  if (ggOled2Ok) Serial.println(F("[oled2] status display OK on Wire1 (SDA=16 SCL=17)"));
  else Serial.println(F("[oled2] status display not found on Wire1 (optional)"));

  // ESP-NOW link to S3 co-processor
  ggLinkBegin();

  // HSPI bus for all 4 CC1101 radios
  ggCcBegin();
  if (!ggCcPresentA && !ggCcPresentB && !ggCcPresentC && !ggCcPresentD)
    Serial.println(F("[radio] no CC1101 answered - check 3V3, HSPI wiring"));

  // Configure detected CC1101s with saved profiles
  if (ggCcPresentA) ggCcInit(GG_PIN_CCA_CSN, gCcA);
  if (ggCcPresentB) ggCcInit(GG_PIN_CCB_CSN, gCcB);
  // C and D use A's default config for now
  GgCcProfile gCcC_cfg = ggDefaultCc(); strncpy(gCcC_cfg.name, "CC1101-C", sizeof(gCcC_cfg.name));
  GgCcProfile gCcD_cfg = ggDefaultCc(); strncpy(gCcD_cfg.name, "CC1101-D", sizeof(gCcD_cfg.name));
  if (ggCcPresentC) ggCcInit(GG_PIN_CCC_CSN, gCcC_cfg);
  if (ggCcPresentD) ggCcInit(GG_PIN_CCD_CSN, gCcD_cfg);

  int nRadios = (ggCcPresentA?1:0)+(ggCcPresentB?1:0)+(ggCcPresentC?1:0)+(ggCcPresentD?1:0);
  ggLinkRadioCount = nRadios;
  if (nRadios > 0) ggLedStatOn();

  ggBootAnimate(ggOled, ggCcPresentA, ggCcPresentB, ggCcPresentC, ggCcPresentD);

  ggOled2SetFreqs(gCcA.freqMHz, gCcB.freqMHz, gCcC_cfg.freqMHz, gCcD_cfg.freqMHz);
  ggOled2BootAnimate(ggCcPresentA, ggCcPresentB, ggCcPresentC, ggCcPresentD);

  ggClickOk();
  ggHome();
}

static uint32_t ggLastTelemTx = 0;

void loop() {
  ggLinkPump();

  if (millis() - ggLastTelemTx >= 2000) {
    ggLastTelemTx = millis();
    uint8_t wAct = (ggWifiTxTask != nullptr) ? 1 : 0;
    uint8_t bAct = ggBleTxActive ? 1 : 0;
    uint8_t cJam = ggCcJamActive ? 1 : 0;
    uint8_t atk  = (wAct || bAct || cJam || ggCcBruteActive ||
                    ggCcCwActive || ggPortalActive) ? 1 : 0;
    ggLinkSendTelemetry(wAct, bAct, cJam, gCcA.freqMHz, 0, atk, gScreen);
  }

  ggUiLoop();
  ggOled2UpdateStatus();
  if (ggAudioReqPend) {
    ggAudioReqPend = false;
    ggBeep(ggAudioReqFreq, ggAudioReqMs);
  }
  if (ggNewAlert) {
    ggNewAlert = false;
    Serial.printf("[alert] L%d: %s\n", ggLastAlertLvl, ggLastAlertMsg);
  }
  // ---- RF TX from R40T SD card via ESP-NOW ----
  if (ggRfTxPending) {
    ggRfTxPending = false;
    uint8_t txCsn = 0;
    if      (ggCcPresentA) txCsn = GG_PIN_CCA_CSN;
    else if (ggCcPresentB) txCsn = GG_PIN_CCB_CSN;
    else if (ggCcPresentC) txCsn = GG_PIN_CCC_CSN;
    else if (ggCcPresentD) txCsn = GG_PIN_CCD_CSN;
    if (txCsn != 0) {
      for (uint8_t r = 0; r < ggRfTxRepeats; r++) {
        ggCcReplay(txCsn, &ggRfTxRec);
        ggLedTx();
        if (r + 1 < ggRfTxRepeats) delay(50);
      }
      ggCcInit(txCsn, gCcA);
      Serial.printf("[rf_tx] done %.2fMHz %d bytes x%d\n",
                    ggRfTxRec.freqMHz, ggRfTxRec.len, ggRfTxRepeats);
    }
  }

  if (ggVoiceNeuralCmd != 0xFF) {
    uint8_t vcmd = ggVoiceNeuralCmd;
    ggVoiceNeuralCmd = 0xFF;
    for (int i = 0; i < GG_NEURAL_CMD_COUNT; i++) {
      if (ggNeuralCmds[i].cmdId == vcmd) {
        strncpy(ggS2NnCmd, ggNeuralCmds[i].name, sizeof(ggS2NnCmd) - 1);
        ggS2NnCmd[sizeof(ggS2NnCmd) - 1] = '\0';
        break;
      }
    }
    Serial.printf("[voice] EXEC cmd %d\n", vcmd);
    switch (vcmd) {
      case 0:  ggStopTx(); ggHome(); break;
      case 40: ggGo(GS_COP_SCAN); break;
      case 41: ggGo(GS_TRACKER_SCAN); break;
      case 42: ggGo(GS_NET_AUDIT); break;
      case 43: ggGo(GS_SIGNAL_METER); break;
      case 44: ggGo(GS_DEAUTH_DET); break;
      case 45: ggStopTx(); ggHome(); break;
      case 46: ggHome(); break;
      case 50: ggGo(GS_CC_REPLAY); break;
      case 51: ggGo(GS_CC_REPLAY); break;
      case 52: ggGo(GS_CC_JAM); break;
      case 53: ggGo(GS_CC_REC); break;
      case 54: ggGo(GS_CC_BRUTE); break;
      case 55: ggGo(GS_CC_SCAN); break;
      case 56: ggGo(GS_CC_REC); break;
    }
  }
  ggLedUpdate();
  delay(4);
}
