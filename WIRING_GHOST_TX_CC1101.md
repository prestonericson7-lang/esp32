# GHOST TX CC1101 EDITION — WIRING & PINOUT

**Board:** Lonely Binary *Gold Edition* ESP32 — **ESP32-WROOM-32U** (classic
ESP32, external antenna jack, 38-pin, no PSRAM).

**Radios:** 4x CC1101 (sub-GHz) on shared HSPI bus.
**Screens:** 2x SSD1306 128x64 I2C OLED (Wire + Wire1, separate buses).
**Audio:** 1x speaker on LEDC PWM.
**LEDs:** 3x status indicators (RED/GREEN/BLUE).
**Input:** Buttons on ESP32-S3 co-processor, forwarded via ESP-NOW.

---

## 0. FULL GPIO MAP

| GPIO | Signal             | Bus/Type    | Notes                          |
|------|--------------------|-------------|--------------------------------|
| **4**  | CC1101-B CSN     | HSPI CS     |                                |
| **5**  | CC1101-A CSN     | HSPI CS     | Strapping: CS idles HIGH = safe |
| **13** | SPI SCK          | HSPI        | Shared clock for all 4 CC1101  |
| **14** | SPI MOSI         | HSPI        | Shared data out                |
| **15** | LED BLUE (stat)  | Output      | Strapping: OK after boot       |
| **16** | OLED2 SDA        | I2C (Wire1) |                                |
| **17** | OLED2 SCL        | I2C (Wire1) |                                |
| **18** | SPI MISO         | HSPI        | Shared data in                 |
| **19** | CC1101 GDO0      | (not wired) | Available but unused           |
| **21** | OLED1 SDA        | I2C (Wire)  |                                |
| **22** | OLED1 SCL        | I2C (Wire)  |                                |
| **25** | CC1101-D CSN     | HSPI CS     |                                |
| **26** | LED GREEN (RX)   | Output      |                                |
| **27** | CC1101-C CSN     | HSPI CS     |                                |
| **32** | Speaker          | LEDC PWM    |                                |
| **33** | LED RED (TX)     | Output      |                                |

**Not used on main ESP:** GPIO 0, 2, 23, 34, 35, 36, 39.
**Reserved (never use):** GPIO 6-11 (flash SPI), GPIO 1/3 (USB serial).

---

## 1. CC1101 WIRING — all 4 modules on shared HSPI

| Signal          | Radio A | Radio B | Radio C | Radio D |
|-----------------|---------|---------|---------|---------|
| **VCC**         | 3.3V    | 3.3V    | 3.3V    | 3.3V    |
| **GND**         | GND     | GND     | GND     | GND     |
| **SCK** (SCLK)  | **13**  | **13**  | **13**  | **13**  |
| **MOSI** (SI)   | **14**  | **14**  | **14**  | **14**  |
| **MISO** (SO)   | **18**  | **18**  | **18**  | **18**  |
| **CSN** (CS/SS) | **5**   | **4**   | **27**  | **25**  |
| **GDO0**        | n/c     | n/c     | n/c     | n/c     |
| **GDO2**        | n/c     | n/c     | n/c     | n/c     |

All 4 radios share the same 3 SPI data lines. Only the CSN (chip select) is
unique per radio. GDO0/GDO2 are unused — the driver polls status registers.

**10 uF cap across each module's VCC-GND** right at the module.

---

## 2. DUAL OLEDs — separate I2C buses

| OLED   | SDA    | SCL    | Address  | I2C Bus  | Role           |
|--------|--------|--------|----------|----------|----------------|
| Main   | **21** | **22** | **0x3C** | Wire     | UI / menus     |
| Status | **16** | **17** | **0x3C** | Wire1    | Radio status   |

Both OLEDs are at address 0x3C but on separate I2C buses (Wire and Wire1).
No address jumper changes needed.

---

## 3. BUTTONS — via ESP-NOW from S3

