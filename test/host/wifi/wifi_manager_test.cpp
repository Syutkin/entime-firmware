#include "../support/application/environment.h"
#include "../../../src/hal/wifi/wifi.h"
#include <vector>

void delay(uint32_t ms) {
  host_project::now_ms += ms;
  if (host_wifi::during_delay) host_wifi::during_delay();
}

namespace {
struct State { WiFiState state; int8_t rssi; };
std::vector<State> changes;
void changed(WiFiState state, int8_t rssi) { changes.push_back({state, rssi}); }
void begin() {
  host_project::now_ms = 10000;
  wifiManager.setStateCallback(changed);
  wifiManager.begin();
}
void connect(bool secured = true) {
  begin();
  require(wifiManager.connect("RaceNetwork", secured ? "secret-pass" : ""),
          "connection must start");
}
void connected() { connect(); host_wifi::emit(ARDUINO_EVENT_WIFI_STA_GOT_IP); }
void advance(uint32_t ms) { host_project::now_ms += ms; wifiManager.update(); }
void lost() { host_wifi::emit(ARDUINO_EVENT_WIFI_STA_DISCONNECTED); }
void retry() { advance(5000); }
void expect_hidden_connection() {
  expect(!wifiManager.isConnected() && wifiManager.getRSSI() == 0 &&
             wifiManager.getIP().empty() && wifiManager.getSSID().empty(),
         "connection details must be unavailable until the station has IP");
}

void uninitialized_is_inert() {
  expect(wifiManager.getState() == WiFiState::UNINITIALIZED, "initial state");
  expect(!wifiManager.connect("RaceNetwork", "secret-pass"), "begin is required");
  wifiManager.update(); wifiManager.disconnect();
  wifiManager.onWiFiEvent(ARDUINO_EVENT_WIFI_STA_GOT_IP, {});
  expect(wifiManager.end() && host_wifi::attempts.empty() &&
             host_wifi::disconnects.empty() && host_wifi::registrations == 0,
         "inactive operations must not touch the radio");
  expect_hidden_connection();
}
void begin_is_idempotent() {
  begin(); wifiManager.begin();
  expect(wifiManager.getState() == WiFiState::OFF && host_wifi::mode == WIFI_STA &&
             host_wifi::mode_calls == 1 && host_wifi::registrations == 1 &&
             !host_wifi::core_auto_reconnect && changes.size() == 1,
         "begin must configure STA once, register once and disable core retries");
}
void empty_ssid_is_rejected() {
  begin(); changes.clear();
  expect(!wifiManager.connect("", "secret-pass") && host_wifi::attempts.empty() &&
             changes.empty() && wifiManager.getState() == WiFiState::OFF,
         "empty SSID must not start a connection or change state");
}
void begin_preserves_active_connection() {
  connected(); const auto count = changes.size(); wifiManager.begin();
  expect(wifiManager.isConnected() && changes.size() == count &&
             host_wifi::mode_calls == 1 && host_wifi::attempts.size() == 1,
         "repeated begin must not reset or restart an active connection");
}
void station_start_and_stop() {
  begin(); host_wifi::emit(ARDUINO_EVENT_WIFI_STA_START);
  expect(wifiManager.getState() == WiFiState::OFF && host_wifi::attempts.empty(),
         "STA_START without configured credentials must not create a connection");
  require(wifiManager.connect("RaceNetwork", "secret-pass"), "connection must start");
  host_wifi::emit(ARDUINO_EVENT_WIFI_STA_GOT_IP);
  host_wifi::emit(ARDUINO_EVENT_WIFI_STA_STOP); advance(60000);
  expect(wifiManager.getState() == WiFiState::OFF && host_wifi::attempts.size() == 1,
         "STA_STOP must expose OFF without scheduling a reconnect");
  expect_hidden_connection();
}
template <bool Secured> void connection_lifecycle() {
  connect(Secured);
  expect(wifiManager.getState() == WiFiState::CONNECTING &&
             host_wifi::attempts.size() == 1 &&
             host_wifi::attempts[0].ssid == "RaceNetwork" &&
             host_wifi::attempts[0].password == (Secured ? "secret-pass" : "") &&
             host_wifi::attempts[0].secured == Secured,
         "select the correct open/secured WiFi.begin overload and credentials");
  expect_hidden_connection();
  host_wifi::emit(ARDUINO_EVENT_WIFI_STA_START);
  host_wifi::emit(ARDUINO_EVENT_WIFI_STA_CONNECTED);
  expect(wifiManager.getState() == WiFiState::CONNECTING, "association is not IP readiness");
  host_wifi::emit(ARDUINO_EVENT_WIFI_STA_GOT_IP);
  expect(wifiManager.isConnected() && wifiManager.getState() == WiFiState::CONNECTED &&
             wifiManager.getRSSI() == -55 && wifiManager.getIP() == "192.0.2.17" &&
             wifiManager.getSSID() == "RaceNetwork" && changes.size() == 3 &&
             changes.back().state == WiFiState::CONNECTED && changes.back().rssi == -55,
         "IP event must expose connection data and notify state once");
  host_wifi::status = WL_DISCONNECTED;
  expect(!wifiManager.isConnected() && wifiManager.getIP().empty() &&
             wifiManager.getSSID().empty(), "core status must also confirm the connection");
}
void unchanged_events_do_not_notify() {
  connected(); const auto count = changes.size();
  host_wifi::emit(ARDUINO_EVENT_WIFI_STA_GOT_IP);
  host_wifi::emit(ARDUINO_EVENT_WIFI_STA_CONNECTED);
  host_wifi::emit(ARDUINO_EVENT_OTHER);
  advance(1000);
  expect(changes.size() == count, "same state/RSSI and unrelated events must not notify");
}
template <bool Wrap> void reconnect_interval() {
  connected();
  if constexpr (Wrap) host_project::now_ms = UINT32_MAX - 2500;
  lost();
  expect(wifiManager.getState() == WiFiState::RECONNECTING &&
             changes.back().rssi == 0, "loss must clear RSSI and schedule reconnect");
  expect_hidden_connection();
  advance(4999);
  expect(host_wifi::attempts.size() == 1, "no retry before five seconds");
  advance(1);
  expect(host_wifi::attempts.size() == 2 &&
             wifiManager.getState() == WiFiState::CONNECTING &&
             host_wifi::attempts.back().password == "secret-pass",
         "retry at exactly five seconds must reuse credentials, including after rollover");
  advance(60000);
  expect(host_wifi::attempts.size() == 2, "update must not duplicate an in-progress attempt");
  host_wifi::emit(ARDUINO_EVENT_WIFI_STA_GOT_IP);
  expect(wifiManager.isConnected(), "retry must recover after receiving IP");
}
void open_network_retries_without_password() {
  connect(false); lost(); retry();
  expect(host_wifi::attempts.size() == 2 && !host_wifi::attempts.back().secured,
         "open network retry must use the password-free overload");
}
void retries_are_bounded() {
  connect(); lost();
  for (unsigned i = 0; i < 10; ++i) {
    retry();
    require(host_wifi::attempts.size() == i + 2, "each failed attempt must be retried once");
    lost();
  }
  expect(wifiManager.getState() == WiFiState::ERROR, "stop after ten reconnection attempts");
  advance(60000); lost(); advance(60000);
  expect(host_wifi::attempts.size() == 11, "errors and more loss events must not restart retries");
  require(wifiManager.connect("NewNetwork", "new-secret"), "explicit reconnect must be allowed");
  lost(); retry();
  expect(host_wifi::attempts.size() == 13 && host_wifi::attempts.back().ssid == "NewNetwork" &&
             host_wifi::attempts.back().password == "new-secret",
         "explicit connect must reset retry budget and replace credentials");
}
void successful_connection_resets_retry_budget() {
  connect(); lost();
  for (unsigned i = 0; i < 8; ++i) { retry(); lost(); }
  retry(); host_wifi::emit(ARDUINO_EVENT_WIFI_STA_GOT_IP); lost();
  for (unsigned i = 0; i < 8; ++i) { retry(); lost(); }
  expect(wifiManager.getState() == WiFiState::RECONNECTING && host_wifi::attempts.size() == 18,
         "a successful IP acquisition must replenish the retry budget");
}
template <bool Wrap> void rssi_polling() {
  connect();
  if constexpr (Wrap) host_project::now_ms = UINT32_MAX - 500;
  host_wifi::emit(ARDUINO_EVENT_WIFI_STA_GOT_IP);
  const auto count = changes.size();
  const auto reads = host_wifi::rssi_reads;
  host_wifi::rssi = -72; advance(999);
  expect(host_wifi::rssi_reads == reads && changes.size() == count,
         "RSSI must not be sampled before one second");
  advance(1);
  expect(host_wifi::rssi_reads == reads + 1 && wifiManager.getRSSI() == -72 &&
             changes.size() == count + 1 && changes.back().rssi == -72,
         "changed RSSI must notify at one second, including after rollover");
  advance(1000);
  expect(changes.size() == count + 1, "unchanged RSSI must not notify");
  lost(); const auto after = host_wifi::rssi_reads; advance(1000);
  expect(host_wifi::rssi_reads == after, "disconnected station must not poll RSSI");
}
void disconnect_cancels_retry() {
  connected(); lost(); wifiManager.disconnect();
  expect(wifiManager.getState() == WiFiState::OFF && host_wifi::disconnects.size() == 1,
         "manual disconnect must stop the current connection");
  lost(); advance(60000);
  expect(host_wifi::attempts.size() == 1 && wifiManager.getState() == WiFiState::OFF,
         "manual disconnect must clear credentials and suppress automatic retries");
  expect_hidden_connection();
}
void disconnect_callback_has_zero_rssi() {
  connected(); wifiManager.disconnect();
  expect(changes.back().state == WiFiState::OFF && changes.back().rssi == 0,
         "OFF callback must not expose RSSI from the previous connection");
}
void disconnect_callback_can_start_new_connection() {
  connected();
  wifiManager.setStateCallback([](WiFiState state, int8_t rssi) {
    changed(state, rssi);
    if (state == WiFiState::OFF) {
      expect(rssi == 0, "OFF callback must observe cleaned connection state");
      require(wifiManager.connect("NewNetwork", "new-pass"), "callback may start a new connection");
    }
  });
  wifiManager.disconnect();
  host_wifi::emit(ARDUINO_EVENT_WIFI_STA_GOT_IP);
  expect(wifiManager.isConnected() && wifiManager.getSSID() == "NewNetwork",
         "disconnect must not clear a new connection started by its callback");
  lost(); retry();
  expect(host_wifi::attempts.size() == 3 && host_wifi::attempts.back().password == "new-pass",
         "callback-started connection must retain credentials and automatic recovery");
}
void late_ip_after_disconnect_is_ignored() {
  connected(); wifiManager.disconnect();
  const auto count = changes.size(); const auto reads = host_wifi::rssi_reads;
  // The queued callback arrives after the radio has already disconnected.
  // Unlike emit(), it must not change the current core status back to connected.
  host_wifi::callback(ARDUINO_EVENT_WIFI_STA_GOT_IP, {}); advance(60000);
  expect(wifiManager.getState() == WiFiState::OFF && !wifiManager.isConnected() &&
             changes.size() == count && host_wifi::rssi_reads == reads,
         "a late IP event must not reactivate a manually disconnected station");
}
void ip_without_connection_request_is_ignored() {
  begin(); host_wifi::emit(ARDUINO_EVENT_WIFI_STA_GOT_IP);
  expect(wifiManager.getState() == WiFiState::OFF && !wifiManager.isConnected() &&
             changes.size() == 1 && host_wifi::rssi_reads == 0,
         "IP without a connection request must not activate an idle manager");
}
void cancelled_connection_can_be_restarted() {
  connect(); wifiManager.disconnect();
  host_wifi::callback(ARDUINO_EVENT_WIFI_STA_GOT_IP, {});
  expect(wifiManager.getState() == WiFiState::OFF,
         "IP from a cancelled in-progress connection must be ignored");
  require(wifiManager.connect("NewNetwork", "new-pass"), "explicit connection must restart");
  host_wifi::emit(ARDUINO_EVENT_WIFI_STA_GOT_IP);
  expect(wifiManager.isConnected() && wifiManager.getSSID() == "NewNetwork",
         "IP from a new explicit connection must still be accepted");
  lost(); retry();
  expect(host_wifi::attempts.size() == 3 && host_wifi::attempts.back().ssid == "NewNetwork",
         "new connection must retain normal automatic recovery");
}
template <unsigned Mode> void end_and_restart() {
  connected();
  if constexpr (Mode != 0) host_wifi::stop_completes = false;
  if constexpr (Mode == 1) host_wifi::during_delay = [] {
    if (host_project::now_ms >= 10030) host_wifi::mode = WIFI_OFF;
  };
  if constexpr (Mode == 3) host_project::now_ms = UINT32_MAX - 500;
  const uint32_t started = millis();
  const bool stopped = wifiManager.end();
  expect(stopped == (Mode < 2) &&
             static_cast<uint32_t>(millis() - started) == (Mode == 0 ? 0U : Mode == 1 ? 30U : 1000U),
         "end must wait for OFF, report timeout and handle millis rollover");
  expect(wifiManager.getState() == WiFiState::UNINITIALIZED &&
             host_wifi::disconnects.back() == std::make_pair(true, true),
         "end must request radio shutdown and erase core credentials");
  expect_hidden_connection();
  const auto count = changes.size();
  for (auto event : {ARDUINO_EVENT_WIFI_STA_GOT_IP, ARDUINO_EVENT_WIFI_STA_DISCONNECTED,
                     ARDUINO_EVENT_WIFI_STA_START, ARDUINO_EVENT_WIFI_STA_STOP})
    host_wifi::emit(event);
  advance(60000);
  expect(changes.size() == count && host_wifi::attempts.size() == 1,
         "late events after end must neither notify nor reconnect");
  expect(wifiManager.end() && host_wifi::disconnects.size() == 1,
         "repeated end must not request another hardware shutdown");
  wifiManager.begin();
  expect(host_wifi::registrations == 1 && host_wifi::mode_calls == 2,
         "restart must reuse the event handler and enable STA again");
  require(wifiManager.connect("Restarted", ""), "restart must accept a new connection");
  lost(); retry();
  expect(host_wifi::attempts.size() == 3 && host_wifi::attempts.back().ssid == "Restarted",
         "connect after end must restore automatic reconnection");
}
void removed_callback_is_not_called() {
  connected(); const auto count = changes.size(); wifiManager.setStateCallback(nullptr);
  lost(); retry(); host_wifi::emit(ARDUINO_EVENT_WIFI_STA_GOT_IP);
  expect(changes.size() == count && wifiManager.isConnected(),
         "removing callback must preserve behavior without notifications");
}
void password_is_not_logged() {
  connect(); lost(); retry();
  for (const auto &line : host_project::logs)
    expect(line.find("secret-pass") == std::string::npos, "WiFi logs must not reveal password");
}

const Test kTests[] = {
  {"wifi_uninitialized_inert", uninitialized_is_inert},
  {"wifi_begin_idempotent", begin_is_idempotent},
  {"wifi_empty_ssid_rejected", empty_ssid_is_rejected},
  {"wifi_begin_preserves_active_connection", begin_preserves_active_connection},
  {"wifi_station_start_and_stop", station_start_and_stop},
  {"wifi_secured_connection", connection_lifecycle<true>},
  {"wifi_open_connection", connection_lifecycle<false>},
  {"wifi_unchanged_events_no_notify", unchanged_events_do_not_notify},
  {"wifi_reconnect_interval", reconnect_interval<false>},
  {"wifi_reconnect_rollover", reconnect_interval<true>},
  {"wifi_open_reconnect", open_network_retries_without_password},
  {"wifi_retry_limit_and_explicit_recovery", retries_are_bounded},
  {"wifi_success_resets_retry_budget", successful_connection_resets_retry_budget},
  {"wifi_rssi_interval", rssi_polling<false>},
  {"wifi_rssi_rollover", rssi_polling<true>},
  {"wifi_disconnect_cancels_retry", disconnect_cancels_retry},
  {"wifi_disconnect_callback_zero_rssi", disconnect_callback_has_zero_rssi},
  {"wifi_disconnect_callback_restart", disconnect_callback_can_start_new_connection},
  {"wifi_late_ip_after_disconnect", late_ip_after_disconnect_is_ignored},
  {"wifi_ip_without_connection_request", ip_without_connection_request_is_ignored},
  {"wifi_cancelled_connection_restart", cancelled_connection_can_be_restarted},
  {"wifi_end_immediate", end_and_restart<0>},
  {"wifi_end_delayed", end_and_restart<1>},
  {"wifi_end_timeout", end_and_restart<2>},
  {"wifi_end_timeout_rollover", end_and_restart<3>},
  {"wifi_callback_removed", removed_callback_is_not_called},
  {"wifi_password_not_logged", password_is_not_logged},
};
} // namespace
int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
