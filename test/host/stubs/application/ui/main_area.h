#pragma once
#include "timing/event_timestamp.h"
#include <vector>
enum class MainAreaType { LOADING, START, FINISH, BATTERY, GPS };
class MainArea {
public:
  MainAreaType type = MainAreaType::LOADING;
  unsigned draws = 0;
  std::vector<std::string> logs;
  MainAreaType getType() const { return type; }
  void setType(MainAreaType value) { type = value; }
  void draw() { ++draws; }
  void addLogLine(const String &line) { logs.push_back(line); }
  unsigned displayed = 0;
  EventTimestampData last{};
  std::vector<uint8_t> countdown;
  void updateCountdown(uint8_t seconds) { countdown.push_back(seconds); }
  void displayEventTimestamp(const EventTimestampData &data) {
    ++displayed; last = data;
  }
};
