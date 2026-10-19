#define HOST_REAL_RTC_SQW
#include "../support/time_sync_fixture.h"

namespace {

void expect_current_utc() {
  int64_t utc_us = 0;
  expect(time_sync_esp_to_utc_us(host::clock.now_us, utc_us),
         "RTC event time should remain available during short SQW holdover");
  const int64_t expected = static_cast<int64_t>(kBaseUtcSec) * kSecondUs +
                           host::clock.now_us - kBaseEspUs;
  expect_error_at_most(utc_us, expected, 0);
  expect(host::clock.system_utc_us() == expected,
         "SQW recovery must not step system UTC by a raw edge count error");
  expect(std::llabs(time_sync_status().last_offset_us) <= 1000,
         "raw SQW count errors must not appear as seconds of RTC offset");
}

void poll_at(int64_t timestamp_us) {
  host::clock.now_us = timestamp_us;
  rtc.utc_sec = kBaseUtcSec + (timestamp_us - kBaseEspUs) / kSecondUs;
  time_sync_update();
}

void edge_at(int64_t timestamp_us, bool poll = true) {
  require(timestamp_us >= host::clock.now_us,
          "GPIO time must not run backwards");
  host::clock.now_us = timestamp_us;
  host::fire_gpio(RTC_SQW_PIN);
  if (poll)
    poll_at(timestamp_us + 1000);
}

void second(unsigned elapsed) {
  edge_at(kBaseEspUs + static_cast<int64_t>(elapsed) * kSecondUs);
}

void start_rtc(bool auto_sync) {
  reset();
  rtc.ready = true;
  gps.ready = false;
  settings.sync.auto_sync = auto_sync;
  for (unsigned elapsed = 0; elapsed <= 5; ++elapsed)
    second(elapsed);
  require(time_sync_state() == TimeSyncState::RTC_OK,
          "real SQW warmup must establish a trusted RTC anchor");
  expect_current_utc();
}

template <bool Observed, bool AutoSync, unsigned ExtraEdges>
void extra_edges_keep_utc_continuous() {
  start_rtc(AutoSync);
  const auto original_anchor = time_sync_status().anchor_esp_us;
  for (unsigned i = 1; i <= ExtraEdges; ++i) {
    edge_at(kBaseEspUs + 5 * kSecondUs + i * 100000, Observed);
    if (Observed) {
      expect(time_sync_state() == TimeSyncState::RTC_DEGRADED,
             "observed short SQW periods must drop lock");
      expect_current_utc();
    }
  }
  bool recovered = false;
  for (unsigned elapsed = 6; elapsed <= 25; ++elapsed) {
    second(elapsed);
    expect_current_utc();
    if (time_sync_state() == TimeSyncState::RTC_OK && !recovered) {
      recovered = true;
      expect(
          time_sync_status().anchor_esp_us != original_anchor,
          "recovery from raw count discontinuity must rebuild count binding");
    }
  }
  expect(recovered && time_sync_state() == TimeSyncState::RTC_OK,
         "stable SQW should recover RTC_OK after spurious edges");
}

template <bool AutoSync> void missing_edge_keeps_utc_continuous() {
  start_rtc(AutoSync);
  // No GPIO edge at second 6; polling at 1.7s after the last edge loses lock.
  poll_at(kBaseEspUs + 6700000);
  expect(time_sync_state() == TimeSyncState::RTC_DEGRADED,
         "missing physical SQW pulse must enter holdover");
  expect_current_utc();
  for (unsigned elapsed = 7; elapsed <= 25; ++elapsed) {
    second(elapsed);
    expect_current_utc();
  }
  expect(time_sync_state() == TimeSyncState::RTC_OK,
         "SQW must relock without losing the missing UTC second");
}

void unobserved_missing_edge_still_checks_count() {
  start_rtc(true);
  // The application does not poll the driver while the missing edge occurs.
  // When it returns, the driver sees a 2s period and starts warmup itself.
  for (unsigned elapsed = 7; elapsed <= 20; ++elapsed) {
    second(elapsed);
    expect_current_utc();
  }
  expect(time_sync_state() == TimeSyncState::RTC_OK,
         "SQW must recover even if the application missed the outage");
}

void delayed_loop_keeps_correct_physical_count() {
  start_rtc(true);
  const auto anchor = time_sync_status().anchor_esp_us;
  // The ISR still captures both real pulses while the application is busy.
  edge_at(kBaseEspUs + 6 * kSecondUs, false);
  second(7);
  expect_current_utc();
  for (unsigned elapsed = 8; elapsed <= 10; ++elapsed) {
    second(elapsed);
    expect_current_utc();
  }
  expect(time_sync_state() == TimeSyncState::RTC_OK,
         "normal signal should relock after delayed polling");
  expect(time_sync_status().anchor_esp_us > anchor,
         "lock recovery must refresh SQW count binding");
}

void enabling_auto_sync_after_noise_keeps_utc() {
  start_rtc(false);
  edge_at(kBaseEspUs + 5100000);
  for (unsigned elapsed = 6; elapsed <= 8; ++elapsed)
    second(elapsed);
  require(time_sync_state() == TimeSyncState::RTC_OK,
          "SQW must relock before enabling discipline");
  settings.sync.auto_sync = true;
  for (unsigned elapsed = 9; elapsed <= 20; ++elapsed) {
    second(elapsed);
    expect_current_utc();
    expect(time_sync_state() == TimeSyncState::RTC_OK,
           "enabling discipline must not apply phantom SQW seconds");
  }
}

template <bool AutoSync> void repeated_noise_recovery_keeps_utc() {
  start_rtc(AutoSync);
  for (unsigned cycle = 0; cycle < 4; ++cycle) {
    const unsigned base_second = 5 + cycle * 10;
    edge_at(kBaseEspUs + base_second * kSecondUs + 100000, cycle % 2 == 0);
    for (unsigned elapsed = base_second + 1; elapsed <= base_second + 10;
         ++elapsed) {
      second(elapsed);
      expect_current_utc();
    }
    expect(time_sync_state() == TimeSyncState::RTC_OK,
           "repeated SQW faults must not accumulate UTC seconds");
  }
}

template <bool AutoSync> void gps_calibrated_sqw_phase_survives_noise() {
  constexpr int64_t phase_us = 354000;
  reset();
  rtc.ready = true;
  gps.ready = false;
  settings.sync.auto_sync = AutoSync;
  for (unsigned elapsed = 0; elapsed <= 5; ++elapsed)
    edge_at(kBaseEspUs + elapsed * kSecondUs + phase_us);
  require(time_sync_state() == TimeSyncState::RTC_OK,
          "SQW should lock before GPS phase calibration");

  gps.ready = true;
  process_pps(16, kBaseUtcSec + 6, true, 1000);
  require(time_sync_state() == TimeSyncState::GPS_OK,
          "GPS should establish UTC and measure SQW phase");
  pps_signal.locked = false;
  edge_at(kBaseEspUs + 6 * kSecondUs + phase_us);
  require(time_sync_state() == TimeSyncState::RTC_OK,
          "GPS-calibrated phase should survive RTC handoff");
  expect_current_utc();

  edge_at(kBaseEspUs + 6 * kSecondUs + phase_us + 100000);
  for (unsigned elapsed = 7; elapsed <= 25; ++elapsed) {
    edge_at(kBaseEspUs + elapsed * kSecondUs + phase_us);
    expect_current_utc();
  }
  expect(time_sync_state() == TimeSyncState::RTC_OK,
         "RTC should recover while preserving measured fractional UTC phase");
}

const Test kTests[] = {
    {"rtc_sqw_observed_extra_edge_auto_on",
     extra_edges_keep_utc_continuous<true, true, 1>},
    {"rtc_sqw_observed_extra_edge_auto_off",
     extra_edges_keep_utc_continuous<true, false, 1>},
    {"rtc_sqw_hidden_extra_edge_auto_on",
     extra_edges_keep_utc_continuous<false, true, 1>},
    {"rtc_sqw_hidden_extra_edge_auto_off",
     extra_edges_keep_utc_continuous<false, false, 1>},
    {"rtc_sqw_observed_multiple_extra_edges",
     extra_edges_keep_utc_continuous<true, true, 3>},
    {"rtc_sqw_hidden_multiple_extra_edges",
     extra_edges_keep_utc_continuous<false, true, 3>},
    {"rtc_sqw_missing_physical_edge_auto_on",
     missing_edge_keeps_utc_continuous<true>},
    {"rtc_sqw_missing_physical_edge_auto_off",
     missing_edge_keeps_utc_continuous<false>},
    {"rtc_sqw_unobserved_missing_edge",
     unobserved_missing_edge_still_checks_count},
    {"rtc_sqw_delayed_loop_correct_count",
     delayed_loop_keeps_correct_physical_count},
    {"rtc_sqw_enabling_auto_sync_after_noise",
     enabling_auto_sync_after_noise_keeps_utc},
    {"rtc_sqw_repeated_noise_auto_on", repeated_noise_recovery_keeps_utc<true>},
    {"rtc_sqw_repeated_noise_auto_off",
     repeated_noise_recovery_keeps_utc<false>},
    {"rtc_sqw_calibrated_phase_noise_auto_on",
     gps_calibrated_sqw_phase_survives_noise<true>},
    {"rtc_sqw_calibrated_phase_noise_auto_off",
     gps_calibrated_sqw_phase_survives_noise<false>},
};
} // namespace

int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
