#pragma once
#include <cstdint>
#include <ctime>
#include <esp_timer.h>
enum class GPSState { OFF, SEARCHING, ACTIVE };
struct GPSSatelliteInfo {};
class HostNmea {
public:
  bool valid = false;
  uint32_t utc_sec = 1800000000;
  int used = 7;
  bool isValid() { return valid; }
  int getNumSatellites() { return used; }
  uint16_t getYear() { return calendar().tm_year + 1900; }
  uint8_t getMonth() { return calendar().tm_mon + 1; }
  uint8_t getDay() { return calendar().tm_mday; }
  uint8_t getHour() { return calendar().tm_hour; }
  uint8_t getMinute() { return calendar().tm_min; }
  uint8_t getSecond() { return calendar().tm_sec; }
private:
  std::tm calendar() { std::time_t seconds = utc_sec; return *std::gmtime(&seconds); }
};
class GPS {
public:
  static constexpr uint8_t MAX_SATELLITES = 32;
  bool ready = false;
  GPSState state = GPSState::OFF;
  HostNmea data;
  int64_t sentence_start_us = 0, utc_received_us = 0;
  bool isReady() { return ready; }
  GPSState getState() { return state; }
  HostNmea &nmea() { return data; }
  bool utcFresh(int64_t max_age) {
    const auto age = esp_timer_get_time() - utc_received_us;
    return ready && utc_received_us > 0 && age >= 0 && age <= max_age;
  }
  bool nmeaFresh() { return utcFresh(1500000); }
  uint8_t satellites(GPSSatelliteInfo *, uint8_t, bool) { return 0; }
  int64_t lastFixUs() { return data.valid ? utc_received_us : 0; }
  bool lastSentenceUs(int64_t &out) { out = sentence_start_us; return out > 0; }
  bool lastUtcUpdateSentenceStartUs(int64_t &out) { return lastSentenceUs(out); }
  bool lastGsvUs(int64_t &out) { out = 0; return false; }
  bool lastUtcUpdateUs(int64_t &out) { out = utc_received_us; return out > 0; }
};
extern GPS gps;
