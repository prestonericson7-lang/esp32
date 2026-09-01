#include "uart_link.h"

namespace uartlink {

static HardwareSerial *_port = nullptr;

static const uint8_t SYNC0 = 0xAA;
static const uint8_t SYNC1 = 0x55;

enum class State : uint8_t { WAIT_SYNC0,
                              WAIT_SYNC1,
                              TYPE,
                              LEN_LO,
                              LEN_HI,
                              PAYLOAD,
                              CHECKSUM };
static State _state = State::WAIT_SYNC0;
static uint8_t _type;
static uint16_t _len, _idx;
static uint8_t _buf[MAX_PAYLOAD];
static uint8_t _chk;

void begin(HardwareSerial &port, unsigned long baud, int8_t rxPin, int8_t txPin) {
  _port = &port;
  _port->begin(baud, SERIAL_8N1, rxPin, txPin);
  _state = State::WAIT_SYNC0;
}

void send(uint8_t type, const uint8_t *payload, uint16_t len) {
  if (!_port || len > MAX_PAYLOAD) {
    return;
  }
  uint8_t chk = type ^ (uint8_t)(len & 0xFF) ^ (uint8_t)(len >> 8);
  for (uint16_t i = 0; i < len; i++) {
    chk ^= payload[i];
  }
  _port->write(SYNC0);
  _port->write(SYNC1);
  _port->write(type);
  _port->write((uint8_t)(len & 0xFF));
  _port->write((uint8_t)(len >> 8));
  if (len) {
    _port->write(payload, len);
  }
  _port->write(chk);
}

bool poll(Msg &out) {
  if (!_port) {
    return false;
  }
  while (_port->available()) {
    uint8_t b = (uint8_t)_port->read();
    switch (_state) {
      case State::WAIT_SYNC0:
        if (b == SYNC0) {
          _state = State::WAIT_SYNC1;
        }
        break;
      case State::WAIT_SYNC1:
        _state = (b == SYNC1) ? State::TYPE : State::WAIT_SYNC0;
        break;
      case State::TYPE:
        _type = b;
        _state = State::LEN_LO;
        break;
      case State::LEN_LO:
        _len = b;
        _state = State::LEN_HI;
        break;
      case State::LEN_HI:
        _len |= ((uint16_t)b) << 8;
        if (_len > MAX_PAYLOAD) {
          _state = State::WAIT_SYNC0;  // bogus length, resync on next frame
          break;
        }
        _idx = 0;
        _chk = _type ^ (uint8_t)(_len & 0xFF) ^ (uint8_t)(_len >> 8);
        _state = (_len == 0) ? State::CHECKSUM : State::PAYLOAD;
        break;
      case State::PAYLOAD:
        _buf[_idx++] = b;
        _chk ^= b;
        if (_idx >= _len) {
          _state = State::CHECKSUM;
        }
        break;
      case State::CHECKSUM:
        _state = State::WAIT_SYNC0;
        if (b == _chk) {
          out.type = _type;
          out.len = _len;
          if (_len) {
            memcpy(out.payload, _buf, _len);
          }
          return true;
        }
        break;
    }
  }
  return false;
}

}  // namespace uartlink
