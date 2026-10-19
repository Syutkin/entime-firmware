#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
class TwoWire {
public:
  uint8_t address = 0, register_value = 0;
  uint8_t end_error = 0, returned_bytes = 1;
  unsigned transmissions = 0, requests = 0;
  bool last_stop = true;
  std::vector<uint8_t> written;
  void beginTransmission(uint8_t addr) { address = addr; written.clear(); ++transmissions; }
  size_t write(uint8_t byte) { written.push_back(byte); return 1; }
  uint8_t endTransmission(bool stop = true) {
    last_stop = stop;
    if (end_error == 0 && written.size() == 2) register_value = written[1];
    return end_error;
  }
  uint8_t requestFrom(uint8_t addr, uint8_t count) {
    address = addr; ++requests; return count == 1 ? returned_bytes : 0;
  }
  int read() { return register_value; }
};
inline TwoWire Wire;
