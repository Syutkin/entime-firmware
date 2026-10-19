#pragma once

#include "fake_clock.h"
#include "test_runner.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/time.h>

namespace host {
Clock clock;

int get_time(timeval *tv, void *) {
  const int64_t utc_us = clock.system_utc_us();
  tv->tv_sec = utc_us / 1000000;
  tv->tv_usec = utc_us % 1000000;
  return 0;
}

int set_time(const timeval *tv, const void *) {
  clock.system_offset_us =
      static_cast<int64_t>(tv->tv_sec) * 1000000 + tv->tv_usec - clock.now_us;
  return 0;
}
} // namespace host

// Compile the current production implementation, not a saved copy. Including
// it here also permits setting up an established RTC anchor without boot/I2C.
// System time calls are intercepted only in this translation unit.
#define gettimeofday host::get_time
#define settimeofday host::set_time
#include "../../../src/timing/time_sync.cpp"
#undef gettimeofday
#undef settimeofday

GPS gps;
RTC rtc;
HostSettings settings;

namespace {
constexpr uint32_t kBaseUtcSec = 1800000000;
constexpr int64_t kBaseEspUs = 10000000;
constexpr int64_t kSecondUs = 1000000;

struct Signal {
  int64_t edge_us = 0;
  uint32_t count = 0;
  bool locked = false;
};

Signal pps_signal;
Signal sqw_signal;
bool sqw_edge_at_begin = false;

int64_t event_utc_us(int64_t esp_us) {
  int64_t utc_us = 0;
  require(time_sync_esp_to_utc_us(esp_us, utc_us), "missing event UTC anchor");
  return utc_us;
}

void expect_error_at_most(int64_t actual_us, int64_t expected_us,
                          int64_t tolerance_us) {
  const int64_t error_us = actual_us - expected_us;
  expect(std::llabs(error_us) <= tolerance_us,
         "UTC error=" + std::to_string(error_us) +
             " us, tolerance=" + std::to_string(tolerance_us) + " us");
}

void reset_environment() {
  host::clock = host::Clock{};
  host::clock.system_offset_us =
      static_cast<int64_t>(kBaseUtcSec) * kSecondUs - kBaseEspUs;
  gps = GPS{};
  rtc = RTC{};
  settings = HostSettings{};
  pps_signal = Signal{};
  sqw_signal = Signal{};
  sqw_edge_at_begin = false;
}

void reset() {
  reset_environment();
  time_sync_begin();
}

[[maybe_unused]] void establish_rtc_anchor(int64_t error_us = 0) {
  rtc.ready = true;
  set_anchor(TimeSource::RTC,
             static_cast<int64_t>(kBaseUtcSec) * kSecondUs + error_us,
             kBaseEspUs);
  s_rtc_anchor_sqw_count = 10;
  s_last_sqw_count = 10;
  s_last_sqw_edge_us = kBaseEspUs;
  s_last_rtc_resync_us = kBaseEspUs;
  s_status.synced = true;
  s_in_rtc_fallback = true;
  s_rtc_fallback_guard_active = false;
  sqw_signal = {kBaseEspUs, 10, true};
  host::clock.system_offset_us += error_us;
}

[[maybe_unused]] void process_pps(uint32_t count, uint32_t utc_sec,
                                  bool valid_nmea = true,
                                  int64_t processing_delay_us = 200000) {
  const int64_t edge_us =
      kBaseEspUs + static_cast<int64_t>(count - 10) * kSecondUs;
  host::clock.now_us = edge_us + processing_delay_us;
  pps_signal = {edge_us, count, true};
  gps.data.valid = valid_nmea;
  // A loop within the RTC write window cannot have received a complete NMEA
  // sentence for this PPS yet. Supply the preceding second's buffered NMEA.
  const bool early_loop = processing_delay_us <= 3000;
  gps.data.utc_sec = early_loop ? utc_sec - 1 : utc_sec;
  gps.sentence_start_us =
      early_loop ? edge_us - 900000 : edge_us + processing_delay_us / 2;
  gps.utc_received_us = early_loop ? edge_us - 800000 : host::clock.now_us;
  time_sync_update();
}

[[maybe_unused]] void process_sqw(unsigned elapsed_seconds, uint32_t count,
                                  int64_t esp_period_us = kSecondUs,
                                  bool locked = true,
                                  int32_t rtc_error_sec = 0) {
  const int64_t edge_us =
      kBaseEspUs + static_cast<int64_t>(elapsed_seconds) * esp_period_us;
  host::clock.now_us = edge_us + 1000;
  sqw_signal = {edge_us, count, locked};
  rtc.utc_sec = kBaseUtcSec + elapsed_seconds + rtc_error_sec;
  time_sync_update();
}

} // namespace

int64_t esp_timer_get_time() { return host::clock.now_us; }
void esp_rom_delay_us(unsigned us) { host::clock.now_us += us; }
void rtc_sqw_begin(int, int) {
  sqw_signal =
      sqw_edge_at_begin ? Signal{host::clock.now_us, 1, false} : Signal{};
}
bool rtc_sqw_get_raw(int64_t &timestamp, uint32_t &count) {
  timestamp = sqw_signal.edge_us;
  count = sqw_signal.count;
  return timestamp != 0;
}
bool rtc_sqw_is_locked() { return sqw_signal.locked; }
bool pps_is_locked() { return pps_signal.locked; }
bool pps_get_raw(int64_t &timestamp, uint32_t &count) {
  timestamp = pps_signal.edge_us;
  count = pps_signal.count;
  return timestamp != 0;
}
void pps_set_gps_utc_second(uint32_t) {}
