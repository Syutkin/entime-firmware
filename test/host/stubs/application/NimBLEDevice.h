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
namespace host_ble {
inline bool server_ok = true, advertising_available = true, start_ok = true;
inline unsigned service_calls = 0, characteristic_calls = 0;
inline unsigned fail_service_at = 0, fail_characteristic_at = 0;
inline unsigned init_calls = 0, deinit_calls = 0;
inline std::string name;
inline uint16_t mtu = 0;
inline std::vector<std::string> trace;
inline bool bonding = false, mitm = false, secure = false;
}
class NimBLEAddress {
public:
  std::string toString() const { return "00:11:22:33:44:55"; }
};
class NimBLEConnInfo {
public:
  uint16_t mtu = 23, handle = 7;
  uint16_t getMTU() const { return mtu; }
  uint16_t getConnHandle() const { return handle; }
  NimBLEAddress getAddress() const { return {}; }
};
class NimBLEUUID {
public:
  uint16_t id = 0;
  std::string text;
  NimBLEUUID(const char *value) : text(value) {}
  NimBLEUUID(uint16_t value) : id(value) {}
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
  NimBLEUUID uuid{uint16_t(0)};
  int properties = 0;
  unsigned sets = 0;
  NimBLECharacteristicCallbacks *callbacks = nullptr;
  std::string getValue() { return value; }
  void setCallbacks(NimBLECharacteristicCallbacks *value) { callbacks = value; }
  void setValue(const uint8_t *data, size_t size) {
    ++sets; value.assign(reinterpret_cast<const char *>(data), size);
  }
  void setValue(const char *data) { ++sets; value = data; }
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
public:
  NimBLEUUID uuid{uint16_t(0)};
  std::vector<std::unique_ptr<NimBLECharacteristic>> characteristics;
  NimBLECharacteristic *createCharacteristic(const NimBLEUUID &uuid, int properties) {
    if (++host_ble::characteristic_calls == host_ble::fail_characteristic_at) return nullptr;
    characteristics.emplace_back(std::make_unique<NimBLECharacteristic>());
    characteristics.back()->uuid = uuid; characteristics.back()->properties = properties;
    return characteristics.back().get();
  }
  bool start() { return true; }
};
class NimBLEServer;
class NimBLEServerCallbacks {
public:
  virtual ~NimBLEServerCallbacks() = default;
  virtual void onConnect(NimBLEServer *, NimBLEConnInfo &) {}
  virtual void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) {}
  virtual void onMTUChange(uint16_t, NimBLEConnInfo &) {}
};
class NimBLEServer {
public:
  std::vector<std::unique_ptr<NimBLEService>> services;
  std::unique_ptr<NimBLEServerCallbacks> callbacks;
  std::vector<uint16_t> params;
  NimBLEService *createService(const NimBLEUUID &uuid) {
    if (++host_ble::service_calls == host_ble::fail_service_at) return nullptr;
    services.emplace_back(std::make_unique<NimBLEService>());
    services.back()->uuid = uuid;
    return services.back().get();
  }
  void setCallbacks(NimBLEServerCallbacks *value) { callbacks.reset(value); }
  void updateConnParams(uint16_t handle, uint16_t min, uint16_t max, uint16_t latency, uint16_t timeout) {
    params = {handle,min,max,latency,timeout};
  }
};
class NimBLEAdvertisementData {
public:
  uint8_t flags = 0;
  std::string name;
  std::vector<std::string> uuids;
  void addServiceUUID(const char *uuid) { uuids.emplace_back(uuid); }
  void setFlags(uint8_t value) { flags = value; }
  void setName(const char *value) { name = value; }
};
class NimBLEAdvertising {
public:
  bool active = false;
  unsigned starts = 0, stops = 0, resets = 0, payloads = 0;
  NimBLEAdvertisementData data, scan;
  void stop() { ++stops; active = false; }
  void reset() { ++resets; }
  void setAdvertisementData(const NimBLEAdvertisementData &value) { ++payloads; data = value; }
  void setScanResponseData(const NimBLEAdvertisementData &value) { scan = value; }
  bool start() { ++starts; host_ble::trace.push_back("advertise"); return active = host_ble::start_ok; }
  bool isAdvertising() const { return active; }
};
#define BLE_HS_IO_NO_INPUT_OUTPUT 3
#define BLE_SM_PAIR_KEY_DIST_ENC 1
#define BLE_SM_PAIR_KEY_DIST_ID 2
class NimBLEDevice {
public:
  static inline std::unique_ptr<NimBLEServer> server;
  static inline NimBLEAdvertising advertising;
  static void setSecurityAuth(bool bond, bool mitm, bool secure) {
    host_ble::bonding = bond; host_ble::mitm = mitm; host_ble::secure = secure;
  }
  static void setSecurityIOCap(int) {}
  static void setSecurityInitKey(int) {}
  static void setSecurityRespKey(int) {}
  static void init(const char *name) { ++host_ble::init_calls; host_ble::name = name; }
  static void setMTU(uint16_t value) { host_ble::mtu = value; }
  static NimBLEServer *createServer() {
    if (!host_ble::server_ok) return nullptr;
    server = std::make_unique<NimBLEServer>(); return server.get();
  }
  static NimBLEAdvertising *getAdvertising() {
    return host_ble::advertising_available ? &advertising : nullptr;
  }
  static void deinit(bool) {
    ++host_ble::deinit_calls; host_ble::trace.push_back("deinit");
    server.reset(); advertising = {};
  }
};
