#pragma once
// ############################################################################
//  gs_audio.h  —  GHOST TX S3 NEURAL CO-PROCESSOR  /  audio codec driver
//
//  LAFVIN audio codec module: I2S digital audio + I2C control.
//  Dual MEMS microphones (stereo input) + mono speaker output with PA.
//
//  I2S pins:  BCLK=4, WS=5, DIN=6 (S3→codec speaker), DOUT=7 (codec mics→S3), MCLK=8
//  I2C pins:  SCL=14, SDA=15  (codec register access)
//  PA_EN=16:  HIGH = speaker amplifier on
//
//  Uses the ESP-IDF I2S driver (driver/i2s.h) for ESP32-S3.
// ############################################################################

#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s.h>
#include "gs_config.h"

#define GS_I2S_PORT       I2S_NUM_0
#define GS_I2S_SAMPLE_RATE 16000
#define GS_I2S_BUF_COUNT  4
#define GS_I2S_BUF_LEN    512

static bool gsAudioReady = false;
static bool gsPaEnabled  = false;
static uint8_t gsCodecAddr = 0;

// ===== Codec I2C register access =============================================

static inline bool gsCodecWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(gsCodecAddr);
  Wire.write(reg);
  Wire.write(val);
  uint8_t err = Wire.endTransmission();
  if (err != 0) {
    Serial.printf("[audio] I2C write reg 0x%02X=0x%02X FAILED (err=%d)\n", reg, val, err);
    return false;
  }
  return true;
}

