#pragma once

#include "esp_log.h"
#include <array>
#include <cstdint>
#include <stdexcept>

#define IRAM_ATTR
#define INPUT 1
#define INPUT_PULLUP 2
#define RISING 1
#define FALLING 2

namespace host {
using InterruptHandler = void (*)();
inline std::array<InterruptHandler, 64> gpio_handlers{};
inline std::array<int, 64> gpio_modes{};
inline std::array<int, 64> gpio_edges{};
inline int interrupt_depth = 0;

inline void fire_gpio(int gpio) {
  if (!gpio_handlers.at(gpio))
    throw std::runtime_error("setup: GPIO interrupt is not attached");
  gpio_handlers.at(gpio)();
}
} // namespace host

inline void pinMode(int gpio, int mode) { host::gpio_modes.at(gpio) = mode; }
inline int digitalPinToInterrupt(int gpio) { return gpio; }
inline void attachInterrupt(int gpio, host::InterruptHandler handler,
                            int edge) {
  host::gpio_handlers.at(gpio) = handler;
  host::gpio_edges.at(gpio) = edge;
}
inline void noInterrupts() { ++host::interrupt_depth; }
inline void interrupts() { --host::interrupt_depth; }

void esp_rom_delay_us(unsigned us);
uint32_t millis();
void delay(uint32_t ms);
void configTime(long offset, int daylight, const char *server1,
                const char *server2 = nullptr, const char *server3 = nullptr);
