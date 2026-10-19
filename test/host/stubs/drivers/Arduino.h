#pragma once
#include "../application/Arduino.h"
#include "../../support/application/environment.h"
#include <deque>
#include <cctype>
#include <cmath>

#define OUTPUT 3
#define HIGH 1
#define LOW 0
#define SERIAL_8N1 0
namespace host_driver {
inline std::array<int,64> levels{};
inline std::deque<uint8_t> uart;
inline unsigned uart_begins = 0;
inline uint32_t baud = 0;
inline int rx_pin = -1, tx_pin = -1;
inline void feed(const std::string &s) { uart.insert(uart.end(),s.begin(),s.end()); }
}
inline void digitalWrite(int gpio, int level) { host_driver::levels.at(gpio) = level; }
class HardwareSerial : public Stream {
public:
  explicit HardwareSerial(int) {}
  void begin(uint32_t baud, int, int rx, int tx) {
    ++host_driver::uart_begins; host_driver::baud = baud;
    host_driver::rx_pin = rx; host_driver::tx_pin = tx;
  }
  int available() override { return static_cast<int>(host_driver::uart.size()); }
  int read() override {
    if (host_driver::uart.empty()) return -1;
    const auto c = host_driver::uart.front(); host_driver::uart.pop_front(); return c;
  }
  int peek() override { return host_driver::uart.empty() ? -1 : host_driver::uart.front(); }
  void flush() override {}
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t *, size_t size) override { return size; }
};
