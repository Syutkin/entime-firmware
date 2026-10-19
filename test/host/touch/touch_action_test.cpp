#include "../support/application/environment.h"
#include "app/touch_action_handler.h"

namespace {
MainArea area;
WiFiManager wifi;
TouchActionHandler handler;
void begin(uint8_t type = 1) {
  require(settings.begin(), "settings initialize");
  auto device = settings.getDevice(); device.type = type;
  require(settings.setDevice(device), "supported role");
  handler.init(area, settings, wifi);
}
void tap(UiTouchTarget target, TouchEventType type = TouchEventType::Release) {
  TouchEvent event; event.type = type; handler.handleTouchEvent(event, target);
}
void uninitialized_handler_is_inert() {
  tap(UiTouchTarget::StatusBarWifi); tap(UiTouchTarget::StatusBarBattery);
  expect(area.draws == 0 && wifi.begins == 0 && wifi.stops == 0,
         "touch before init must not act on missing dependencies");
}
void press_move_and_unused_targets_are_inert() {
  begin();
  for (auto target : {UiTouchTarget::StatusBarWifi, UiTouchTarget::StatusBarBattery,
                      UiTouchTarget::StatusBarGps, UiTouchTarget::StatusBarBackground}) {
    tap(target, TouchEventType::Press); tap(target, TouchEventType::Move);
  }
  for (auto target : {UiTouchTarget::None, UiTouchTarget::StatusBarClock,
                      UiTouchTarget::StatusBarBluetooth, UiTouchTarget::MainArea, UiTouchTarget::Footer})
    tap(target);
  expect(area.draws == 0 && area.logs.empty() && wifi.begins == 0 && wifi.stops == 0,
         "only supported Release targets may perform actions");
}
template <bool Finish, bool Gps> void info_screen_toggle() {
  begin(Finish ? 2 : 1);
  const auto working = Finish ? MainAreaType::FINISH : MainAreaType::START;
  const auto screen = Gps ? MainAreaType::GPS : MainAreaType::BATTERY;
  const auto target = Gps ? UiTouchTarget::StatusBarGps : UiTouchTarget::StatusBarBattery;
  area.setType(working); tap(target);
  expect(area.getType() == screen && area.draws == 1, "first tap must open requested information");
  tap(target);
  expect(area.getType() == working && area.draws == 2,
         "second tap must return to the role-specific working screen");
}
template <bool Finish> void loading_screen_toggle() {
  begin(Finish ? 2 : 1); tap(UiTouchTarget::StatusBarBackground);
  expect(area.getType() == (Finish ? MainAreaType::FINISH : MainAreaType::START),
         "background tap from LOADING must select configured role");
  area.setType(MainAreaType::GPS); tap(UiTouchTarget::StatusBarBackground);
  expect(area.getType() == MainAreaType::LOADING && area.draws == 2,
         "background tap from an information screen must open the loading log");
}
void switching_between_info_screens() {
  begin(); tap(UiTouchTarget::StatusBarBattery); tap(UiTouchTarget::StatusBarGps);
  expect(area.getType() == MainAreaType::GPS && area.draws == 2,
         "GPS tap from battery screen must switch directly to GPS");
  tap(UiTouchTarget::StatusBarBattery);
  expect(area.getType() == MainAreaType::BATTERY, "battery tap from GPS screen must switch directly");
}
template <bool Uninitialized, bool Open> void wifi_enable() {
  begin(); wifi.state = Uninitialized ? WiFiState::UNINITIALIZED : WiFiState::OFF;
  auto config = settings.getWifi(); config.ssid = "RaceNetwork";
  config.passwd = Open ? "" : "secret-pass";
  require(settings.setWifi(config), "stored WiFi credentials");
  const auto before = host_project::nvs;
  tap(UiTouchTarget::StatusBarWifi);
  expect(wifi.begins == 1 && wifi.connects == 1 && wifi.stops == 0 &&
             wifi.ssid == "RaceNetwork" && wifi.password == (Open ? "" : "secret-pass") &&
             wifi.state == WiFiState::CONNECTING &&
             area.logs == std::vector<std::string>{"WiFi connecting..."} && host_project::nvs == before,
         "tap must start WiFi using saved credentials without persisting temporary state");
}
void missing_ssid_does_not_start_wifi() {
  begin(); tap(UiTouchTarget::StatusBarWifi);
  expect(wifi.begins == 0 && wifi.connects == 0 &&
             area.logs == std::vector<std::string>{"ERROR: WiFi is not configured"},
         "missing SSID must report a UI error without starting hardware");
}
void connect_failure_is_reported() {
  begin(); auto config = settings.getWifi(); config.ssid = "RaceNetwork";
  require(settings.setWifi(config), "saved SSID"); wifi.start_ok = false;
  tap(UiTouchTarget::StatusBarWifi);
  expect(wifi.begins == 1 && wifi.connects == 1 &&
             area.logs == std::vector<std::string>{"ERROR: WiFi connect failed"},
         "failed start must not be reported as connecting");
}
template <unsigned Index> void wifi_disable() {
  begin(); const WiFiState states[] = {WiFiState::CONNECTING, WiFiState::CONNECTED,
      WiFiState::DISCONNECTED, WiFiState::RECONNECTING, WiFiState::ERROR};
  wifi.state = states[Index]; const auto before = host_project::nvs;
  tap(UiTouchTarget::StatusBarWifi);
  expect(wifi.stops == 1 && wifi.begins == 0 && wifi.connects == 0 &&
             area.logs == std::vector<std::string>{"WiFi disabled"} && host_project::nvs == before,
         "tap from any active/error state must stop WiFi without saving settings");
}
void stop_timeout_is_reported() {
  begin(); wifi.state = WiFiState::CONNECTED; wifi.stop_ok = false;
  tap(UiTouchTarget::StatusBarWifi);
  expect(wifi.stops == 1 && wifi.state == WiFiState::CONNECTED &&
             area.logs == std::vector<std::string>{"ERROR: WiFi stop timeout"},
         "stop timeout must not report success");
}
const Test kTests[] = {
  {"touch_action_before_init", uninitialized_handler_is_inert},
  {"touch_action_only_supported_release", press_move_and_unused_targets_are_inert},
  {"touch_action_start_battery_toggle", info_screen_toggle<false,false>},
  {"touch_action_finish_battery_toggle", info_screen_toggle<true,false>},
  {"touch_action_start_gps_toggle", info_screen_toggle<false,true>},
  {"touch_action_finish_gps_toggle", info_screen_toggle<true,true>},
  {"touch_action_start_loading_toggle", loading_screen_toggle<false>},
  {"touch_action_finish_loading_toggle", loading_screen_toggle<true>},
  {"touch_action_switch_info_screens", switching_between_info_screens},
  {"touch_action_wifi_uninitialized_secured", wifi_enable<true,false>},
  {"touch_action_wifi_off_secured", wifi_enable<false,false>},
  {"touch_action_wifi_open", wifi_enable<false,true>},
  {"touch_action_wifi_no_ssid", missing_ssid_does_not_start_wifi},
  {"touch_action_wifi_start_failure", connect_failure_is_reported},
  {"touch_action_wifi_disable_connecting", wifi_disable<0>},
  {"touch_action_wifi_disable_connected", wifi_disable<1>},
  {"touch_action_wifi_disable_disconnected", wifi_disable<2>},
  {"touch_action_wifi_disable_reconnecting", wifi_disable<3>},
  {"touch_action_wifi_disable_error", wifi_disable<4>},
  {"touch_action_wifi_stop_timeout", stop_timeout_is_reported},
};
} // namespace
int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
