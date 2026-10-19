#pragma once

struct WiFiManager {
  bool connected = true;
  bool isConnected() const { return connected; }
};
