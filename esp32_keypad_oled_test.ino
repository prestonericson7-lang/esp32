#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Keypad.h>

// --- OLED Config ---
// Each OLED on its own I2C bus, both at 0x3C, fully independent.
//   OLED 1: Wire  (bus 0) — SDA = GPIO 21, SCL = GPIO 22
//   OLED 2: Wire1 (bus 1) — SDA = GPIO 16, SCL = GPIO 17
#define SCREEN_W 128
#define SCREEN_H 64
#define OLED_ADDR 0x3C

TwoWire I2C_1 = TwoWire(0);
TwoWire I2C_2 = TwoWire(1);

Adafruit_SSD1306 oled1(SCREEN_W, SCREEN_H, &I2C_1, -1);
Adafruit_SSD1306 oled2(SCREEN_W, SCREEN_H, &I2C_2, -1);

// --- 4x4 Keypad Config ---
// Keypad pinout (left-to-right looking at the ribbon cable):
//   Pin1  Pin2  Pin3  Pin4  Pin5  Pin6  Pin7  Pin8
//   ROW1  ROW2  ROW3  ROW4  COL1  COL2  COL3  COL4
const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte rowPins[ROWS] = {13, 12, 14, 27};
byte colPins[COLS] = {26, 25, 33, 32};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// --- Input buffer ---
#define MAX_CHARS 80
char buffer[MAX_CHARS + 1];
int bufLen = 0;

void drawOled(Adafruit_SSD1306 &disp, const char *label) {
  disp.clearDisplay();

  disp.setTextSize(1);
  disp.setTextColor(SSD1306_WHITE);
  disp.setCursor(0, 0);
  disp.print(label);
  disp.drawLine(0, 10, SCREEN_W - 1, 10, SSD1306_WHITE);

  disp.setCursor(0, 14);
  disp.setTextSize(2);
  disp.print(buffer);

  if ((millis() / 500) % 2 == 0) {
    int cx = disp.getCursorX();
    int cy = disp.getCursorY();
    disp.fillRect(cx, cy, 10, 14, SSD1306_WHITE);
  }

  disp.display();
}

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 Keypad + Dual OLED Test");

  I2C_1.begin(21, 22);
  I2C_2.begin(16, 17);

  if (!oled1.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED 1 not found on bus 0 (GPIO 21/22)!");
  }
  if (!oled2.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED 2 not found on bus 1 (GPIO 16/17)!");
  }

  buffer[0] = '\0';

  oled1.clearDisplay();
  oled1.setTextSize(1);
  oled1.setTextColor(SSD1306_WHITE);
  oled1.setCursor(10, 20);
  oled1.print("OLED 1 Ready");
  oled1.setCursor(10, 36);
  oled1.print("Bus 0  GPIO 21/22");
  oled1.display();

  oled2.clearDisplay();
  oled2.setTextSize(1);
  oled2.setTextColor(SSD1306_WHITE);
  oled2.setCursor(10, 20);
  oled2.print("OLED 2 Ready");
  oled2.setCursor(10, 36);
  oled2.print("Bus 1  GPIO 16/17");
  oled2.display();

  delay(1500);
}

void loop() {
  char key = keypad.getKey();

  if (key) {
    Serial.print("Key: ");
    Serial.println(key);

    if (key == '#') {
      bufLen = 0;
      buffer[0] = '\0';
    } else {
      if (bufLen < MAX_CHARS) {
        buffer[bufLen++] = key;
        buffer[bufLen] = '\0';
      }
    }
  }

  drawOled(oled1, "OLED-1 [Bus 0]");
  drawOled(oled2, "OLED-2 [Bus 1]");
}
