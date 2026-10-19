#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
enum { ESP_MAC_WIFI_STA };
namespace host_ble {
inline bool mac_ok = true;
inline unsigned mac_reads = 0;
inline std::array<uint8_t,6> mac{0x00,0x12,0xab,0xcd,0xef,0x89};
}
inline int esp_read_mac(uint8_t *out, int) {
  ++host_ble::mac_reads;
  if (!host_ble::mac_ok) return -1;
  std::copy(host_ble::mac.begin(), host_ble::mac.end(), out); return 0;
}
