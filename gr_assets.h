#pragma once
// ############################################################################
//  gr_assets.h  -  KODI  /  SD card RGB565 asset loader for ST7796
//
//  Loads pre-converted RGB565 .raw bitmaps from the SD card and draws them
//  onto the display using Arduino_GFX's draw16bitRGBBitmap().
//
//  Asset file format (.raw):
//    Bytes 0-1: width  (uint16_t little-endian)
//    Bytes 2-3: height (uint16_t little-endian)
//    Bytes 4+:  pixel data (width * height * 2 bytes, RGB565 big-endian)
//
//  Strip buffering: large images are loaded and drawn in horizontal strips
//  to stay within ESP32 heap limits. Default strip height = 32 rows.
//  320px * 32 rows * 2 bytes = 20,480 bytes per strip.
//
//  Usage:
//    grAssetDraw(grGfx, "/assets/icons/ui_icons/skull.raw", 10, 40);
//    grAssetDrawTinted(grGfx, "/assets/icons/status/node_up.raw", 5, 5, GR_C_GRN);
//    grAssetDrawScaled(grGfx, "/assets/sprites/gecko/gecko_icon.raw", 50, 100, 2);
// ############################################################################

#include <Arduino.h>
#include <SD.h>
#include <Arduino_GFX_Library.h>
#include "gr_config.h"

#define GR_ASSET_STRIP_H    32
#define GR_ASSET_MAX_W     320
#define GR_ASSET_STRIP_BUF (GR_ASSET_MAX_W * GR_ASSET_STRIP_H)

// shared strip buffer — reused across calls, never allocated on stack
static uint16_t grAssetBuf[GR_ASSET_STRIP_BUF];

// ---- asset header ----------------------------------------------------------
struct GrAssetHeader {
  uint16_t width;
  uint16_t height;
};

static inline bool grAssetReadHeader(const char* path, GrAssetHeader& hdr) {
  if (!grSdReady) return false;
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  uint8_t raw[4];
  if (f.read(raw, 4) != 4) { f.close(); return false; }
  hdr.width  = raw[0] | (raw[1] << 8);
  hdr.height = raw[2] | (raw[3] << 8);
  f.close();
  if (hdr.width == 0 || hdr.height == 0 || hdr.width > 480 || hdr.height > 480)
    return false;
  return true;
}

// ---- core draw: strip-buffered RGB565 from SD ------------------------------
static inline bool grAssetDraw(Arduino_GFX* gfx, const char* path,
                                int16_t x, int16_t y) {
  if (!grSdReady || !gfx) return false;

  File f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.printf("[asset] not found: %s\n", path);
    return false;
  }

  uint8_t hdr[4];
  if (f.read(hdr, 4) != 4) { f.close(); return false; }
  uint16_t w = hdr[0] | (hdr[1] << 8);
  uint16_t h = hdr[2] | (hdr[3] << 8);

  if (w == 0 || h == 0 || w > 480 || h > 480) { f.close(); return false; }

  uint16_t stripH = GR_ASSET_STRIP_H;
  if (w > GR_ASSET_MAX_W) stripH = GR_ASSET_STRIP_BUF / w;
  if (stripH == 0) stripH = 1;

  uint16_t rowsDone = 0;
  while (rowsDone < h) {
    uint16_t rows = min((uint16_t)(h - rowsDone), stripH);
    size_t bytes = (size_t)w * rows * 2;
    size_t rd = f.read((uint8_t*)grAssetBuf, bytes);
    if (rd < bytes) {
      memset(((uint8_t*)grAssetBuf) + rd, 0, bytes - rd);
    }
    gfx->draw16bitRGBBitmap(x, y + rowsDone, grAssetBuf, w, rows);
    rowsDone += rows;
  }

  f.close();
  return true;
}

// ---- draw with color tint (multiplies each pixel by tint color) ------------
static inline bool grAssetDrawTinted(Arduino_GFX* gfx, const char* path,
                                      int16_t x, int16_t y, uint16_t tint) {
  if (!grSdReady || !gfx) return false;

  File f = SD.open(path, FILE_READ);
  if (!f) return false;

  uint8_t hdr[4];
  if (f.read(hdr, 4) != 4) { f.close(); return false; }
  uint16_t w = hdr[0] | (hdr[1] << 8);
  uint16_t h = hdr[2] | (hdr[3] << 8);

  if (w == 0 || h == 0 || w > 480 || h > 480) { f.close(); return false; }

  uint8_t tr = (tint >> 11) & 0x1F;
  uint8_t tg = (tint >> 5) & 0x3F;
  uint8_t tb = tint & 0x1F;

  uint16_t stripH = GR_ASSET_STRIP_H;
  if (w > GR_ASSET_MAX_W) stripH = GR_ASSET_STRIP_BUF / w;
  if (stripH == 0) stripH = 1;

  uint16_t rowsDone = 0;
  while (rowsDone < h) {
    uint16_t rows = min((uint16_t)(h - rowsDone), stripH);
    size_t bytes = (size_t)w * rows * 2;
    f.read((uint8_t*)grAssetBuf, bytes);

    uint32_t pixCount = (uint32_t)w * rows;
    for (uint32_t i = 0; i < pixCount; i++) {
      uint16_t px = grAssetBuf[i];
      uint8_t pr = (px >> 11) & 0x1F;
      uint8_t pg = (px >> 5) & 0x3F;
      uint8_t pb = px & 0x1F;
      uint8_t brightness = (pr > pg / 2) ? pr : pg / 2;
      if (pb > brightness) brightness = pb;
      pr = (tr * brightness) >> 5;
      pg = (tg * brightness) >> 6;
      pb = (tb * brightness) >> 5;
      grAssetBuf[i] = (pr << 11) | (pg << 5) | pb;
    }

    gfx->draw16bitRGBBitmap(x, y + rowsDone, grAssetBuf, w, rows);
    rowsDone += rows;
  }

  f.close();
  return true;
}

