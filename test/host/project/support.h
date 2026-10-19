#pragma once
#include "../support/test_runner.h"
#include <Arduino.h>
#include <deque>

namespace host_project {
inline uint32_t now_ms = 0;
struct MemoryStream : Stream {
  std::deque<uint8_t> input;
  std::string output;
  void feed(const std::string &s) { input.insert(input.end(), s.begin(), s.end()); }
  int available() override { return static_cast<int>(input.size()); }
  int read() override {
    if (input.empty()) return -1;
    int c = input.front(); input.pop_front(); return c;
  }
  int peek() override { return input.empty() ? -1 : input.front(); }
  void flush() override {}
  size_t write(uint8_t c) override { output += static_cast<char>(c); return 1; }
  size_t write(const uint8_t *s, size_t n) override {
    output.append(reinterpret_cast<const char *>(s), n); return n;
  }
};
} // namespace host_project
