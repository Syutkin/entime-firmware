#include "support.h"
#include "hal/ble/nus_service.h"
#include "storage/settings.h"
#include "timing/event_dispatcher.h"
#include "timing/event_isr.h"
#include "ui/main_area.h"

SettingsManager settings;
// UTC conversion is controlled here; formatting, ISR, dispatcher and NUS TX
// execute their actual production implementations.
bool time_sync_esp_to_utc_us(int64_t esp_us, int64_t &utc_us) {
  utc_us = 1800000000000000LL + esp_us;
  return true;
}

namespace {
constexpr int kEventPin = 15;
void connect(NimBLEServer &server) {
  require(nusService.init(&server), "NUS must initialize");
  NimBLEConnInfo client;
  nusService.onConnect(client, 23);
  nusService.onNotifyStateChanged(true);
  host_project::now_ms = 10000;
  event_isr_init(kEventPin);
}

std::string capture_event(ModuleType type, MainArea &area) {
  Serial.output.clear();
  host::fire_gpio(kEventPin);
  event_dispatcher_handle_event_isr(type, area);
  require(!Serial.output.empty(), "the event must be formatted and sent to Serial");
  return Serial.output;
}

void successful_event_delivery() {
  NimBLEServer server; connect(server); MainArea area;
  const auto packet = capture_event(ModuleType::FINISH, area);
  expect(host_project::delivered == packet && area.displayed == 1,
         "healthy BLE must deliver exactly the event displayed on screen");
}

void transient_backpressure_delivers_once() {
  NimBLEServer server; connect(server); MainArea area;
  host_project::notify_results = {false, false, false, true};
  const auto packet = capture_event(ModuleType::FINISH, area);
  expect(host_project::delivered == packet && host_project::notify_calls == 4,
         "transient notify failures must retry without duplicating event bytes");
}

template <bool Partial> void exhausted_retries_preserve_event() {
  NimBLEServer server; connect(server); MainArea area;
  host_project::notify_succeeds = false;
  if constexpr (Partial) host_project::notify_results = {true};
  const auto packet = capture_event(Partial ? ModuleType::START : ModuleType::FINISH,
                                    area);
  if constexpr (Partial)
    require(!host_project::delivered.empty() &&
                host_project::delivered.size() < packet.size(),
            "the first MTU chunk must be sent before the simulated failure");
  else
    require(host_project::delivered.empty(), "all notify attempts must fail");
  require(host_project::notify_calls >= 50, "retry budget must be exhausted");
  host_project::notify_succeeds = true;
  for (unsigned i = 0; i < 4; ++i) {
    host_project::now_ms += 1000;
    event_dispatcher_handle_event_isr(Partial ? ModuleType::START : ModuleType::FINISH,
                                      area);
  }
  expect(host_project::delivered.find(packet) != std::string::npos,
         "AU002: event must remain available for complete delivery after BLE recovers");
}

void subscription_gap_preserves_event() {
  NimBLEServer server; connect(server); MainArea area;
  nusService.onNotifyStateChanged(false);
  const auto packet = capture_event(ModuleType::FINISH, area);
  require(host_project::delivered.empty(), "unsubscribed client cannot receive notifications");
  nusService.onNotifyStateChanged(true);
  for (unsigned i = 0; i < 4; ++i) {
    host_project::now_ms += 1000;
    event_dispatcher_handle_event_isr(ModuleType::FINISH, area);
  }
  expect(host_project::delivered == packet,
         "AU002: subscription recovery must deliver the pending event");
}

void mtu_fragmentation_preserves_bytes() {
  NimBLEServer server; connect(server); MainArea area;
  const auto packet = capture_event(ModuleType::START, area);
  require(packet.size() > 20, "START packet must cross the default ATT MTU");
  expect(host_project::notify_calls == 2 && host_project::delivered == packet,
         "MTU fragmentation must preserve full packet contents and delimiter");
}

const Test kTests[] = {
  {"ble_event_delivery_control", successful_event_delivery},
  {"ble_transient_backpressure_control", transient_backpressure_delivers_once},
  {"ble_exhausted_retries_preserve_event", exhausted_retries_preserve_event<false>, "AU002"},
  {"ble_partial_packet_is_recoverable", exhausted_retries_preserve_event<true>, "AU002"},
  {"ble_subscription_gap_preserves_event", subscription_gap_preserves_event, "AU002"},
  {"ble_mtu_fragmentation_control", mtu_fragmentation_preserves_bytes},
};
} // namespace
int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
