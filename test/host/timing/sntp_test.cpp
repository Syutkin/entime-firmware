#include "../support/test_runner.h"
#include "fake_clock.h"
#include <array>
#include <sys/time.h>

namespace host {
Clock clock;
int get_time(timeval *tv, void *) {
  const int64_t utc_us = clock.system_utc_us();
  tv->tv_sec = utc_us / 1000000;
  tv->tv_usec = utc_us % 1000000;
  return 0;
}
} // namespace host

#define gettimeofday host::get_time
#include "../../../src/timing/sntp.cpp"
#undef gettimeofday

namespace {
constexpr uint32_t kNtpUtcSec = 1800000000;
bool server_responds = true;
bool configured = false;
uint32_t response_delay_ms = 100;
int64_t response_fraction_us = 123000;
int64_t configured_at_us = 0;
unsigned reset_calls = 0;
sntp_sync_status_t sync_status = SNTP_SYNC_STATUS_RESET;
std::array<std::string, 3> servers;
RTC rtc_device;
WiFiManager wifi;

bool sync(const char *primary = "ntp1.example", const char *secondary = nullptr,
          const char *third = nullptr, uint32_t *duration = nullptr,
          uint32_t sync_timeout_ms = 1000, uint32_t edge_timeout_ms = 1500) {
  rtc_device.ready = true;
  return syncRtcUtcFromNtpPrecise(rtc_device, wifi, primary, secondary, third,
                                  sync_timeout_ms, edge_timeout_ms, 1500,
                                  duration);
}

template <bool NullPrimary> void ntp_rejects_missing_primary() {
  uint32_t duration = 42;
  expect(!sync(NullPrimary ? nullptr : "", nullptr, nullptr, &duration),
         "missing primary NTP server should fail");
  expect(!configured && rtc_device.writes == 0 && duration == 0,
         "invalid request must not start SNTP or write RTC");
}

void ntp_requires_wifi() {
  wifi.connected = false;
  expect(!sync(), "NTP needs a WiFi connection");
  expect(!configured && rtc_device.writes == 0,
         "disconnected WiFi must not start SNTP or write RTC");
}

void ntp_requires_rtc() {
  uint32_t duration = 42;
  expect(!syncRtcUtcFromNtpPrecise(rtc_device, wifi, "ntp1.example", nullptr,
                                   nullptr, 1000, 1500, 1500, &duration),
         "NTP-to-RTC operation needs an initialized RTC");
  expect(!configured && duration == 0, "missing RTC must not start SNTP");
}

template <unsigned ServerCount> void ntp_success_writes_utc_on_second_edge() {
  uint32_t duration = 0;
  expect(sync("ntp1.example", ServerCount >= 2 ? "ntp2.example" : nullptr,
              ServerCount >= 3 ? "ntp3.example" : nullptr, &duration),
         "NTP synchronization should succeed");
  expect(rtc_device.writes == 1 && rtc_device.utc_sec == kNtpUtcSec + 1,
         "RTC should receive UTC seconds of the next second edge");
  expect(host::clock.system_utc_us() % 1000000 <= 1500,
         "RTC write must occur in the configured second-edge window");
  expect(rtc_device.last_write_esp_us == host::clock.now_us,
         "write timestamp should match the captured second edge");
  expect(duration >= response_delay_ms && duration <= 1500,
         "reported duration must include synchronization and edge wait");
  expect(servers[0] == "ntp1.example" &&
             servers[1] == (ServerCount >= 2 ? "ntp2.example" : "") &&
             servers[2] == (ServerCount >= 3 ? "ntp3.example" : ""),
         "SNTP configuration should preserve requested servers");
}

void ntp_uses_third_when_second_empty() {
  expect(sync("ntp1.example", "", "ntp3.example"), "NTP should succeed");
  expect(servers[0] == "ntp1.example" && servers[1] == "ntp3.example" &&
             servers[2].empty(),
         "third NTP server should fill empty secondary slot");
}

void ntp_resets_previous_completed_status() {
  sync_status = SNTP_SYNC_STATUS_COMPLETED;
  expect(sync(), "new NTP attempt should succeed");
  expect(reset_calls == 1 &&
             host::clock.now_us - configured_at_us >= response_delay_ms * 1000,
         "old COMPLETED status must not bypass waiting for the new attempt");
}

void ntp_sync_timeout_does_not_write_rtc() {
  server_responds = false;
  uint32_t duration = 42;
  const auto start = host::clock.now_us;
  expect(!sync("ntp1.example", nullptr, nullptr, &duration, 150),
         "unresponsive NTP should time out");
  expect(rtc_device.writes == 0 && duration == 0,
         "timeout must not report successful RTC synchronization");
  expect(host::clock.now_us - start == 150000,
         "NTP wait should respect its timeout");
}

void ntp_edge_timeout_does_not_write_rtc() {
  uint32_t duration = 42;
  expect(!sync("ntp1.example", nullptr, nullptr, &duration, 1000, 100),
         "too short second-edge wait should time out");
  expect(rtc_device.writes == 0 && duration == 0,
         "edge timeout must not write RTC or report a successful duration");
}

void ntp_success_without_duration_output() {
  expect(sync(), "duration output is optional");
  expect(rtc_device.writes == 1, "RTC should still be written");
}

void ntp_handles_millis_wraparound() {
  host::clock.now_us = (static_cast<int64_t>(UINT32_MAX) - 60) * 1000;
  uint32_t duration = 0;
  expect(sync("ntp1.example", nullptr, nullptr, &duration),
         "millis wraparound must not break SNTP or second-edge wait");
  expect(duration == 977 && rtc_device.writes == 1,
         "duration must remain correct across millis wraparound");
}

template <uint32_t Timeout, bool Succeeds>
void ntp_response_deadline_boundary() {
  response_delay_ms = 100;
  uint32_t duration = 42;
  expect(
      sync("ntp1.example", nullptr, nullptr, &duration, Timeout) == Succeeds,
      "sync wait uses an exclusive deadline: a sample at timeout is too late");
  expect(rtc_device.writes == (Succeeds ? 1U : 0U),
         "only a response observed before the deadline may write RTC");
  if (!Succeeds)
    expect(duration == 0, "deadline timeout must clear success duration");
}

template <int64_t Fraction, uint32_t RtcSecond>
void ntp_second_edge_window_boundary() {
  response_fraction_us = Fraction;
  expect(sync(), "second-edge wait should eventually find its window");
  expect(rtc_device.writes == 1 && rtc_device.utc_sec == RtcSecond,
         "1500us is inside the write window; 1501us must wait for next second");
  expect(host::clock.system_utc_us() % 1000000 <= 1500,
         "RTC write must occur inside the window even after skipping its edge");
  const int64_t elapsed = rtc_device.last_write_esp_us - configured_at_us;
  expect(Fraction <= 1500 ? elapsed == 100000 : elapsed > 100000,
         "outside-window sync must not write immediately");
}

template <uint32_t Timeout, bool Succeeds> void ntp_edge_deadline_boundary() {
  // Response at 100ms sets fractional UTC to 123ms. Its next second boundary
  // is observed 877ms later by the real polling loop.
  uint32_t duration = 42;
  expect(sync("ntp1.example", nullptr, nullptr, &duration, 1000, Timeout) ==
             Succeeds,
         "second-edge wait must enforce an exclusive deadline");
  expect(rtc_device.writes == (Succeeds ? 1U : 0U),
         "edge observed at the deadline must not write RTC");
  expect(duration == (Succeeds ? 977U : 0U), "unexpected reported duration");
}

const Test kTests[] = {
    {"ntp_rejects_null_primary", ntp_rejects_missing_primary<true>},
    {"ntp_rejects_empty_primary", ntp_rejects_missing_primary<false>},
    {"ntp_requires_wifi", ntp_requires_wifi},
    {"ntp_requires_rtc", ntp_requires_rtc},
    {"ntp_success_one_server", ntp_success_writes_utc_on_second_edge<1>},
    {"ntp_success_two_servers", ntp_success_writes_utc_on_second_edge<2>},
    {"ntp_success_three_servers", ntp_success_writes_utc_on_second_edge<3>},
    {"ntp_uses_third_when_second_empty", ntp_uses_third_when_second_empty},
    {"ntp_resets_previous_completed_status",
     ntp_resets_previous_completed_status},
    {"ntp_sync_timeout_does_not_write_rtc",
     ntp_sync_timeout_does_not_write_rtc},
    {"ntp_edge_timeout_does_not_write_rtc",
     ntp_edge_timeout_does_not_write_rtc},
    {"ntp_success_without_duration_output",
     ntp_success_without_duration_output},
    {"ntp_handles_millis_wraparound", ntp_handles_millis_wraparound},
    {"ntp_response_observed_before_deadline",
     ntp_response_deadline_boundary<101, true>},
    {"ntp_response_at_exact_deadline",
     ntp_response_deadline_boundary<100, false>},
    {"ntp_response_after_deadline", ntp_response_deadline_boundary<99, false>},
    {"ntp_write_at_zero_fraction",
     ntp_second_edge_window_boundary<0, kNtpUtcSec>},
    {"ntp_write_at_window_edge_1500us",
     ntp_second_edge_window_boundary<1500, kNtpUtcSec>},
    {"ntp_waits_outside_window_1501us",
     ntp_second_edge_window_boundary<1501, kNtpUtcSec + 1>},
    {"ntp_second_edge_before_deadline", ntp_edge_deadline_boundary<878, true>},
    {"ntp_second_edge_at_exact_deadline",
     ntp_edge_deadline_boundary<877, false>},
};
} // namespace

uint32_t millis() { return static_cast<uint32_t>(host::clock.now_us / 1000); }
void delay(uint32_t ms) {
  host::clock.now_us += static_cast<int64_t>(ms) * 1000;
}
void configTime(long offset, int daylight, const char *server1,
                const char *server2, const char *server3) {
  require(offset == 0 && daylight == 0, "SNTP must configure UTC");
  configured = true;
  configured_at_us = host::clock.now_us;
  servers = {server1 ? server1 : "", server2 ? server2 : "",
             server3 ? server3 : ""};
}
void sntp_set_sync_status(sntp_sync_status_t status) {
  sync_status = status;
  ++reset_calls;
}
sntp_sync_status_t sntp_get_sync_status() {
  if (configured && server_responds &&
      sync_status != SNTP_SYNC_STATUS_COMPLETED &&
      host::clock.now_us - configured_at_us >= response_delay_ms * 1000) {
    host::clock.system_offset_us = static_cast<int64_t>(kNtpUtcSec) * 1000000 +
                                   response_fraction_us - host::clock.now_us;
    sync_status = SNTP_SYNC_STATUS_COMPLETED;
  }
  return sync_status;
}

int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
