#pragma once

#include "../../stubs/Arduino.h"
#include "esp_log.h"
#include <ArduinoJson.h>
#include <cstddef>
#include <cstring>
#include <string>

class String : public std::string {
public:
  using std::string::string;
  String(const std::string &s) : std::string(s) {}
  char charAt(size_t i) const { return at(i); }
  size_t write(uint8_t c) { *this += static_cast<char>(c); return 1; }
  size_t write(const uint8_t *s, size_t n) {
    append(reinterpret_cast<const char *>(s), n); return n;
  }
};

class Stream {
public:
  virtual ~Stream() = default;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual void flush() = 0;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t *, size_t) = 0;
  size_t print(const String &s) {
    return write(reinterpret_cast<const uint8_t *>(s.data()), s.size());
  }
};

struct HostSerial {
  std::string output;
  size_t print(const String &s) { output += s; return s.size(); }
  size_t write(const char *s, size_t n) { output.append(s,n); return n; }
};
inline HostSerial Serial;

template <> struct ArduinoJson::Converter<String> {
  static void toJson(const String &s, ArduinoJson::JsonVariant out) {
    out.set(static_cast<const std::string &>(s));
  }
  static String fromJson(ArduinoJson::JsonVariantConst value) {
    return String(value.as<std::string>());
  }
  static bool checkJson(ArduinoJson::JsonVariantConst value) {
    return value.is<std::string>();
  }
};
