#pragma once
// ############################################################################
//  gr_sort.h  -  GHOST TX R40T  /  Sorting Queue Classifier
//
//  Fixed 216-byte GrCaptureRecord structs in rolling capture file.
//  Sort burst when idle (no captures for 2+ seconds).
//  Classification: freq band → modulation → byte pattern → device type.
//  Section 14.9 of the master plan.
// ############################################################################

#include <Arduino.h>
#include <SD.h>
#include "gr_config.h"

// ---- capture record (fixed 216 bytes) per Section 14.9 --------------------
struct __attribute__((packed)) GrCaptureRecord {
  uint32_t timestamp;     // millis() at capture
  float    freqMHz;       // capture frequency
  uint8_t  radioId;       // which CC1101 (0-3)
  uint8_t  modulation;    // 0=OOK, 1=2FSK, 2=GFSK, 3=MSK, 4=4FSK
  uint8_t  dataLen;       // raw data length (1-200)
  uint8_t  classified;    // 0=unsorted, 1=classified
  uint8_t  deviceType;    // category enum below
  uint8_t  reserved[3];   // alignment padding
  uint8_t  data[200];     // raw capture bytes
};
// static_assert(sizeof(GrCaptureRecord) == 216);

// ---- device type categories per Section 04 Tier 4 -------------------------
enum GrDeviceType : uint8_t {
  GR_DEV_UNKNOWN         = 0,
  GR_DEV_GARAGE_FIXED    = 1,
  GR_DEV_GARAGE_ROLLING  = 2,
  GR_DEV_GATE_BARRIER    = 3,
  GR_DEV_CAR_FOB         = 4,
  GR_DEV_TPMS            = 5,
  GR_DEV_DOORBELL        = 6,
  GR_DEV_WEATHER         = 7,
  GR_DEV_ALARM_SENSOR    = 8,
  GR_DEV_ALARM_PANEL     = 9,
  GR_DEV_SMART_HOME      = 10,
  GR_DEV_UTILITY_METER   = 11,
  GR_DEV_INDUSTRIAL      = 12,
  GR_DEV_MEDICAL         = 13,
  GR_DEV_VEHICLE         = 14,
  GR_DEV_ACCESS_CONTROL  = 15,
  GR_DEV_PAGER           = 16,
  GR_DEV_BABY_MONITOR    = 17,
  GR_DEV_GARDEN_OUTDOOR  = 18,
  GR_DEV_RC_TOY          = 19,
  GR_DEV_LORA_ENERGY     = 20,
  GR_DEV_KEYFOB_PANIC    = 21,
  GR_DEV_LIGHT_SWITCH    = 22,
  GR_DEV_FAN_REMOTE      = 23,
  GR_DEV_APPLIANCE       = 24,
};

// ---- folder map: device type → SD path ------------------------------------
static const char* grDevFolder(uint8_t dt) {
  switch (dt) {
    case GR_DEV_GARAGE_FIXED:
    case GR_DEV_GARAGE_ROLLING:  return "/signals/garage_doors";
    case GR_DEV_GATE_BARRIER:    return "/signals/gates_barriers";
    case GR_DEV_CAR_FOB:         return "/signals/car_fobs";
    case GR_DEV_TPMS:            return "/signals/tpms";
    case GR_DEV_DOORBELL:        return "/signals/doorbells";
    case GR_DEV_WEATHER:         return "/signals/weather_stations";
    case GR_DEV_ALARM_SENSOR:
    case GR_DEV_ALARM_PANEL:     return "/signals/alarm_systems";
    case GR_DEV_SMART_HOME:
    case GR_DEV_LIGHT_SWITCH:
    case GR_DEV_FAN_REMOTE:      return "/signals/smart_home";
    case GR_DEV_UTILITY_METER:   return "/signals/utility_meters";
    case GR_DEV_INDUSTRIAL:      return "/signals/industrial";
    case GR_DEV_MEDICAL:         return "/signals/medical";
    case GR_DEV_VEHICLE:         return "/signals/vehicles";
    case GR_DEV_ACCESS_CONTROL:  return "/signals/access_control";
    case GR_DEV_PAGER:           return "/signals/pagers";
    case GR_DEV_BABY_MONITOR:    return "/signals/baby_monitors";
    case GR_DEV_GARDEN_OUTDOOR:  return "/signals/garden_outdoor";
    case GR_DEV_RC_TOY:          return "/signals/rc_toys";
    case GR_DEV_LORA_ENERGY:     return "/signals/lora";
    case GR_DEV_KEYFOB_PANIC:    return "/signals/alarm_systems";
    case GR_DEV_APPLIANCE:       return "/signals/smart_home";
    default:                     return "/signals/unidentified";
  }
}

