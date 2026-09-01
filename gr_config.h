#pragma once
// ############################################################################
//  gr_config.h  -  KODI  /  pin map + shared types
//
//  Board:  ESP32-R40T = CLASSIC ESP32 (same chip as Node 0).
//          4.0" ST7796 display (320x480 PORTRAIT) on HSPI
//          128GB SD card on VSPI
//          Speaker (GPIO 26 DAC2 + GPIO 4 amp enable)
//          BOOT button (GPIO 0) for screen cycling
//          Touch (GPIO 33) CRACKED — NOT USED
//
//  ---- BUILD ---------------------------------------------------------------
//  Board:   "ESP32 Dev Module"  (esp32 core). Classic ESP32, NOT S3.
//  FQBN:    esp32:esp32:esp32:PartitionScheme=huge_app,UploadSpeed=115200
//  Serial:  115200
//  Libraries (Library Manager):
//     * Arduino_GFX_Library  by moononournation
//  SPI / SD / WiFi / Preferences ship with the core.
// ############################################################################

#include <Arduino.h>

#define GR_VERSION "KODI v2.0"
#define GR_BOARD_TYPE  2    // 0 = main ESP32U, 1 = S3, 2 = R40T

// ===== ST7796 Display on HSPI ================================================
#define GR_PIN_LCD_CS    15
#define GR_PIN_LCD_DC     2
#define GR_PIN_LCD_SCK   14
#define GR_PIN_LCD_MOSI  13
#define GR_PIN_LCD_MISO  12
#define GR_PIN_LCD_BL    27

#define GR_SCR_W  320
#define GR_SCR_H  480

// ===== SD Card on VSPI =======================================================
#define GR_PIN_SD_CS      5
#define GR_PIN_SD_SCK    18
#define GR_PIN_SD_MISO   19
#define GR_PIN_SD_MOSI   23

// ===== Speaker (DAC2 + amplifier) ============================================
#define GR_PIN_SPK       26   // DAC channel 2 on classic ESP32
#define GR_PIN_AMP_EN     4   // amplifier enable, active LOW
#define GR_SPK_CH         0   // LEDC channel (core 2.x)

// ===== Touch (CRACKED — NOT USED) ============================================
#define GR_PIN_TOUCH_CS  33   // XPT2046 — cracked, do not init

// ===== BOOT Button ===========================================================
#define GR_PIN_BOOT       0   // built-in BOOT button, always scrolls screens

// ===== Input feel (for BOOT button + ESP-NOW button events) ==================
#define GR_DEBOUNCE_MS    25
#define GR_LONGPRESS_MS  550
#define GR_REPEAT_MS     150
#define GR_NAV_DELAY_MS  380

// ===== Screen indices ========================================================
#define GR_SCREEN_DASHBOARD    0
#define GR_SCREEN_SPECTRUM     1
#define GR_SCREEN_NEURAL       2
#define GR_SCREEN_MEMORY       3
#define GR_SCREEN_CONFIG       4
#define GR_SCREEN_COUNT        5

// ===== Display colors (RGB565) ===============================================
#define GR_C_BG    0x0000   // black
#define GR_C_BAR   0x18E3   // dark gray
#define GR_C_INK   0xFFFF   // white
#define GR_C_DIM   0x8410   // mid gray
#define GR_C_CYAN  0x07FF   // cyan
#define GR_C_MAG   0xF81F   // magenta
#define GR_C_GRN   0x07E0   // green
#define GR_C_RED   0xF800   // red
#define GR_C_AMB   0xFD20   // amber
#define GR_C_PUR   0x881F   // purple
#define GR_C_LINE  0x4208   // subtle line

// ===== Content area (between status bar and nav bar) =========================
#define GR_STATUS_H   28
#define GR_NAV_H      24
#define GR_CONTENT_Y  GR_STATUS_H
#define GR_CONTENT_H  (GR_SCR_H - GR_STATUS_H - GR_NAV_H)
