#include "support.h"
#include "hal/ble/nus_service.h"
#include "storage/settings.h"
#include "timing/event_dispatcher.h"
#include "timing/event_isr.h"
#include "ui/main_area.h"
#include <ctime>

static int64_t supplied_utc = 0;
static bool conversion_ok = true;
bool time_sync_esp_to_utc_us(int64_t, int64_t &out) {
  out = supplied_utc; return conversion_ok;
}

namespace {
constexpr int64_t kMidnight = 1704067200000000LL; // 2024-01-01 UTC
struct Case {
  int64_t utc;
  int16_t zone;
  const char *local;
  int32_t correction;
  bool include_correction;
};
const Case kCases[] = {
  {kMidnight, 0, "00:00:00,000", 0, true},
  {kMidnight + 999, 0, "00:00:00,000", 0, true},
  {kMidnight + 1000, 0, "00:00:00,001", -1, true},
  {kMidnight + 14999000, 0, "00:00:14,999", -14999, true},
  {kMidnight + 15000000, 0, "00:00:15,000", -15000, true},
  {kMidnight + 15001000, 0, "00:00:15,001", -15001, false},
  {kMidnight + 29999000, 0, "00:00:29,999", -29999, false},
  {kMidnight + 30000000, 0, "00:00:30,000", -30000, false},
  {kMidnight + 30001000, 0, "00:00:30,001", 29999, false},
  {kMidnight + 44999000, 0, "00:00:44,999", 15001, false},
  {kMidnight + 45000000, 0, "00:00:45,000", 15000, true},
  {kMidnight + 45001000, 0, "00:00:45,001", 14999, true},
  {kMidnight + 59999999, 0, "00:00:59,999", 1, true},
  {kMidnight + 60000000, 0, "00:01:00,000", 0, true},
  {kMidnight - 1, 0, "23:59:59,999", 1, true},
  {kMidnight, -720, "12:00:00,000", 0, true},
  {kMidnight, 840, "14:00:00,000", 0, true},
  {kMidnight, 345, "05:45:00,000", 0, true},
  {kMidnight, -210, "20:30:00,000", 0, true},
  {kMidnight + 86399999999LL, 180, "02:59:59,999", 1, true},
  {-1, 0, "23:59:59,999", 1, true}, // Signed remainder in local timeval.
};

template <size_t Index, bool Start> void timestamp_and_packet() {
  const auto &c = kCases[Index];
  require(settings.begin(), "settings must initialize");
  auto device = settings.getDevice(); device.timezone_offset_min = c.zone;
  require(settings.setDevice(device), "timezone must be supported");
  supplied_utc = c.utc;
  host_project::precise_us = 10000123;
  NimBLEServer server; require(nusService.init(&server), "NUS must initialize");
  NimBLEConnInfo client; nusService.onConnect(client, 23);
  nusService.onNotifyStateChanged(true);
  event_isr_init(EXT_INT_PIN);
  MainArea area;
  host::fire_gpio(EXT_INT_PIN);
  event_dispatcher_handle_event_isr(Start ? ModuleType::START : ModuleType::FINISH, area);
  const auto &data = area.last;
  expect(area.displayed == 1 && data.success, "event must be processed once");
  expect(data.esp_timestamp_us == 10000123 && data.utc_timestamp_us == c.utc,
         "formatting must preserve captured ESP and UTC timestamps");
  expect(std::string(data.local_time_str) == c.local, "independent expected local time");
  const int64_t local_us = c.utc + static_cast<int64_t>(c.zone) * 60000000;
  const int64_t floor_sec = local_us / 1000000 - (local_us % 1000000 < 0 ? 1 : 0);
  expect(data.local_time.tv_sec == floor_sec &&
             data.local_time.tv_usec == local_us - floor_sec * 1000000,
         "local timeval must retain microseconds with a nonnegative remainder");
  expect(data.correction_ms == (Start ? c.correction : 0), "independent START correction");
  std::string expected = Start ? "$" : "F";
  expected += c.local;
  if (Start && c.include_correction) expected += ";" + std::to_string(c.correction);
  expected += "#\n";
  expect(Serial.output == expected && host_project::delivered == expected,
         "both transports must match the independently specified packet");
}

void no_utc_does_not_send() {
  conversion_ok = false;
  host_project::precise_us = 10000000;
  event_isr_init(EXT_INT_PIN); host::fire_gpio(EXT_INT_PIN);
  MainArea area; event_dispatcher_handle_event_isr(ModuleType::FINISH, area);
  expect(Serial.output.empty() && host_project::delivered.empty(), "no packet without UTC");
  expect(area.displayed == 1 && !area.last.success &&
             std::string(area.last.local_time_str) == "--:--:--,---",
         "unavailable UTC must be represented explicitly");
}

template <char Header> void timed_packet() {
  NimBLEServer server; require(nusService.init(&server), "NUS must initialize");
  NimBLEConnInfo client; nusService.onConnect(client, 23);
  nusService.onNotifyStateChanged(true);
  std::tm tm{}; tm.tm_hour = 23; tm.tm_min = 59; tm.tm_sec = 59;
  sendTimedPacket(Header, tm);
  const std::string expected = std::string(1, Header) + "23:59:59#\n";
  expect(Serial.output == expected && host_project::delivered == expected,
         "timed packet must use its supplied time and protocol delimiter");
}

#define TIME_CASE(index, label) \
  {"timestamp_start_" label, timestamp_and_packet<index, true>}, \
  {"timestamp_finish_" label, timestamp_and_packet<index, false>}
const Test kTests[] = {
  TIME_CASE(0, "minute_zero"), TIME_CASE(1, "submillisecond"),
  TIME_CASE(2, "first_millisecond"), TIME_CASE(3, "negative_limit_inside"),
  TIME_CASE(4, "negative_limit_exact"), TIME_CASE(5, "negative_limit_outside"),
  TIME_CASE(6, "minute_midpoint_before"), TIME_CASE(7, "minute_midpoint_tie"),
  TIME_CASE(8, "minute_midpoint_after"), TIME_CASE(9, "positive_limit_outside"),
  TIME_CASE(10, "positive_limit_exact"), TIME_CASE(11, "positive_limit_inside"),
  TIME_CASE(12, "last_microsecond"), TIME_CASE(13, "next_minute"),
  TIME_CASE(14, "previous_day"), TIME_CASE(15, "zone_minimum"),
  TIME_CASE(16, "zone_maximum"), TIME_CASE(17, "zone_quarter_hour"),
  TIME_CASE(18, "zone_negative_half_hour"), TIME_CASE(19, "zone_next_day"),
  TIME_CASE(20, "negative_epoch_remainder"),
  {"timestamp_no_utc_no_packet", no_utc_does_not_send},
  {"timestamp_beep_packet", timed_packet<'B'>},
  {"timestamp_voice_packet", timed_packet<'V'>},
};
#undef TIME_CASE
} // namespace
int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
