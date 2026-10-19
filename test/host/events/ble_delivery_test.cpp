#include "../support/application/environment.h"
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
  expect(host_project::delivered == packet,
         "event must remain available for complete delivery after BLE recovers");
  expect(Serial.output == packet && area.displayed == 1,
         "retries must not repeat Serial output or UI updates");
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
         "subscription recovery must deliver the pending event");
}

void disconnected_event_reaches_next_client() {
  NimBLEServer server; connect(server); MainArea area;
  nusService.onDisconnect(0);
  const auto packet = capture_event(ModuleType::FINISH, area);
  require(host_project::delivered.empty(), "offline event must remain pending");
  NimBLEConnInfo client;
  nusService.onConnect(client, 23);
  nusService.onNotifyStateChanged(true);
  event_dispatcher_handle_event_isr(ModuleType::FINISH, area);
  expect(host_project::delivered == packet && area.displayed == 1,
         "reconnection must deliver the retained event exactly once");
}

void partial_packet_restarts_for_new_client() {
  NimBLEServer server; connect(server); MainArea area;
  host_project::notify_succeeds = false;
  host_project::notify_results = {true};
  const auto packet = capture_event(ModuleType::START, area);
  require(!host_project::delivered.empty() && host_project::delivered.size() < packet.size(),
          "old client must receive only the first fragment");
  nusService.onDisconnect(0);
  NimBLEConnInfo client;
  nusService.onConnect(client, 23);
  nusService.onNotifyStateChanged(true);
  host_project::delivered.clear(); // New client has no bytes from the old session.
  host_project::notify_succeeds = true;
  event_dispatcher_handle_event_isr(ModuleType::START, area);
  expect(host_project::delivered == packet && area.displayed == 1,
         "new client must receive the whole event, including its prefix");
}

void pending_event_keeps_next_capture() {
  NimBLEServer server; connect(server); MainArea area;
  host_project::notify_succeeds = false;
  const auto first = capture_event(ModuleType::FINISH, area);
  host_project::now_ms += 200;
  host::fire_gpio(kEventPin);
  event_dispatcher_handle_event_isr(ModuleType::FINISH, area);
  expect(area.displayed == 1 && Serial.output == first,
         "pending packet must not be overwritten by the next capture");
  host_project::notify_succeeds = true;
  event_dispatcher_handle_event_isr(ModuleType::FINISH, area);
  expect(area.displayed == 2 && host_project::delivered == Serial.output &&
             host_project::delivered.size() == first.size() * 2 &&
             host_project::delivered.substr(0, first.size()) == first,
         "both retained events must be delivered in capture order");
  const auto delivered = host_project::delivered;
  event_dispatcher_handle_event_isr(ModuleType::FINISH, area);
  expect(host_project::delivered == delivered && area.displayed == 2,
         "polling must not repeat completed events");
}

void partial_packet_resumes_after_mtu_change() {
  NimBLEServer server; connect(server); MainArea area;
  host_project::notify_succeeds = false;
  host_project::notify_results = {true};
  const auto packet = capture_event(ModuleType::START, area);
  nusService.onMtuUpdated(100);
  host_project::notify_succeeds = true;
  event_dispatcher_handle_event_isr(ModuleType::START, area);
  expect(host_project::delivered == packet,
         "MTU change must preserve the byte offset of a pending event");
}

void pending_packet_precedes_command_reply() {
  NimBLEServer server; connect(server); MainArea area;
  host_project::notify_succeeds = false;
  host_project::notify_results = {true};
  const auto packet = capture_event(ModuleType::START, area);
  require(!host_project::delivered.empty() && host_project::delivered.size() < packet.size(),
          "event must remain partially sent before the command reply");
  const std::string reply = "{\"status\":\"ok\"}\n";
  expect(nusService.write(reinterpret_cast<const uint8_t *>(reply.data()), reply.size()) == 0,
         "blocked event must prevent interleaving a reply into its bytes");
  host_project::notify_succeeds = true;
  expect(nusService.write(reinterpret_cast<const uint8_t *>(reply.data()), reply.size()) == reply.size(),
         "recovered connection must accept the command reply");
  expect(host_project::delivered == packet + reply,
         "pending event must finish before any reply bytes are written");
  event_dispatcher_handle_event_isr(ModuleType::START, area);
  expect(host_project::delivered == packet + reply && area.displayed == 1,
         "dispatcher must not resend the event completed by another write");
}

