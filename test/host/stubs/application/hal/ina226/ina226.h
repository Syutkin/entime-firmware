#pragma once
#include <cstdint>
enum class InaBatteryLevel : uint8_t { NoData, Critical, Empty, Low, Mid, Full };
class Ina226Hal {
public:
  bool ready = false, valid = false;
  float voltage = 7.5f;
  bool isReady() { return ready; }
  bool hasValidSample() { return valid; }
  float getBusVoltage() { return voltage; }
};
extern Ina226Hal ina226;
