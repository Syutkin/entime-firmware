#include "../support/time_sync_fixture.h"

namespace {

void sample(unsigned elapsed, uint32_t count, bool valid_nmea = true,
            int64_t age_us = 1000) {
  const int64_t edge = kBaseEspUs + static_cast<int64_t>(elapsed) * kSecondUs;
  host::clock.now_us = edge + age_us;
  pps_signal = {edge, count, true};
  gps.data.valid = valid_nmea;
  gps.data.utc_sec = kBaseUtcSec + elapsed - 1;
  gps.sentence_start_us = edge - 900000;
  gps.utc_received_us = edge - 800000;
  time_sync_update();
  expect_error_at_most(
      event_utc_us(host::clock.now_us),
      static_cast<int64_t>(kBaseUtcSec + elapsed) * kSecondUs + age_us, 0);
}

void start(uint32_t count = 10) {
  reset();
  rtc.ready = true;
  sample(0, count);
  require(rtc.writes == 0 && s_rtc_pps_pending,
          "first good PPS must schedule a future RTC write");
}

template <unsigned Missed> void skipped_target_is_rescheduled() {
  start();
  sample(Missed + 1, 11 + Missed);
  expect(rtc.writes == 0,
         "an overdue request must not write its stale UTC second");
  sample(Missed + 2, 12 + Missed);
  expect(rtc.writes == 1 && rtc.utc_sec == kBaseUtcSec + Missed + 2,
         "RTC write must retry at the next PPS using a freshly scheduled UTC "
         "second");
}

void repeated_skips_eventually_retry() {
  start();
  for (unsigned elapsed : {2U, 4U, 6U}) {
    sample(elapsed, 10 + elapsed);
    expect(rtc.writes == 0,
           "each skipped target must be cancelled, not written late");
  }
  sample(7, 17);
  expect(rtc.writes == 1 && rtc.utc_sec == kBaseUtcSec + 7,
         "repeated skipped targets must not permanently block RTC discipline");
}

template <uint32_t Initial> void skipped_target_across_counter_wrap() {
  start(Initial);
  sample(2, static_cast<uint32_t>(Initial + 2U));
  expect(rtc.writes == 0, "wrapped overdue target must not execute late");
  sample(3, static_cast<uint32_t>(Initial + 3U));
  expect(rtc.writes == 1 && rtc.utc_sec == kBaseUtcSec + 3,
         "counter wrap must not prevent rescheduling or select a stale UTC "
         "second");
}

void repeated_polling_keeps_future_target() {
  start();
  for (unsigned poll = 0; poll < 100; ++poll) {
    host::clock.now_us += 1000;
    time_sync_update();
  }
  expect(rtc.writes == 0, "polling before target must not write RTC");
  sample(1, 11);
  expect(rtc.writes == 1 && rtc.utc_sec == kBaseUtcSec + 1,
         "polling the previous PPS must not cancel the future request");
  for (unsigned poll = 0; poll < 100; ++poll)
    time_sync_update();
  expect(rtc.writes == 1,
         "processing one target repeatedly must not duplicate RTC writes");
}

void skipped_target_retries_after_degraded_gps() {
  start();
  sample(2, 12, false);
  expect(rtc.writes == 0,
         "skipped target must not execute during GPS holdover");
  sample(3, 13);
  expect(rtc.writes == 0, "restored GPS should first schedule a new request");
  sample(4, 14);
  expect(
      rtc.writes == 1 && rtc.utc_sec == kBaseUtcSec + 4,
      "restored NMEA should enable a fresh RTC write after the skipped target");
}

void skipped_target_respects_auto_sync_toggle() {
  start();
  settings.sync.auto_sync = false;
  sample(2, 12);
  sample(3, 13);
  expect(rtc.writes == 0, "disabled auto_sync must suppress RTC retries");
  settings.sync.auto_sync = true;
  sample(4, 14);
  expect(rtc.writes == 0,
         "reenabled discipline should schedule, not run the old request");
  sample(5, 15);
  expect(rtc.writes == 1 && rtc.utc_sec == kBaseUtcSec + 5,
         "reenabling discipline must not leave an overdue request stuck");
}

void skipped_target_retries_when_rtc_returns() {
  start();
  rtc.ready = false;
  sample(2, 12);
  rtc.ready = true;
  sample(3, 13);
  expect(rtc.writes == 1 && rtc.utc_sec == kBaseUtcSec + 3,
         "missing RTC during overdue-target cancellation must not block the "
         "retry");
}

void rejected_target_pulse_does_not_block_retry() {
  start();
  // Raw PPS 11 is a spurious GPIO edge rejected by the interval guard.
  host::clock.now_us = kBaseEspUs + 251000;
  pps_signal = {kBaseEspUs + 250000, 11, true};
  time_sync_update();
  expect(rtc.writes == 0, "rejected PPS must never trigger RTC write");
  sample(1, 12);
  expect(rtc.writes == 0, "first valid edge should replan the skipped request");
  sample(2, 13);
  expect(rtc.writes == 1 && rtc.utc_sec == kBaseUtcSec + 2,
         "rejected target edge must not keep the pending request forever");
}

void overdue_retry_respects_write_window() {
  start();
  sample(2, 12);
  sample(3, 13, true, 3001);
  expect(rtc.writes == 0,
         "rescheduled write outside the edge window must be skipped");
  sample(4, 14);
  expect(
      rtc.writes == 1 && rtc.utc_sec == kBaseUtcSec + 4,
      "late retry must reschedule once more instead of waiting another period");
}

void periodic_discipline_retries_without_extra_ten_minute_delay() {
  start();
  sample(1, 11);
  require(rtc.writes == 1, "initial discipline should succeed");
  sample(600, 610);
  sample(601,
         611); // Ten minutes after the first successful write: plan PPS 612.
  expect(rtc.writes == 1,
         "periodic discipline should first plan a future edge");
  sample(603, 613); // PPS 612 was not processed.
  expect(rtc.writes == 1, "missed periodic target must not execute late");
  sample(604, 614);
  expect(rtc.writes == 2 && rtc.utc_sec == kBaseUtcSec + 604,
         "failed periodic attempt must retry immediately, not wait ten more "
         "minutes");
}

template <bool ValidNmea>
void missing_physical_pps_does_not_write_stale_second() {
  start();
  // A physical PPS is absent: the raw counter advances by one after TWO UTC
  // seconds.
  sample(2, 11, ValidNmea);
  expect(rtc.writes == 0,
         "matching counter must not write a stale scheduled UTC second");
  sample(3, 12);
  if (ValidNmea) {
    expect(rtc.writes == 1 && rtc.utc_sec == kBaseUtcSec + 3,
           "next request must use current UTC progression after the missing "
           "pulse");
  } else {
    expect(rtc.writes == 0,
           "restored NMEA must first schedule a fresh request");
    sample(4, 13);
    expect(rtc.writes == 1 && rtc.utc_sec == kBaseUtcSec + 4,
           "holdover-derived stale target must not block a future confirmed "
           "write");
  }
}

void planned_write_can_use_confirmed_gps_holdover() {
  start();
  sample(1, 11, false);
  expect(rtc.writes == 1 && rtc.utc_sec == kBaseUtcSec + 1,
         "one missing NMEA must not cancel an on-time write from confirmed GPS "
         "UTC");
}

const Test kTests[] = {
    {"rtc_pps_one_skipped_target", skipped_target_is_rescheduled<1>},
    {"rtc_pps_three_skipped_targets", skipped_target_is_rescheduled<3>},
    {"rtc_pps_long_loop_delay", skipped_target_is_rescheduled<100>},
    {"rtc_pps_repeated_target_skips", repeated_skips_eventually_retry},
    {"rtc_pps_skipped_target_before_counter_wrap",
     skipped_target_across_counter_wrap<UINT32_MAX - 1>},
    {"rtc_pps_skipped_zero_target",
     skipped_target_across_counter_wrap<UINT32_MAX>},
    {"rtc_pps_polling_does_not_cancel_future_target",
     repeated_polling_keeps_future_target},
    {"rtc_pps_retry_after_degraded_gps",
     skipped_target_retries_after_degraded_gps},
    {"rtc_pps_retry_respects_auto_sync",
     skipped_target_respects_auto_sync_toggle},
    {"rtc_pps_retry_after_rtc_return", skipped_target_retries_when_rtc_returns},
    {"rtc_pps_rejected_target_does_not_block_retry",
     rejected_target_pulse_does_not_block_retry},
    {"rtc_pps_retry_respects_write_window",
     overdue_retry_respects_write_window},
    {"rtc_pps_periodic_discipline_retry",
     periodic_discipline_retries_without_extra_ten_minute_delay},
    {"rtc_pps_missing_physical_pulse",
     missing_physical_pps_does_not_write_stale_second<true>},
    {"rtc_pps_missing_physical_pulse_without_nmea",
     missing_physical_pps_does_not_write_stale_second<false>},
    {"rtc_pps_on_time_write_with_confirmed_holdover",
     planned_write_can_use_confirmed_gps_holdover},
};
} // namespace

int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
