#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace host_project {
inline bool notify_succeeds = true;
inline unsigned notify_calls = 0;
inline std::deque<bool> notify_results;
inline std::string delivered;
inline std::function<void()> after_notify;
}
class NimBLECharacteristic;
class NimBLEConnInfo {};
class NimBLEUUID {
public:
  NimBLEUUID(const char *) {}
  NimBLEUUID(uint16_t) {}
};
namespace NIMBLE_PROPERTY { enum { READ=1, WRITE=2, WRITE_NR=4, NOTIFY=8 }; }
class NimBLECharacteristicCallbacks {
public:
  virtual ~NimBLECharacteristicCallbacks() = default;
  virtual void onWrite(NimBLECharacteristic *, NimBLEConnInfo &) {}
  virtual void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &, uint16_t) {}
};
class NimBLECharacteristic {
  std::string value;
public:
  std::string getValue() { return value; }
  void setCallbacks(NimBLECharacteristicCallbacks *) {}
  void setValue(const uint8_t *data, size_t size) {
    value.assign(reinterpret_cast<const char *>(data), size);
  }
  bool notify() {
    ++host_project::notify_calls;
    bool ok = host_project::notify_succeeds;
    if (!host_project::notify_results.empty()) {
      ok = host_project::notify_results.front();
      host_project::notify_results.pop_front();
    }
    if (ok) host_project::delivered += value;
    const auto callback = host_project::after_notify;
    if (callback) callback();
    return ok;
  }
};
class NimBLEService {
  std::vector<std::unique_ptr<NimBLECharacteristic>> characteristics;
public:
  NimBLECharacteristic *createCharacteristic(const NimBLEUUID &, int) {
    characteristics.emplace_back(std::make_unique<NimBLECharacteristic>());
    return characteristics.back().get();
  }
  bool start() { return true; }
};
class NimBLEServer {
  std::vector<std::unique_ptr<NimBLEService>> services;
public:
  NimBLEService *createService(const char *) {
    services.emplace_back(std::make_unique<NimBLEService>());
    return services.back().get();
  }
};
class NimBLEAdvertisementData {
public:
  void addServiceUUID(const char *) {}
};
