#define HOST_REAL_RTC_SQW
#include "../support/time_sync_fixture.h"
#include "timing/sntp.h"
#include <esp_sntp.h>
#include <time.h>

// Execute the real NTP-to-RTC path alongside time_sync and the SQW ISR.
// Its private log tag stays separate from time_sync.cpp's private tag.
namespace host_sntp {
#define gettimeofday host::get_time
#include "../../../src/timing/sntp.cpp"
#undef gettimeofday
}

namespace {
int32_t rtc_correction_sec = 0;
int32_t ntp_correction_sec = 0;
bool ntp_responds = true;
bool ntp_configured = false;
int64_t ntp_started_us = 0;
int64_t next_gpio_us = kBaseEspUs;
sntp_sync_status_t ntp_status = SNTP_SYNC_STATUS_RESET;

int64_t expected_utc(int64_t esp_us, int32_t correction_sec) {
  return static_cast<int64_t>(kBaseUtcSec + correction_sec) * kSecondUs +
         esp_us - kBaseEspUs;
}

void poll_at(int64_t timestamp_us) {
  require(timestamp_us >= host::clock.now_us, "poll time must move forwards");
  host::clock.now_us = timestamp_us;
  rtc.utc_sec = kBaseUtcSec + (timestamp_us - kBaseEspUs) / kSecondUs +
                rtc_correction_sec;
  time_sync_update();
}

void second(unsigned elapsed, int64_t phase_us = 0) {
  const auto edge_us = kBaseEspUs + elapsed * kSecondUs + phase_us;
  require(edge_us >= host::clock.now_us, "SQW time must move forwards");
  host::clock.now_us = edge_us;
  host::fire_gpio(RTC_SQW_PIN);
  next_gpio_us = edge_us + kSecondUs;
  poll_at(edge_us + 1000);
}

void start_rtc(bool auto_sync = false, int64_t phase_us = 0) {
  reset();
  rtc.ready = true;
  gps.ready = false;
  settings.sync.auto_sync = auto_sync;
  for (unsigned elapsed = 0; elapsed <= 5; ++elapsed)
    second(elapsed, phase_us);
  require(time_sync_state() == TimeSyncState::RTC_OK,
          "SQW should acquire RTC UTC before correction");
}

void write_correction(int64_t timestamp_us, int32_t seconds,
                      bool update_system = false) {
  require(timestamp_us >= host::clock.now_us, "write time must move forwards");
  host::clock.now_us = timestamp_us;
  rtc_correction_sec = seconds;
  rtc.setTime(kBaseUtcSec + (timestamp_us - kBaseEspUs) / kSecondUs + seconds);
  if (update_system)
    host::clock.system_offset_us =
        static_cast<int64_t>(kBaseUtcSec + seconds) * kSecondUs - kBaseEspUs;
}

void expect_corrected_utc() {
  expect(time_sync_state() == TimeSyncState::RTC_OK,
         "fresh locked SQW should provide corrected RTC UTC");
  for (int64_t offset : {-100000, 0, 250000}) {
    const auto timestamp = time_sync_status().anchor_esp_us + offset;
    expect_error_at_most(event_utc_us(timestamp),
                         expected_utc(timestamp, rtc_correction_sec), 0);
  }
  expect_error_at_most(event_utc_us(host::clock.now_us),
                       expected_utc(host::clock.now_us, rtc_correction_sec), 0);
}

template <int32_t Correction, bool AutoSync = false, bool UpdateSystem = false>
void explicit_write_updates_event_time() {
  start_rtc(AutoSync);
  write_correction(kBaseEspUs + 5200000, Correction, UpdateSystem);
  const auto system_offset = host::clock.system_offset_us;
  for (unsigned elapsed = 6; elapsed <= 35; ++elapsed) {
    second(elapsed);
    expect_corrected_utc();
    if (!AutoSync)
      expect(host::clock.system_offset_us == system_offset,
             "auto_sync=false must preserve the separately set system clock");
  }
}

void old_sqw_cannot_receive_new_calendar_time() {
  start_rtc();
  write_correction(kBaseEspUs + 5200000, 1);
  int64_t utc = 0;
  expect(!time_sync_esp_to_utc_us(host::clock.now_us, utc),
         "event conversion must notice the RTC write before the next update");
  poll_at(kBaseEspUs + 5300000);
  expect(!time_sync_esp_to_utc_us(host::clock.now_us, utc),
         "old RTC UTC must be unavailable until a post-write SQW is accepted");
  second(6);
  expect_corrected_utc();
}

void latest_write_wins_before_fresh_edge() {
  start_rtc();
  write_correction(kBaseEspUs + 5200000, 1);
  poll_at(kBaseEspUs + 5250000);
  write_correction(kBaseEspUs + 5300000, -1);
  second(6);
  expect_corrected_utc();
}

void repeated_writes_do_not_wait_for_periodic_refresh() {
  start_rtc();
  for (unsigned elapsed = 6; elapsed <= 15; ++elapsed) {
    write_correction(kBaseEspUs + (elapsed - 1) * kSecondUs + 200000,
                      elapsed % 2 ? -1 : 1);
    second(elapsed);
    expect_corrected_utc();
  }
}

template <bool AutoSync> void old_gps_phase_is_discarded() {
  constexpr int64_t phase_us = 354000;
  start_rtc(AutoSync, phase_us);
  gps.ready = true;
  process_pps(16, kBaseUtcSec + 6, true, 1000);
  require(time_sync_state() == TimeSyncState::GPS_OK,
          "GPS must calibrate the old SQW phase");
  pps_signal.locked = false;
  second(6, phase_us);
  write_correction(kBaseEspUs + 7 * kSecondUs, 1, true);
  // The new RTC phase differs from the old GPS-calibrated 354ms phase.
  for (unsigned elapsed = 8; elapsed <= 25; ++elapsed) {
    second(elapsed);
    if (elapsed >= 11) expect_corrected_utc();
  }
}

void write_during_sqw_outage_recovers_new_utc() {
  start_rtc();
  poll_at(kBaseEspUs + 6700000);
  write_correction(kBaseEspUs + 6800000, -1);
  for (unsigned elapsed = 7; elapsed <= 20; ++elapsed) {
    second(elapsed);
    if (elapsed >= 10) expect_corrected_utc();
  }
}

void invalid_written_date_is_rejected() {
  start_rtc();
  host::clock.now_us = kBaseEspUs + 5200000;
  rtc.setTime(100);
  host::clock.now_us = kBaseEspUs + 6 * kSecondUs;
  host::fire_gpio(RTC_SQW_PIN);
  host::clock.now_us += 1000;
  time_sync_update();
  int64_t utc = 0;
  expect(!time_sync_esp_to_utc_us(host::clock.now_us, utc),
         "an invalid RTC write must not preserve the superseded RTC anchor");
  write_correction(kBaseEspUs + 6200000, 1);
  second(7);
  expect_corrected_utc();
}

void write_preserves_live_gps_utc() {
  reset();
  settings.sync.auto_sync = false;
  process_pps(10, kBaseUtcSec);
  rtc.ready = true;
  write_correction(kBaseEspUs + 300000, 1, true);
  process_pps(11, kBaseUtcSec + 1);
  expect(time_sync_state() == TimeSyncState::GPS_OK,
         "manual RTC writes must preserve live GPS UTC");
  expect_error_at_most(event_utc_us(host::clock.now_us),
                       expected_utc(host::clock.now_us, 0), 0);
}

void gps_cannot_relearn_phase_from_pre_write_edge() {
  constexpr int64_t old_phase_us = 354000;
  start_rtc(false, old_phase_us);
  gps.ready = true;
  process_pps(16, kBaseUtcSec + 6, true, 1000);
  pps_signal.locked = false;
  second(6, old_phase_us);
  write_correction(kBaseEspUs + 6500000, 1, true);
  // Process PPS before the first SQW interrupt of the new phase is delivered.
  // The cached SQW edge still belongs to the pre-write 354ms phase.
  process_pps(17, kBaseUtcSec + 7, true, 0);
  require(time_sync_state() == TimeSyncState::GPS_OK,
          "GPS must remain available while waiting for post-write SQW");
  pps_signal.locked = false;
  for (unsigned elapsed = 7; elapsed <= 20; ++elapsed) {
    second(elapsed);
    if (elapsed >= 10) expect_corrected_utc();
  }
}

template <bool AutoSync> void write_with_i2c_latency_keeps_utc() {
  start_rtc(AutoSync);
  write_correction(kBaseEspUs + 5200000, 1, AutoSync);
  const auto system_offset = host::clock.system_offset_us;
  rtc.validation_delay_us = 5000;
  rtc.read_delay_us = 7000;
  for (unsigned elapsed = 6; elapsed <= 25; ++elapsed) {
    second(elapsed);
    expect_corrected_utc();
    expect(host::clock.system_offset_us == system_offset,
           "RTC write and read latency must not cause a system clock step");
  }
}

void write_recovers_lost_power_rtc() {
  reset();
  rtc.ready = true;
  rtc.lost_power = true;
  gps.ready = false;
  settings.sync.auto_sync = false;
  for (unsigned elapsed = 0; elapsed <= 5; ++elapsed) second(elapsed);
  require(time_sync_state() == TimeSyncState::NONE,
          "RTC with lost power must not provide UTC before a valid write");
  write_correction(kBaseEspUs + 5200000, 1);
  second(6);
  expect_corrected_utc();
}

void calendar_read_race_still_selects_nearest_second() {
  start_rtc();
  // No explicit write: model a one-off adjacent-second RTC read at refresh.
  for (unsigned elapsed = 6; elapsed <= 12; ++elapsed) second(elapsed);
  host::clock.now_us = kBaseEspUs + 13 * kSecondUs;
  host::fire_gpio(RTC_SQW_PIN);
  host::clock.now_us += 1000;
  rtc.utc_sec = kBaseUtcSec + 14;
  time_sync_update();
  expect_corrected_utc();
}

template <int32_t Correction, bool Responds = true, bool AutoSync = false>
void ntp_write_updates_time_sync() {
  start_rtc(AutoSync);
  ntp_correction_sec = Correction;
  ntp_responds = Responds;
  WiFiManager wifi;
  const auto writes_before = rtc.writes;
  const bool ok = host_sntp::syncRtcUtcFromNtpPrecise(
      rtc, wifi, "ntp.example", nullptr, nullptr, 500, 1500, 1500, nullptr);
  expect(ok == Responds, "NTP result must match the modeled response");
  expect(rtc.writes == writes_before + (Responds ? 1 : 0),
         "only successful NTP synchronization should write RTC");
  if (Responds) rtc_correction_sec = Correction;
  const auto offset_after_ntp = host::clock.system_offset_us;
  poll_at(host::clock.now_us + 1000);
  for (unsigned elapsed = Responds ? 7 : 6; elapsed <= 25; ++elapsed) {
    second(elapsed);
    expect_corrected_utc();
    expect(host::clock.system_offset_us == offset_after_ntp,
           "time_sync must preserve the correctly set system clock");
  }
}

const Test kTests[] = {
    {"rtc_manual_forward_one_second", explicit_write_updates_event_time<1>},
    {"rtc_manual_backward_one_second", explicit_write_updates_event_time<-1>},
    {"rtc_manual_forward_minute", explicit_write_updates_event_time<60>},
    {"rtc_manual_backward_minute", explicit_write_updates_event_time<-60>},
    {"rtc_manual_forward_day", explicit_write_updates_event_time<86400>},
    {"rtc_manual_backward_day", explicit_write_updates_event_time<-86400>},
    {"rtc_manual_zero_correction", explicit_write_updates_event_time<0>},
    {"rtc_manual_auto_on_forward",
     explicit_write_updates_event_time<1, true, true>},
    {"rtc_manual_auto_on_backward",
     explicit_write_updates_event_time<-1, true, true>},
    {"rtc_manual_rejects_pre_write_edge",
     old_sqw_cannot_receive_new_calendar_time},
    {"rtc_manual_latest_write_wins", latest_write_wins_before_fresh_edge},
    {"rtc_manual_repeated_writes",
     repeated_writes_do_not_wait_for_periodic_refresh},
    {"rtc_manual_discards_gps_phase_auto_off", old_gps_phase_is_discarded<false>},
    {"rtc_manual_discards_gps_phase_auto_on", old_gps_phase_is_discarded<true>},
    {"rtc_manual_during_sqw_outage", write_during_sqw_outage_recovers_new_utc},
    {"rtc_manual_invalid_date", invalid_written_date_is_rejected},
    {"rtc_manual_preserves_live_gps", write_preserves_live_gps_utc},
    {"rtc_manual_gps_rejects_pre_write_phase",
     gps_cannot_relearn_phase_from_pre_write_edge},
    {"rtc_manual_i2c_latency_auto_off", write_with_i2c_latency_keeps_utc<false>},
    {"rtc_manual_i2c_latency_auto_on", write_with_i2c_latency_keeps_utc<true>},
    {"rtc_manual_recovers_lost_power", write_recovers_lost_power_rtc},
    {"rtc_manual_preserves_read_race_guard",
     calendar_read_race_still_selects_nearest_second},
    {"rtc_manual_ntp_forward", ntp_write_updates_time_sync<1>},
    {"rtc_manual_ntp_backward", ntp_write_updates_time_sync<-1>},
    {"rtc_manual_ntp_timeout", ntp_write_updates_time_sync<1, false>},
    {"rtc_manual_ntp_forward_auto_on",
     ntp_write_updates_time_sync<1, true, true>},
    {"rtc_manual_ntp_backward_auto_on",
     ntp_write_updates_time_sync<-1, true, true>},
};
} // namespace

