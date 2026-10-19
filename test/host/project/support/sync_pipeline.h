#pragma once
#include "../support.h"
#include <sys/time.h>

namespace host_project {
inline int64_t system_offset_us = 0;
inline int get_time(timeval *tv, void *) {
  const auto utc = precise_us + system_offset_us;
  tv->tv_sec = utc / 1000000; tv->tv_usec = utc % 1000000; return 0;
}
inline int set_time(const timeval *tv, const void *) {
  system_offset_us = static_cast<int64_t>(tv->tv_sec) * 1000000 + tv->tv_usec - precise_us;
  return 0;
}
}
#define gettimeofday host_project::get_time
#define settimeofday host_project::set_time
#include "../../../../src/timing/time_sync.cpp"
#undef gettimeofday
#undef settimeofday
