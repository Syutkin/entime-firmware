#include "support.h"
#include <esp_timer.h>

uint32_t millis() { return host_project::now_ms; }
int64_t esp_timer_get_time() { return static_cast<int64_t>(millis()) * 1000; }