static const char* grDevLabel(uint8_t dt) {
  switch (dt) {
    case GR_DEV_GARAGE_FIXED:    return "garage_fixed";
    case GR_DEV_GARAGE_ROLLING:  return "garage_rolling";
    case GR_DEV_GATE_BARRIER:    return "gate";
    case GR_DEV_CAR_FOB:         return "car_fob";
    case GR_DEV_TPMS:            return "tpms";
    case GR_DEV_DOORBELL:        return "doorbell";
    case GR_DEV_WEATHER:         return "weather";
    case GR_DEV_ALARM_SENSOR:    return "alarm_sensor";
    case GR_DEV_ALARM_PANEL:     return "alarm_panel";
    case GR_DEV_SMART_HOME:      return "smart_home";
    case GR_DEV_UTILITY_METER:   return "meter";
    case GR_DEV_INDUSTRIAL:      return "industrial";
    case GR_DEV_MEDICAL:         return "medical";
    case GR_DEV_VEHICLE:         return "vehicle";
    case GR_DEV_ACCESS_CONTROL:  return "access";
    case GR_DEV_PAGER:           return "pager";
    case GR_DEV_BABY_MONITOR:    return "baby_mon";
    case GR_DEV_GARDEN_OUTDOOR:  return "garden";
    case GR_DEV_RC_TOY:          return "rc_toy";
    case GR_DEV_LORA_ENERGY:     return "lora_energy";
    case GR_DEV_KEYFOB_PANIC:    return "panic_fob";
    case GR_DEV_LIGHT_SWITCH:    return "light_sw";
    case GR_DEV_FAN_REMOTE:      return "fan_remote";
    case GR_DEV_APPLIANCE:       return "appliance";
    default:                     return "unknown";
  }
}

static const char* grModLabel(uint8_t m) {
  switch (m) {
    case 0: return "OOK";
    case 1: return "2FSK";
    case 2: return "GFSK";
    case 3: return "MSK";
    case 4: return "4FSK";
    default: return "UNK";
  }
}

// ---- sorting queue state ---------------------------------------------------
static uint16_t grSortQueueDepth   = 0;
static uint16_t grSortItemsSorted  = 0;
static bool     grSortActive       = false;
static uint32_t grSortLastCapture  = 0;
static uint32_t grSortCaptureFile  = 0;  // current capture file number
#define GR_SORT_IDLE_TRIGGER_MS  2000
#define GR_SORT_MAX_PER_TICK     5

// ---- classify a capture record (Tiers 1-4 condensed) ----------------------
static uint8_t grClassify(const GrCaptureRecord& rec) {
  float f = rec.freqMHz;

  // Tier 0-1: frequency band + modulation → rough category
  // 315 MHz band (US)
  if (f >= 314.0f && f <= 316.0f) {
    if (rec.modulation == 0) return GR_DEV_GARAGE_FIXED;
    return GR_DEV_SMART_HOME;
  }
  // 433 MHz band (EU/worldwide)
  if (f >= 433.0f && f <= 434.0f) {
    if (rec.modulation == 0) {
      // OOK at 433 MHz: check data patterns
      if (rec.dataLen >= 3 && rec.dataLen <= 6) return GR_DEV_GARAGE_FIXED;
      if (rec.dataLen >= 7 && rec.dataLen <= 12) return GR_DEV_DOORBELL;
      if (rec.dataLen >= 20) return GR_DEV_WEATHER;
      return GR_DEV_GARAGE_FIXED;
    }
    if (rec.modulation == 1 || rec.modulation == 2) {
      if (rec.dataLen >= 8 && rec.dataLen <= 15) return GR_DEV_TPMS;
      if (rec.dataLen >= 20) return GR_DEV_WEATHER;
      return GR_DEV_SMART_HOME;
    }
    return GR_DEV_UNKNOWN;
  }
  // 868 MHz band (EU)
  if (f >= 868.0f && f <= 870.0f) {
    if (rec.modulation == 2) return GR_DEV_SMART_HOME;
    if (rec.modulation == 1) return GR_DEV_ALARM_SENSOR;
    return GR_DEV_SMART_HOME;
  }
  // 915 MHz band (US ISM / LoRa)
  if (f >= 902.0f && f <= 928.0f) {
    return GR_DEV_LORA_ENERGY;
  }
  // 300-348 MHz
  if (f >= 300.0f && f <= 348.0f) {
    if (rec.modulation == 0) return GR_DEV_GATE_BARRIER;
    return GR_DEV_INDUSTRIAL;
  }
  // 387-464 MHz (wider range)
  if (f >= 387.0f && f <= 464.0f) {
    return GR_DEV_PAGER;
  }
  // 779-928 MHz (wider range)
  if (f >= 779.0f && f <= 928.0f) {
    return GR_DEV_UTILITY_METER;
  }

  return GR_DEV_UNKNOWN;
}

