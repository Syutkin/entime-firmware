#pragma once
#include <cstdint>
class RTC {
public:
  bool isReady();
  bool lostPower();
  uint32_t unixTime();
  float getTemperature();
  int64_t lastSyncUs();
  bool getAgingOffsetPpm(float &);
  bool setAgingOffsetPpm(float, float &);
};
extern RTC rtc;
