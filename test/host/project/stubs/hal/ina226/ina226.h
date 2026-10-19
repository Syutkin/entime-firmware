#pragma once
class Ina226Hal {
public:
  bool isReady();
  bool hasValidSample();
  float getBusVoltage();
};
extern Ina226Hal ina226;
