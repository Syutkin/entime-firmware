#define HOST_REAL_RTC_SQW
#include "../support/time_sync_fixture.h"

namespace {
void edge(unsigned second, bool invalid_date = false) {
  // Inject drift after acquisition, so boot uncertainty is not part of the
  // independent UTC expectation. RTC seconds advance at their own cadence.
  host::clock.now_us = kBaseEspUs + second * kSecondUs +
                       (second > 5 ? second - 5 : 0) * 100;
  host::fire_gpio(RTC_SQW_PIN);
  rtc.utc_sec = invalid_date ? 0 : kBaseUtcSec + second;
  host::clock.now_us += 1000;
  time_sync_update();
}

void start(bool auto_sync) {
  reset();
  gps.ready = false;
  rtc.ready = true;
  settings.sync.source = 2;
  settings.sync.auto_sync = auto_sync;
  for (unsigned second = 0; second <= 5; ++second) edge(second);
  require(time_sync_state() == TimeSyncState::RTC_OK,
          "real SQW lock and valid RTC must establish UTC before fault injection");
}

template <bool LostPower, bool AutoSync, bool Expire>
void untrusted_rtc_does_not_keep_healthy_state() {
  start(AutoSync);
  rtc.lost_power = LostPower;
  for (unsigned second = 6; second <= (Expire ? 100 : 20); ++second)
    edge(second, !LostPower);
  require(rtc_sqw_is_locked(), "SQW must continue throughout the trust failure");
  require(!rtc.timeValid(MIN_VALID_UNIX_SEC), "RTC trust check must reject the injected fault");
  if constexpr (Expire) {
    int64_t utc_us = 0;
    expect(time_sync_state() == TimeSyncState::NONE &&
               !time_sync_esp_to_utc_us(host::clock.now_us, utc_us),
           "continuing SQW must not extend untrusted RTC holdover indefinitely");
  } else {
    expect(time_sync_state() != TimeSyncState::RTC_OK,
           "rejected RTC validation must stop reporting RTC_OK");
  }
}

template <bool LostPower> void trusted_rtc_can_recover() {
  start(true);
  rtc.lost_power = LostPower;
  for (unsigned second = 6; second <= 20; ++second) edge(second, !LostPower);
  rtc.lost_power = false;
  for (unsigned second = 21; second <= 35; ++second) edge(second);
  expect(time_sync_state() == TimeSyncState::RTC_OK,
         "valid RTC must recover while SQW remains present");
  expect_error_at_most(event_utc_us(host::clock.now_us),
                       static_cast<int64_t>(kBaseUtcSec + 35) * kSecondUs + 1000, 0);
}

template <bool AutoSync> void recovery_after_holdover_expired() {
  start(AutoSync);
  rtc.lost_power = true;
  for (unsigned second = 6; second <= 100; ++second) edge(second);
  require(time_sync_state() == TimeSyncState::NONE, "holdover must expire first");
  rtc.lost_power = false;
  for (unsigned second = 101; second <= 105; ++second) edge(second);
  expect(time_sync_state() == TimeSyncState::RTC_OK,
         "trusted RTC must recover after holdover expires");
  expect_error_at_most(event_utc_us(host::clock.now_us),
                       static_cast<int64_t>(kBaseUtcSec + 105) * kSecondUs + 1000, 0);
}

void trust_failure_holdover_boundary() {
  start(true);
  rtc.lost_power = true;
  for (unsigned second = 6; second <= 20; ++second) edge(second);
  require(time_sync_state() == TimeSyncState::RTC_DEGRADED,
          "rejected validation must enter degraded holdover");
  require(time_sync_estimate_accuracy_us() == -1,
          "untrusted RTC cannot provide healthy accuracy");
  int64_t utc_us = 0;
  const int64_t last_trusted_edge = s_last_sqw_edge_us;
  for (unsigned second = 21;
       kBaseEspUs + second * kSecondUs + (second - 5) * 100 <=
           last_trusted_edge + kRtcLossHoldoverMaxAgeUs; ++second)
    edge(second);
  host::clock.now_us = last_trusted_edge + kRtcLossHoldoverMaxAgeUs;
  require(rtc_sqw_is_locked(), "SQW must remain locked at the holdover boundary");
  expect(time_sync_esp_to_utc_us(host::clock.now_us, utc_us),
         "last trusted anchor remains usable through the holdover limit");
  ++host::clock.now_us;
  expect(!time_sync_esp_to_utc_us(host::clock.now_us, utc_us) &&
             time_sync_state() == TimeSyncState::NONE,
         "holdover must expire even before the next update with SQW still locked");
}

void rtc_unavailable_with_continuing_sqw() {
  start(true);
  rtc.ready = false;
  edge(6);
  expect(time_sync_state() == TimeSyncState::RTC_DEGRADED,
         "unavailable RTC must degrade even while SQW remains locked");
  rtc.ready = true;
  for (unsigned second = 7; second <= 12; ++second) edge(second);
  expect(time_sync_state() == TimeSyncState::RTC_OK,
         "available RTC must be validated and recover");
}

void healthy_sqw_bounds_drift() {
  start(true);
  for (unsigned second = 6; second <= 100; ++second) edge(second);
  expect(time_sync_state() == TimeSyncState::RTC_OK, "healthy RTC must stay synchronized");
  expect_error_at_most(event_utc_us(host::clock.now_us),
                       static_cast<int64_t>(kBaseUtcSec + 100) * kSecondUs + 1000, 0);
}

const Test kTests[] = {
  {"rtc_unavailable_with_continuing_sqw", rtc_unavailable_with_continuing_sqw},
  {"rtc_trust_failure_holdover_boundary", trust_failure_holdover_boundary},
  {"rtc_recovery_after_holdover_expired_auto_on", recovery_after_holdover_expired<true>},
  {"rtc_recovery_after_holdover_expired_auto_off", recovery_after_holdover_expired<false>},
  {"rtc_lost_power_downgrades_auto_on", untrusted_rtc_does_not_keep_healthy_state<true,true,false>},
  {"rtc_lost_power_downgrades_auto_off", untrusted_rtc_does_not_keep_healthy_state<true,false,false>},
  {"rtc_invalid_date_downgrades_auto_on", untrusted_rtc_does_not_keep_healthy_state<false,true,false>},
  {"rtc_invalid_date_downgrades_auto_off", untrusted_rtc_does_not_keep_healthy_state<false,false,false>},
  {"rtc_lost_power_holdover_expires_auto_on", untrusted_rtc_does_not_keep_healthy_state<true,true,true>},
  {"rtc_lost_power_holdover_expires_auto_off", untrusted_rtc_does_not_keep_healthy_state<true,false,true>},
  {"rtc_invalid_date_holdover_expires_auto_on", untrusted_rtc_does_not_keep_healthy_state<false,true,true>},
  {"rtc_invalid_date_holdover_expires_auto_off", untrusted_rtc_does_not_keep_healthy_state<false,false,true>},
  {"rtc_lost_power_trust_recovery_control", trusted_rtc_can_recover<true>},
  {"rtc_invalid_date_trust_recovery_control", trusted_rtc_can_recover<false>},
  {"rtc_trusted_sqw_drift_control", healthy_sqw_bounds_drift},
};
} // namespace
int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
