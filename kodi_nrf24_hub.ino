// ############################################################################
//  kodi_nrf24_hub.ino  -  KODI NRF24  /  Hub (Display + Control)
//
//  ESP32-WROOM-32U + 2x SSD1306 OLED + 4x4 matrix keypad + speaker.
//  Controls the Radio Node (Node 2) via ESP-NOW.
//
//  Screen 0 (bus 0, GPIO 21/22) = LIVE STATUS — radio state, counters
//  Screen 1 (bus 1, GPIO 16/17) = UI — menus, config, interaction
//
//  Board:  "ESP32 Dev Module"
//  Serial: 115200
//  Libs:   Adafruit SSD1306, Adafruit GFX, Keypad
//
//  PIN MAP:
//    Status OLED:  SDA=21  SCL=22  (bus 0 / Wire)
//    UI OLED:      SDA=16  SCL=17  (bus 1 / TwoWire(1))
//    Keypad rows:  13, 12, 14, 27
//    Keypad cols:  26, 25, 33, 32
//    Speaker:      GPIO 19  (LEDC PWM)
//    FREE:         GPIO 0, 2, 5, 15, 18, 23
//
//  KEYPAD NAV:
//    2=UP  8=DOWN  5=SELECT  *=BACK  #=HOME
//    Hold 5=selLong  Hold *=HOME
// ############################################################################

#include <DNSServer.h>
#include <WebServer.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "gh_config.h"
#include "gh_audio.h"
#include "gh_input.h"
#include "gh_store.h"
#include "gh_nrf.h"
#include "gh_boot.h"
#include "gh_ui.h"

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("==== KODI NRF24 Edition - HUB ===="));
  randomSeed(micros());

  ggStoreBegin();
  if (!ggLoadSettings(gSet)) { gSet = ggDefaultSettings(); Serial.println(F("[nvs] default settings")); }
  if (!ggLoadLive(gNrfA, gNrfB)) {
    gNrfA = ggDefaultNrf();
    gNrfB = ggDefaultNrf(); strncpy(gNrfB.name, "NRF-B", sizeof(gNrfB.name)); gNrfB.channel = 100; gNrfB.addr[4] = '2';
    Serial.println(F("[nvs] default radio configs"));
  }
  if (gNrfA.plLen < 1 || gNrfA.plLen > GG_PAYLOAD_MAX) gNrfA.plLen = 8;
  if (gNrfB.plLen < 1 || gNrfB.plLen > GG_PAYLOAD_MAX) gNrfB.plLen = 8;
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

  ggInputBegin();
  ggAudioBegin();
  ggSetVolume(gSet.volume);
  ggUiBegin();
  if (!ggOledOk) Serial.println(F("[oled] UI OLED not found (SDA=16 SCL=17)"));
  if (!ggOledStOk) Serial.println(F("[oled] Status OLED not found (SDA=21 SCL=22)"));

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  Serial.printf("[espnow] MAC: %s\n", WiFi.macAddress().c_str());
  ghEspNowBegin();

  ggBootAnimate(ggOled, ggOledSt, ggOledStOk);
  ggClickOk();
  ggHome();
  Serial.println(F("[hub] ready"));
}

void loop() {
  ggUiLoop();
  delay(4);
}
