#include "../support/time_sync_fixture.h"
#include <vector>

namespace {

void expect_state(TimeSyncState state, TimeSource source) {
  const auto status = time_sync_status();
  expect(time_sync_state() == state,
         "state=" + std::to_string(static_cast<int>(time_sync_state())) +
             ", expected=" + std::to_string(static_cast<int>(state)));
  expect(status.source == source, "unexpected active source");
  const bool valid = state != TimeSyncState::NONE;
  expect(status.synced == valid, "synced flag disagrees with expected state");
  int64_t utc_us = 0;
  expect(time_sync_esp_to_utc_us(host::clock.now_us, utc_us) == valid,
         "event conversion validity disagrees with expected state");
}

void expect_utc_now(int64_t tolerance_us = 0) {
  expect_error_at_most(event_utc_us(host::clock.now_us),
                       static_cast<int64_t>(kBaseUtcSec) * kSecondUs +
                           host::clock.now_us - kBaseEspUs,
                       tolerance_us);
}

void start_rtc(bool auto_sync = true) {
  reset();
  gps.ready = false;
  rtc.ready = true;
  settings.sync.auto_sync = auto_sync;
  process_sqw(1, 1);
  expect_state(TimeSyncState::RTC_OK, TimeSource::RTC);
}

template <bool GpsPresent, bool RtcPresent> void startup_without_signals() {
  reset();
  gps.ready = GpsPresent;
  rtc.ready = RtcPresent;
  // A ready HAL alone must not create a runtime anchor without a second edge.
  for (unsigned second = 0; second <= 60; ++second) {
    host::clock.now_us = kBaseEspUs + second * kSecondUs;
    time_sync_update();
    expect_state(TimeSyncState::NONE, TimeSource::NONE);
    expect(time_sync_estimate_accuracy_us() == -1,
           "no source must have unknown accuracy");
  }
}

template <bool Edge, bool AutoSync> void boot_trusted_rtc() {
  reset_environment();
  rtc.ready = true;
  rtc.utc_sec = kBaseUtcSec;
  gps.ready = false;
  settings.sync.auto_sync = AutoSync;
  sqw_edge_at_begin = Edge;
  host::clock.system_offset_us = -host::clock.now_us;
  time_sync_begin();
  expect_state(TimeSyncState::RTC_DEGRADED, TimeSource::RTC);
  expect_error_at_most(event_utc_us(host::clock.now_us),
                       static_cast<int64_t>(kBaseUtcSec) * kSecondUs, 0);
  expect(host::clock.system_utc_us() ==
             static_cast<int64_t>(kBaseUtcSec) * kSecondUs,
         "boot must load system clock even with auto_sync=false");
  expect(host::clock.now_us - kBaseEspUs == (Edge ? 0 : 1500000),
         "boot should wait for SQW only when no edge arrives");
}

template <bool Edge, bool LostPower> void boot_untrusted_rtc() {
  reset_environment();
  rtc.ready = true;
  rtc.lost_power = LostPower;
  rtc.utc_sec = LostPower ? kBaseUtcSec : MIN_VALID_UNIX_SEC - 1;
  sqw_edge_at_begin = Edge;
  time_sync_begin();
  expect_state(TimeSyncState::NONE, TimeSource::NONE);
  expect(host::clock.system_utc_us() ==
             static_cast<int64_t>(rtc.utc_sec) * kSecondUs,
         "untrusted RTC may initialize UI clock but not event timestamps");
}

void boot_rtc_warms_up_and_locks() {
  reset_environment();
  rtc.ready = true;
  sqw_edge_at_begin = true;
  time_sync_begin();
  expect_state(TimeSyncState::RTC_DEGRADED, TimeSource::RTC);
  process_sqw(1, 2, kSecondUs, false);
  expect_state(TimeSyncState::RTC_DEGRADED, TimeSource::RTC);
  process_sqw(2, 3, kSecondUs, false);
  process_sqw(3, 4);
  expect_state(TimeSyncState::RTC_OK, TimeSource::RTC);
  expect_utc_now();
}

void gps_nmea_without_pps_is_not_a_phase_source() {
  reset();
  gps.data.valid = true;
  gps.data.utc_sec = kBaseUtcSec;
  gps.utc_received_us = host::clock.now_us;
  gps.sentence_start_us = host::clock.now_us;
  time_sync_update();
  expect(time_sync_status().gps_time_valid, "NMEA should be valid");
  expect_state(TimeSyncState::NONE, TimeSource::NONE);
}

void gps_waits_for_pps_lock() {
  reset();
  pps_signal = {kBaseEspUs, 1, false};
  gps.data.valid = true;
  gps.utc_received_us = host::clock.now_us;
  gps.sentence_start_us = host::clock.now_us;
  time_sync_update();
  expect_state(TimeSyncState::NONE, TimeSource::NONE);
  process_pps(11, kBaseUtcSec + 1);
  expect_state(TimeSyncState::GPS_OK, TimeSource::GPS_PPS);
  expect_utc_now();
}

void gps_syncs_from_invalid_system_clock() {
  reset();
  host::clock.system_offset_us = -host::clock.now_us;
  process_pps(10, kBaseUtcSec);
  expect_state(TimeSyncState::GPS_OK, TimeSource::GPS_PPS);
  expect_utc_now();
  expect(host::clock.system_utc_us() == event_utc_us(host::clock.now_us),
         "GPS should initialize system clock");
}

void pps_without_nmea_uses_valid_system_second() {
  reset();
  gps.ready = false;
  process_pps(10, kBaseUtcSec, false);
  expect_state(TimeSyncState::GPS_DEGRADED, TimeSource::GPS_PPS);
  expect_utc_now();
  expect(time_sync_estimate_accuracy_us() == -1,
         "unconfirmed PPS second must not claim GPS accuracy");
}

void pps_without_any_utc_is_not_synced() {
  reset();
  gps.ready = false;
  host::clock.system_offset_us = -host::clock.now_us;
  process_pps(10, kBaseUtcSec, false);
  expect_state(TimeSyncState::NONE, TimeSource::NONE);
}

template <bool RtcLostPower> void gps_works_with_unusable_rtc() {
  reset();
  rtc.ready = RtcLostPower;
  rtc.lost_power = RtcLostPower;
  process_pps(10, kBaseUtcSec);
  expect_state(TimeSyncState::GPS_OK, TimeSource::GPS_PPS);
  expect_utc_now();
}

void gps_nmea_before_next_pps_selects_next_second() {
  reset();
  host::clock.now_us = kBaseEspUs + 1000;
  pps_signal = {kBaseEspUs, 10, true};
  gps.data.valid = true;
  gps.data.utc_sec = kBaseUtcSec - 1;
  gps.sentence_start_us = kBaseEspUs - 800000;
  gps.utc_received_us = gps.sentence_start_us + 70000;
  time_sync_update();
  expect_state(TimeSyncState::GPS_OK, TimeSource::GPS_PPS);
  expect_utc_now();
}

void gps_repeated_loop_is_idempotent() {
  reset();
  process_pps(10, kBaseUtcSec);
  const auto before = time_sync_status();
  for (unsigned i = 0; i < 100; ++i) {
    host::clock.now_us += 1000;
    time_sync_update();
  }
  const auto after = time_sync_status();
  expect(before.anchor_esp_us == after.anchor_esp_us &&
             before.anchor_utc_us == after.anchor_utc_us &&
             before.last_sync_us == after.last_sync_us,
         "reprocessing the same PPS must not move anchor or resync clock");
  expect_utc_now();
}

void gps_long_run_keeps_event_time_and_quality() {
  reset();
  for (unsigned count = 10; count <= 3610; ++count) {
    process_pps(count, kBaseUtcSec + count - 10);
    expect_state(TimeSyncState::GPS_OK, TimeSource::GPS_PPS);
    expect_utc_now();
    expect(time_sync_estimate_accuracy_us() >= 0 &&
               time_sync_estimate_accuracy_us() <= 1000,
           "fresh aligned GPS should have sub-millisecond estimate");
  }
}

template <uint32_t InitialSecond> void gps_crosses_calendar_boundary() {
  reset();
  for (unsigned step = 0; step < 4; ++step) {
    process_pps(10 + step, InitialSecond + step);
    expect_state(TimeSyncState::GPS_OK, TimeSource::GPS_PPS);
    expect_error_at_most(event_utc_us(pps_signal.edge_us),
                         static_cast<int64_t>(InitialSecond + step) * kSecondUs,
                         0);
  }
}

template <int64_t OffsetUs> void gps_corrects_system_clock() {
  reset();
  process_pps(10, kBaseUtcSec);
  host::clock.system_offset_us += OffsetUs;
  process_pps(11, kBaseUtcSec + 1);
  expect_utc_now();
  expect(host::clock.system_utc_us() == event_utc_us(host::clock.now_us),
         "system clock should follow GPS target");
  expect(time_sync_status().last_offset_us == -OffsetUs,
         "reported correction should have opposite sign to clock error");
}

void gps_small_system_jitter_does_not_step_clock() {
  reset();
  process_pps(10, kBaseUtcSec);
  host::clock.system_offset_us += 500;
  const auto offset = host::clock.system_offset_us;
  const auto last_sync = time_sync_status().last_sync_us;
  process_pps(11, kBaseUtcSec + 1);
  expect(host::clock.system_offset_us == offset &&
             time_sync_status().last_sync_us == last_sync,
         "500us jitter must not trigger a system clock step");
  expect_utc_now();
}

void gps_periodically_disciplines_even_without_offset() {
  reset();
  process_pps(10, kBaseUtcSec);
  const auto initial_sync = time_sync_status().last_sync_us;
  for (unsigned count = 11; count <= 41; ++count)
    process_pps(count, kBaseUtcSec + count - 10);
  expect(time_sync_status().last_sync_us > initial_sync,
         "system clock should be refreshed after maximum holdoff");
  expect_utc_now();
}

void gps_loses_nmea_and_recovers() {
  reset();
  process_pps(10, kBaseUtcSec);
  for (unsigned count = 11; count <= 15; ++count) {
    process_pps(count, kBaseUtcSec + count - 10, false);
    expect_state(TimeSyncState::GPS_DEGRADED, TimeSource::GPS_PPS);
    expect_utc_now();
  }
  process_pps(16, kBaseUtcSec + 6);
  expect_state(TimeSyncState::GPS_OK, TimeSource::GPS_PPS);
  expect_utc_now();
}

void gps_stale_nmea_is_not_phase_aligned() {
  reset();
  process_pps(10, kBaseUtcSec);
  pps_signal = {kBaseEspUs + 2000000, 12, true};
  host::clock.now_us = pps_signal.edge_us + 1000;
  time_sync_update();
  expect(!time_sync_status().gps_time_valid, "old NMEA must expire");
  expect_state(TimeSyncState::GPS_DEGRADED, TimeSource::GPS_PPS);
  expect_utc_now();
}

void gps_recovers_after_short_pps_outage() {
  reset();
  process_pps(10, kBaseUtcSec);
  pps_signal.locked = false;
  host::clock.now_us = kBaseEspUs + 3000000;
  time_sync_update();
  expect_state(TimeSyncState::GPS_DEGRADED, TimeSource::GPS_PPS);
  expect_utc_now();
  process_pps(14, kBaseUtcSec + 4);
  expect_state(TimeSyncState::GPS_OK, TimeSource::GPS_PPS);
  expect_utc_now();
}

void gps_recovers_after_expired_holdover() {
  reset();
  process_pps(10, kBaseUtcSec);
  pps_signal.locked = false;
  host::clock.now_us = kBaseEspUs + 11000000;
  time_sync_update();
  expect_state(TimeSyncState::NONE, TimeSource::NONE);
  process_pps(25, kBaseUtcSec + 15);
  expect_state(TimeSyncState::GPS_OK, TimeSource::GPS_PPS);
  expect_utc_now();
}

void gps_holdover_ten_second_boundary() {
  reset();
  process_pps(10, kBaseUtcSec);
  pps_signal.locked = false;
  host::clock.now_us = kBaseEspUs + 10000000;
  time_sync_update();
  expect_state(TimeSyncState::GPS_DEGRADED, TimeSource::GPS_PPS);
  expect_utc_now();
  ++host::clock.now_us;
  time_sync_update();
  expect_state(TimeSyncState::NONE, TimeSource::NONE);
}

void gps_single_unobserved_pulse_preserves_second() {
  reset();
  process_pps(10, kBaseUtcSec);
  process_pps(12, kBaseUtcSec + 2);
  expect_state(TimeSyncState::GPS_OK, TimeSource::GPS_PPS);
  expect_utc_now();
}

void gps_rtc_write_on_next_pps() {
  reset();
  rtc.ready = true;
  process_pps(10, kBaseUtcSec, true, 1000);
  expect(rtc.writes == 0, "first PPS should only schedule RTC write");
  process_pps(11, kBaseUtcSec + 1, true, 1000);
  expect(rtc.writes == 1 && rtc.utc_sec == kBaseUtcSec + 1,
         "next PPS should write the corresponding UTC second to RTC");
  expect_utc_now();
}

void gps_rtc_write_cancels_on_pps_loss() {
  reset();
  rtc.ready = true;
  process_pps(10, kBaseUtcSec, true, 1000);
  pps_signal.locked = false;
  host::clock.now_us += 100000;
  time_sync_update();
  process_pps(11, kBaseUtcSec + 1, true, 1000);
  expect(rtc.writes == 0, "cancelled RTC write must not run on old target");
  process_pps(12, kBaseUtcSec + 2, true, 1000);
  expect(rtc.writes == 1, "restored GPS should schedule a new RTC write");
}

void gps_does_not_write_rtc_when_auto_sync_disabled() {
  reset();
  rtc.ready = true;
  settings.sync.auto_sync = false;
  for (unsigned count = 10; count <= 15; ++count)
    process_pps(count, kBaseUtcSec + count - 10, true, 1000);
  expect(rtc.writes == 0, "auto_sync=false must not discipline RTC");
  expect_utc_now();
}

void gps_does_not_write_rtc_from_degraded_time() {
  reset();
  rtc.ready = true;
  for (unsigned count = 10; count <= 15; ++count)
    process_pps(count, kBaseUtcSec + count - 10, false, 1000);
  expect(rtc.writes == 0, "unconfirmed PPS seconds must not discipline RTC");
  expect_state(TimeSyncState::GPS_DEGRADED, TimeSource::GPS_PPS);
}

void rtc_cold_acquisition_waits_for_lock() {
  reset();
  gps.ready = false;
  rtc.ready = true;
  process_sqw(1, 1, kSecondUs, false);
  expect_state(TimeSyncState::NONE, TimeSource::NONE);
  process_sqw(2, 2, kSecondUs, false);
  expect_state(TimeSyncState::NONE, TimeSource::NONE);
  process_sqw(3, 3);
  expect_state(TimeSyncState::RTC_OK, TimeSource::RTC);
  expect_utc_now();
}

template <bool LostPower> void rtc_untrusted_time_is_not_acquired() {
  reset();
  gps.ready = false;
  rtc.ready = true;
  rtc.lost_power = LostPower;
  rtc.utc_sec = LostPower ? kBaseUtcSec : MIN_VALID_UNIX_SEC - 1;
  sqw_signal = {kBaseEspUs, 1, true};
  time_sync_update();
  expect_state(TimeSyncState::NONE, TimeSource::NONE);
  expect(time_sync_estimate_accuracy_us() == -1, "RTC time is not trusted");
}

void rtc_auto_sync_false_uses_rtc_not_system() {
  reset();
  rtc.ready = true;
  settings.sync.auto_sync = false;
  host::clock.system_offset_us = -kBaseEspUs;
  const auto offset = host::clock.system_offset_us;
  for (unsigned second = 1; second <= 30; ++second)
    process_sqw(second, second);
  expect_state(TimeSyncState::RTC_OK, TimeSource::RTC);
  expect_utc_now();
  expect(host::clock.system_offset_us == offset,
         "RTC event anchor should not change system with auto_sync=false");
}

void rtc_keeps_working_when_gps_nmea_has_no_pps() {
  start_rtc();
  gps.ready = true;
  gps.data.valid = true;
  gps.data.utc_sec = kBaseUtcSec + 2;
  gps.utc_received_us = kBaseEspUs + 2100000;
  gps.sentence_start_us = gps.utc_received_us;
  host::clock.now_us = gps.utc_received_us;
  time_sync_update();
  expect_state(TimeSyncState::RTC_OK, TimeSource::RTC);
  expect_utc_now();
}

void rtc_repeated_loop_does_not_reanchor() {
  start_rtc();
  const auto before = time_sync_status();
  for (unsigned i = 0; i < 100; ++i) {
    host::clock.now_us += 1000;
    time_sync_update();
  }
  expect(time_sync_status().anchor_esp_us == before.anchor_esp_us &&
             time_sync_status().anchor_utc_us == before.anchor_utc_us,
         "re-reading the same SQW must not reanchor");
  expect_utc_now();
}

void rtc_recovers_after_short_sqw_loss() {
  start_rtc();
  sqw_signal.locked = false;
  host::clock.now_us = kBaseEspUs + 4000000;
  time_sync_update();
  expect_state(TimeSyncState::RTC_DEGRADED, TimeSource::RTC);
  expect_utc_now();
  expect(time_sync_estimate_accuracy_us() == -1,
         "unlocked SQW should have unknown accuracy");
  process_sqw(5, 5);
  expect_state(TimeSyncState::RTC_OK, TimeSource::RTC);
  expect_utc_now();
}

void rtc_holdover_thirty_second_boundary() {
  start_rtc();
  sqw_signal.locked = false;
  host::clock.now_us = kBaseEspUs + 31000000;
  time_sync_update();
  expect_state(TimeSyncState::RTC_DEGRADED, TimeSource::RTC);
  expect_utc_now();
  ++host::clock.now_us;
  time_sync_update();
  expect_state(TimeSyncState::NONE, TimeSource::NONE);
}

void rtc_recovers_after_expired_holdover() {
  start_rtc();
  sqw_signal.locked = false;
  host::clock.now_us = kBaseEspUs + 32000000;
  time_sync_update();
  expect_state(TimeSyncState::NONE, TimeSource::NONE);
  process_sqw(35, 35);
  expect_state(TimeSyncState::RTC_OK, TimeSource::RTC);
  expect_utc_now();
}

template <uint8_t Policy> void source_policy_uses_rtc_without_gps() {
  reset();
  rtc.ready = true;
  settings.sync.source = Policy;
  process_sqw(1, 1);
  expect_state(TimeSyncState::RTC_OK, TimeSource::RTC);
  expect_utc_now();
}

void source_rtc_only_ignores_available_gps() {
  reset();
  settings.sync.source = 2;
  process_pps(10, kBaseUtcSec);
  expect_state(TimeSyncState::NONE, TimeSource::NONE);
  expect(!time_sync_status().pps_locked && !time_sync_status().gps_time_valid,
         "RTC-only policy must exclude GPS diagnostics from active sync");
}

void source_rtc_only_keeps_rtc_with_live_gps() {
  start_rtc();
  settings.sync.source = 2;
  gps.ready = true;
  process_pps(12, kBaseUtcSec + 2);
  expect_state(TimeSyncState::RTC_OK, TimeSource::RTC);
  expect_utc_now();
}

void source_switch_to_rtc_without_rtc_drops_gps_holdover() {
  reset();
  process_pps(10, kBaseUtcSec);
  settings.sync.source = 2;
  time_sync_update();
  expect_state(TimeSyncState::NONE, TimeSource::NONE);
}

void rtc_to_gps_handoff_is_continuous() {
  start_rtc();
  gps.ready = true;
  const int64_t before = event_utc_us(kBaseEspUs + 2500000);
  process_pps(12, kBaseUtcSec + 2);
  expect_state(TimeSyncState::GPS_OK, TimeSource::GPS_PPS);
  expect(event_utc_us(kBaseEspUs + 2500000) == before,
         "RTC to GPS handoff must preserve UTC for the same event");
  expect_utc_now();
}

template <int64_t SqwPhaseUs> void gps_to_rtc_handoff_preserves_phase() {
  reset();
  rtc.ready = true;
  const int64_t previous_sqw =
      kBaseEspUs + (SqwPhaseUs > 0 ? SqwPhaseUs - kSecondUs : SqwPhaseUs);
  sqw_signal = {previous_sqw, 9, true};
  process_pps(10, kBaseUtcSec);
  const int64_t event_esp = kBaseEspUs + 2500000;
  const int64_t before = event_utc_us(event_esp);
  pps_signal.locked = false;
  const int64_t next_sqw = kBaseEspUs + 3 * kSecondUs + SqwPhaseUs;
  host::clock.now_us = next_sqw + 1000;
  sqw_signal = {next_sqw, 13, true};
  rtc.utc_sec = kBaseUtcSec + 3;
  time_sync_update();
  expect_state(TimeSyncState::RTC_OK, TimeSource::RTC);
  expect_error_at_most(event_utc_us(event_esp), before, 0);
  expect_utc_now();
}

void gps_loss_waits_for_rtc_warmup() {
  reset();
  rtc.ready = true;
  process_pps(10, kBaseUtcSec);
  pps_signal.locked = false;
  process_sqw(3, 3, kSecondUs, false);
  expect_state(TimeSyncState::GPS_DEGRADED, TimeSource::GPS_PPS);
  expect_utc_now();
  process_sqw(4, 4);
  expect_state(TimeSyncState::RTC_OK, TimeSource::RTC);
  expect_utc_now();
}

void gps_loss_with_rtc_but_no_sqw_expires_holdover() {
  reset();
  rtc.ready = true;
  process_pps(10, kBaseUtcSec);
  pps_signal.locked = false;
  host::clock.now_us = kBaseEspUs + 3000000;
  time_sync_update();
  expect_state(TimeSyncState::GPS_DEGRADED, TimeSource::GPS_PPS);
  host::clock.now_us = kBaseEspUs + 11000000;
  time_sync_update();
  expect_state(TimeSyncState::NONE, TimeSource::NONE);
}

void repeated_gps_rtc_round_trips_are_continuous() {
  reset();
  rtc.ready = true;
  unsigned elapsed = 0;
  for (unsigned cycle = 0; cycle < 10; ++cycle) {
    process_pps(10 + elapsed, kBaseUtcSec + elapsed);
    expect_state(TimeSyncState::GPS_OK, TimeSource::GPS_PPS);
    expect_utc_now();
    pps_signal.locked = false;
    elapsed += 3;
    process_sqw(elapsed, 10 + elapsed);
    expect_state(TimeSyncState::RTC_OK, TimeSource::RTC);
    expect_utc_now();
    ++elapsed;
  }
}

void all_sources_lost_then_gps_recovers() {
  start_rtc();
  gps.ready = true;
  process_pps(12, kBaseUtcSec + 2);
  rtc.ready = false;
  sqw_signal.locked = false;
  pps_signal.locked = false;
  host::clock.now_us = kBaseEspUs + 13000000;
  time_sync_update();
  expect_state(TimeSyncState::NONE, TimeSource::NONE);
  process_pps(25, kBaseUtcSec + 15);
  expect_state(TimeSyncState::GPS_OK, TimeSource::GPS_PPS);
  expect_utc_now();
}

std::vector<TimeSyncState> notifications;
void on_state(TimeSyncState state) { notifications.push_back(state); }

void callbacks_report_only_state_transitions() {
  reset();
  time_sync_set_state_callback(on_state);
  process_pps(10, kBaseUtcSec);
  process_pps(11, kBaseUtcSec + 1);
  process_pps(12, kBaseUtcSec + 2, false);
  process_pps(13, kBaseUtcSec + 3, false);
  process_pps(14, kBaseUtcSec + 4);
  pps_signal.locked = false;
  host::clock.now_us += 11000000;
  time_sync_update();
  expect(
      notifications == std::vector<TimeSyncState>{TimeSyncState::NONE,
                                                  TimeSyncState::GPS_OK,
                                                  TimeSyncState::GPS_DEGRADED,
                                                  TimeSyncState::GPS_OK,
                                                  TimeSyncState::NONE},
      "callback sequence should contain initial state and actual transitions");
}

void callbacks_can_be_removed() {
  reset();
  time_sync_set_state_callback(on_state);
  time_sync_set_state_callback(nullptr);
  process_pps(10, kBaseUtcSec);
  expect(notifications == std::vector<TimeSyncState>{TimeSyncState::NONE},
         "removed callback must not be invoked");
}

void boot_without_sqw_anchor_expires() {
  reset_environment();
  rtc.ready = true;
  time_sync_begin();
  const int64_t anchor_time = host::clock.now_us;
  host::clock.now_us = anchor_time + 30000000;
  time_sync_update();
  expect_state(TimeSyncState::RTC_DEGRADED, TimeSource::RTC);
  ++host::clock.now_us;
  time_sync_update();
  expect_state(TimeSyncState::NONE, TimeSource::NONE);
}

void enabling_auto_sync_disciplines_system_from_gps() {
  reset();
  settings.sync.auto_sync = false;
  host::clock.system_offset_us += 20000000;
  process_pps(10, kBaseUtcSec);
  expect_utc_now();
  settings.sync.auto_sync = true;
  process_pps(11, kBaseUtcSec + 1);
  expect(host::clock.system_utc_us() == event_utc_us(host::clock.now_us),
         "enabling auto_sync should apply trusted GPS time to system clock");
  expect_utc_now();
}

void disabling_auto_sync_preserves_gps_event_time() {
  reset();
  process_pps(10, kBaseUtcSec);
  settings.sync.auto_sync = false;
  host::clock.system_offset_us += 10000;
  const auto offset = host::clock.system_offset_us;
  process_pps(11, kBaseUtcSec + 1);
  expect(host::clock.system_offset_us == offset,
         "disabled discipline should leave system clock alone");
  expect_state(TimeSyncState::GPS_OK, TimeSource::GPS_PPS);
  expect_utc_now();
}

void source_switch_gps_to_rtc_only_and_back() {
  reset();
  rtc.ready = true;
  process_pps(10, kBaseUtcSec);
  settings.sync.source = 2;
  process_sqw(1, 11);
  expect_state(TimeSyncState::RTC_OK, TimeSource::RTC);
  expect_utc_now();
  settings.sync.source = 0;
  process_pps(12, kBaseUtcSec + 2);
  expect_state(TimeSyncState::GPS_OK, TimeSource::GPS_PPS);
  expect_utc_now();
}

void gps_repairs_lost_power_rtc_before_fallback() {
  reset();
  rtc.ready = true;
  rtc.lost_power = true;
  process_pps(10, kBaseUtcSec, true, 1000);
  process_pps(11, kBaseUtcSec + 1, true, 1000);
  expect(rtc.writes == 1 && !rtc.lost_power,
         "GPS discipline should repair RTC oscillator-stop trust flag");
  pps_signal.locked = false;
  process_sqw(2, 12);
  expect_state(TimeSyncState::RTC_OK, TimeSource::RTC);
  expect_utc_now();
}

void rtc_to_pps_without_nmea_uses_degraded_holdover() {
  start_rtc();
  gps.ready = true;
  process_pps(12, kBaseUtcSec + 2, false);
  expect_state(TimeSyncState::GPS_DEGRADED, TimeSource::GPS_PPS);
  expect_utc_now();
  process_pps(13, kBaseUtcSec + 3);
  expect_state(TimeSyncState::GPS_OK, TimeSource::GPS_PPS);
  expect_utc_now();
}

#define NOMINAL(name) {#name, name}
const Test kTests[] = {
    {"startup_no_gps_no_rtc", startup_without_signals<false, false>},
    {"startup_gps_only_no_signals", startup_without_signals<true, false>},
    {"startup_rtc_only_no_signals", startup_without_signals<false, true>},
    {"startup_both_present_no_signals", startup_without_signals<true, true>},
    {"boot_rtc_sqw_auto_on", boot_trusted_rtc<true, true>},
    {"boot_rtc_sqw_auto_off", boot_trusted_rtc<true, false>},
    {"boot_rtc_no_sqw_auto_on", boot_trusted_rtc<false, true>},
    {"boot_rtc_no_sqw_auto_off", boot_trusted_rtc<false, false>},
    {"boot_rtc_lost_power_with_sqw", boot_untrusted_rtc<true, true>},
    {"boot_rtc_lost_power_no_sqw", boot_untrusted_rtc<false, true>},
    {"boot_rtc_old_date_with_sqw", boot_untrusted_rtc<true, false>},
    {"boot_rtc_old_date_no_sqw", boot_untrusted_rtc<false, false>},
    NOMINAL(boot_rtc_warms_up_and_locks),
    NOMINAL(gps_nmea_without_pps_is_not_a_phase_source),
    NOMINAL(gps_waits_for_pps_lock),
    NOMINAL(gps_syncs_from_invalid_system_clock),
    NOMINAL(pps_without_nmea_uses_valid_system_second),
    NOMINAL(pps_without_any_utc_is_not_synced),
    {"gps_works_without_rtc", gps_works_with_unusable_rtc<false>},
    {"gps_works_with_lost_power_rtc", gps_works_with_unusable_rtc<true>},
    NOMINAL(gps_nmea_before_next_pps_selects_next_second),
    NOMINAL(gps_repeated_loop_is_idempotent),
    NOMINAL(gps_long_run_keeps_event_time_and_quality),
    {"gps_crosses_minute", gps_crosses_calendar_boundary<1800000059>},
    {"gps_crosses_day", gps_crosses_calendar_boundary<1799971199>},
    {"gps_crosses_year", gps_crosses_calendar_boundary<1798761599>},
    {"gps_crosses_leap_day", gps_crosses_calendar_boundary<1835395199>},
    {"gps_corrects_system_ahead", gps_corrects_system_clock<20000>},
    {"gps_corrects_system_behind", gps_corrects_system_clock<-20000>},
    NOMINAL(gps_small_system_jitter_does_not_step_clock),
    NOMINAL(gps_periodically_disciplines_even_without_offset),
    NOMINAL(gps_loses_nmea_and_recovers),
    NOMINAL(gps_stale_nmea_is_not_phase_aligned),
    NOMINAL(gps_recovers_after_short_pps_outage),
    NOMINAL(gps_recovers_after_expired_holdover),
    NOMINAL(gps_holdover_ten_second_boundary),
    NOMINAL(gps_single_unobserved_pulse_preserves_second),
    NOMINAL(gps_rtc_write_on_next_pps),
    NOMINAL(gps_rtc_write_cancels_on_pps_loss),
    NOMINAL(gps_does_not_write_rtc_when_auto_sync_disabled),
    NOMINAL(gps_does_not_write_rtc_from_degraded_time),
    NOMINAL(rtc_cold_acquisition_waits_for_lock),
    {"rtc_rejects_lost_power", rtc_untrusted_time_is_not_acquired<true>},
    {"rtc_rejects_old_date", rtc_untrusted_time_is_not_acquired<false>},
    NOMINAL(rtc_auto_sync_false_uses_rtc_not_system),
    NOMINAL(rtc_keeps_working_when_gps_nmea_has_no_pps),
    NOMINAL(rtc_repeated_loop_does_not_reanchor),
    NOMINAL(rtc_recovers_after_short_sqw_loss),
    NOMINAL(rtc_holdover_thirty_second_boundary),
    NOMINAL(rtc_recovers_after_expired_holdover),
    {"source_auto_falls_back_to_rtc", source_policy_uses_rtc_without_gps<0>},
    {"source_gps_falls_back_to_rtc", source_policy_uses_rtc_without_gps<1>},
    {"source_rtc_only_uses_rtc", source_policy_uses_rtc_without_gps<2>},
    NOMINAL(source_rtc_only_ignores_available_gps),
    NOMINAL(source_rtc_only_keeps_rtc_with_live_gps),
    NOMINAL(source_switch_to_rtc_without_rtc_drops_gps_holdover),
    NOMINAL(rtc_to_gps_handoff_is_continuous),
    {"gps_to_rtc_handoff_zero_phase", gps_to_rtc_handoff_preserves_phase<0>},
    {"gps_to_rtc_handoff_positive_phase",
     gps_to_rtc_handoff_preserves_phase<354000>},
    {"gps_to_rtc_handoff_negative_phase",
     gps_to_rtc_handoff_preserves_phase<-200000>},
    NOMINAL(gps_loss_waits_for_rtc_warmup),
    NOMINAL(gps_loss_with_rtc_but_no_sqw_expires_holdover),
    NOMINAL(repeated_gps_rtc_round_trips_are_continuous),
    NOMINAL(all_sources_lost_then_gps_recovers),
    NOMINAL(callbacks_report_only_state_transitions),
    NOMINAL(callbacks_can_be_removed),
    NOMINAL(boot_without_sqw_anchor_expires),
    NOMINAL(enabling_auto_sync_disciplines_system_from_gps),
    NOMINAL(disabling_auto_sync_preserves_gps_event_time),
    NOMINAL(source_switch_gps_to_rtc_only_and_back),
    NOMINAL(gps_repairs_lost_power_rtc_before_fallback),
    NOMINAL(rtc_to_pps_without_nmea_uses_degraded_holdover),
};
#undef NOMINAL
} // namespace

int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
