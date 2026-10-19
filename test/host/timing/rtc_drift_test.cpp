#define HOST_REAL_RTC_SQW
#include "../support/time_sync_fixture.h"

namespace {

void edge_at(int64_t timestamp_us, uint32_t utc_sec, int64_t delay_us = 1000) {
  host::clock.now_us = timestamp_us;
  host::fire_gpio(RTC_SQW_PIN);
  rtc.utc_sec = utc_sec;
  host::clock.now_us += delay_us;
  time_sync_update();
}

void start_rtc(bool auto_sync, int64_t phase_us = 0) {
  reset();
  rtc.ready = true;
  gps.ready = false;
  settings.sync.source = 2;
  settings.sync.auto_sync = auto_sync;
  for (unsigned second = 0; second <= 5; ++second)
    edge_at(kBaseEspUs + second * kSecondUs + phase_us,
            kBaseUtcSec + second);
  require(time_sync_state() == TimeSyncState::RTC_OK,
          "real SQW warmup should establish RTC UTC");
}

// The oscillator starts drifting after lock. Expected UTC comes from elapsed
// RTC seconds, independently of esp_timer, gettimeofday and the current anchor.
template <int DriftUs, bool AutoSync, int64_t DelayUs = 1000>
void rtc_bounds_oscillator_drift() {
  start_rtc(AutoSync);
  const int64_t original_system_offset = host::clock.system_offset_us;
  for (unsigned second = 6; second <= 3605; ++second) {
    const int64_t edge_us = kBaseEspUs + 5 * kSecondUs +
                           (second - 5) * (kSecondUs + DriftUs);
    edge_at(edge_us, kBaseUtcSec + second, DelayUs);
    expect(time_sync_state() == TimeSyncState::RTC_OK,
           "oscillator drift must not cause SQW lock or count binding loss");
    const int64_t edge_utc =
        static_cast<int64_t>(kBaseUtcSec + second) * kSecondUs;
    expect_error_at_most(event_utc_us(edge_us), edge_utc, 0);
    // Check captured events on either side of the latest edge, including a
    // timestamp captured before loop refreshed the anchor.
    for (int64_t offset : {-100000LL, 250000LL, 900000LL}) {
      const int64_t esp_offset = offset * (kSecondUs + DriftUs) / kSecondUs;
      expect_error_at_most(event_utc_us(edge_us + esp_offset),
                           edge_utc + offset, 500);
    }
    if (AutoSync)
      expect_error_at_most(host::clock.system_utc_us(),
                           edge_utc + DelayUs, 1500);
    else
      expect(host::clock.system_offset_us == original_system_offset,
             "auto_sync=false must leave the drifting system clock alone");
  }
}

template <int DriftUs, bool AutoSync> void calibrated_phase_survives_drift() {
  constexpr int64_t phase_us = 354000;
  start_rtc(AutoSync, phase_us);
  settings.sync.source = 0;
  gps.ready = true;
  process_pps(16, kBaseUtcSec + 6, true, 1000);
  require(time_sync_state() == TimeSyncState::GPS_OK,
          "GPS must calibrate SQW phase before fallback");
  pps_signal.locked = false;
  for (unsigned second = 6; second <= 606; ++second) {
    const int64_t edge_us = kBaseEspUs + 6 * kSecondUs + phase_us +
                           (second - 6) * (kSecondUs + DriftUs);
    edge_at(edge_us, kBaseUtcSec + second);
    expect(time_sync_state() == TimeSyncState::RTC_OK,
           "GPS loss must select locked RTC SQW");
    expect_error_at_most(
        event_utc_us(edge_us),
        static_cast<int64_t>(kBaseUtcSec + second) * kSecondUs + phase_us, 0);
  }
  // Return to GPS with the same ideal UTC after ten minutes on RTC.
  process_pps(617, kBaseUtcSec + 607, true, 1000);
  expect(time_sync_state() == TimeSyncState::GPS_OK,
         "GPS must recover after RTC holdover with oscillator drift");
  expect_error_at_most(
      event_utc_us(pps_signal.edge_us),
      static_cast<int64_t>(kBaseUtcSec + 607) * kSecondUs, 0);
}

void changing_auto_sync_keeps_rtc_utc() {
  start_rtc(false);
  for (unsigned second = 6; second <= 305; ++second) {
    if (second == 106) settings.sync.auto_sync = true;
    if (second == 206) settings.sync.auto_sync = false;
    const int64_t edge_us = kBaseEspUs + 5 * kSecondUs +
                           (second - 5) * (kSecondUs + 100);
    edge_at(edge_us, kBaseUtcSec + second);
    expect_error_at_most(
        event_utc_us(edge_us),
        static_cast<int64_t>(kBaseUtcSec + second) * kSecondUs, 0);
    expect(time_sync_state() == TimeSyncState::RTC_OK,
           "toggling system clock discipline must preserve RTC UTC");
  }
}

void periodic_reanchor_ignores_system_clock_step() {
  start_rtc(true);
  for (unsigned second = 6; second <= 45; ++second) {
    // Inject a separate system clock correction before every periodic refresh.
    host::clock.system_offset_us += 20000;
    edge_at(kBaseEspUs + second * kSecondUs, kBaseUtcSec + second);
    const int64_t expected =
        static_cast<int64_t>(kBaseUtcSec + second) * kSecondUs;
    expect_error_at_most(event_utc_us(host::clock.now_us), expected + 1000, 0);
    expect_error_at_most(host::clock.system_utc_us(), expected + 1000, 0);
  }
}

const Test kTests[] = {
    {"rtc_drift_fast_100ppm_auto_on", rtc_bounds_oscillator_drift<100, true>},
    {"rtc_drift_slow_100ppm_auto_on", rtc_bounds_oscillator_drift<-100, true>},
    {"rtc_drift_fast_500ppm_auto_on", rtc_bounds_oscillator_drift<500, true>},
    {"rtc_drift_slow_500ppm_auto_on", rtc_bounds_oscillator_drift<-500, true>},
    {"rtc_drift_fast_100ppm_auto_off", rtc_bounds_oscillator_drift<100, false>},
    {"rtc_drift_slow_100ppm_auto_off", rtc_bounds_oscillator_drift<-100, false>},
    {"rtc_drift_fast_500ppm_auto_off", rtc_bounds_oscillator_drift<500, false>},
    {"rtc_drift_slow_500ppm_auto_off", rtc_bounds_oscillator_drift<-500, false>},
    {"rtc_drift_fast_delayed_loop",
     rtc_bounds_oscillator_drift<500, true, 850000>},
    {"rtc_drift_slow_delayed_loop",
     rtc_bounds_oscillator_drift<-500, true, 850000>},
    {"rtc_drift_calibrated_phase_fast_auto_on",
     calibrated_phase_survives_drift<100, true>},
    {"rtc_drift_calibrated_phase_slow_auto_on",
     calibrated_phase_survives_drift<-100, true>},
    {"rtc_drift_calibrated_phase_fast_auto_off",
     calibrated_phase_survives_drift<100, false>},
    {"rtc_drift_calibrated_phase_slow_auto_off",
     calibrated_phase_survives_drift<-100, false>},
    {"rtc_drift_changing_auto_sync", changing_auto_sync_keeps_rtc_utc},
    {"rtc_drift_ignores_system_clock_step", periodic_reanchor_ignores_system_clock_step},
};
} // namespace

int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
