#include "command_environment.h"
#include "timing/sntp.h"
#include <esp_system.h>

BLE ble;
BLEState BLE::getState() { return host_project::ble_state; }
uint32_t esp_get_free_heap_size() { return 123456; }
esp_reset_reason_t esp_reset_reason() { return ESP_RST_POWERON; }
void esp_restart() { ++host_project::timer.restarts; }
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out) {
  auto &timer = host_project::timer; ++timer.creates;
  if (!timer.create_ok) return -1;
  timer.callback = args->callback; *out = &timer; return ESP_OK;
}
bool esp_timer_is_active(esp_timer_handle_t) { return host_project::timer.active; }
esp_err_t esp_timer_stop(esp_timer_handle_t) {
  ++host_project::timer.stops; host_project::timer.active = false; return ESP_OK;
}
esp_err_t esp_timer_start_once(esp_timer_handle_t, uint64_t delay) {
  auto &timer = host_project::timer; ++timer.starts;
  if (!timer.start_ok) return -1;
  timer.delay_us = delay; timer.active = true; return ESP_OK;
}
TimeSyncStatus time_sync_status() { return host_project::sync_status; }
TimeSyncState time_sync_state() { return host_project::sync_state; }
int64_t time_sync_estimate_accuracy_us() { return host_project::accuracy_us; }
bool time_sync_esp_to_utc_us(int64_t esp, int64_t &out) {
  out = 1704067200000000LL + esp; return host_project::utc_available;
}
bool pps_is_locked() { return false; }
bool pps_get_raw(int64_t &, uint32_t &) { return false; }
bool syncRtcUtcFromNtpPrecise(RTC &rtc, WiFiManager &, const char *a, const char *b,
                            const char *c, uint32_t sync, uint32_t edge,
                            uint32_t window, uint32_t *duration) {
  ++host_project::ntp_calls;
  host_project::ntp_servers[0] = a; host_project::ntp_servers[1] = b;
  host_project::ntp_servers[2] = c;
  host_project::ntp_timeouts[0] = sync; host_project::ntp_timeouts[1] = edge;
  host_project::ntp_timeouts[2] = window;
  if (duration) *duration = 37;
  if (host_project::ntp_ok) rtc.setTime(1704067200);
  return host_project::ntp_ok;
}
TouchCalibrationFlowResult runTouchCalibrationFlow(bool interactive) {
  require(interactive, "command must request interactive calibration flow");
  ++host_project::calibration_calls; return host_project::calibration_result;
}
