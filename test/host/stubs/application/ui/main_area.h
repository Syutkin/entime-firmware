#pragma once
#include "timing/event_timestamp.h"
#include <vector>
class MainArea {
public:
  unsigned displayed = 0;
  EventTimestampData last{};
  std::vector<uint8_t> countdown;
  void updateCountdown(uint8_t seconds) { countdown.push_back(seconds); }
  void displayEventTimestamp(const EventTimestampData &data) {
    ++displayed; last = data;
  }
};