static inline uint8_t gsCodecRead(uint8_t reg) {
  Wire.beginTransmission(gsCodecAddr);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(gsCodecAddr, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;
}

// ===== ES8388 codec I2C initialization =======================================

static inline bool gsCodecInit() {
  Wire.begin(GS_PIN_CODEC_SDA, GS_PIN_CODEC_SCL, 100000);
  delay(50);

  Serial.println(F("[audio] scanning I2C for codec chip..."));
  const uint8_t knownAddrs[] = { 0x10, 0x11, 0x18, 0x19 };
  for (int i = 0; i < 4; i++) {
    Wire.beginTransmission(knownAddrs[i]);
    if (Wire.endTransmission() == 0) {
      gsCodecAddr = knownAddrs[i];
      break;
    }
  }

  if (gsCodecAddr == 0) {
    Serial.println(F("[audio] not at known addrs, full bus scan:"));
    for (uint8_t a = 0x08; a < 0x78; a++) {
      Wire.beginTransmission(a);
      if (Wire.endTransmission() == 0) {
        Serial.printf("[audio]   found device at 0x%02X\n", a);
        if (gsCodecAddr == 0) gsCodecAddr = a;
      }
    }
  }

  if (gsCodecAddr == 0) {
    Serial.println(F("[audio] NO I2C devices found — check codec wiring"));
    return false;
  }
  Serial.printf("[audio] codec at I2C addr 0x%02X\n", gsCodecAddr);

  // ES8388 init sequence (slave mode, I2S 16-bit, DAC+ADC enabled)
  gsCodecWrite(0x00, 0x80); delay(50);  // soft reset
  gsCodecWrite(0x00, 0x12);             // ENREF=1, SeqEn=0, VMIDSEL=500k
  gsCodecWrite(0x01, 0x50);             // power config, low-power VREF
  delay(100);                           // let VREF settle
  gsCodecWrite(0x02, 0x00);             // ADC + DAC digital power on
  gsCodecWrite(0x03, 0x00);             // ADC power: L+R analog, L+R ADC, mic bias all on
  gsCodecWrite(0x04, 0x3C);             // DAC power: L+R DAC on, LOUT1/ROUT1/LOUT2/ROUT2 on
  gsCodecWrite(0x08, 0x00);             // slave mode (ESP32 is I2S master)

  // ADC settings
  gsCodecWrite(0x09, 0x88);             // MIC PGA gain = 24dB both channels
  gsCodecWrite(0x0A, 0x00);             // select LIN1/RIN1 as ADC input
  gsCodecWrite(0x0C, 0x0C);             // ADC I2S format, 16-bit word length
  gsCodecWrite(0x0D, 0x02);             // ADC MCLK/Fs ratio
  gsCodecWrite(0x10, 0x00);             // left ADC volume 0dB
  gsCodecWrite(0x11, 0x00);             // right ADC volume 0dB

  // DAC settings
  gsCodecWrite(0x17, 0x18);             // DAC I2S format, 16-bit word length
  gsCodecWrite(0x18, 0x02);             // DAC MCLK/Fs ratio
  gsCodecWrite(0x19, 0x00);             // DAC unmute, soft ramp disabled
  gsCodecWrite(0x1A, 0x00);             // left DAC volume 0dB
  gsCodecWrite(0x1B, 0x00);             // right DAC volume 0dB

  // Mixer: route DAC to speaker outputs
  gsCodecWrite(0x26, 0x00);             // DACL → left mixer
  gsCodecWrite(0x27, 0x00);             // DACR → right mixer
  gsCodecWrite(0x2E, 0x1E);             // LOUT1 volume
  gsCodecWrite(0x2F, 0x1E);             // ROUT1 volume
  gsCodecWrite(0x30, 0x1E);             // LOUT2 volume
  gsCodecWrite(0x31, 0x1E);             // ROUT2 volume

  // Verify config
  uint8_t r0 = gsCodecRead(0x00);
  uint8_t r4 = gsCodecRead(0x04);
  Serial.printf("[audio] ES8388 verify: CTRL1=0x%02X DACPWR=0x%02X\n", r0, r4);
  Serial.println(F("[audio] codec I2C configured OK"));
  return true;
}

// ===== I2S init (full duplex: mic in + speaker out) ==========================

static inline bool gsAudioBegin() {
  pinMode(GS_PIN_PA_EN, OUTPUT);
  digitalWrite(GS_PIN_PA_EN, LOW);

  i2s_config_t i2sCfg = {};
  i2sCfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
  i2sCfg.sample_rate = GS_I2S_SAMPLE_RATE;
  i2sCfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2sCfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  i2sCfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2sCfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2sCfg.dma_buf_count = GS_I2S_BUF_COUNT;
  i2sCfg.dma_buf_len = GS_I2S_BUF_LEN;
  i2sCfg.use_apll = false;
  i2sCfg.tx_desc_auto_clear = true;

  if (i2s_driver_install(GS_I2S_PORT, &i2sCfg, 0, NULL) != ESP_OK) {
    Serial.println(F("[audio] I2S driver install FAILED"));
    return false;
  }

  i2s_pin_config_t pinCfg = {};
  pinCfg.bck_io_num   = GS_PIN_I2S_BCLK;
  pinCfg.ws_io_num    = GS_PIN_I2S_WS;
  pinCfg.data_out_num = GS_PIN_I2S_DIN;
  pinCfg.data_in_num  = GS_PIN_I2S_DOUT;
#ifdef I2S_PIN_NO_CHANGE
  pinCfg.mck_io_num   = GS_PIN_I2S_MCLK;
#endif

  if (i2s_set_pin(GS_I2S_PORT, &pinCfg) != ESP_OK) {
    Serial.println(F("[audio] I2S pin config FAILED"));
    return false;
  }

  i2s_zero_dma_buffer(GS_I2S_PORT);
  Serial.println(F("[audio] I2S ready (16kHz 16-bit stereo)"));

  // Initialize codec chip via I2C (ES8388 DAC+ADC power on)
  if (!gsCodecInit()) {
    Serial.println(F("[audio] WARNING: codec I2C init failed — I2S active but no analog output"));
  }

  gsAudioReady = true;
  return true;
}

// ===== PA (speaker amplifier) control ========================================

static inline void gsPaOn() {
  if (!gsPaEnabled) {
    digitalWrite(GS_PIN_PA_EN, HIGH);
    gsPaEnabled = true;
    delay(10);
  }
}

static inline void gsPaOff() {
  if (gsPaEnabled) {
    digitalWrite(GS_PIN_PA_EN, LOW);
    gsPaEnabled = false;
  }
}

// ===== Speaker output: play a tone ===========================================

static inline void gsAudioTone(uint16_t freqHz, uint16_t durMs) {
  if (!gsAudioReady || freqHz == 0 || durMs == 0) return;
  gsPaOn();

  int totalSamples = (GS_I2S_SAMPLE_RATE * durMs) / 1000;
  int16_t buf[128];
  float phase = 0.0f;
  float phaseInc = 2.0f * PI * freqHz / GS_I2S_SAMPLE_RATE;
  int written = 0;

  while (written < totalSamples) {
    int chunk = min(64, totalSamples - written);
    for (int i = 0; i < chunk; i++) {
      int16_t sample = (int16_t)(sinf(phase) * 16000);
      buf[i * 2]     = sample; // left
      buf[i * 2 + 1] = sample; // right
      phase += phaseInc;
      if (phase > 2.0f * PI) phase -= 2.0f * PI;
    }
    size_t bytesOut = 0;
    i2s_write(GS_I2S_PORT, buf, chunk * 4, &bytesOut, portMAX_DELAY);
    written += chunk;
  }

  i2s_zero_dma_buffer(GS_I2S_PORT);
  gsPaOff();
}

// ===== Speaker output: play two tones (alert chime) ==========================

static inline void gsAudioChime(uint16_t freq1, uint16_t freq2,
                                 uint16_t dur1, uint16_t dur2) {
  gsAudioTone(freq1, dur1);
  gsAudioTone(freq2, dur2);
}

// ===== Microphone input: read a buffer of samples ============================

static inline int gsAudioReadMic(int16_t* outBuf, int maxSamples) {
  if (!gsAudioReady || maxSamples <= 0) return 0;
  size_t bytesRead = 0;
  int samplesWanted = min(maxSamples, 512);
  int16_t raw[1024];
  i2s_read(GS_I2S_PORT, raw, samplesWanted * 4, &bytesRead, 100 / portTICK_PERIOD_MS);
  int samplesGot = bytesRead / 4;
  for (int i = 0; i < samplesGot; i++) {
    outBuf[i] = raw[i * 2]; // left channel only (or mix both)
  }
  return samplesGot;
}

// ===== Audio energy detection (for wake word pre-filter) =====================

static inline float gsAudioEnergy() {
  if (!gsAudioReady) return 0.0f;
  int16_t buf[256];
  int n = gsAudioReadMic(buf, 256);
  if (n == 0) return 0.0f;
  float sum = 0.0f;
  for (int i = 0; i < n; i++) {
    float s = (float)buf[i] / 32768.0f;
    sum += s * s;
  }
  return sum / n;
}

// ===== Built-in alert sounds ================================================

static inline void gsAudioAlertLow()    { gsAudioChime(800, 600, 100, 100); }
static inline void gsAudioAlertMedium() { gsAudioChime(1200, 800, 100, 150); }
static inline void gsAudioAlertHigh()   { gsAudioChime(1600, 2000, 80, 120); }
static inline void gsAudioAlertCritical() {
  for (int i = 0; i < 3; i++) { gsAudioTone(2400, 80); delay(40); }
}
static inline void gsAudioBootChime()   { gsAudioChime(523, 784, 100, 200); }