void reconnect_during_notify_restarts_packet() {
  NimBLEServer server; connect(server); MainArea area;
  host_project::after_notify = [] {
    nusService.onDisconnect(0);
    NimBLEConnInfo client;
    nusService.onConnect(client, 23);
    nusService.onNotifyStateChanged(true);
    host_project::delivered.clear();
    host_project::after_notify = nullptr;
  };
  const auto packet = capture_event(ModuleType::START, area);
  event_dispatcher_handle_event_isr(ModuleType::START, area);
  expect(host_project::delivered == packet && area.displayed == 1,
         "connection change during notify must retain the whole packet for the new client");
}

void partial_packet_survives_subscription_gap() {
  NimBLEServer server; connect(server); MainArea area;
  host_project::notify_succeeds = false;
  host_project::notify_results = {true};
  const auto packet = capture_event(ModuleType::START, area);
  nusService.onNotifyStateChanged(false);
  event_dispatcher_handle_event_isr(ModuleType::START, area);
  host_project::notify_succeeds = true;
  nusService.onNotifyStateChanged(true);
  event_dispatcher_handle_event_isr(ModuleType::START, area);
  expect(host_project::delivered == packet && area.displayed == 1,
         "subscription pause in the same connection must resume without duplicate bytes");
}

void pending_packet_survives_ble_restart() {
  NimBLEServer server; connect(server); MainArea area;
  host_project::notify_succeeds = false;
  const auto packet = capture_event(ModuleType::FINISH, area);
  nusService.onBleEnd();
  require(nusService.init(&server), "BLE service must restart");
  NimBLEConnInfo client;
  nusService.onConnect(client, 23);
  nusService.onNotifyStateChanged(true);
  host_project::notify_succeeds = true;
  event_dispatcher_handle_event_isr(ModuleType::FINISH, area);
  expect(host_project::delivered == packet && area.displayed == 1,
         "BLE lifecycle restart must preserve an event awaiting transmission");
}

void mtu_fragmentation_preserves_bytes() {
  NimBLEServer server; connect(server); MainArea area;
  const auto packet = capture_event(ModuleType::START, area);
  require(packet.size() > 20, "START packet must cross the default ATT MTU");
  expect(host_project::notify_calls == 2 && host_project::delivered == packet,
         "MTU fragmentation must preserve full packet contents and delimiter");
}

const Test kTests[] = {
  {"ble_reconnect_during_notify_restarts_packet", reconnect_during_notify_restarts_packet},
  {"ble_partial_packet_survives_subscription_gap", partial_packet_survives_subscription_gap},
  {"ble_pending_packet_survives_ble_restart", pending_packet_survives_ble_restart},
  {"ble_pending_packet_precedes_command_reply", pending_packet_precedes_command_reply},
  {"ble_disconnected_event_reaches_next_client", disconnected_event_reaches_next_client},
  {"ble_partial_packet_restarts_for_new_client", partial_packet_restarts_for_new_client},
  {"ble_pending_event_keeps_next_capture", pending_event_keeps_next_capture},
  {"ble_partial_packet_resumes_after_mtu_change", partial_packet_resumes_after_mtu_change},
  {"ble_event_delivery_control", successful_event_delivery},
  {"ble_transient_backpressure_control", transient_backpressure_delivers_once},
  {"ble_exhausted_retries_preserve_event", exhausted_retries_preserve_event<false>},
  {"ble_partial_packet_is_recoverable", exhausted_retries_preserve_event<true>},
  {"ble_subscription_gap_preserves_event", subscription_gap_preserves_event},
  {"ble_mtu_fragmentation_control", mtu_fragmentation_preserves_bytes},
};
} // namespace
int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
