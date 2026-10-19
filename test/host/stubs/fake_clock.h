#pragma once

#include <cstdint>

namespace host {

struct Clock {
  int64_t now_us = 10000000;
  int64_t system_offset_us = 0;

  int64_t system_utc_us() const { return now_us + system_offset_us; }
};

extern Clock clock;

} // namespace host
