#include "../support/time_sync_fixture.h"

namespace {
void no_source_rejects_event() {
  reset();
  int64_t utc_us = 0;
  expect(!time_sync_esp_to_utc_us(host::clock.now_us, utc_us),
         "an event without a time source must be rejected");
  expect(time_sync_state() == TimeSyncState::NONE, "expected NONE state");
}

void gps_converts_events_around_pps() {
  reset();
  process_pps(10, kBaseUtcSec);
  expect(time_sync_state() == TimeSyncState::GPS_OK, "expected GPS_OK state");
  for (int64_t delta_us : {-250000, 250000}) {
    expect_error_at_most(
        event_utc_us(kBaseEspUs + delta_us),
        static_cast<int64_t>(kBaseUtcSec) * kSecondUs + delta_us, 0);
  }
}

void auto_sync_false_preserves_system_clock() {
  reset();
  settings.sync.auto_sync = false;
  host::clock.system_offset_us += 10000000;
  const int64_t before_offset_us = host::clock.system_offset_us;
  process_pps(10, kBaseUtcSec);
  expect(host::clock.system_offset_us == before_offset_us,
         "auto_sync=false must preserve system clock offset");
  expect_error_at_most(event_utc_us(kBaseEspUs),
                       static_cast<int64_t>(kBaseUtcSec) * kSecondUs, 0);
}

void rtc_without_drift_converts_events() {
  reset();
  settings.sync.source = 2;
  establish_rtc_anchor();
  for (unsigned second = 1; second <= 100; ++second)
    process_sqw(second, 10 + second);
  expect(time_sync_state() == TimeSyncState::RTC_OK, "expected RTC_OK state");
  expect_error_at_most(
      event_utc_us(host::clock.now_us),
      static_cast<int64_t>(kBaseUtcSec + 100) * kSecondUs + 1000, 0);
}

void gps_loss_expires_holdover() {
  reset();
  process_pps(10, kBaseUtcSec);
  pps_signal.locked = false;
  host::clock.now_us = kBaseEspUs + 4000000;
  time_sync_update();
  expect(time_sync_state() == TimeSyncState::GPS_DEGRADED,
         "recent GPS anchor should remain usable during holdover");
  host::clock.now_us = kBaseEspUs + 11000000;
  time_sync_update();
  int64_t utc_us = 0;
  expect(!time_sync_esp_to_utc_us(host::clock.now_us, utc_us),
         "expired GPS holdover must reject event conversion");
}

void gps_replaces_rtc_sixty_seconds_ahead() {
  reset();
  establish_rtc_anchor(60000000);
  for (unsigned count = 10; count <= 15; ++count)
    process_pps(count, kBaseUtcSec + count - 10);
  expect_error_at_most(event_utc_us(pps_signal.edge_us),
                       static_cast<int64_t>(kBaseUtcSec + 5) * kSecondUs, 1000);
}

void gps_replaces_one_second_holdover_error() {
  reset();
  establish_rtc_anchor(kSecondUs);
  process_pps(10, kBaseUtcSec, false);
  require(time_sync_state() == TimeSyncState::GPS_DEGRADED,
          "first PPS should derive its second from RTC holdover");
  for (unsigned count = 11; count <= 15; ++count)
    process_pps(count, kBaseUtcSec + count - 10);
  expect_error_at_most(event_utc_us(pps_signal.edge_us),
                       static_cast<int64_t>(kBaseUtcSec + 5) * kSecondUs, 1000);
}

void missed_rtc_write_target_is_rescheduled() {
  reset();
  rtc.ready = true;
  process_pps(10, kBaseUtcSec, true, 1000);
  require(s_rtc_pps_pending && s_rtc_pps_target_count == 11,
          "RTC write must be scheduled for PPS 11");
  // The loop misses PPS 11, but handles later edges inside the write window.
  for (unsigned count = 12; count <= 16; ++count)
    process_pps(count, kBaseUtcSec + count - 10, true, 1000);
  expect(rtc.writes > 0,
         "RTC writes=" + std::to_string(rtc.writes) +
             ", pending target=" + std::to_string(s_rtc_pps_target_count) +
             ", latest PPS=" + std::to_string(pps_signal.count));
}

void rtc_limits_monotonic_clock_drift() {
  reset();
  settings.sync.source = 2;
  establish_rtc_anchor();
  // RTC is exact; the ESP monotonic/system clocks gain 100us per RTC second.
  for (unsigned second = 1; second <= 100; ++second)
    process_sqw(second, 10 + second, 1000100);
  const int64_t expected_utc_us =
      static_cast<int64_t>(kBaseUtcSec + 100) * kSecondUs + 1000;
  expect_error_at_most(event_utc_us(host::clock.now_us), expected_utc_us, 1500);
}

void rtc_validation_latency_does_not_shift_anchor() {
  reset();
  establish_rtc_anchor();
  rtc.validation_delay_us = 5000;
  process_sqw(10, 20);
  // Compare against independent ideal UTC, not a potentially adjusted system.
  const int64_t expected_utc_us =
      static_cast<int64_t>(kBaseUtcSec) * kSecondUs + host::clock.now_us -
      kBaseEspUs;
  expect_error_at_most(event_utc_us(host::clock.now_us), expected_utc_us, 1000);
}

void external_rtc_correction_updates_event_time() {
  reset();
  establish_rtc_anchor();
  settings.sync.auto_sync = false;
  // Model the result of a successful manual NTP write. No network code runs.
  host::clock.system_offset_us += kSecondUs;
  rtc.setTime(kBaseUtcSec + 1);
  for (unsigned second = 1; second <= 30; ++second)
    process_sqw(second, 10 + second, kSecondUs, true, 1);
  expect_error_at_most(
      event_utc_us(host::clock.now_us),
      static_cast<int64_t>(kBaseUtcSec + 31) * kSecondUs + 1000, 1000);
}

void sqw_extra_edge_does_not_add_utc_second() {
  reset();
  establish_rtc_anchor();
  process_sqw(1, 11);
  host::clock.now_us = 11101000;
  sqw_signal = {11100000, 12, false};
  time_sync_update();
  // Extra edge stays in the raw counter. Three valid periods restore lock.
  for (unsigned second = 2; second <= 15; ++second)
    process_sqw(second, 11 + second, kSecondUs, second >= 4);
  expect_error_at_most(
      event_utc_us(host::clock.now_us),
      static_cast<int64_t>(kBaseUtcSec + 15) * kSecondUs + 1000, 1000);
}

const Test kTests[] = {
    {"no_source_rejects_event", no_source_rejects_event, "-"},
    {"gps_converts_events_around_pps", gps_converts_events_around_pps, "-"},
    {"auto_sync_false_preserves_system_clock",
     auto_sync_false_preserves_system_clock, "-"},
    {"rtc_without_drift_converts_events", rtc_without_drift_converts_events,
     "-"},
    {"gps_loss_expires_holdover", gps_loss_expires_holdover, "-"},
    {"gps_replaces_rtc_sixty_seconds_ahead",
     gps_replaces_rtc_sixty_seconds_ahead, "TS001"},
    {"gps_replaces_one_second_holdover_error",
     gps_replaces_one_second_holdover_error, "TS002"},
    {"missed_rtc_write_target_is_rescheduled",
     missed_rtc_write_target_is_rescheduled, "TS003"},
    {"rtc_limits_monotonic_clock_drift", rtc_limits_monotonic_clock_drift,
     "TS004"},
    {"rtc_validation_latency_does_not_shift_anchor",
     rtc_validation_latency_does_not_shift_anchor, "TS005"},
    {"external_rtc_correction_updates_event_time",
     external_rtc_correction_updates_event_time, "TS006"},
    {"sqw_extra_edge_does_not_add_utc_second",
     sqw_extra_edge_does_not_add_utc_second, "TS007"},
};
} // namespace

int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
