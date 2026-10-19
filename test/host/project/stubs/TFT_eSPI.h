#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#define TFT_MAGENTA 0xf81f
#define TFT_BLACK 0
class TFT_eSPI {
public:
  bool pressed = false;
  unsigned reads = 0;
  std::array<uint16_t, 5> calibration{};
  void setTouch(uint16_t *data) { std::copy_n(data, 5, calibration.begin()); }
  void calibrateTouch(uint16_t *data, uint16_t, uint16_t, uint8_t) {
    std::copy(calibration.begin(), calibration.end(), data);
  }
  bool getTouch(uint16_t *x, uint16_t *y) {
    ++reads; *x = 100; *y = 80; return pressed;
  }
};
