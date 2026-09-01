// OLED + keypad control panel for the ESP32-S3-CAM face ID board.
// Runs on a separate full-size ESP32-S3 DevKit (N16R8), linked to the
// camera board over a 2-wire UART (see uart_link.h). No WiFi involved here
// at all - this board just talks to the camera board directly.
//
// Wiring (defaults below - confirm against your specific DevKit's
// silkscreen and adjust if it differs):
//   OLED (SSD1306 128x64, I2C addr 0x3C): SDA->GPIO8, SCL->GPIO9, VCC->3V3, GND->GND
//   UART to camera board: this board's GPIO18(TX)->camera board GPIO7(RX)
//                          this board's GPIO17(RX)<-camera board GPIO6(TX)
//                          plus a shared GND between the two boards
//   Keypad (4x4, 8 pins): rows->GPIO4,5,6,7   cols->GPIO15,16,38,39
//
// Avoided on purpose: GPIO0/3/45/46 (strapping), GPIO19/20 (native USB),
// GPIO22-37 (flash/octal PSRAM on N16R8).

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Keypad.h>
#include "uart_link.h"

#define OLED_SDA_PIN  8
#define OLED_SCL_PIN  9
#define OLED_I2C_ADDR 0x3C
#define OLED_W        128
#define OLED_H        64

#define PANEL_UART_TX_PIN 18  // -> camera board RX (GPIO7)
#define PANEL_UART_RX_PIN 17  // <- camera board TX (GPIO6)
#define PANEL_UART_BAUD   921600

const byte KEYPAD_ROWS = 4;
const byte KEYPAD_COLS = 4;
char keymap[KEYPAD_ROWS][KEYPAD_COLS] = {
  { '1', '2', '3', 'A' },
  { '4', '5', '6', 'B' },
  { '7', '8', '9', 'C' },
  { '*', '0', '#', 'D' }
};
byte rowPins[KEYPAD_ROWS] = { 4, 5, 6, 7 };
byte colPins[KEYPAD_COLS] = { 15, 16, 38, 39 };

Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, -1);
Keypad keypad = Keypad(makeKeymap(keymap), rowPins, colPins, KEYPAD_ROWS, KEYPAD_COLS);

enum ScreenState { SCREEN_LIVE,
                    SCREEN_MENU,
                    SCREEN_ENROLL_WAIT,
                    SCREEN_DELETE_ENTRY,
                    SCREEN_CONFIRM_CLEAR };
ScreenState screen = SCREEN_LIVE;

uint8_t previewBitmap[1024];
bool havePreview = false;
uint8_t lastEventStatus = 0;  // 0=no face,1=detected,2=recognized,3=unknown
int8_t lastFaceId = -1;
uint8_t enrolledCount = 0;
bool wifiOn = false;
uint32_t wifiIp = 0;

String deleteEntryBuf = "";
uint32_t lastStatusRequestMs = 0;
uint32_t enrollWaitStartMs = 0;

void requestStatus() {
  uartlink::send(uartlink::CMD_REQ_STATUS, nullptr, 0);
  lastStatusRequestMs = millis();
}

void pollUart() {
  uartlink::Msg msg;
  while (uartlink::poll(msg)) {
    switch (msg.type) {
      case uartlink::MSG_PREVIEW:
        if (msg.len == 1024) {
          memcpy(previewBitmap, msg.payload, 1024);
          havePreview = true;
        }
        break;
      case uartlink::MSG_EVENT:
        if (msg.len >= 3) {
          lastEventStatus = msg.payload[0];
          lastFaceId = (int8_t)msg.payload[1];
          enrolledCount = msg.payload[2];
          if (screen == SCREEN_ENROLL_WAIT && lastEventStatus == 2) {
            screen = SCREEN_LIVE;
          }
        }
        break;
      case uartlink::MSG_STATUS:
        if (msg.len >= 6) {
          wifiOn = msg.payload[0] != 0;
          memcpy(&wifiIp, &msg.payload[1], 4);
          enrolledCount = msg.payload[5];
        }
        break;
      default:
        break;
    }
  }
}

