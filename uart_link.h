#pragma once
#include <Arduino.h>

// Simple framed protocol between the camera board (A) and the OLED+keypad
// panel board (B). Wire A's TX to B's RX, A's RX to B's TX, and tie GNDs
// together. Same file is used, unmodified, on both boards - each side just
// sends some message types and receives others.
//
// Frame layout: [0xAA][0x55][TYPE][LEN_LO][LEN_HI][PAYLOAD...][CHECKSUM]
// CHECKSUM = TYPE ^ LEN_LO ^ LEN_HI ^ (every payload byte)

namespace uartlink {

enum : uint8_t {
  // Camera board (A) -> Panel board (B)
  MSG_PREVIEW = 0x01,  // 1024 bytes: 128x64 1bpp bitmap, row-major MSB-first
                        // (matches Adafruit_GFX::drawBitmap() directly)
  MSG_EVENT = 0x02,     // 3 bytes: status, faceId, enrolledCount
                        //   status: 0=no face, 1=detected (recognition off),
                        //           2=recognized, 3=unknown/unrecognized face
                        //   faceId: valid (>=0) only when status==2
  MSG_STATUS = 0x03,    // 6 bytes: wifiOn(1), ip[4] (0.0.0.0 if not connected), enrolledCount(1)

  // Panel board (B) -> Camera board (A)
  CMD_ENROLL = 0x10,     // 0 bytes: arm enrollment for the next detected face
  CMD_DELETE = 0x11,     // 1 byte: face id to delete
  CMD_CLEAR_ALL = 0x12,  // 0 bytes: delete every enrolled face
  CMD_WIFI = 0x13,       // 1 byte: 0=off, 1=on
  CMD_REQ_STATUS = 0x14, // 0 bytes: ask for a fresh MSG_STATUS reply
};

constexpr size_t MAX_PAYLOAD = 1024;

struct Msg {
  uint8_t type;
  uint8_t payload[MAX_PAYLOAD];
  uint16_t len;
};

// Starts the given HardwareSerial (e.g. Serial1) with custom RX/TX pins.
void begin(HardwareSerial &port, unsigned long baud, int8_t rxPin, int8_t txPin);

void send(uint8_t type, const uint8_t *payload, uint16_t len);

// Call every loop() iteration. Returns true and fills `out` when a full,
// checksum-valid frame has just finished arriving. Non-blocking.
bool poll(Msg &out);

}  // namespace uartlink
