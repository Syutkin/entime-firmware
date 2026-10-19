#pragma once

#include "../stubs/Arduino.h"
#include "../stubs/fake_clock.h"
#include "test_runner.h"

namespace host {
Clock clock;
}

int64_t esp_timer_get_time() { return host::clock.now_us; }

inline void fire_edge(int gpio, int64_t timestamp_us) {
  require(timestamp_us >= host::clock.now_us,
          "GPIO time must not go backwards");
  host::clock.now_us = timestamp_us;
  host::fire_gpio(gpio);
  expect(host::interrupt_depth == 0,
         "production code must restore interrupts after GPIO access");
}
