#include "../support/application/environment.h"
#include "hal/ble/nus_service.h"
#include "storage/settings.h"
#include "timing/second_events.h"
#include "ui/main_area.h"
#include "ui/status_bar.h"
#include <ctime>

namespace host_second {
time_t now = 0;
}
// Only this test executable sees the modeled system time; host clocks are unchanged.
extern "C" time_t time(time_t *out) noexcept {
  if (out) *out = host_second::now;
  return host_second::now;
}

namespace {
constexpr time_t kMidnight = 1704067200; // 2024-01-01 00:00:00 UTC
StatusBar bar;
MainArea area;
NimBLEServer server;

void zone(int16_t minutes) {
  auto device = settings.getDevice(); device.timezone_offset_min = minutes;
  require(settings.setDevice(device), "supported timezone");
}
void begin(bool connected = true, bool subscribed = true) {
  require(settings.begin(), "settings must initialize"); zone(0);
  host_project::now_ms = 10000;
  require(nusService.init(&server), "NUS must initialize");
  if (connected) {
    NimBLEConnInfo client; nusService.onConnect(client, 23);
    nusService.onNotifyStateChanged(subscribed);
  }
}
void tick(time_t now, ModuleType type = ModuleType::START) {
  host_second::now = now; second_events_handle_tick(type, bar, area);
}
void packets(const std::string &expected) {
  expect(Serial.output == expected && host_project::delivered == expected,
         "Serial and BLE must contain the independently expected timed packets");
}
void local_time(int year, int month, int day, int hour, int minute, int second) {
  require(!bar.times.empty(), "clock must have received an update");
  const auto &tm = bar.times.back();
  expect(tm.tm_year + 1900 == year && tm.tm_mon + 1 == month && tm.tm_mday == day &&
             tm.tm_hour == hour && tm.tm_min == minute && tm.tm_sec == second,
         "status bar must receive independently expected local calendar time");
}

template <bool Start> void two_minutes() {
  begin();
  for (unsigned second = 0; second < 120; ++second) {
    tick(kMidnight + second, Start ? ModuleType::START : ModuleType::FINISH);
    tick(kMidnight + second, Start ? ModuleType::START : ModuleType::FINISH);
  }
  expect(bar.times.size() == 120, "one clock update per observed second");
  if constexpr (Start) {
    require(area.countdown.size() == 120, "START must update countdown every second");
    for (unsigned second = 0; second < 120; ++second)
      expect(area.countdown[second] == second % 60, "countdown must wrap at the minute");
    packets("V00:00:15#\nB00:00:56#\nV00:01:15#\nB00:01:56#\n");
  } else {
    expect(area.countdown.empty(), "FINISH must not update the START countdown");
    packets("");
  }
  local_time(2024, 1, 1, 0, 1, 59);
}
template <unsigned Second> void same_second_is_not_repeated() {
  begin();
  for (unsigned i = 0; i < 100; ++i) tick(kMidnight + Second);
  expect(bar.times.size() == 1 && area.countdown.size() == 1,
         "repeated loop iterations must not repeat clock or countdown updates");
  packets(Second == 15 ? "V00:00:15#\n" : "B00:00:56#\n");
  expect(host_project::notify_calls == 1, "timed packet must notify once");
}
void unset_clock_is_inert() {
  begin(); tick(0); tick(-1); tick(-1000); tick(0);
  expect(bar.times.empty() && area.countdown.empty() && host_project::notify_calls == 0,
         "unset/nonpositive system clock must not update UI or emit packets");
  packets("");
}
void first_valid_time_is_processed() {
  begin(); tick(0); tick(-1); tick(kMidnight + 15);
  expect(bar.times.size() == 1 && area.countdown == std::vector<uint8_t>{15},
         "first valid time after missing UTC must update clock and countdown");
  packets("V00:00:15#\n");
}
void temporary_clock_loss() {
  begin(); tick(kMidnight + 14); tick(0); tick(-1); tick(kMidnight + 15);
  expect(bar.times.size() == 2 && area.countdown == std::vector<uint8_t>({14,15}),
         "clock loss must pause work and a new valid second must resume it");
  packets("V00:00:15#\n");
}
void missed_deadlines_are_not_replayed() {
  begin(); tick(kMidnight + 14); tick(kMidnight + 16);
  tick(kMidnight + 55); tick(kMidnight + 57);
  expect(area.countdown == std::vector<uint8_t>({14,16,55,57}),
         "loop delay must update only observed seconds");
  packets("");
}
void forward_clock_jump() {
  begin(); tick(kMidnight + 14); tick(kMidnight + 3600 + 56);
  tick(kMidnight + 3600 + 56);
  expect(bar.times.size() == 2 && area.countdown == std::vector<uint8_t>({14,56}),
         "forward adjustment must update current second once without catch-up");
  packets("B01:00:56#\n");
}
void backward_clock_jump() {
  begin(); tick(kMidnight + 120); tick(kMidnight + 15); tick(kMidnight + 15);
  expect(bar.times.size() == 2 && area.countdown == std::vector<uint8_t>({0,15}),
         "backward adjustment must update the new wall-clock second once");
  packets("V00:00:15#\n");
  local_time(2024, 1, 1, 0, 0, 15);
}
template <int Zone> void timezone_packet() {
  begin(); zone(Zone); tick(kMidnight + 15);
  if constexpr (Zone == -720) {
    packets("V12:00:15#\n"); local_time(2023,12,31,12,0,15);
  } else if constexpr (Zone == 840) {
    packets("V14:00:15#\n"); local_time(2024,1,1,14,0,15);
  } else if constexpr (Zone == 345) {
    packets("V05:45:15#\n"); local_time(2024,1,1,5,45,15);
  } else {
    packets("V20:30:15#\n"); local_time(2023,12,31,20,30,15);
  }
}
void timezone_change_applies_on_next_second() {
  begin(); tick(kMidnight + 14); zone(345); tick(kMidnight + 15);
  expect(bar.times.size() == 2, "live timezone change must be used by the next tick");
  packets("V05:45:15#\n"); local_time(2024,1,1,5,45,15);
}
void new_year_rollover() {
  begin(); tick(kMidnight - 4); tick(kMidnight - 1); tick(kMidnight);
  local_time(2024,1,1,0,0,0);
  expect(area.countdown == std::vector<uint8_t>({56,59,0}),
         "year transition must wrap countdown and update the date");
  packets("B23:59:56#\n");
}
void leap_day_rollover() {
  begin();
  tick(1709164799); local_time(2024,2,28,23,59,59);
  tick(1709164800); local_time(2024,2,29,0,0,0);
  tick(1709251200); local_time(2024,3,1,0,0,0);
  expect(bar.times.size() == 3, "different dates with same second-of-minute must all update");
  packets("");
}
void large_epoch() {
  begin(); tick(2147483647); local_time(2038,1,19,3,14,7);
  tick(2147483648LL); local_time(2038,1,19,3,14,8);
  expect(bar.times.size() == 2, "system time must not narrow to signed 32-bit seconds");
  packets("");
}
template <bool Connected> void missing_ble_still_sends_serial() {
  begin(Connected, false); tick(kMidnight + 15);
  expect(Serial.output == "V00:00:15#\n" && host_project::delivered.empty() &&
             host_project::notify_calls == 0 && area.countdown.size() == 1,
         "BLE absence/unsubscribed client must not block Serial or countdown");
  if constexpr (!Connected) { NimBLEConnInfo client; nusService.onConnect(client,23); }
  nusService.onNotifyStateChanged(true); tick(kMidnight + 56);
  expect(Serial.output == "V00:00:15#\nB00:00:56#\n" &&
             host_project::delivered == "B00:00:56#\n",
         "new timed packets must resume after subscription without replaying missed packets");
}

const Test kTests[] = {
  {"second_start_two_minutes", two_minutes<true>},
  {"second_finish_two_minutes", two_minutes<false>},
  {"second_voice_no_duplicates", same_second_is_not_repeated<15>},
  {"second_beep_no_duplicates", same_second_is_not_repeated<56>},
  {"second_unset_clock_inert", unset_clock_is_inert},
  {"second_first_valid_time", first_valid_time_is_processed},
  {"second_temporary_clock_loss", temporary_clock_loss},
  {"second_missed_deadlines_no_replay", missed_deadlines_are_not_replayed},
  {"second_clock_jump_forward", forward_clock_jump},
  {"second_clock_jump_backward", backward_clock_jump},
  {"second_timezone_minimum", timezone_packet<-720>},
  {"second_timezone_maximum", timezone_packet<840>},
  {"second_timezone_quarter_hour", timezone_packet<345>},
  {"second_timezone_negative_half_hour", timezone_packet<-210>},
  {"second_timezone_live_change", timezone_change_applies_on_next_second},
  {"second_new_year", new_year_rollover},
  {"second_leap_day", leap_day_rollover},
  {"second_epoch_2038", large_epoch},
  {"second_ble_disconnected", missing_ble_still_sends_serial<false>},
  {"second_ble_unsubscribed", missing_ble_still_sends_serial<true>},
};
} // namespace
int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
