#pragma once
#include "../base/RTClib.h"
#include "Wire.h"
enum Ds3231SqwPinMode { DS3231_SquareWave1Hz = 0 };
namespace host_driver {
struct Ds3231Model {
  bool found = true, lost_power = false;
  uint32_t seconds = 1704067200;
  float temperature = 23.5f;
  unsigned begins = 0, writes = 0, sqw_setups = 0;
  TwoWire *wire = nullptr;
};
inline Ds3231Model ds3231;
}
class RTC_DS3231 {
public:
  bool begin(TwoWire *wire) {
    auto &m = host_driver::ds3231; ++m.begins; m.wire = wire; return m.found;
  }
  bool lostPower() { return host_driver::ds3231.lost_power; }
  DateTime now() { return DateTime(host_driver::ds3231.seconds); }
  float getTemperature() { return host_driver::ds3231.temperature; }
  void writeSqwPinMode(Ds3231SqwPinMode) { ++host_driver::ds3231.sqw_setups; }
  void adjust(const DateTime &dt) {
    auto &m = host_driver::ds3231; m.seconds = dt.unixtime(); m.lost_power = false; ++m.writes;
  }
};
