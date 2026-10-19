#pragma once
class Ina226Hal {
public:
  bool ready = false, valid = false;
  float voltage = 7.5f;
  bool isReady() { return ready; }
  bool hasValidSample() { return valid; }
  float getBusVoltage() { return voltage; }
};
extern Ina226Hal ina226;
