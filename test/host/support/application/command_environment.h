#pragma once
#include "environment.h"
#include "app/touch_calibration_service.h"
#include "hal/ble/ble.h"
#include "hal/gps/gps.h"
#include "hal/rtc/rtc.h"
#include "hal/wifi/wifi.h"
#include "hal/ina226/ina226.h"
#include "timing/time_sync.h"
#include <esp_timer.h>

namespace host_project {
inline TimeSyncStatus sync_status;
inline TimeSyncState sync_state = TimeSyncState::NONE;
inline bool utc_available = false;
inline int64_t accuracy_us = -1;
inline BLEState ble_state = BLEState::DISCONNECTED;
inline bool ntp_ok = true;
inline unsigned ntp_calls = 0, calibration_calls = 0;
inline std::string ntp_servers[3];
inline uint32_t ntp_timeouts[3]{};
inline TouchCalibrationFlowResult calibration_result;
struct TimerModel {
  bool create_ok = true, start_ok = true, active = false;
  unsigned creates = 0, starts = 0, stops = 0, restarts = 0;
  uint64_t delay_us = 0;
  void (*callback)(void *) = nullptr;
};
inline TimerModel timer;
} // namespace host_project
