// ESP32-S3-CAM (Meshtastic-style) bring-up / sanity test
//
// Goal: prove the board is alive with ZERO dependency on camera or SD
// libraries, so a code problem can't be confused with a wiring problem.
//
// From the schematics:
//   - WS2812 RGB LED data pin = GPIO2
//   - BOOT button (S2)        = GPIO0 (10K pull-up to 3V3)
//   - RESET button (S1)       = EN
//   - USB<->serial is a CH340C on UART0 (GPIO43 TX / GPIO44 RX),
//     NOT the S3's native USB peripheral.
//
// Board settings to use in Arduino IDE (Tools menu):
//   Board: "ESP32S3 Dev Module"
//   USB CDC On Boot: Disabled        (this board uses CH340, not native USB)
//   Flash Size: 16MB
//   Partition Scheme: any "16MB" scheme (e.g. 16M Flash (3MB APP/9.9MB FATFS))
//   PSRAM: OPI PSRAM                 (N16R8 has 8MB PSRAM)
//   Upload Speed: 460800 (drop to 115200 if uploads fail/are flaky)
//   Port: COM31

#include <Adafruit_NeoPixel.h>

#define RGB_LED_PIN     2
#define BOOT_BUTTON_PIN 0
#define NUM_LEDS        1

Adafruit_NeoPixel pixel(NUM_LEDS, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

unsigned long lastHeartbeat = 0;
int colorStep = 0;
bool lastButtonState = HIGH;

void setColor(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}

void setup() {
  Serial.begin(115200);
  unsigned long serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < 3000) {
    delay(10);
  }

  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  pixel.begin();
  pixel.setBrightness(40);
  setColor(0, 0, 0);

  Serial.println();
  Serial.println("========================================");
  Serial.println(" ESP32-S3-CAM bring-up test starting");
  Serial.println("========================================");
  Serial.printf("Chip model:      %s\n", ESP.getChipModel());
  Serial.printf("Chip revision:   %d\n", ESP.getChipRevision());
  Serial.printf("CPU cores:       %d\n", ESP.getChipCores());
  Serial.printf("CPU freq:        %d MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("Flash size:      %u bytes\n", ESP.getFlashChipSize());
  Serial.printf("PSRAM found:     %s\n", psramFound() ? "YES" : "NO");
  if (psramFound()) {
    Serial.printf("PSRAM size:      %u bytes\n", ESP.getPsramSize());
  }
  Serial.printf("Free heap:       %u bytes\n", ESP.getFreeHeap());
  Serial.println("----------------------------------------");
  Serial.println("If you see this text, USB/serial + the");
  Serial.println("chip itself are both working.");
  Serial.println();
  Serial.println("Watch the RGB LED: it should cycle");
  Serial.println("RED -> GREEN -> BLUE -> OFF continuously.");
  Serial.println();
  Serial.println("Press the BOOT button (S2) now and then");
  Serial.println("- you should see a message below each time.");
  Serial.println("========================================");
}

void loop() {
  // --- RGB LED cycle: proves GPIO2 + WS2812 + power rails are good ---
  switch (colorStep % 4) {
    case 0: setColor(40, 0, 0); break;  // red
    case 1: setColor(0, 40, 0); break;  // green
    case 2: setColor(0, 0, 40); break;  // blue
    case 3: setColor(0, 0, 0);  break;  // off
  }
  colorStep++;

  // --- Serial heartbeat: proves the loop is running and serial is alive ---
  if (millis() - lastHeartbeat >= 1000) {
    lastHeartbeat = millis();
    Serial.printf("[%8lu ms] heartbeat - alive, free heap: %u\n",
                  millis(), ESP.getFreeHeap());
  }

  // --- BOOT button read: proves GPIO0 input + button wiring post-boot ---
  bool buttonState = digitalRead(BOOT_BUTTON_PIN);
  if (buttonState == LOW && lastButtonState == HIGH) {
    Serial.println(">>> BOOT button (S2) press detected on GPIO0 <<<");
  }
  lastButtonState = buttonState;

  delay(300);
}