uint32_t millis() { return static_cast<uint32_t>(host::clock.now_us / 1000); }
void delay(uint32_t ms) {
  const auto end_us = host::clock.now_us + static_cast<int64_t>(ms) * 1000;
  while (next_gpio_us <= end_us) {
    host::clock.now_us = next_gpio_us;
    host::fire_gpio(RTC_SQW_PIN);
    next_gpio_us += kSecondUs;
  }
  host::clock.now_us = end_us;
}
void configTime(long offset, int daylight, const char *, const char *, const char *) {
  require(offset == 0 && daylight == 0, "NTP must configure UTC");
  ntp_configured = true;
  ntp_started_us = host::clock.now_us;
}
void sntp_set_sync_status(sntp_sync_status_t status) { ntp_status = status; }
sntp_sync_status_t sntp_get_sync_status() {
  if (ntp_configured && ntp_responds && ntp_status != SNTP_SYNC_STATUS_COMPLETED &&
      host::clock.now_us - ntp_started_us >= 100000) {
    host::clock.system_offset_us =
        static_cast<int64_t>(kBaseUtcSec + ntp_correction_sec) * kSecondUs - kBaseEspUs;
    ntp_status = SNTP_SYNC_STATUS_COMPLETED;
  }
  return ntp_status;
}

int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