**No buttons are wired to the main ESP32.** All input comes from the
ESP32-S3 co-processor via ESP-NOW wireless link.

The S3 has 5 buttons on GPIOs 9-13 with INPUT_PULLUP:
- UP, DOWN, SELECT, BACK, SWAP

Button presses are sent as GT_CMD_BUTTON messages and received by
gg_espnow.h on the main ESP.

---

## 4. SPEAKER

| Speaker | GPIO   | LEDC Channel |
|---------|--------|-------------|
| 1       | **32** | CH 0        |

Single speaker. Wiring options:
- **Piezo:** GPIO -> piezo buzzer -> GND
- **With amp:** GPIO -> PAM8302 amp -> speaker (amp from 5V)

---

## 5. STATUS LEDs — 3 indicators

Each LED: GPIO -> 220 ohm resistor -> LED anode (+), cathode (-) -> GND.

| LED    | GPIO   | Color    | Role                             |
|--------|--------|----------|----------------------------------|
| TX     | **33** | **RED**    | Blinks when any radio transmits |
| RX     | **26** | **GREEN**  | Blinks when data is received    |
| Status | **15** | **BLUE**   | Solid when radios ready         |

---

## 6. POWER

- **3.3V only** on every CC1101 VCC (5V kills them).
- **10 uF electrolytic cap across EACH radio module's VCC-GND** (4 caps).
- **100-470 uF bulk cap on 3V3 rail** near the radios.
- USB-C, **2A+ supply** recommended.
- ESP32 is 3.3V logic — no level shifters needed.

---

## 7. BUILD & FLASH

- **Board:** *ESP32 Dev Module* (esp32 core). Classic ESP32 — NOT S3.
- **Serial:** 115200.
- **Libraries:** `Adafruit SSD1306` (+ Adafruit GFX).
  Wire / SPI / Preferences ship with the core.
  CC1101 driver is built-in (`gg_cc1101.h`).
  nRF24 stub (`gg_nrf.h`) — no RF24 library needed.

```
ghost_tx_cc1101/
  ghost_tx_cc1101.ino          (main sketch)
  gg_config.h                  (pins + types)
  gg_espnow.h                  (ESP-NOW link to S3)
  gg_input.h                   (button events from ESP-NOW)
  gg_audio.h                   (speaker)
  gg_leds.h                    (3 status LEDs)
  gg_nrf.h                     (stub — no nRF hardware)
  gg_cc1101.h                  (CC1101 driver, 4 radios)
  gg_cc1101_tools.h            (sub-GHz tools)
  gg_store.h                   (NVS persistence)
  gg_oled2.h                   (status OLED on Wire1)
  gg_boot.h                    (boot animation)
  gg_ui.h                      (full UI)
  gg_detect.h  gg_wifi_tx.h  gg_ble_tx.h  gg_portal.h  gg_crack.h
```

---

## 8. BUS DIAGRAM

```
                    +---------------------+
                    |   ESP32-WROOM-32U   |
                    |   (Gold Edition)    |
                    |                     |
  HSPI bus ---------| SCK=13             |
  (all 4 CC1101)    | MOSI=14            |
                    | MISO=18            |
                    | CS=5 ----> CC1101-A |
                    | CS=4 ----> CC1101-B |
                    | CS=27 ---> CC1101-C |
                    | CS=25 ---> CC1101-D |
                    |                     |
  I2C Wire ---------| SDA=21             |
                    | SCL=22             |
                    | 0x3C ---> OLED Main |
                    |                     |
  I2C Wire1 --------| SDA=16             |
                    | SCL=17             |
                    | 0x3C ---> OLED Stat |
                    |                     |
  Speaker ----------| 32 --> SPK         |
                    |                     |
  LEDs -------------| 33 --> RED   (TX)  |
                    | 26 --> GREEN (RX)  |
                    | 15 --> BLUE  (Stat)|
                    |                     |
  ESP-NOW ----------| WiFi --> S3 buttons |
                    +---------------------+
```
