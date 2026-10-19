#pragma once
#include "timing/event_timestamp.h"
class MainArea {
public:
  unsigned displayed = 0;
  EventTimestampData last{};
  void displayEventTimestamp(const EventTimestampData &data) {
    ++displayed; last = data;
  }
};
