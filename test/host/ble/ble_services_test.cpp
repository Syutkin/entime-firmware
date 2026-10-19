#include "../support/application/environment.h"
#include "hal/ble/battery_service.h"
#include "hal/ble/device_info_service.h"
#include <esp_mac.h>

namespace {
NimBLEServer server;
NimBLECharacteristic &battery() { return *server.services.front()->characteristics.front(); }
void begin_battery() { require(batteryService.init(&server), "battery service must initialize"); }
void subscribe(uint16_t value) {
  NimBLEConnInfo info; require(battery().callbacks != nullptr, "subscription callback");
  battery().callbacks->onSubscribe(&battery(), info, value);
}
unsigned level() { return static_cast<uint8_t>(battery().getValue().at(0)); }
void battery_initial_value() {
  batteryService.setLevel(45); begin_battery();
  expect(server.services.front()->uuid.id == 0x180f && battery().uuid.id == 0x2a19 &&
             battery().properties == (NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY) &&
             level() == 100 && host_project::notify_calls == 0,
         "battery must expose standard UUIDs and initial readable level without notifying");
}
template <bool Characteristic> void battery_creation_failure() {
  if constexpr (Characteristic) host_ble::fail_characteristic_at = 1;
  else host_ble::fail_service_at = 1;
  expect(!batteryService.init(&server), "service/characteristic failure must propagate");
  batteryService.setLevel(45);
  expect(host_project::notify_calls == 0, "failed service must not send notifications");
  host_ble::fail_service_at = host_ble::fail_characteristic_at = 0;
  expect(batteryService.init(&server), "fresh init must recover after creation failure");
}
template <uint16_t Subscription> void battery_subscription() {
  begin_battery(); subscribe(Subscription); batteryService.setLevel(37);
  expect(level() == 37 && host_project::notify_calls == ((Subscription & 1) ? 1U : 0U),
         "reading must update regardless of subscription; only notify bit enables notifications");
}
void battery_boundaries_and_duplicates() {
  begin_battery(); subscribe(1); batteryService.setLevel(255);
  expect(level() == 100 && host_project::notify_calls == 0, "clamp above 100 and suppress same value");
  batteryService.setLevel(0); batteryService.setLevel(0);
  expect(level() == 0 && host_project::notify_calls == 1, "zero must notify once");
  batteryService.setLevel(99); batteryService.setLevel(100); batteryService.setLevel(101);
  expect(level() == 100 && host_project::notify_calls == 3, "upper boundary must clamp before duplicate check");
}
void battery_unsubscribe_and_disconnect() {
  begin_battery(); subscribe(1); batteryService.setLevel(80);
  subscribe(0); batteryService.setLevel(70); subscribe(1);
  batteryService.onDisconnect(19); batteryService.setLevel(60);
  expect(level() == 60 && host_project::notify_calls == 1,
         "unsubscribe and disconnect must disable notifications while preserving readable level");
  subscribe(1); batteryService.setLevel(50);
  expect(host_project::notify_calls == 2 && level() == 50, "new subscription must resume notifications");
}
void battery_reinit_clears_subscription() {
  begin_battery(); subscribe(1); batteryService.setLevel(10);
  NimBLEServer next; require(batteryService.init(&next), "reinit battery");
  batteryService.setLevel(20);
  expect(host_project::notify_calls == 1 &&
             static_cast<uint8_t>(next.services[0]->characteristics[0]->getValue()[0]) == 20,
         "reinitialized service must not inherit old client's subscription");
}
void device_info_values() {
  require(deviceInfoService.init(&server), "device information init");
  const auto &service = *server.services.front();
  expect(service.uuid.id == 0x180a && service.characteristics.size() == 6, "standard DIS layout");
  const uint16_t ids[] = {0x2a29,0x2a24,0x2a25,0x2a26,0x2a27,0x2a28};
  const char *values[] = {BLE_DIS_MANUFACTURER, BLE_DIS_MODEL,
      BLE_DIS_SERIAL_PREFIX "0012ABCDEF89", VERSION, BLE_DIS_HARDWARE, BLE_DIS_SOFTWARE};
  for (unsigned i = 0; i < 6; ++i)
    expect(service.characteristics[i]->uuid.id == ids[i] &&
               service.characteristics[i]->properties == NIMBLE_PROPERTY::READ &&
               service.characteristics[i]->getValue() == values[i],
           "DIS characteristic must be readable and contain configured build/device information");
  NimBLEServer next; require(deviceInfoService.init(&next), "repeat init");
  expect(host_ble::mac_reads == 1 && next.services[0]->characteristics[2]->getValue() == values[2],
         "serial must remain stable across repeated initialization");
}
void device_info_service_failure() {
  host_ble::fail_service_at = 1;
  expect(!deviceInfoService.init(&server), "DIS service creation failure must propagate");
  host_ble::fail_service_at = 0;
  expect(deviceInfoService.init(&server), "DIS service must recover on retry");
}
void device_info_mac_unavailable() {
  host_ble::mac_ok = false;
  require(deviceInfoService.init(&server), "missing MAC must not prevent service creation");
  expect(server.services[0]->characteristics[2]->getValue() == BLE_DIS_SERIAL_PREFIX "000000000000",
         "unavailable MAC must produce deterministic zero fallback, not uninitialized bytes");
}
const Test kTests[] = {
  {"battery_service_initial", battery_initial_value},
  {"battery_service_create_failure", battery_creation_failure<false>},
  {"battery_characteristic_create_failure", battery_creation_failure<true>},
  {"battery_subscription_off", battery_subscription<0>},
  {"battery_subscription_notify", battery_subscription<1>},
  {"battery_subscription_indicate_only", battery_subscription<2>},
  {"battery_subscription_both_bits", battery_subscription<3>},
  {"battery_boundaries_duplicates", battery_boundaries_and_duplicates},
  {"battery_unsubscribe_disconnect", battery_unsubscribe_and_disconnect},
  {"battery_reinit_no_subscription", battery_reinit_clears_subscription},
  {"dis_values_stable_serial", device_info_values},
  {"dis_service_failure_retry", device_info_service_failure},
  {"dis_mac_unavailable", device_info_mac_unavailable},
};
} // namespace
int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