// ---- queue a capture (called from ESP-NOW rx handler) ----------------------
static inline bool grSortQueueCapture(float freqMHz, uint8_t radioId,
                                       uint8_t mod, const uint8_t* data,
                                       uint8_t len) {
  if (!grSdReady || len == 0 || len > 200) return false;

  GrCaptureRecord rec = {};
  rec.timestamp  = millis();
  rec.freqMHz    = freqMHz;
  rec.radioId    = radioId;
  rec.modulation = mod;
  rec.dataLen    = len;
  rec.classified = 0;
  rec.deviceType = GR_DEV_UNKNOWN;
  memcpy(rec.data, data, len);

  char path[48];
  snprintf(path, sizeof(path), "/brain/sorting_queue/capture_%04u.raw",
           grSortCaptureFile);

  File f = SD.open(path, FILE_APPEND);
  if (!f) {
    grSortCaptureFile++;
    snprintf(path, sizeof(path), "/brain/sorting_queue/capture_%04u.raw",
             grSortCaptureFile);
    f = SD.open(path, FILE_WRITE);
    if (!f) return false;
  }
  f.write((const uint8_t*)&rec, sizeof(rec));
  f.close();

  grSortQueueDepth++;
  grSortLastCapture = millis();
  return true;
}

// ---- write sorted .bin + .meta files to proper folder ---------------------
static inline bool grSortWriteClassified(const GrCaptureRecord& rec, uint8_t devType) {
  const char* folder = grDevFolder(devType);
  const char* label  = grDevLabel(devType);

  // build filename: {label}_{freq}MHz_{timestamp}.bin
  char binPath[96], metaPath[96];
  uint32_t sec = rec.timestamp / 1000;
  uint16_t h = (sec / 3600) % 24;
  uint16_t m = (sec / 60) % 60;
  uint16_t s = sec % 60;

  snprintf(binPath, sizeof(binPath), "%s/%s_%.2fMHz_%02u-%02u-%02u.bin",
           folder, label, rec.freqMHz, h, m, s);
  snprintf(metaPath, sizeof(metaPath), "%s/%s_%.2fMHz_%02u-%02u-%02u.meta",
           folder, label, rec.freqMHz, h, m, s);

  // write .bin
  File bf = SD.open(binPath, FILE_WRITE);
  if (!bf) return false;
  bf.write(rec.data, rec.dataLen);
  bf.close();

  // write .meta per Section 06
  File mf = SD.open(metaPath, FILE_WRITE);
  if (!mf) return false;
  mf.printf("Signal:        %s\n", grDevLabel(devType));
  mf.printf("Frequency:     %.3f MHz\n", rec.freqMHz);
  mf.printf("Modulation:    %s\n", grModLabel(rec.modulation));
  mf.printf("Confidence:    %d%%\n", (devType != GR_DEV_UNKNOWN) ? 75 : 0);
  mf.printf("Radio:         CC1101-%c (id=%d)\n", 'A' + rec.radioId, rec.radioId);
  mf.printf("Data Length:   %d bytes\n", rec.dataLen);
  mf.printf("Captured:      %02u:%02u:%02u (session-relative)\n", h, m, s);
  mf.printf("Replays:       0\n");
  mf.printf("Last Replayed: never\n");
  mf.printf("Notes:         auto-classified by R40T deep brain\n");
  mf.close();

  return true;
}

// ---- sort burst: process queue items --------------------------------------
static inline uint16_t grSortBurstTick() {
  if (!grSdReady || grSortQueueDepth == 0) return 0;

  char path[48];
  snprintf(path, sizeof(path), "/brain/sorting_queue/capture_%04u.raw",
           grSortCaptureFile);

  File f = SD.open(path, FILE_READ);
  if (!f) return 0;

  uint16_t processed = 0;
  GrCaptureRecord rec;

  while (processed < GR_SORT_MAX_PER_TICK && f.available() >= (int)sizeof(rec)) {
    if (f.read((uint8_t*)&rec, sizeof(rec)) != sizeof(rec)) break;
    if (rec.classified) continue;

    uint8_t devType = grClassify(rec);
    rec.classified = 1;
    rec.deviceType = devType;

    grSortWriteClassified(rec, devType);
    processed++;
    grSortItemsSorted++;
    if (grSortQueueDepth > 0) grSortQueueDepth--;
  }
  f.close();

  if (grSortQueueDepth == 0) {
    SD.remove(path);
    grSortActive = false;
    Serial.printf("[sort] burst complete, %u items sorted\n", grSortItemsSorted);
  }

  return processed;
}

// ---- check if sort burst should trigger -----------------------------------
static inline bool grSortShouldTrigger() {
  if (grSortQueueDepth == 0) return false;
  if (grSortActive) return true;
  return (millis() - grSortLastCapture >= GR_SORT_IDLE_TRIGGER_MS);
}