// ---- draw scaled (integer scale 2x, 3x, 4x) — small icons only -----------
static inline bool grAssetDrawScaled(Arduino_GFX* gfx, const char* path,
                                      int16_t x, int16_t y, uint8_t scale) {
  if (!grSdReady || !gfx || scale < 1 || scale > 4) return false;
  if (scale == 1) return grAssetDraw(gfx, path, x, y);

  File f = SD.open(path, FILE_READ);
  if (!f) return false;

  uint8_t hdr[4];
  if (f.read(hdr, 4) != 4) { f.close(); return false; }
  uint16_t w = hdr[0] | (hdr[1] << 8);
  uint16_t h = hdr[2] | (hdr[3] << 8);

  if (w == 0 || h == 0 || w > 64 || h > 64) { f.close(); return false; }

  size_t bytes = (size_t)w * h * 2;
  if (bytes > sizeof(grAssetBuf)) { f.close(); return false; }

  f.read((uint8_t*)grAssetBuf, bytes);
  f.close();

  for (uint16_t row = 0; row < h; row++) {
    for (uint16_t col = 0; col < w; col++) {
      uint16_t px = grAssetBuf[row * w + col];
      if (px != 0x0000) {
        gfx->fillRect(x + col * scale, y + row * scale, scale, scale, px);
      }
    }
  }
  return true;
}

// ---- draw raw boot frame (no header, full 320x480, strip buffered) ---------
static inline bool grAssetDrawBootFrame(Arduino_GFX* gfx, const char* path) {
  if (!grSdReady || !gfx) return false;

  File f = SD.open(path, FILE_READ);
  if (!f) return false;

  for (uint16_t strip = 0; strip < 15; strip++) {
    size_t rd = f.read((uint8_t*)grAssetBuf, GR_ASSET_STRIP_H * GR_SCR_W * 2);
    if (rd == 0) break;
    uint16_t rows = rd / (GR_SCR_W * 2);
    gfx->draw16bitRGBBitmap(0, strip * GR_ASSET_STRIP_H, grAssetBuf, GR_SCR_W, rows);
  }

  f.close();
  return true;
}

// ---- check if asset exists on SD -------------------------------------------
static inline bool grAssetExists(const char* path) {
  return grSdReady && SD.exists(path);
}

// ---- get asset dimensions without drawing ----------------------------------
static inline bool grAssetSize(const char* path, uint16_t& w, uint16_t& h) {
  GrAssetHeader hdr;
  if (!grAssetReadHeader(path, hdr)) return false;
  w = hdr.width;
  h = hdr.height;
  return true;
}

// ---- icon convenience: draw 16x16 or 24x24 status icon at position --------
static inline void grAssetIcon16(Arduino_GFX* gfx, const char* path,
                                  int16_t x, int16_t y) {
  if (!grAssetDraw(gfx, path, x, y)) {
    gfx->fillRect(x, y, 16, 16, GR_C_DIM);
  }
}

static inline void grAssetIcon24(Arduino_GFX* gfx, const char* path,
                                  int16_t x, int16_t y) {
  if (!grAssetDraw(gfx, path, x, y)) {
    gfx->fillRect(x, y, 24, 24, GR_C_DIM);
  }
}

// ---- draw icon with fallback text (when asset missing, draw letter) --------
static inline void grAssetIconOrText(Arduino_GFX* gfx, const char* path,
                                      int16_t x, int16_t y,
                                      uint16_t sz, char fallback,
                                      uint16_t color) {
  if (grAssetExists(path)) {
    grAssetDraw(gfx, path, x, y);
  } else {
    gfx->setTextSize(sz > 16 ? 2 : 1);
    gfx->setTextColor(color);
    gfx->setCursor(x + 2, y + 2);
    gfx->print(fallback);
  }
}
