#include "../support/application/environment.h"
#include "hal/ble/ble.h"

namespace {
std::vector<BLEState> states;
void changed(BLEState state) { states.push_back(state); }
struct Plugin : IBleServicePlugin {
  bool ok = true;
  unsigned inits = 0, payloads = 0, connects = 0, disconnects = 0, ends = 0;
  uint16_t mtu = 0;
  int reason = 0;
  bool init(NimBLEServer *server) override {
    require(server != nullptr, "plugin must receive the server");
    ++inits; host_ble::trace.push_back("plugin"); return ok;
  }
  void configureAdvertising(NimBLEAdvertisementData &data) override {
    ++payloads; data.addServiceUUID("test-service");
  }
  void onConnect(NimBLEConnInfo &, uint16_t value) override { ++connects; mtu = value; }
  void onDisconnect(int value) override { ++disconnects; reason = value; }
  void onMtuUpdated(uint16_t value) override { mtu = value; }
  void onBleEnd() override {
    expect(NimBLEDevice::server != nullptr, "plugins must release resources before deinit");
    ++ends; host_ble::trace.push_back("end-plugin");
  }
};
Plugin plugin;
void begin() {
  ble.setStateCallback(changed);
  require(ble.init("RaceTimer-7"), "BLE init must succeed");
  require(ble.registerService(plugin), "plugin registration must succeed");
}
void advertise() { begin(); ble.startAdvertising(); }
void connect(NimBLEConnInfo &info) {
  NimBLEDevice::advertising.active = false;
  NimBLEDevice::server->callbacks->onConnect(NimBLEDevice::server.get(), info);
}
void idle_operations() {
  ble.setStateCallback(changed); ble.startAdvertising(); ble.stopAdvertising();
  expect(ble.getState() == BLEState::DISCONNECTED && !ble.isConnected() &&
             !ble.isAdvertising() && !ble.registerService(plugin) && plugin.inits == 0 &&
             states == std::vector<BLEState>{BLEState::DISCONNECTED},
         "uninitialized server must not register plugins or advertise");
}
void init_is_idempotent() {
  begin(); require(ble.init("Ignored"), "repeated init succeeds");
  expect(host_ble::init_calls == 1 && host_ble::name == "RaceTimer-7" &&
             host_ble::mtu == 517 && host_ble::bonding && host_ble::secure && !host_ble::mitm &&
             NimBLEDevice::server->callbacks != nullptr && plugin.inits == 0,
         "init must configure device/security once and defer plugin initialization");
}
void server_creation_failure_can_retry() {
  host_ble::server_ok = false;
  expect(!ble.init("RaceTimer-7") && !ble.registerService(plugin), "server failure must propagate");
  ble.startAdvertising(); expect(!ble.isAdvertising(), "failed server must not advertise");
  host_ble::server_ok = true; begin(); ble.startAdvertising();
  expect(ble.isAdvertising() && plugin.inits == 1, "retry must recover server and plugins");
}
void advertising_is_configured_once() {
  advertise(); auto &adv = NimBLEDevice::advertising;
  expect(host_ble::trace == std::vector<std::string>({"plugin","advertise"}) &&
             adv.resets == 1 && adv.payloads == 1 && adv.data.flags == 0x06 &&
             adv.data.uuids == std::vector<std::string>{"test-service"} &&
             adv.scan.name == "RaceTimer-7" && ble.getState() == BLEState::ADVERTISING,
         "plugins must initialize before advertising and supply payload; name goes in scan response");
  ble.startAdvertising();
  expect(plugin.inits == 1 && plugin.payloads == 1 && adv.starts == 1,
         "repeated start must not initialize or start advertising twice");
  expect(!ble.registerService(plugin), "registration must close after plugin initialization");
  ble.stopAdvertising(); ble.stopAdvertising(); ble.startAdvertising();
  expect(adv.starts == 2 && adv.resets == 1 && plugin.inits == 1 &&
             ble.isAdvertising(), "restart must reuse advertising payload and services");
}
template <bool Missing> void advertising_failure_can_retry() {
  begin();
  if constexpr (Missing) host_ble::advertising_available = false;
  else host_ble::start_ok = false;
  ble.startAdvertising();
  expect(!ble.isAdvertising() && ble.getState() == BLEState::DISCONNECTED,
         "unavailable/failed advertising must not report advertising");
  host_ble::advertising_available = true; host_ble::start_ok = true;
  ble.startAdvertising();
  expect(ble.isAdvertising() && plugin.inits == 1 && plugin.payloads == 1,
         "retry must recover advertising without reinitializing plugins");
}
void plugin_limit() {
  require(ble.init("RaceTimer-7"), "server"); Plugin plugins[9];
  for (unsigned i = 0; i < 8; ++i) expect(ble.registerService(plugins[i]), "eight plugins fit");
  expect(!ble.registerService(plugins[8]), "ninth plugin must be rejected");
  ble.startAdvertising();
  for (unsigned i = 0; i < 8; ++i) expect(plugins[i].inits == 1, "registered plugin initializes once");
  expect(plugins[8].inits == 0, "rejected plugin must not initialize");
}
void failing_plugin_does_not_skip_others() {
  begin(); plugin.ok = false; Plugin other; require(ble.registerService(other), "second plugin");
  ble.startAdvertising(); ble.startAdvertising();
  expect(plugin.inits == 1 && other.inits == 1 && ble.isAdvertising(),
         "one plugin init failure must not skip others or trigger repeated initialization");
}
void callbacks_and_mtu() {
  advertise(); NimBLEConnInfo info; info.mtu = 185; connect(info);
  expect(ble.isConnected() && ble.getClientCount() == 1 &&
             ble.getState() == BLEState::CONNECTED && plugin.connects == 1 && plugin.mtu == 185 &&
             NimBLEDevice::server->params == std::vector<uint16_t>({7,6,6,0,500}),
         "server connect callback must forward MTU, update client count and request connection parameters");
  NimBLEDevice::server->callbacks->onMTUChange(247, info);
  expect(plugin.mtu == 247 && plugin.connects == 1, "MTU callback must not create another connection");
  NimBLEDevice::server->callbacks->onDisconnect(NimBLEDevice::server.get(), info, 19);
  expect(!ble.isConnected() && ble.getClientCount() == 0 && ble.isAdvertising() &&
             plugin.disconnects == 1 && plugin.reason == 19 && plugin.inits == 1 &&
             states.back() == BLEState::ADVERTISING,
         "disconnect callback must forward reason and resume advertising using existing services");
}
template <unsigned Clients> void remaining_client_keeps_connected_state() {
  advertise(); NimBLEConnInfo info;
  for (unsigned i = 0; i < Clients; ++i) {
    if (i) ble.startAdvertising();
    info.handle = 7 + i; connect(info);
  }
  require(ble.getClientCount() == Clients, "all clients must be counted");
  for (unsigned remaining = Clients; remaining > 0; --remaining) {
    info.handle = 7 + remaining - 1;
    NimBLEDevice::server->callbacks->onDisconnect(NimBLEDevice::server.get(), info, 19);
    const bool connected = remaining > 1;
    const BLEState expected = connected ? BLEState::CONNECTED : BLEState::ADVERTISING;
    expect(ble.getClientCount() == remaining - 1 && ble.isConnected() == connected &&
               ble.getState() == expected && states.back() == expected &&
               plugin.disconnects == Clients - remaining + 1,
           "connection state and callbacks must match the count until the last client disconnects");
  }
  info.handle = 23;
  connect(info);
  expect(ble.isConnected() && ble.getClientCount() == 1 && states.back() == BLEState::CONNECTED,
         "new connection after the final disconnect must start with a clean count");
}
void disconnect_count_does_not_underflow() {
  advertise(); ble.onDisconnect(0); ble.onDisconnect(0);
  expect(ble.getClientCount() == 0 && !ble.isConnected(), "unmatched disconnect must not underflow count");
}
void end_and_reinitialize() {
  advertise(); NimBLEConnInfo info; connect(info); ble.end();
  expect(plugin.ends == 1 && host_ble::deinit_calls == 1 &&
             ble.getClientCount() == 0 && !ble.isConnected() && !ble.isAdvertising() &&
             states.back() == BLEState::DISCONNECTED &&
             host_ble::trace[host_ble::trace.size()-2] == "end-plugin" &&
             host_ble::trace.back() == "deinit", "end must notify plugins before releasing BLE and reset state");
  require(ble.init("Restarted-8"), "restart"); ble.startAdvertising();
  expect(plugin.inits == 1 && NimBLEDevice::advertising.scan.name == "Restarted-8",
         "restart must discard previous registrations and use the new name");
}
void callback_removal() {
  advertise(); const auto count = states.size(); ble.setStateCallback(nullptr);
  ble.stopAdvertising(); ble.startAdvertising();
  expect(states.size() == count && ble.isAdvertising(), "callback removal must not stop BLE operations");
}
const Test kTests[] = {
  {"ble_idle_operations", idle_operations}, {"ble_init_idempotent", init_is_idempotent},
  {"ble_server_creation_retry", server_creation_failure_can_retry},
  {"ble_advertising_configured_once", advertising_is_configured_once},
  {"ble_advertising_missing_retry", advertising_failure_can_retry<true>},
  {"ble_advertising_start_failure_retry", advertising_failure_can_retry<false>},
  {"ble_plugin_limit", plugin_limit}, {"ble_plugin_failure_isolation", failing_plugin_does_not_skip_others},
  {"ble_callbacks_mtu_reconnect", callbacks_and_mtu},
  {"ble_remaining_client_connected", remaining_client_keeps_connected_state<2>},
  {"ble_three_clients_disconnect", remaining_client_keeps_connected_state<3>},
  {"ble_client_count_no_underflow", disconnect_count_does_not_underflow},
  {"ble_end_reinitialize", end_and_reinitialize}, {"ble_callback_removed", callback_removal},
};
} // namespace
int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
