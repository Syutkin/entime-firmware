#pragma once
#include <cstdint>
enum class GPSState { OFF, SEARCHING, ACTIVE };
struct GPSSatelliteInfo {};
class HostNmea {
public:
  bool isValid();
  int getNumSatellites();
};
class GPS {
public:
  static constexpr uint8_t MAX_SATELLITES = 32;
  GPSState getState();
  HostNmea &nmea();
  bool nmeaFresh();
  uint8_t satellites(GPSSatelliteInfo *, uint8_t, bool);
  int64_t lastFixUs();
  bool lastSentenceUs(int64_t &);
  bool lastGsvUs(int64_t &);
  bool lastUtcUpdateUs(int64_t &);
};
extern GPS gps;
