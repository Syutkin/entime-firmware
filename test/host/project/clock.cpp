#include "support.h"
#include <esp_timer.h>

uint32_t millis() {
  return host_project::precise_us >= 0
      ? static_cast<uint32_t>(host_project::precise_us / 1000)
      : host_project::now_ms;
}
int64_t esp_timer_get_time() {
  return host_project::precise_us >= 0 ? host_project::precise_us
                                      : static_cast<int64_t>(millis()) * 1000;
}
void esp_rom_delay_us(unsigned us) { host_project::precise_us += us; }