void handleKey(char key) {
  switch (screen) {
    case SCREEN_LIVE:
      if (key == '#') {
        screen = SCREEN_MENU;
      }
      break;

    case SCREEN_MENU:
      if (key == '1') {
        uartlink::send(uartlink::CMD_ENROLL, nullptr, 0);
        screen = SCREEN_ENROLL_WAIT;
        enrollWaitStartMs = millis();
      } else if (key == '2') {
        deleteEntryBuf = "";
        screen = SCREEN_DELETE_ENTRY;
      } else if (key == '3') {
        screen = SCREEN_CONFIRM_CLEAR;
      } else if (key == '4') {
        wifiOn = !wifiOn;
        uint8_t v = wifiOn ? 1 : 0;
        uartlink::send(uartlink::CMD_WIFI, &v, 1);
        screen = SCREEN_LIVE;
      } else if (key == '*') {
        screen = SCREEN_LIVE;
      }
      break;

    case SCREEN_ENROLL_WAIT:
      if (key == '*') {
        screen = SCREEN_LIVE;  // give up waiting locally; camera board still
                                // completes enrollment whenever it next sees a face
      }
      break;

    case SCREEN_DELETE_ENTRY:
      if (key >= '0' && key <= '9') {
        if (deleteEntryBuf.length() < 2) {
          deleteEntryBuf += key;
        }
      } else if (key == '#') {
        if (deleteEntryBuf.length() > 0) {
          uint8_t id = (uint8_t)deleteEntryBuf.toInt();
          uartlink::send(uartlink::CMD_DELETE, &id, 1);
        }
        screen = SCREEN_LIVE;
      } else if (key == '*') {
        screen = SCREEN_LIVE;
      }
      break;

    case SCREEN_CONFIRM_CLEAR:
      if (key == '#') {
        uartlink::send(uartlink::CMD_CLEAR_ALL, nullptr, 0);
        screen = SCREEN_LIVE;
      } else if (key == '*') {
        screen = SCREEN_LIVE;
      }
      break;
  }
}

void render() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  switch (screen) {
    case SCREEN_LIVE:
      {
        if (havePreview) {
          display.drawBitmap(0, 0, previewBitmap, 128, 64, SSD1306_WHITE);
        }
        const char *statusText = "no-face";
        if (lastEventStatus == 1) {
          statusText = "detect";
        } else if (lastEventStatus == 2) {
          statusText = "OK";
        } else if (lastEventStatus == 3) {
          statusText = "unknown";
        }
        display.fillRect(0, 56, 128, 8, SSD1306_BLACK);
        display.setCursor(0, 56);
        display.printf("%s id%d n%d %s", statusText, lastFaceId, enrolledCount, wifiOn ? "wifi" : "");
        break;
      }

    case SCREEN_MENU:
      display.setCursor(0, 0);
      display.println("MENU");
      display.println("1 Enroll face");
      display.println("2 Delete by ID");
      display.println("3 Clear all");
      display.printf("4 WiFi: %s\n", wifiOn ? "on->off" : "off->on");
      display.println("* Back");
      break;

    case SCREEN_ENROLL_WAIT:
      display.setCursor(0, 0);
      display.println("Look at camera");
      display.println("Enrolling...");
      display.println();
      if (millis() - enrollWaitStartMs > 10000) {
        display.println("Taking a while -");
        display.println("keep looking, or *");
      } else {
        display.println("* Cancel");
      }
      break;

    case SCREEN_DELETE_ENTRY:
      display.setCursor(0, 0);
      display.println("Delete face ID:");
      display.println(deleteEntryBuf);
      display.println();
      display.println("# Confirm  * Cancel");
      break;

    case SCREEN_CONFIRM_CLEAR:
      display.setCursor(0, 0);
      display.println("Clear ALL faces?");
      display.println();
      display.println("# Yes   * No");
      break;
  }
  display.display();
}

void setup() {
  Serial.begin(115200);

  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    Serial.println("SSD1306 init failed - check wiring/I2C address");
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Panel booting...");
  display.display();

  uartlink::begin(Serial1, PANEL_UART_BAUD, PANEL_UART_RX_PIN, PANEL_UART_TX_PIN);
  keypad.setDebounceTime(20);
  keypad.setHoldTime(600);

  requestStatus();
}

void loop() {
  pollUart();

  char key = keypad.getKey();
  if (key) {
    handleKey(key);
  }

  if (millis() - lastStatusRequestMs > 5000) {
    requestStatus();
  }

  render();
  delay(20);
}
