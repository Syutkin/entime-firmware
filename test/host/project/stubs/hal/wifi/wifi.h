#pragma once
#include <Arduino.h>
enum class WiFiState {
  UNINITIALIZED, OFF, CONNECTING, RECONNECTING, CONNECTED, DISCONNECTED, ERROR
};
class WiFiManager {
public:
  WiFiState state = WiFiState::OFF;
  bool start_ok = true, stop_ok = true;
  unsigned begins = 0, connects = 0, stops = 0;
  String ssid, password;
  WiFiState getState() { return state; }
  int getRSSI() { return -55; }
  String getIP() { return isConnected() ? String("192.0.2.1") : String(""); }
  String getSSID() { return ssid; }
  bool isConnected() { return state == WiFiState::CONNECTED; }
  bool end() { ++stops; if (stop_ok) state = WiFiState::OFF; return stop_ok; }
  void begin() { ++begins; }
  bool connect(const String &name, const String &pass) {
    ++connects; ssid = name; password = pass;
    if (start_ok) state = WiFiState::CONNECTING;
    return start_ok;
  }
};
extern WiFiManager wifiManager;
