#include "../support/application/environment.h"
#include "../../../src/ui/main_screen.h"
#include <functional>

// Keep the real component declarations; only drawing/update endpoints are modeled.
namespace host_ui {
std::vector<std::string> calls;
BLEState ble = BLEState::DISCONNECTED;
WiFiState wifi = WiFiState::UNINITIALIZED;
GPSState gps = GPSState::OFF;
TimeSyncState sync = TimeSyncState::NONE;
int8_t rssi = 0, sats = -1;
InaBatteryLevel battery = InaBatteryLevel::NoData;
bool charging = false;
std::function<void()> during_wifi;
}
void Footer::draw() { host_ui::calls.push_back("draw-footer"); }
void MainArea::draw() { host_ui::calls.push_back("draw-area"); }
void StatusBar::draw() { host_ui::calls.push_back("draw-bar"); }
void Footer::updateSats(int8_t value) { host_ui::calls.push_back("sats"); host_ui::sats = value; }
void Footer::updateTimeSyncState(TimeSyncState value) { host_ui::calls.push_back("sync"); host_ui::sync = value; }
void StatusBar::updateBluetoothIcon(BLEState value) { host_ui::calls.push_back("ble"); host_ui::ble = value; }
void StatusBar::updateWiFiIcon(WiFiState value, int8_t rssi) {
  host_ui::calls.push_back("wifi"); host_ui::wifi = value; host_ui::rssi = rssi;
  const auto hook = host_ui::during_wifi; if (hook) hook();
}
void StatusBar::updateGPSIcon(GPSState value) { host_ui::calls.push_back("gps"); host_ui::gps = value; }
void StatusBar::updateBatteryIcon(InaBatteryLevel value, bool charging) {
  host_ui::calls.push_back("battery"); host_ui::battery = value; host_ui::charging = charging;
}

namespace {
MainScreen screen;
Footer footer;
MainArea area;
StatusBar bar;
void begin() { screen.init(footer, area, bar); }
void before_init_is_inert() {
  screen.draw(); screen.update();
  expect(host_ui::calls.empty(), "uninitialized coordinator must not draw or update components");
}
void full_draw_and_idle_update() {
  begin(); screen.draw();
  expect(host_ui::calls == std::vector<std::string>({"draw-bar","draw-area","draw-footer"}),
         "full draw must visit all three components in layer order");
  host_ui::calls.clear(); screen.update(); screen.update();
  expect(host_ui::calls.empty(), "idle update must not redraw components");
}
void posts_are_deferred_and_coalesced() {
  begin(); screen.postBleState(BLEState::ADVERTISING); screen.postBleState(BLEState::CONNECTED);
  screen.postWiFiState(WiFiState::CONNECTED,-55); screen.postWiFiState(WiFiState::CONNECTED,-71);
  screen.postGpsState(GPSState::SEARCHING); screen.postGpsState(GPSState::ACTIVE);
  screen.postTimeSyncState(TimeSyncState::RTC_OK); screen.postTimeSyncState(TimeSyncState::GPS_OK);
  screen.postSats(5); screen.postSats(12);
  screen.postBatteryState(InaBatteryLevel::Low,false); screen.postBatteryState(InaBatteryLevel::Full,true);
  expect(host_ui::calls.empty(), "posting from callbacks must not draw immediately");
  screen.update();
  expect(host_ui::calls == std::vector<std::string>({"ble","wifi","gps","sync","sats","battery"}) &&
             host_ui::ble == BLEState::CONNECTED && host_ui::wifi == WiFiState::CONNECTED &&
             host_ui::rssi == -71 && host_ui::gps == GPSState::ACTIVE &&
             host_ui::sync == TimeSyncState::GPS_OK && host_ui::sats == 12 &&
             host_ui::battery == InaBatteryLevel::Full && host_ui::charging,
         "flush must apply only the latest value for every dirty field");
  host_ui::calls.clear(); screen.update();
  expect(host_ui::calls.empty(), "flushed dirty bits must be consumed once");
}
void duplicate_posts_are_suppressed() {
  begin();
  for (unsigned i = 0; i < 2; ++i) {
    screen.postBleState(BLEState::CONNECTED); screen.postWiFiState(WiFiState::CONNECTED,-55);
    screen.postGpsState(GPSState::ACTIVE); screen.postTimeSyncState(TimeSyncState::GPS_OK);
    screen.postSats(7); screen.postBatteryState(InaBatteryLevel::Mid,false);
    screen.update();
    expect(host_ui::calls.size() == 6, "same values must not schedule a second component update");
  }
}
void rssi_change_alone_updates_wifi() {
  begin(); screen.postWiFiState(WiFiState::CONNECTED,-55); screen.update(); host_ui::calls.clear();
  screen.postWiFiState(WiFiState::CONNECTED,-70); screen.update();
  expect(host_ui::calls == std::vector<std::string>{"wifi"} && host_ui::rssi == -70,
         "RSSI change must update WiFi without changing state or touching other fields");
}
void charging_animation_and_stop() {
  begin(); screen.postBatteryState(InaBatteryLevel::Mid,true); screen.update();
  screen.postBatteryState(InaBatteryLevel::Mid,true); screen.update();
  screen.postBatteryState(InaBatteryLevel::Mid,false); screen.update();
  screen.postBatteryState(InaBatteryLevel::Mid,false); screen.update();
  expect(host_ui::calls == std::vector<std::string>({"battery","battery","battery"}) &&
             !host_ui::charging, "charging must animate repeated values and stop repeating after charging ends");
}
void pending_before_init_is_preserved() {
  screen.postSats(9); screen.update();
  expect(host_ui::calls.empty(), "pre-init update must not consume pending changes");
  begin(); screen.update();
  expect(host_ui::calls == std::vector<std::string>{"sats"} && host_ui::sats == 9,
         "first initialized update must flush pending callbacks");
}
void post_during_flush_is_not_lost() {
  begin(); screen.postWiFiState(WiFiState::CONNECTED,-55);
  host_ui::during_wifi = [] {
    expect(host_project::critical_depth == 0, "component updates must run outside the pending-state lock");
    screen.postGpsState(GPSState::ACTIVE);
  };
  screen.update();
  expect(host_ui::calls == std::vector<std::string>{"wifi"},
         "updates posted during a flush must not alter the captured snapshot");
  host_ui::during_wifi = {}; screen.update();
  expect(host_ui::calls == std::vector<std::string>({"wifi","gps"}) && host_ui::gps == GPSState::ACTIVE,
         "next flush must consume the update posted during the previous flush");
}
void return_to_default_before_flush() {
  begin(); screen.postGpsState(GPSState::ACTIVE); screen.postGpsState(GPSState::OFF); screen.update();
  expect(host_ui::calls == std::vector<std::string>{"gps"} && host_ui::gps == GPSState::OFF,
         "a batch returning to default must retain its final value");
}
const Test kTests[] = {
  {"ui_before_init_inert", before_init_is_inert},
  {"ui_full_draw_idle_update", full_draw_and_idle_update},
  {"ui_posts_deferred_coalesced", posts_are_deferred_and_coalesced},
  {"ui_duplicate_posts_suppressed", duplicate_posts_are_suppressed},
  {"ui_rssi_change_only", rssi_change_alone_updates_wifi},
  {"ui_charging_animation_stop", charging_animation_and_stop},
  {"ui_pending_before_init", pending_before_init_is_preserved},
  {"ui_post_during_flush", post_during_flush_is_not_lost},
  {"ui_batch_returns_to_default", return_to_default_before_flush},
};
} // namespace
int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
