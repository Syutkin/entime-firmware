#pragma once
#include "Wire.h"
#include <string>
#include <vector>

constexpr int INA226_ERR_NONE = 0;
constexpr uint8_t INA226_1_SAMPLE = 0, INA226_16_SAMPLES = 2;
constexpr uint8_t INA226_588_us = 3, INA226_1100_us = 4;
constexpr uint16_t INA226_CONVERSION_READY = 8;
namespace host_driver {
struct InaModel {
  bool found = true, bootstrap_ready = true, conversion_ready = true;
  int calibration_error = 0, read_error = 0;
  float voltage = 7.5f, current = .05f, power = .375f;
  std::string fail_operation;
  unsigned fail_occurrence = 1, begins = 0, voltage_reads = 0, ready_checks = 0;
  std::vector<std::string> operations;
  float max_current = 0, shunt = 0;
  uint32_t bootstrap_timeout = 0;
  bool operation(const char *name) {
    operations.emplace_back(name);
    unsigned occurrence = 0;
    for (const auto &op : operations) if (op == name) ++occurrence;
    return fail_operation != name || occurrence < fail_occurrence;
  }
};
inline InaModel ina;
}
class INA226 {
public:
  INA226(uint8_t, TwoWire *) {}
  bool begin() { ++host_driver::ina.begins; return host_driver::ina.found; }
  int setMaxCurrentShunt(float current, float shunt) {
    host_driver::ina.max_current = current; host_driver::ina.shunt = shunt;
    return host_driver::ina.calibration_error;
  }
  bool setAverage(uint8_t) { return host_driver::ina.operation("average"); }
  bool setBusVoltageConversionTime(uint8_t) { return host_driver::ina.operation("bus_time"); }
  bool setShuntVoltageConversionTime(uint8_t) { return host_driver::ina.operation("shunt_time"); }
  bool setModeShuntBusContinuous() { return host_driver::ina.operation("continuous"); }
  bool waitConversionReady(uint32_t timeout) {
    host_driver::ina.bootstrap_timeout = timeout; return host_driver::ina.bootstrap_ready;
  }
  bool setAlertPolarity(bool) { return host_driver::ina.operation("polarity"); }
  bool setAlertLatchEnable(bool) { return host_driver::ina.operation("latch"); }
  bool setAlertRegister(uint16_t) { return host_driver::ina.operation("alert"); }
  bool isConversionReady() { ++host_driver::ina.ready_checks; return host_driver::ina.conversion_ready; }
  float getBusVoltage() { ++host_driver::ina.voltage_reads; return host_driver::ina.voltage; }
  float getCurrent() { return host_driver::ina.current; }
  float getPower() { return host_driver::ina.power; }
  int getLastError() { return host_driver::ina.read_error; }
};
