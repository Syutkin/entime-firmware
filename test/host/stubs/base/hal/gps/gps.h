#pragma once

#include "Arduino.h"
#include "fake_clock.h"
#include <cstdint>
#include <ctime>

struct HostNmea {
  bool valid = false;
  uint32_t utc_sec = 1800000000;

  bool isValid() const { return valid; }
  uint16_t getYear() const { return calendar().tm_year + 1900; }
  uint8_t getMonth() const { return calendar().tm_mon + 1; }
  uint8_t getDay() const { return calendar().tm_mday; }
  uint8_t getHour() const { return calendar().tm_hour; }
  uint8_t getMinute() const { return calendar().tm_min; }
  uint8_t getSecond() const { return calendar().tm_sec; }

private:
  std::tm calendar() const {
    const std::time_t seconds = utc_sec;
    return *std::gmtime(&seconds);
  }
};

struct GPS {
  bool ready = true;
  HostNmea data;
  int64_t sentence_start_us = 0;
  int64_t utc_received_us = 0;

  bool isReady() const { return ready; }
  HostNmea &nmea() { return data; }
  bool utcFresh(int64_t max_age_us) const {
    const int64_t age_us = host::clock.now_us - utc_received_us;
    return ready && utc_received_us != 0 && age_us >= 0 && age_us <= max_age_us;
  }
  bool lastUtcUpdateSentenceStartUs(int64_t &out) const {
    out = sentence_start_us;
    return out != 0;
  }
};

extern GPS gps;
