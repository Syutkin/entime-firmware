#pragma once

#include <cstdint>
#include <ctime>

// Only the calendar-to-Unix conversion used by time_sync.cpp is modeled.
class DateTime {
public:
  explicit DateTime(uint32_t unix_sec) : unix_sec_(unix_sec) {}

  DateTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
           uint8_t minute, uint8_t second) {
    std::tm value{};
    value.tm_year = year - 1900;
    value.tm_mon = month - 1;
    value.tm_mday = day;
    value.tm_hour = hour;
    value.tm_min = minute;
    value.tm_sec = second;
    unix_sec_ = static_cast<uint32_t>(timegm(&value));
  }

  uint32_t unixtime() const { return unix_sec_; }
  uint16_t year() const { return calendar().tm_year + 1900; }
  uint8_t month() const { return calendar().tm_mon + 1; }
  uint8_t day() const { return calendar().tm_mday; }
  uint8_t hour() const { return calendar().tm_hour; }
  uint8_t minute() const { return calendar().tm_min; }
  uint8_t second() const { return calendar().tm_sec; }

private:
  std::tm calendar() const {
    const std::time_t seconds = unix_sec_;
    return *std::gmtime(&seconds);
  }
  uint32_t unix_sec_;
};
