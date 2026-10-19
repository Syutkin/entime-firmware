#pragma once
#include <cstdint>
#include <esp_timer.h>
class RTC {
public:
  bool ready = false, lost_power = false;
  bool read_ok = true, write_ok = true;
  uint32_t utc_sec = 1800000000;
  unsigned writes = 0, aging_reads = 0, aging_writes = 0;
  int64_t last_write_us = 0;
  float aging = 0;
  bool isReady() { return ready; }
  bool lostPower() { return lost_power; }
  uint32_t unixTime() { return utc_sec; }
  bool timeValid(uint32_t min_sec) { return ready && !lost_power && utc_sec >= min_sec; }
  float getTemperature() { return 23.5f; }
  int64_t lastSyncUs() { return last_write_us; }
  void setTime(uint32_t sec) {
    utc_sec = sec; lost_power = false; ++writes; last_write_us = esp_timer_get_time();
  }
  bool getAgingOffsetPpm(float &out) { ++aging_reads; out = aging; return read_ok; }
  bool setAgingOffsetPpm(float value, float &out) {
    ++aging_writes;
    if (!write_ok) return false;
    aging = out = value; return true;
  }
};
extern RTC rtc;
