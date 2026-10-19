#pragma once
#include <Arduino.h>
enum class WiFiState {
  UNINITIALIZED, OFF, CONNECTING, RECONNECTING, CONNECTED, DISCONNECTED, ERROR
};
// Declarations only: unrelated handlers are discarded by the linker. An
// unexpected invocation fails linking rather than silently doing nothing.
class WiFiManager {
public:
  WiFiState getState();
  int getRSSI();
  String getIP();
  String getSSID();
  bool isConnected();
  bool end();
  void begin();
  bool connect(const String &, const String &);
};
extern WiFiManager wifiManager;
