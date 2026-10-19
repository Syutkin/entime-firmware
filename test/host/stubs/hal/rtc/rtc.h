#pragma once

#include "fake_clock.h"
#include <RTClib.h>
#include <cstdint>

struct RTC {
  bool ready = false;
  bool lost_power = false;
  uint32_t utc_sec = 1800000000;
  unsigned writes = 0;
  int64_t validation_delay_us = 0;
  int64_t read_delay_us = 0;
  int64_t last_write_esp_us = 0;

  bool isReady() const { return ready; }
  bool lostPower() const { return lost_power; }
  uint32_t unixTime() const {
    host::clock.now_us += read_delay_us;
    return utc_sec;
  }
  DateTime now() const { return DateTime(utc_sec); }
  bool timeValid(uint32_t min_unix_sec) const {
    host::clock.now_us += validation_delay_us;
    return ready && !lost_power && utc_sec >= min_unix_sec;
  }
  void setTime(uint32_t seconds) {
    utc_sec = seconds;
    lost_power = false;
    last_write_esp_us = host::clock.now_us;
    ++writes;
  }
};

extern RTC rtc;
