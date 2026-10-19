#include "support/sync_pipeline.h"
#include "hal/ble/nus_service.h"
#include "timing/event_dispatcher.h"
#include "timing/event_isr.h"
#include "ui/main_area.h"

namespace {
constexpr int64_t kSecond = 1000000;
constexpr uint32_t kUtc = 1704067200; // 2024-01-01 00:00:00 UTC
int64_t base_esp = 10000000;
NimBLEServer server;
MainArea area;

void begin(bool auto_sync = true) {
  require(settings.begin(), "settings must initialize");
  auto device = settings.getDevice(); device.timezone_offset_min = 0;
  require(settings.setDevice(device), "UTC timezone must validate");
  auto sync = settings.getSync(); sync.auto_sync = auto_sync;
  require(settings.setSync(sync), "sync policy must validate");
  host_project::precise_us = base_esp;
  host_project::system_offset_us = static_cast<int64_t>(kUtc) * kSecond - base_esp;
  time_sync_begin(); // RTC is absent during boot; no fake boot SQW edge.
  pps_init(GPS_PPS_PIN); event_isr_init(EXT_INT_PIN);
  require(nusService.init(&server), "NUS must initialize");
  NimBLEConnInfo client; nusService.onConnect(client, 23);
  nusService.onNotifyStateChanged(true);
}

void tick(unsigned second, bool gps_edge, bool rtc_edge) {
  host_project::precise_us = base_esp + second * kSecond;
  rtc.utc_sec = kUtc + second;
  if (rtc_edge) host::fire_gpio(RTC_SQW_PIN);
  if (gps_edge) {
    host::fire_gpio(GPS_PPS_PIN);
    gps.ready = true; gps.data.valid = true; gps.data.utc_sec = kUtc + second;
    gps.sentence_start_us = host_project::precise_us + 100000;
    gps.utc_received_us = host_project::precise_us + 200000;
  }
  host_project::precise_us += gps_edge ? 200000 : 1000;
  time_sync_update();
}

void acquire_gps(bool with_rtc = false) {
  rtc.ready = with_rtc;
  for (unsigned second = 0; second <= 6; ++second) tick(second, true, with_rtc);
  require(time_sync_state() == TimeSyncState::GPS_OK, "real PPS must acquire GPS lock");
}
void acquire_rtc() {
  rtc.ready = true;
  for (unsigned second = 0; second <= 6; ++second) tick(second, false, true);
  require(time_sync_state() == TimeSyncState::RTC_OK, "real SQW must acquire RTC lock");
}
void capture(int64_t esp) {
  host_project::precise_us = esp; host::fire_gpio(EXT_INT_PIN);
  require(host::interrupt_depth == 0, "interrupt mask must be restored");
}
void dispatch(ModuleType type = ModuleType::FINISH) {
  event_dispatcher_handle_event_isr(type, area);
  expect(host::interrupt_depth == 0, "event consumption must restore interrupts");
}
void expect_first(ModuleType type = ModuleType::FINISH) {
  const std::string expected = type == ModuleType::START
      ? "$00:00:06,250;-6250#\n" : "F00:00:06,250#\n";
  expect(Serial.output == expected && host_project::delivered == expected,
         "full pipeline must produce independently specified UTC packet");
  expect(area.last.esp_timestamp_us == base_esp + 6250123 &&
             area.last.utc_timestamp_us == static_cast<int64_t>(kUtc) * kSecond + 6250123,
         "processing latency must not replace the captured microsecond timestamp");
}

template <bool Rtc, bool Auto, bool Start> void healthy_pipeline() {
  begin(Auto); if (Rtc) acquire_rtc(); else acquire_gps();
  capture(base_esp + 6250123); dispatch(Start ? ModuleType::START : ModuleType::FINISH);
  expect_first(Start ? ModuleType::START : ModuleType::FINISH);
}
void next_anchor_does_not_move_pending_event() {
  begin(); acquire_gps(); capture(base_esp + 6250123);
  tick(7, true, false); dispatch(); expect_first();
}
template <bool FromGps> void source_change_preserves_event() {
  begin(); if (FromGps) acquire_gps(true); else acquire_rtc();
  capture(base_esp + 6250123);
  for (unsigned second = 7; second <= 12; ++second) tick(second, !FromGps, true);
  require(time_sync_state() == (FromGps ? TimeSyncState::RTC_OK : TimeSyncState::GPS_OK),
          "source switch must complete before pending event processing");
  dispatch(); expect_first();
}
void acquisition_after_capture_preserves_event_time() {
  begin(); capture(base_esp + 6250123);
  for (unsigned second = 7; second <= 12; ++second) tick(second, true, false);
  require(time_sync_state() == TimeSyncState::GPS_OK, "GPS must acquire after event capture");
  dispatch(); expect_first();
}
void no_source_no_packet() {
  begin(); capture(base_esp + 6250123); dispatch();
  expect(Serial.output.empty() && host_project::delivered.empty() &&
             area.displayed == 1 && !area.last.success, "no UTC must not create a valid packet");
}
template <int64_t Delta, bool Start, bool Accepted> void repeated_event_boundary() {
  begin(); acquire_gps(); const auto type = Start ? ModuleType::START : ModuleType::FINISH;
  capture(base_esp + 6250123); dispatch(type);
  const auto first_serial = Serial.output;
  capture(base_esp + 6250123 + Delta); time_sync_update(); dispatch(type);
  expect(area.displayed == (Accepted ? 2U : 1U), "event filter boundary must select correct count");
  expect((Serial.output.size() > first_serial.size()) == Accepted,
         "filtered event must not produce another packet");
  if (Accepted)
    expect(area.last.esp_timestamp_us == base_esp + 6250123 + Delta,
           "accepted second event must retain its own timestamp");
}
void pending_slot_keeps_first_capture() {
  begin(); acquire_gps(); capture(base_esp + 6250123);
  capture(base_esp + 6500123); dispatch(); expect_first();
}
void polling_does_not_duplicate() {
  begin(); acquire_gps(); capture(base_esp + 6250123);
  for (unsigned i = 0; i < 10; ++i) dispatch();
  expect(area.displayed == 1, "one capture must be consumed exactly once"); expect_first();
}
void long_uptime_keeps_microseconds() {
  base_esp += (1LL << 32) * 1000;
  begin(); acquire_gps(); capture(base_esp + 6250123); dispatch(); expect_first();
}

const Test kTests[] = {
  {"pipeline_gps_finish_auto_on", healthy_pipeline<false,true,false>},
  {"pipeline_gps_finish_auto_off", healthy_pipeline<false,false,false>},
  {"pipeline_rtc_finish_auto_on", healthy_pipeline<true,true,false>},
  {"pipeline_rtc_finish_auto_off", healthy_pipeline<true,false,false>},
  {"pipeline_gps_start", healthy_pipeline<false,true,true>},
  {"pipeline_rtc_start", healthy_pipeline<true,true,true>},
  {"pipeline_event_before_next_pps_processing", next_anchor_does_not_move_pending_event},
  {"pipeline_pending_gps_to_rtc", source_change_preserves_event<true>},
  {"pipeline_pending_rtc_to_gps", source_change_preserves_event<false>},
  {"pipeline_acquires_utc_after_capture", acquisition_after_capture_preserves_event_time},
  {"pipeline_no_source_no_packet", no_source_no_packet},
  {"pipeline_finish_deadtime_before", repeated_event_boundary<99999,false,false>},
  {"pipeline_finish_deadtime_exact", repeated_event_boundary<100000,false,true>},
  {"pipeline_finish_deadtime_after", repeated_event_boundary<100001,false,true>},
  {"pipeline_start_interval_before", repeated_event_boundary<1999999,true,false>},
  {"pipeline_start_interval_exact", repeated_event_boundary<2000000,true,true>},
  {"pipeline_start_interval_after", repeated_event_boundary<2000001,true,true>},
  {"pipeline_finish_ignores_start_interval", repeated_event_boundary<150000,false,true>},
  {"pipeline_pending_slot_preserves_first", pending_slot_keeps_first_capture},
  {"pipeline_polling_consumes_event_once", polling_does_not_duplicate},
  {"pipeline_uptime_beyond_millis_rollover", long_uptime_keeps_microseconds},
};
} // namespace
int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
