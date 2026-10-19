#define HOST_REAL_RTC_SQW
#include "../support/time_sync_fixture.h"

namespace {

int64_t ideal_utc(int64_t esp_us) {
  return static_cast<int64_t>(kBaseUtcSec) * kSecondUs + esp_us - kBaseEspUs;
}

void poll_at(int64_t timestamp_us) {
  host::clock.now_us = timestamp_us;
  rtc.utc_sec = kBaseUtcSec + (timestamp_us - kBaseEspUs) / kSecondUs;
  time_sync_update();
}

void edge_at(int64_t timestamp_us, int64_t loop_delay_us = 1000) {
  require(timestamp_us >= host::clock.now_us, "SQW time must move forwards");
  host::clock.now_us = timestamp_us;
  host::fire_gpio(RTC_SQW_PIN);
  poll_at(timestamp_us + loop_delay_us);
}

void second(unsigned elapsed, int64_t loop_delay_us = 1000) {
  edge_at(kBaseEspUs + elapsed * kSecondUs, loop_delay_us);
}

void prepare_rtc(bool auto_sync = true) {
  reset();
  rtc.ready = true;
  gps.ready = false;
  settings.sync.source = 2;
  settings.sync.auto_sync = auto_sync;
}

void expect_event_and_system_utc() {
  expect(time_sync_state() == TimeSyncState::RTC_OK,
         "stable SQW should provide RTC_OK despite I2C latency");
  expect_error_at_most(event_utc_us(host::clock.now_us),
                       ideal_utc(host::clock.now_us), 0);
  expect_error_at_most(host::clock.system_utc_us(),
                       ideal_utc(host::clock.now_us), 0);
  // Also check timestamps captured on either side of the accepted SQW edge.
  for (int64_t offset : {-100000, 0, 250000}) {
    const auto timestamp = time_sync_status().anchor_esp_us + offset;
    expect_error_at_most(event_utc_us(timestamp), ideal_utc(timestamp), 0);
  }
}

void start_rtc() {
  prepare_rtc();
  for (unsigned elapsed = 0; elapsed <= 5; ++elapsed)
    second(elapsed);
  require(time_sync_state() == TimeSyncState::RTC_OK,
          "SQW should acquire lock before injecting latency");
}

template <int64_t DelayUs> void initial_validation_keeps_utc() {
  prepare_rtc();
  rtc.validation_delay_us = DelayUs;
  for (unsigned elapsed = 0; elapsed <= 15; ++elapsed) {
    second(elapsed);
    if (elapsed >= 3) expect_event_and_system_utc();
  }
}

template <bool AutoSync> void calendar_read_keeps_utc() {
  prepare_rtc(AutoSync);
  rtc.validation_delay_us = 5000;
  rtc.read_delay_us = 7000;
  // With auto_sync=false the initial anchor and subsequent calendar checks
  // use unixTime(); system clocks must stay untouched throughout.
  const auto original_offset = host::clock.system_offset_us;
  for (unsigned elapsed = 0; elapsed <= 35; ++elapsed) {
    second(elapsed);
    if (elapsed >= 3) expect_event_and_system_utc();
    expect(host::clock.system_offset_us == original_offset,
           "I2C latency must not cause a spurious system clock correction");
  }
}

template <int64_t LoopDelayUs> void periodic_validation_keeps_system_utc() {
  start_rtc();
  rtc.validation_delay_us = 5000;
  for (unsigned elapsed = 6; elapsed <= 45; ++elapsed) {
    second(elapsed, LoopDelayUs);
    expect_event_and_system_utc();
  }
}

void relock_validation_keeps_utc() {
  start_rtc();
  rtc.validation_delay_us = 5000;
  edge_at(kBaseEspUs + 5100000);
  require(time_sync_state() == TimeSyncState::RTC_DEGRADED,
          "short SQW period must invalidate the old count binding");
  for (unsigned elapsed = 6; elapsed <= 25; ++elapsed) {
    second(elapsed);
    if (elapsed >= 8) expect_event_and_system_utc();
  }
}

template <int64_t PhaseUs> void gps_fallback_validation_keeps_utc() {
  prepare_rtc();
  for (unsigned elapsed = 0; elapsed <= 5; ++elapsed)
    edge_at(kBaseEspUs + elapsed * kSecondUs + PhaseUs);
  settings.sync.source = 0;
  gps.ready = true;
  if (PhaseUs == 0) {
    host::clock.now_us = kBaseEspUs + 6 * kSecondUs;
    host::fire_gpio(RTC_SQW_PIN);
  }
  process_pps(16, kBaseUtcSec + 6, true, 1000);
  require(time_sync_state() == TimeSyncState::GPS_OK,
          "GPS should establish UTC before fallback");
  rtc.validation_delay_us = 5000;
  pps_signal.locked = false;
  if (PhaseUs == 0) {
    poll_at(kBaseEspUs + 6 * kSecondUs + 2000);
    expect_event_and_system_utc();
  }
  for (unsigned elapsed = PhaseUs == 0 ? 7 : 6; elapsed <= 35; ++elapsed) {
    edge_at(kBaseEspUs + elapsed * kSecondUs + PhaseUs);
    expect_event_and_system_utc();
  }
}

void gps_fallback_without_phase_keeps_utc() {
  reset();
  process_pps(10, kBaseUtcSec);
  require(time_sync_state() == TimeSyncState::GPS_OK,
          "GPS should acquire UTC without an RTC");
  rtc.ready = true;
  rtc.validation_delay_us = 5000;
  pps_signal.locked = false;
  for (unsigned elapsed = 1; elapsed <= 20; ++elapsed) {
    second(elapsed);
    if (elapsed >= 4) expect_event_and_system_utc();
  }
}

template <int64_t DelayUs> void validation_window_includes_i2c_time() {
  start_rtc();
  for (unsigned elapsed = 6; elapsed <= 12; ++elapsed)
    second(elapsed);
  const auto anchor_before = time_sync_status().anchor_esp_us;
  rtc.validation_delay_us = DelayUs;
  // Processing starts inside the window but I2C may finish outside it.
  second(13, 850000);
  require(host::clock.now_us == kBaseEspUs + 13 * kSecondUs + 850000 + DelayUs,
          "periodic RTC validation must exercise the injected I2C delay");
  const auto new_anchor = time_sync_status().anchor_esp_us;
  if (850000 + DelayUs > 900000) {
    expect(new_anchor == anchor_before,
           "expired SQW sample must not replace the current anchor");
  } else {
    expect(new_anchor == kBaseEspUs + 13 * kSecondUs,
           "sample at the inclusive window boundary should be accepted");
  }
  expect_event_and_system_utc();
  rtc.validation_delay_us = 0;
  for (unsigned elapsed = 15; elapsed <= 19; ++elapsed)
    second(elapsed);
  expect_event_and_system_utc();
}

void expired_calendar_read_preserves_anchor() {
  start_rtc();
  for (unsigned elapsed = 6; elapsed <= 12; ++elapsed)
    second(elapsed);
  settings.sync.auto_sync = false;
  rtc.read_delay_us = 200000;
  const auto old_anchor = time_sync_status().anchor_esp_us;
  const auto old_offset = host::clock.system_offset_us;
  second(13, 850000);
  require(host::clock.now_us == kBaseEspUs + 14 * kSecondUs + 50000,
          "periodic calendar read must cross into the next second");
  expect(time_sync_status().anchor_esp_us == old_anchor,
         "calendar read finishing in the next second must not rebind UTC");
  expect(host::clock.system_offset_us == old_offset,
         "auto_sync=false must preserve system time on expired RTC reads");
  expect_event_and_system_utc();
  rtc.read_delay_us = 0;
  for (unsigned elapsed = 15; elapsed <= 19; ++elapsed)
    second(elapsed);
  expect_event_and_system_utc();
}

const Test kTests[] = {
    {"rtc_latency_initial_zero", initial_validation_keeps_utc<0>},
    {"rtc_latency_initial_200us", initial_validation_keeps_utc<200>},
    {"rtc_latency_initial_1ms", initial_validation_keeps_utc<1000>},
    {"rtc_latency_initial_5ms", initial_validation_keeps_utc<5000>},
    {"rtc_latency_initial_25ms", initial_validation_keeps_utc<25000>},
    {"rtc_latency_calendar_auto_on", calendar_read_keeps_utc<true>},
    {"rtc_latency_calendar_auto_off", calendar_read_keeps_utc<false>},
    {"rtc_latency_periodic", periodic_validation_keeps_system_utc<1000>},
    {"rtc_latency_periodic_late_loop",
     periodic_validation_keeps_system_utc<850000>},
    {"rtc_latency_relock", relock_validation_keeps_utc},
    {"rtc_latency_gps_fallback_zero_phase",
     gps_fallback_validation_keeps_utc<0>},
    {"rtc_latency_gps_fallback_calibrated_phase",
     gps_fallback_validation_keeps_utc<354000>},
    {"rtc_latency_gps_fallback_without_phase",
     gps_fallback_without_phase_keeps_utc},
    {"rtc_latency_window_exact", validation_window_includes_i2c_time<50000>},
    {"rtc_latency_window_exceeded_1us",
     validation_window_includes_i2c_time<50001>},
    {"rtc_latency_window_crossed_second",
     validation_window_includes_i2c_time<200000>},
    {"rtc_latency_expired_calendar_read", expired_calendar_read_preserves_anchor},
};
} // namespace

int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
