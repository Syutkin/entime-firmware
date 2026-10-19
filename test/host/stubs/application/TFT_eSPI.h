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
  uint16_t x = 100, y = 80;
  unsigned calibration_calls = 0, applied_calls = 0;
  bool use_wizard_result = false;
  std::array<uint16_t,5> wizard_result{};
  std::array<uint16_t, 5> calibration{};
  void setTouch(uint16_t *data) { ++applied_calls; std::copy_n(data, 5, calibration.begin()); }
  void calibrateTouch(uint16_t *data, uint16_t, uint16_t, uint8_t) {
    ++calibration_calls;
    const auto &result = use_wizard_result ? wizard_result : calibration;
    std::copy(result.begin(), result.end(), data);
  }
  bool getTouch(uint16_t *x, uint16_t *y) {
    ++reads; *x = this->x; *y = this->y; return pressed;
  }
};
