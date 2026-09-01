#pragma once
// ############################################################################
//  gr_sd.h  -  KODI  /  SD card on VSPI
//
//  128GB microSD on VSPI (SCK=18, MISO=19, MOSI=23, CS=5).
//  Creates folder tree: boot/, brain/, signals/, logs/, stats/ and subdirs.
// ############################################################################

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "gr_config.h"

static SPIClass grSdSPI(VSPI);
static bool     grSdReady = false;
static uint16_t grSdFreeMb = 0;

static inline bool grSdBegin() {
  grSdSPI.begin(GR_PIN_SD_SCK, GR_PIN_SD_MISO, GR_PIN_SD_MOSI, GR_PIN_SD_CS);
  if (!SD.begin(GR_PIN_SD_CS, grSdSPI)) {
    Serial.println(F("[sd] mount FAILED"));
    return false;
  }

  uint64_t total = SD.totalBytes();
  uint64_t used  = SD.usedBytes();
  grSdFreeMb = (uint16_t)((total - used) / (1024ULL * 1024ULL));
  Serial.printf("[sd] mounted — %u MB free\n", grSdFreeMb);

  // create folder tree per Section 05 + 14.8
  const char* dirs[] = {
    "/brain",
    "/brain/config",
    "/brain/sorting_queue",
    "/brain/models",
    "/brain/knowledge",
    "/brain/episodes",
    "/brain/training",
    "/signals",
    "/signals/garage_doors",
    "/signals/gates_barriers",
    "/signals/car_fobs",
    "/signals/tpms",
    "/signals/doorbells",
    "/signals/weather_stations",
    "/signals/alarm_systems",
    "/signals/smart_home",
    "/signals/utility_meters",
    "/signals/industrial",
    "/signals/medical",
    "/signals/vehicles",
    "/signals/access_control",
    "/signals/pagers",
    "/signals/baby_monitors",
    "/signals/garden_outdoor",
    "/signals/rc_toys",
    "/signals/lora",
    "/signals/unidentified",
    "/credentials",
    "/credentials/wifi_handshakes",
    "/credentials/ble_keys",
    "/credentials/rf_codes",
    "/credentials/rf_codes/fixed_codes",
    "/credentials/rf_codes/rolling_codes",
    "/credentials/rf_codes/encrypted_codes",
    "/logs",
    "/logs/captures",
    "/logs/neural",
    "/logs/system",
    "/logs/attacks",
    "/logs/missed",
    "/logs/backbone",
    "/playbooks",
    "/boot",
    "/boot/frames",
    "/boot/themes",
    "/stats",
    "/assets",
    "/assets/icons",
    "/assets/icons/game_icons",
    "/assets/icons/pixel_icons",
    "/assets/icons/ui_icons",
    "/assets/icons/status",
    "/assets/icons/nav",
    "/assets/sprites",
    "/assets/sprites/gecko",
    "/assets/sprites/vfx",
    "/assets/sprites/characters",
    "/assets/sprites/animations",
    "/assets/fonts",
    "/assets/backgrounds",
    "/assets/backgrounds/cyberpunk",
    "/assets/backgrounds/circuit",
    "/assets/backgrounds/textures",
    "/assets/palettes",
    "/assets/sounds",
    "/assets/ui",
    "/assets/ui/bars",
    "/assets/ui/buttons",
    "/assets/ui/panels",
    "/assets/ui/frames",
  };
  for (auto d : dirs) {
    if (!SD.exists(d)) {
      SD.mkdir(d);
      Serial.printf("[sd] mkdir %s\n", d);
    }
  }

  // write index.json if missing
  if (!SD.exists("/index.json")) {
    File idx = SD.open("/index.json", FILE_WRITE);
    if (idx) {
      idx.println(F("{\"version\":\"1.0\",\"node\":\"R40T\",\"created\":0}"));
      idx.close();
      Serial.println(F("[sd] index.json created"));
    }
  }

  grSdReady = true;
  return true;
}

static inline void grSdUpdateFree() {
  if (!grSdReady) return;
  uint64_t total = SD.totalBytes();
  uint64_t used  = SD.usedBytes();
  grSdFreeMb = (uint16_t)((total - used) / (1024ULL * 1024ULL));
}

// ---- file helpers ----------------------------------------------------------
static inline bool grSdWriteFile(const char* path, const uint8_t* data, size_t len) {
  if (!grSdReady) return false;
  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;
  size_t w = f.write(data, len);
  f.close();
  return (w == len);
}

static inline bool grSdAppendFile(const char* path, const uint8_t* data, size_t len) {
  if (!grSdReady) return false;
  File f = SD.open(path, FILE_APPEND);
  if (!f) return false;
  size_t w = f.write(data, len);
  f.close();
  return (w == len);
}

static inline size_t grSdReadFile(const char* path, uint8_t* buf, size_t maxLen) {
  if (!grSdReady) return 0;
  File f = SD.open(path, FILE_READ);
  if (!f) return 0;
  size_t r = f.read(buf, maxLen);
  f.close();
  return r;
}

static inline bool grSdFileExists(const char* path) {
  if (!grSdReady) return false;
  return SD.exists(path);
}

static inline size_t grSdFileSize(const char* path) {
  if (!grSdReady) return 0;
  File f = SD.open(path, FILE_READ);
  if (!f) return 0;
  size_t s = f.size();
  f.close();
  return s;
}

static inline bool grSdDeleteFile(const char* path) {
  if (!grSdReady) return false;
  return SD.remove(path);
}

static inline uint16_t grSdCountFiles(const char* dirPath) {
  if (!grSdReady) return 0;
  File dir = SD.open(dirPath);
  if (!dir || !dir.isDirectory()) return 0;
  uint16_t count = 0;
  File f = dir.openNextFile();
  while (f) {
    if (!f.isDirectory()) count++;
    f = dir.openNextFile();
  }
  return count;
}

static inline bool grSdAppendLine(const char* path, const char* line) {
  if (!grSdReady) return false;
  File f = SD.open(path, FILE_APPEND);
  if (!f) return false;
  f.println(line);
  f.close();
  return true;
}
