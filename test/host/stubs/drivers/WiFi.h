#pragma once
#include <Arduino.h>
#include <functional>
#include <vector>

enum wifi_mode_t { WIFI_OFF, WIFI_STA };
enum wl_status_t { WL_DISCONNECTED, WL_CONNECTED };
enum arduino_event_id_t {
  ARDUINO_EVENT_WIFI_STA_START, ARDUINO_EVENT_WIFI_STA_CONNECTED,
  ARDUINO_EVENT_WIFI_STA_GOT_IP, ARDUINO_EVENT_WIFI_STA_DISCONNECTED,
  ARDUINO_EVENT_WIFI_STA_STOP, ARDUINO_EVENT_OTHER,
};
struct arduino_event_info_t {
  struct { char ssid[33] = "RaceNetwork"; } wifi_sta_connected;
  struct { int reason = 0; } wifi_sta_disconnected;
};
using wifi_event_id_t = unsigned;
using WiFiEventCb = void (*)(arduino_event_id_t, arduino_event_info_t);

namespace host_wifi {
struct Attempt { std::string ssid, password; bool secured; };
inline std::vector<Attempt> attempts;
inline std::vector<std::pair<bool, bool>> disconnects;
inline unsigned registrations = 0, mode_calls = 0, rssi_reads = 0;
inline bool core_auto_reconnect = true, stop_completes = true;
inline wifi_mode_t mode = WIFI_OFF;
inline wl_status_t status = WL_DISCONNECTED;
inline int8_t rssi = -55;
inline WiFiEventCb callback = nullptr;
inline std::function<void()> during_delay;
inline void emit(arduino_event_id_t event) {
  if (!callback) throw std::runtime_error("setup: WiFi callback not registered");
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) status = WL_CONNECTED;
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED ||
      event == ARDUINO_EVENT_WIFI_STA_STOP) status = WL_DISCONNECTED;
  callback(event, {});
}
} // namespace host_wifi

class IPAddress {
public:
  String toString() const { return "192.0.2.17"; }
};
class HostWiFi {
public:
  void mode(wifi_mode_t value) { ++host_wifi::mode_calls; host_wifi::mode = value; }
  wifi_mode_t getMode() const { return host_wifi::mode; }
  void setAutoReconnect(bool value) { host_wifi::core_auto_reconnect = value; }
  wifi_event_id_t onEvent(WiFiEventCb callback) {
    ++host_wifi::registrations; host_wifi::callback = callback; return 1;
  }
  void begin(const char *ssid, const char *password) {
    host_wifi::attempts.push_back({ssid, password, true});
  }
  void begin(const char *ssid) { host_wifi::attempts.push_back({ssid, "", false}); }
  void disconnect(bool off = false, bool erase = false) {
    host_wifi::disconnects.emplace_back(off, erase);
    host_wifi::status = WL_DISCONNECTED;
    if (off && host_wifi::stop_completes) host_wifi::mode = WIFI_OFF;
  }
  wl_status_t status() const { return host_wifi::status; }
  int8_t RSSI() const { ++host_wifi::rssi_reads; return host_wifi::rssi; }
  IPAddress localIP() const { return {}; }
  String SSID() const {
    return host_wifi::attempts.empty() ? String("")
                                     : String(host_wifi::attempts.back().ssid);
  }
};
inline HostWiFi WiFi;
