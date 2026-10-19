#include "../support/time_sync_fixture.h"

namespace {

int64_t expected_utc(int64_t esp_us) {
  return static_cast<int64_t>(kBaseUtcSec) * kSecondUs + esp_us - kBaseEspUs;
}

void expect_correct_gps() {
  expect(time_sync_state() == TimeSyncState::GPS_OK,
         "independently confirmed NMEA must establish GPS_OK");
  expect_error_at_most(event_utc_us(host::clock.now_us),
                       expected_utc(host::clock.now_us), 0);
}

template <int32_t ErrorSeconds, bool AutoSync>
void gps_replaces_coarse_rtc_utc() {
  reset();
  settings.sync.auto_sync = AutoSync;
  establish_rtc_anchor(static_cast<int64_t>(ErrorSeconds) * kSecondUs);
  const auto system_offset = host::clock.system_offset_us;
  for (unsigned count = 10; count <= 12; ++count) {
    process_pps(count, kBaseUtcSec + count - 10);
    if (count < 12) {
      expect(time_sync_state() == TimeSyncState::GPS_DEGRADED,
             "a UTC step from RTC needs three independent confirmations");
      expect(rtc.writes == 0,
             "unconfirmed GPS candidate must not discipline RTC");
    }
  }
  expect_correct_gps();
  if (!AutoSync)
    expect(host::clock.system_offset_us == system_offset,
           "UTC anchor correction must respect auto_sync=false");
  else
    expect(host::clock.system_utc_us() == expected_utc(host::clock.now_us),
           "confirmed GPS UTC must discipline system clock");
}

template <int32_t ErrorSeconds>
void gps_replaces_unconfirmed_system_holdover() {
  reset();
  host::clock.system_offset_us +=
      static_cast<int64_t>(ErrorSeconds) * kSecondUs;
  process_pps(10, kBaseUtcSec, false);
  require(time_sync_state() == TimeSyncState::GPS_DEGRADED,
          "initial UTC must be derived from system holdover");
  for (unsigned count = 11; count <= 13; ++count)
    process_pps(count, kBaseUtcSec + count - 10);
  expect_correct_gps();
}

template <int32_t WrongSeconds> void single_bad_nmea_does_not_replace_rtc() {
  reset();
  establish_rtc_anchor();
  process_pps(10, kBaseUtcSec + WrongSeconds);
  expect(time_sync_state() == TimeSyncState::GPS_DEGRADED,
         "one disagreeing NMEA must not become confirmed GPS UTC");
  expect_error_at_most(event_utc_us(host::clock.now_us),
                       expected_utc(host::clock.now_us), 0);
  expect(rtc.writes == 0, "one bad NMEA must not schedule RTC discipline");
  process_pps(11, kBaseUtcSec + 1);
  // A +1s bad tuple equals the next real UTC tuple. The GPS layer therefore
  // may retain the old sentence timestamp until UTC advances once more.
  expect_error_at_most(event_utc_us(host::clock.now_us),
                       expected_utc(host::clock.now_us), 0);
  process_pps(12, kBaseUtcSec + 2);
  expect_correct_gps();
}

template <int32_t WrongSeconds> void confirmed_gps_rejects_single_bad_nmea() {
  reset();
  process_pps(10, kBaseUtcSec);
  process_pps(11, kBaseUtcSec + 1 + WrongSeconds);
  expect_error_at_most(event_utc_us(host::clock.now_us),
                       expected_utc(host::clock.now_us), 0);
  process_pps(12, kBaseUtcSec + 2);
  expect_correct_gps();
}

void repeated_nmea_is_not_independent_confirmation() {
  reset();
  establish_rtc_anchor(60000000);
  process_pps(10, kBaseUtcSec);
  for (unsigned poll = 0; poll < 20; ++poll) {
    host::clock.now_us += 1000;
    time_sync_update();
  }
  expect(time_sync_state() == TimeSyncState::GPS_DEGRADED,
         "repeated loop calls cannot confirm one NMEA tuple");
  // The same NMEA sentence can be used to number the next PPS. It must not
  // count as a second independent UTC observation.
  host::clock.now_us = kBaseEspUs + kSecondUs + 1000;
  pps_signal = {kBaseEspUs + kSecondUs, 11, true};
  time_sync_update();
  process_pps(12, kBaseUtcSec + 2);
  expect(time_sync_state() == TimeSyncState::GPS_DEGRADED,
         "one repeated sentence plus a new sentence is only two confirmations");
  process_pps(13, kBaseUtcSec + 3);
  expect_correct_gps();
}

void invalid_nmea_breaks_step_confirmation() {
  reset();
  establish_rtc_anchor(60000000);
  process_pps(10, kBaseUtcSec);
  process_pps(11, kBaseUtcSec + 1);
  process_pps(12, kBaseUtcSec + 2, false);
  for (unsigned count = 13; count <= 15; ++count) {
    process_pps(count, kBaseUtcSec + count - 10);
    if (count < 15)
      expect(time_sync_state() == TimeSyncState::GPS_DEGRADED,
             "confirmation must restart after invalid NMEA");
  }
  expect_correct_gps();
}

void inconsistent_nmea_restarts_step_confirmation() {
  reset();
  establish_rtc_anchor(60000000);
  process_pps(10, kBaseUtcSec);
  process_pps(11, kBaseUtcSec + 1);
  process_pps(12, kBaseUtcSec + 12);
  for (unsigned count = 13; count <= 15; ++count) {
    process_pps(count, kBaseUtcSec + count - 10);
    if (count < 15)
      expect(time_sync_state() == TimeSyncState::GPS_DEGRADED,
             "inconsistent UTC progression must restart confirmation");
  }
  expect_correct_gps();
}

void rejected_pps_breaks_step_confirmation() {
  reset();
  establish_rtc_anchor(60000000);
  process_pps(10, kBaseUtcSec);
  process_pps(11, kBaseUtcSec + 1);
  // A spurious edge is rejected by the real interval guard before alignment.
  host::clock.now_us = kBaseEspUs + 1300000;
  pps_signal = {kBaseEspUs + 1250000, 12, true};
  time_sync_update();
  for (unsigned count = 13; count <= 15; ++count) {
    process_pps(count, kBaseUtcSec + count - 10);
    if (count < 15)
      expect(time_sync_state() == TimeSyncState::GPS_DEGRADED,
             "rejected raw PPS must restart confirmation");
  }
  expect_correct_gps();
}

void confirmed_gps_utc_survives_nmea_loss() {
  reset();
  process_pps(10, kBaseUtcSec);
  for (unsigned count = 11; count <= 20; ++count)
    process_pps(count, kBaseUtcSec + count - 10, false);
  // PPS kept advancing the last confirmed UTC, so one bad returning sentence
  // must not shift it by a second just because phase_aligned was false.
  process_pps(21, kBaseUtcSec + 12);
  expect_error_at_most(event_utc_us(host::clock.now_us),
                       expected_utc(host::clock.now_us), 0);
  process_pps(22, kBaseUtcSec + 12);
  expect_correct_gps();
}

void rtc_handoff_clears_previous_gps_trust() {
  reset();
  process_pps(10, kBaseUtcSec);
  // After a source change RTC/system UTC is coarse again, even if this
  // device had confirmed GPS before the switch.
  settings.sync.source = 2;
  host::clock.system_offset_us += kSecondUs;
  rtc.ready = true;
  process_sqw(1, 11, kSecondUs, true, 1);
  require(time_sync_status().source == TimeSource::RTC, "must switch to RTC");
  settings.sync.source = 0;
  for (unsigned count = 12; count <= 14; ++count)
    process_pps(count, kBaseUtcSec + count - 10);
  expect_correct_gps();
}

void begin_clears_previous_gps_trust() {
  reset();
  process_pps(10, kBaseUtcSec);
  // Reinitialize only the public subsystem. Do not reset its static internals.
  rtc.ready = false;
  time_sync_begin();
  host::clock.system_offset_us += kSecondUs;
  process_pps(11, kBaseUtcSec + 1, false);
  for (unsigned count = 12; count <= 14; ++count)
    process_pps(count, kBaseUtcSec + count - 10);
  expect_correct_gps();
}

template <int32_t ErrorSeconds> void gps_replaces_actual_boot_rtc_utc() {
  reset_environment();
  rtc.ready = true;
  rtc.utc_sec = kBaseUtcSec + ErrorSeconds;
  sqw_edge_at_begin = true;
  time_sync_begin();
  require(time_sync_status().source == TimeSource::RTC,
          "public boot should create the initial RTC anchor");
  for (unsigned count = 10; count <= 12; ++count)
    process_pps(count, kBaseUtcSec + count - 10);
  expect_correct_gps();
}

void bad_phase_breaks_step_confirmation() {
  reset();
  establish_rtc_anchor(60000000);
  process_pps(10, kBaseUtcSec);
  process_pps(11, kBaseUtcSec + 1);
  host::clock.now_us = kBaseEspUs + 2001000;
  pps_signal = {kBaseEspUs + 2000000, 12, true};
  gps.data.utc_sec = kBaseUtcSec + 2;
  gps.sentence_start_us = pps_signal.edge_us - 995000;
  gps.utc_received_us = gps.sentence_start_us + 100000;
  time_sync_update();
  for (unsigned count = 13; count <= 15; ++count) {
    process_pps(count, kBaseUtcSec + count - 10);
    if (count < 15)
      expect(time_sync_state() == TimeSyncState::GPS_DEGRADED,
             "rejected phase must restart confirmation");
  }
  expect_correct_gps();
}

void pps_lock_loss_breaks_step_confirmation() {
  reset();
  establish_rtc_anchor(60000000);
  process_pps(10, kBaseUtcSec);
  process_pps(11, kBaseUtcSec + 1);
  pps_signal.locked = false;
  host::clock.now_us += 1000;
  time_sync_update();
  for (unsigned count = 12; count <= 14; ++count) {
    process_pps(count, kBaseUtcSec + count - 10);
    if (count < 14)
      expect(time_sync_state() == TimeSyncState::GPS_DEGRADED,
             "PPS lock loss must restart independent UTC confirmation");
  }
  expect_correct_gps();
}

void long_confirmation_gap_starts_new_sequence() {
  reset();
  establish_rtc_anchor(60000000);
  process_pps(10, kBaseUtcSec);
  process_pps(11, kBaseUtcSec + 1);
  for (unsigned count = 16; count <= 18; ++count) {
    process_pps(count, kBaseUtcSec + count - 10);
    if (count < 18)
      expect(time_sync_state() == TimeSyncState::GPS_DEGRADED,
             "stale confirmations must not contribute to a new UTC step");
  }
  expect_correct_gps();
}

const Test kTests[] = {
    {"gps_corrects_rtc_ahead_60s_auto_on",
     gps_replaces_coarse_rtc_utc<60, true>},
    {"gps_corrects_rtc_behind_60s_auto_on",
     gps_replaces_coarse_rtc_utc<-60, true>},
    {"gps_corrects_rtc_ahead_1s_auto_on", gps_replaces_coarse_rtc_utc<1, true>},
    {"gps_corrects_rtc_behind_1s_auto_on",
     gps_replaces_coarse_rtc_utc<-1, true>},
    {"gps_corrects_rtc_ahead_60s_auto_off",
     gps_replaces_coarse_rtc_utc<60, false>},
    {"gps_corrects_rtc_behind_60s_auto_off",
     gps_replaces_coarse_rtc_utc<-60, false>},
    {"gps_corrects_rtc_ahead_1s_auto_off",
     gps_replaces_coarse_rtc_utc<1, false>},
    {"gps_corrects_rtc_behind_1s_auto_off",
     gps_replaces_coarse_rtc_utc<-1, false>},
    {"gps_corrects_system_holdover_ahead",
     gps_replaces_unconfirmed_system_holdover<1>},
    {"gps_corrects_system_holdover_behind",
     gps_replaces_unconfirmed_system_holdover<-1>},
    {"gps_corrects_system_holdover_large_error",
     gps_replaces_unconfirmed_system_holdover<60>},
    {"gps_single_bad_nmea_1s_keeps_rtc",
     single_bad_nmea_does_not_replace_rtc<1>},
    {"gps_single_bad_nmea_60s_keeps_rtc",
     single_bad_nmea_does_not_replace_rtc<60>},
    {"gps_confirmed_rejects_single_nmea_ahead_1s",
     confirmed_gps_rejects_single_bad_nmea<1>},
    {"gps_confirmed_rejects_single_nmea_behind_1s",
     confirmed_gps_rejects_single_bad_nmea<-1>},
    {"gps_confirmed_rejects_single_large_nmea_jump",
     confirmed_gps_rejects_single_bad_nmea<60>},
    {"gps_repeated_nmea_is_not_independent_confirmation",
     repeated_nmea_is_not_independent_confirmation},
    {"gps_invalid_nmea_breaks_step_confirmation",
     invalid_nmea_breaks_step_confirmation},
    {"gps_inconsistent_nmea_restarts_step_confirmation",
     inconsistent_nmea_restarts_step_confirmation},
    {"gps_rejected_pps_breaks_step_confirmation",
     rejected_pps_breaks_step_confirmation},
    {"gps_confirmed_utc_survives_nmea_loss",
     confirmed_gps_utc_survives_nmea_loss},
    {"gps_rtc_handoff_clears_previous_trust",
     rtc_handoff_clears_previous_gps_trust},
    {"gps_begin_clears_previous_trust", begin_clears_previous_gps_trust},
    {"gps_corrects_public_boot_rtc_ahead_60s",
     gps_replaces_actual_boot_rtc_utc<60>},
    {"gps_corrects_public_boot_rtc_behind_60s",
     gps_replaces_actual_boot_rtc_utc<-60>},
    {"gps_bad_phase_breaks_step_confirmation",
     bad_phase_breaks_step_confirmation},
    {"gps_lock_loss_breaks_step_confirmation",
     pps_lock_loss_breaks_step_confirmation},
    {"gps_long_confirmation_gap_starts_new_sequence",
     long_confirmation_gap_starts_new_sequence},
};
} // namespace

int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
