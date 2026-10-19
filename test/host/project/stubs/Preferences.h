#pragma once
#include "Arduino.h"
#include <unordered_map>
#include <variant>
namespace host_project {
using Value = std::variant<std::string, bool, uint8_t, int16_t, uint16_t, float>;
inline std::unordered_map<std::string, Value> nvs;
inline bool write_fails = false;
inline bool clear_fails = false;
inline std::string failing_key;
inline unsigned failed_writes = 0;
inline unsigned failed_clears = 0;
}
class Preferences {
public:
  bool begin(const char *, bool) { return true; }
  bool isKey(const char *key) const { return host_project::nvs.count(key); }
  bool clear() {
    if (host_project::clear_fails) {
      ++host_project::failed_clears;
      return false;
    }
    host_project::nvs.clear(); return true;
  }
  String getString(const char *key, const char *def) const {
    if (!isKey(key)) return String(def);
    return String(std::get<std::string>(host_project::nvs.at(key)));
  }
  size_t putString(const char *key, const String &value) {
    if (host_project::write_fails || host_project::failing_key == key) {
      ++host_project::failed_writes;
      return 0;
    }
    host_project::nvs[key] = static_cast<const std::string &>(value);
    return value.size();
  }
  template<typename T> T get(const char *key, T def) const {
    return isKey(key) ? std::get<T>(host_project::nvs.at(key)) : def;
  }
  template<typename T> size_t put(const char *key, T value) {
    if (host_project::write_fails || host_project::failing_key == key) {
      ++host_project::failed_writes;
      return 0;
    }
    host_project::nvs[key] = value; return sizeof(T);
  }
  bool getBool(const char *k, bool d) const { return get(k,d); }
  uint8_t getUChar(const char *k, uint8_t d) const { return get(k,d); }
  int16_t getShort(const char *k, int16_t d) const { return get(k,d); }
  uint16_t getUShort(const char *k, uint16_t d) const { return get(k,d); }
  float getFloat(const char *k, float d) const { return get(k,d); }
  size_t putBool(const char *k, bool v) { return put(k,v); }
  size_t putUChar(const char *k, uint8_t v) { return put(k,v); }
  size_t putShort(const char *k, int16_t v) { return put(k,v); }
  size_t putUShort(const char *k, uint16_t v) { return put(k,v); }
  size_t putFloat(const char *k, float v) { return put(k,v); }
};
