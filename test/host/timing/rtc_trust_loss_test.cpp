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
           "AU003: continuing SQW must not extend untrusted RTC holdover indefinitely");
  } else {
    expect(time_sync_state() != TimeSyncState::RTC_OK,
           "AU003: rejected RTC validation must stop reporting RTC_OK");
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

void healthy_sqw_bounds_drift() {
  start(true);
  for (unsigned second = 6; second <= 100; ++second) edge(second);
  expect(time_sync_state() == TimeSyncState::RTC_OK, "healthy RTC must stay synchronized");
  expect_error_at_most(event_utc_us(host::clock.now_us),
                       static_cast<int64_t>(kBaseUtcSec + 100) * kSecondUs + 1000, 0);
}

const Test kTests[] = {
  {"rtc_lost_power_downgrades_auto_on", untrusted_rtc_does_not_keep_healthy_state<true,true,false>, "AU003"},
  {"rtc_lost_power_downgrades_auto_off", untrusted_rtc_does_not_keep_healthy_state<true,false,false>, "AU003"},
  {"rtc_invalid_date_downgrades_auto_on", untrusted_rtc_does_not_keep_healthy_state<false,true,false>, "AU003"},
  {"rtc_invalid_date_downgrades_auto_off", untrusted_rtc_does_not_keep_healthy_state<false,false,false>, "AU003"},
  {"rtc_lost_power_holdover_expires_auto_on", untrusted_rtc_does_not_keep_healthy_state<true,true,true>, "AU003"},
  {"rtc_lost_power_holdover_expires_auto_off", untrusted_rtc_does_not_keep_healthy_state<true,false,true>, "AU003"},
  {"rtc_invalid_date_holdover_expires_auto_on", untrusted_rtc_does_not_keep_healthy_state<false,true,true>, "AU003"},
  {"rtc_invalid_date_holdover_expires_auto_off", untrusted_rtc_does_not_keep_healthy_state<false,false,true>, "AU003"},
  {"rtc_lost_power_trust_recovery_control", trusted_rtc_can_recover<true>},
  {"rtc_invalid_date_trust_recovery_control", trusted_rtc_can_recover<false>},
  {"rtc_trusted_sqw_drift_control", healthy_sqw_bounds_drift},
};
} // namespace
int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
