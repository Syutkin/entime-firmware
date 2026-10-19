#include "../support/application/environment.h"
#include "hal/ble/ble.h"
#include "hal/ble/nus_service.h"
#include "hal/ble/battery_service.h"
#include "hal/ble/device_info_service.h"

namespace {
void begin() {
  require(ble.init("RaceTimer-7"), "BLE init");
  require(ble.registerService(nusService) && ble.registerService(batteryService) &&
              ble.registerService(deviceInfoService), "all production plugins must register");
  ble.startAdvertising();
}
NimBLECharacteristic &characteristic(unsigned service, unsigned index) {
  return *NimBLEDevice::server->services.at(service)->characteristics.at(index);
}
NimBLEConnInfo info;
void connect() {
  NimBLEDevice::advertising.active = false;
  NimBLEDevice::server->callbacks->onConnect(NimBLEDevice::server.get(), info);
}
void subscribe(unsigned service, unsigned index) {
  auto &value = characteristic(service,index);
  require(value.callbacks != nullptr, "characteristic must have subscription callback");
  value.callbacks->onSubscribe(&value, info, 1);
}
void write(const char *text) {
  auto &value = characteristic(0,0); value.setValue(text);
  value.callbacks->onWrite(&value, info);
}
void layout_and_payload() {
  begin(); const auto &services = NimBLEDevice::server->services;
  expect(services.size() == 3 && services[0]->characteristics.size() == 2 &&
             services[1]->characteristics.size() == 1 && services[2]->characteristics.size() == 6 &&
             services[1]->uuid.id == 0x180f && services[2]->uuid.id == 0x180a &&
             NimBLEDevice::advertising.data.uuids == std::vector<std::string>{
                 "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"} &&
             NimBLEDevice::advertising.scan.name == "RaceTimer-7",
         "real plugins must coexist and advertise NUS with the configured device name");
}
void connection_reaches_services() {
  begin(); info.mtu = 100; connect();
  subscribe(0,1); subscribe(1,0); write("ping\n");
  expect(nusService.available() == 5 && nusService.read() == 'p',
         "server lifecycle and RX characteristic callback must reach the real NUS stream");
  const std::string packet(80,'x');
  expect(nusService.write(reinterpret_cast<const uint8_t *>(packet.data()),packet.size()) == 80 &&
             host_project::notify_calls == 1 && host_project::delivered == packet,
         "MTU from server callback must allow an 80-byte packet in one notification");
  batteryService.setLevel(42);
  expect(host_project::notify_calls == 2 && characteristic(1,0).getValue() == std::string(1,char(42)),
         "battery subscription must operate independently alongside NUS");
}
void disconnect_and_reconnect() {
  begin(); connect(); subscribe(0,1); subscribe(1,0); write("old-partial");
  const auto generation = nusService.connectionGeneration();
  NimBLEDevice::server->callbacks->onDisconnect(NimBLEDevice::server.get(),info,19);
  batteryService.setLevel(80);
  expect(nusService.available() == 0 && nusService.connectionGeneration() != generation &&
             host_project::notify_calls == 0 && ble.isAdvertising(),
         "disconnect must clear RX, subscriptions and resume advertising");
  connect(); write("new\n"); batteryService.setLevel(70);
  expect(nusService.available() == 4 && nusService.read() == 'n' && host_project::notify_calls == 0,
         "reconnected client must receive clean RX and explicitly resubscribe");
  subscribe(1,0); batteryService.setLevel(60);
  expect(host_project::notify_calls == 1, "battery notifications must resume after fresh subscription");
}
void end_and_restart() {
  begin(); connect(); subscribe(0,1); write("old"); ble.end();
  expect(nusService.available() == 0 && !ble.isConnected(), "BLE end must release NUS RX state");
  begin(); connect(); subscribe(0,1); write("fresh\n");
  expect(nusService.available() == 6 && NimBLEDevice::server->services.size() == 3,
         "restart must rebuild all real services and accept clean RX");
}
void missing_battery_does_not_remove_nus() {
  host_ble::fail_service_at = 2; begin(); connect(); subscribe(0,1); write("ping\n");
  expect(ble.isConnected() && nusService.available() == 5 &&
             NimBLEDevice::server->services.size() == 2 &&
             NimBLEDevice::server->services[1]->uuid.id == 0x180a,
         "battery service failure must not prevent NUS or later DIS initialization");
}
const Test kTests[] = {
  {"ble_stack_layout_advertising", layout_and_payload},
  {"ble_stack_callbacks_services", connection_reaches_services},
  {"ble_stack_disconnect_reconnect", disconnect_and_reconnect},
  {"ble_stack_end_restart", end_and_restart},
  {"ble_stack_battery_failure_isolation", missing_battery_does_not_remove_nus},
};
} // namespace
int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
