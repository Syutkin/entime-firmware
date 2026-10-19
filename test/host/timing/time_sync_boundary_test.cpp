#include "../support/time_sync_fixture.h"

namespace {

void publish_pps(int64_t edge, uint32_t count, uint32_t second,
                 int64_t processing_delay = 1000,
                 int64_t sentence_delta = 800000) {
  host::clock.now_us = edge + processing_delay;
  pps_signal = {edge, count, true};
  gps.data.valid = true;
  gps.data.utc_sec = sentence_delta >= 0 ? second - 1 : second;
  gps.sentence_start_us = edge - sentence_delta;
  gps.utc_received_us = gps.sentence_start_us + 100;
  time_sync_update();
}

void expect_current_utc(int64_t tolerance = 0) {
  expect_error_at_most(event_utc_us(host::clock.now_us),
                       static_cast<int64_t>(kBaseUtcSec) * kSecondUs +
                           host::clock.now_us - kBaseEspUs,
                       tolerance);
}

template <int64_t SentenceAge, bool Aligned>
void nmea_sentence_freshness_boundary() {
  reset();
  const int64_t sentence_start = kBaseEspUs - 800000;
  host::clock.now_us = sentence_start + SentenceAge;
  pps_signal = {kBaseEspUs, 10, true};
  gps.data.valid = true;
  gps.data.utc_sec = kBaseUtcSec - 1;
  gps.sentence_start_us = sentence_start;
  // Completion is still fresh: isolate the real sentence-start age guard.
  gps.utc_received_us = sentence_start + 100000;
  time_sync_update();
  require(time_sync_status().gps_time_valid,
          "UTC completion must remain fresh in this scenario");
  expect(time_sync_status().phase_aligned == Aligned,
         "sentence-start freshness must include 1.5s and exclude older data");
  expect_current_utc();
}

void nmea_future_sentence_is_not_aligned() {
  reset();
  pps_signal = {kBaseEspUs, 10, true};
  gps.data.valid = true;
  gps.data.utc_sec = kBaseUtcSec;
  gps.utc_received_us = host::clock.now_us;
  gps.sentence_start_us = host::clock.now_us + 1;
  time_sync_update();
  expect(!time_sync_status().phase_aligned,
         "future sentence timestamp must not confirm UTC second");
  expect(time_sync_state() == TimeSyncState::GPS_DEGRADED,
         "valid system UTC may still supply degraded PPS holdover");
  expect_current_utc();
}

void nmea_future_completion_is_not_trusted() {
  reset();
  pps_signal = {kBaseEspUs, 10, true};
  gps.data.valid = true;
  gps.data.utc_sec = kBaseUtcSec;
  gps.utc_received_us = host::clock.now_us + 1;
  gps.sentence_start_us = host::clock.now_us;
  time_sync_update();
  expect(!time_sync_status().gps_time_valid &&
             !time_sync_status().phase_aligned,
         "future UTC completion must not be treated as fresh GPS time");
  expect_current_utc();
}

template <int64_t Delta, bool Aligned> void nmea_phase_window_boundary() {
  reset();
  const int64_t delay = Delta < 0 ? -Delta + 1000 : 1000;
  publish_pps(kBaseEspUs, 10, kBaseUtcSec, delay, Delta);
  expect(time_sync_status().phase_aligned == Aligned,
         "unexpected alignment at phase delta=" + std::to_string(Delta));
  expect_current_utc();
}

template <int64_t Interval, bool Accepted> void pps_interval_boundary() {
  reset();
  publish_pps(kBaseEspUs, 10, kBaseUtcSec);
  const auto old_anchor = time_sync_status().anchor_esp_us;
  const int64_t edge = kBaseEspUs + Interval;
  const uint32_t elapsed = (Interval + 500000) / kSecondUs;
  publish_pps(edge, 11, kBaseUtcSec + elapsed);
  expect(time_sync_status().anchor_esp_us == (Accepted ? edge : old_anchor),
         "unexpected acceptance for PPS interval=" + std::to_string(Interval));
  if (!Accepted)
    expect_current_utc();
}

void pps_duplicate_timestamp_does_not_move_anchor() {
  reset();
  publish_pps(kBaseEspUs, 10, kBaseUtcSec);
  const auto before = time_sync_status();
  // A changed counter does not make an already handled edge a new second.
  publish_pps(kBaseEspUs, 11, kBaseUtcSec);
  expect(time_sync_status().anchor_esp_us == before.anchor_esp_us &&
             time_sync_status().anchor_utc_us == before.anchor_utc_us,
         "duplicate timestamp must not reanchor to a new UTC second");
}

void gps_counter_wrap_preserves_utc() {
  reset();
  for (unsigned step = 0; step < 4; ++step) {
    const uint32_t count = static_cast<uint32_t>(UINT32_MAX - 1U + step);
    publish_pps(kBaseEspUs + step * kSecondUs, count, kBaseUtcSec + step);
    expect(time_sync_state() == TimeSyncState::GPS_OK,
           "PPS counter wrap must preserve GPS_OK");
    expect(time_sync_status().last_pps_timestamp_us == pps_signal.edge_us,
           "even counter zero must process the current PPS");
    expect_current_utc();
  }
}

void gps_rtc_write_target_wraps_to_zero() {
  reset();
  rtc.ready = true;
  publish_pps(kBaseEspUs, UINT32_MAX, kBaseUtcSec);
  require(s_rtc_pps_pending && s_rtc_pps_target_count == 0,
          "planned counter should wrap to zero");
  publish_pps(kBaseEspUs + kSecondUs, 0, kBaseUtcSec + 1);
  expect(rtc.writes == 1 && rtc.utc_sec == kBaseUtcSec + 1,
         "RTC write scheduled for wrapped PPS counter must execute");
  expect_current_utc();
}

void publish_sqw(unsigned second, uint32_t count, int64_t delay = 1000) {
  const int64_t edge = kBaseEspUs + second * kSecondUs;
  host::clock.now_us = edge + delay;
  sqw_signal = {edge, count, true};
  rtc.utc_sec = kBaseUtcSec + second;
  time_sync_update();
}

void rtc_counter_wrap_preserves_cached_anchor() {
  reset();
  rtc.ready = true;
  for (unsigned step = 0; step < 3; ++step) {
    publish_sqw(step, static_cast<uint32_t>(UINT32_MAX + step));
    expect(time_sync_state() == TimeSyncState::RTC_OK,
           "SQW counter rollover must preserve an established RTC anchor");
    expect_current_utc();
  }
}

template <int64_t Age, bool Acquired> void rtc_first_anchor_age_boundary() {
  reset();
  rtc.ready = true;
  publish_sqw(1, 1, Age);
  expect(time_sync_status().synced == Acquired,
         "initial RTC anchor window must include 50ms, excluding older edges");
  if (Acquired)
    expect_current_utc();
  publish_sqw(2, 2);
  expect(time_sync_state() == TimeSyncState::RTC_OK,
         "a timely next edge should acquire or retain RTC synchronization");
  expect_current_utc();
}

template <int64_t Age, bool Acquired> void rtc_late_edge_age_boundary() {
  reset();
  rtc.ready = true;
  // Warmup deliberately sees 100ms-old edges: no anchor yet. The fourth
  // tick isolates the regular 900ms age window from the initial 50ms guard.
  for (unsigned second = 1; second <= 3; ++second) {
    publish_sqw(second, second, 100000);
    require(!time_sync_status().synced, "initial reanchor should still wait");
  }
  publish_sqw(4, 4, Age);
  expect(time_sync_status().synced == Acquired,
         "regular RTC age window must include 900ms, excluding older edges");
  if (Acquired)
    expect_current_utc();
  publish_sqw(5, 5);
  expect(time_sync_state() == TimeSyncState::RTC_OK,
         "skipped late SQW must not prevent acquisition on the next edge");
  expect_current_utc();
}

template <int64_t Age, unsigned Writes> void rtc_pps_write_window_boundary() {
  reset();
  rtc.ready = true;
  publish_pps(kBaseEspUs, 10, kBaseUtcSec);
  publish_pps(kBaseEspUs + kSecondUs, 11, kBaseUtcSec + 1, Age);
  expect(rtc.writes == Writes,
         "RTC PPS write window must include 3000us, excluding 3001us");
  expect_current_utc();
  if (Writes == 0) {
    publish_pps(kBaseEspUs + 2 * kSecondUs, 12, kBaseUtcSec + 2);
    expect(rtc.writes == 1, "late target should be rescheduled for next PPS");
  }
}

void rtc_disappears_before_scheduled_write() {
  reset();
  rtc.ready = true;
  publish_pps(kBaseEspUs, 10, kBaseUtcSec);
  rtc.ready = false;
  publish_pps(kBaseEspUs + kSecondUs, 11, kBaseUtcSec + 1);
  expect(rtc.writes == 0, "missing RTC must not receive a scheduled write");
  expect(time_sync_state() == TimeSyncState::GPS_OK,
         "RTC loss must not disturb GPS synchronization");
  rtc.ready = true;
  publish_pps(kBaseEspUs + 2 * kSecondUs, 12, kBaseUtcSec + 2);
  expect(rtc.writes == 1,
         "restored RTC should receive a newly scheduled write");
  expect_current_utc();
}

void auto_sync_disabled_before_scheduled_rtc_write() {
  reset();
  rtc.ready = true;
  publish_pps(kBaseEspUs, 10, kBaseUtcSec);
  settings.sync.auto_sync = false;
  const auto offset = host::clock.system_offset_us;
  publish_pps(kBaseEspUs + kSecondUs, 11, kBaseUtcSec + 1);
  expect(rtc.writes == 0 && host::clock.system_offset_us == offset,
         "disabled auto_sync must suppress a previously scheduled RTC write");
  settings.sync.auto_sync = true;
  publish_pps(kBaseEspUs + 2 * kSecondUs, 12, kBaseUtcSec + 2);
  expect(rtc.writes == 0,
         "reenabling discipline should first plan a new write");
  publish_pps(kBaseEspUs + 3 * kSecondUs, 13, kBaseUtcSec + 3);
  expect(rtc.writes == 1, "reenabled discipline should write on the next PPS");
  expect_current_utc();
}

const Test kTests[] = {
    {"nmea_sentence_fresh_at_1500000us",
     nmea_sentence_freshness_boundary<1500000, true>},
    {"nmea_sentence_stale_at_1500001us",
     nmea_sentence_freshness_boundary<1500001, false>},
    {"nmea_future_sentence_is_not_aligned",
     nmea_future_sentence_is_not_aligned},
    {"nmea_future_completion_is_not_trusted",
     nmea_future_completion_is_not_trusted},
    {"nmea_phase_zero", nmea_phase_window_boundary<0, true>},
    {"nmea_phase_positive_window_edge",
     nmea_phase_window_boundary<990000, true>},
    {"nmea_phase_negative_window_edge",
     nmea_phase_window_boundary<-990000, true>},
    {"nmea_phase_positive_outside_window",
     nmea_phase_window_boundary<990001, false>},
    {"nmea_phase_negative_outside_window",
     nmea_phase_window_boundary<-990001, false>},
    {"nmea_phase_positive_deadband_edge",
     nmea_phase_window_boundary<995000, false>},
    {"nmea_phase_negative_deadband_edge",
     nmea_phase_window_boundary<-995000, false>},
    {"nmea_phase_positive_one_second",
     nmea_phase_window_boundary<1000000, false>},
    {"nmea_phase_negative_one_second",
     nmea_phase_window_boundary<-1000000, false>},
    {"pps_interval_minimum_850000us", pps_interval_boundary<850000, true>},
    {"pps_interval_below_minimum_849999us",
     pps_interval_boundary<849999, false>},
    {"pps_interval_maximum_1150000us", pps_interval_boundary<1150000, true>},
    {"pps_interval_above_maximum_1150001us",
     pps_interval_boundary<1150001, false>},
    {"pps_interval_two_periods_lower_edge",
     pps_interval_boundary<1850000, true>},
    {"pps_interval_two_periods_upper_edge",
     pps_interval_boundary<2150000, true>},
    {"pps_interval_bad_before_rebaseline",
     pps_interval_boundary<4849999, false>},
    {"pps_interval_new_baseline_after_long_gap",
     pps_interval_boundary<5150001, true>},
    {"pps_duplicate_timestamp_does_not_move_anchor",
     pps_duplicate_timestamp_does_not_move_anchor},
    {"gps_counter_wrap_preserves_utc", gps_counter_wrap_preserves_utc},
    {"gps_rtc_write_target_wraps_to_zero", gps_rtc_write_target_wraps_to_zero},
    {"rtc_counter_wrap_preserves_cached_anchor",
     rtc_counter_wrap_preserves_cached_anchor},
    {"rtc_first_anchor_at_50000us", rtc_first_anchor_age_boundary<50000, true>},
    {"rtc_first_anchor_after_50000us",
     rtc_first_anchor_age_boundary<50001, false>},
    {"rtc_edge_at_900000us", rtc_late_edge_age_boundary<900000, true>},
    {"rtc_edge_after_900000us", rtc_late_edge_age_boundary<900001, false>},
    {"rtc_pps_write_at_3000us", rtc_pps_write_window_boundary<3000, 1>},
    {"rtc_pps_write_after_3000us", rtc_pps_write_window_boundary<3001, 0>},
    {"rtc_disappears_before_scheduled_write",
     rtc_disappears_before_scheduled_write},
    {"auto_sync_disabled_before_scheduled_rtc_write",
     auto_sync_disabled_before_scheduled_rtc_write},
};
} // namespace

int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
