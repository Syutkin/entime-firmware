#include "rtc_sqw_isr.h"
#include <Arduino.h>
#include <esp_timer.h>

static const char* TAG = "SQW";

static volatile int64_t  s_last_edge_us = 0;
static volatile uint32_t s_count = 0;

static int64_t  s_prev_edge_us = 0;
static uint32_t s_prev_count   = 0;

static uint32_t s_good_periods = 0;
static bool     s_locked = false;

static constexpr int64_t kMinPeriodUs = 800000;   // 0.8s
static constexpr int64_t kMaxPeriodUs = 1200000;  // 1.2s
static constexpr uint32_t kLockAfter  = 3;

static constexpr int64_t kLossTimeoutUs = 1500000; // 1.5s

static void IRAM_ATTR on_sqw_isr() {
  s_last_edge_us = esp_timer_get_time();
  const uint32_t count = s_count;
  s_count = count + 1U;
}

void rtc_sqw_begin(int gpio, int edge_mode) {
  attachInterrupt(digitalPinToInterrupt(gpio), on_sqw_isr, edge_mode);

  s_last_edge_us = 0;
  s_count = 0;
  s_prev_edge_us = 0;
  s_prev_count = 0;
  s_good_periods = 0;
  s_locked = false;
}

bool rtc_sqw_get_raw(int64_t &edge_esp_us, uint32_t &count) {
  int64_t  t = 0;
  uint32_t c = 0;
  noInterrupts();
  t = s_last_edge_us;
  c = s_count;
  interrupts();
  if (t == 0 || c == 0) return false;

  edge_esp_us = t;
  count = c;

  // обновляем locked в «пользовательском» контексте (не в ISR)
  if (c != s_prev_count) {
    if (s_prev_edge_us != 0) {
      int64_t period = t - s_prev_edge_us;
      if (period >= kMinPeriodUs && period <= kMaxPeriodUs) {
        if (s_good_periods < kLockAfter) s_good_periods++;
      } else {
        s_good_periods = 0;
      }
      s_locked = (s_good_periods >= kLockAfter);
    }
    s_prev_edge_us = t;
    s_prev_count = c;
  }

  return true;
}

bool rtc_sqw_is_locked() {
  int64_t now  = esp_timer_get_time();
  int64_t last = 0;
  noInterrupts();
  last = s_last_edge_us;
  interrupts();

  bool has_signal = (last != 0) && ((now - last) < kLossTimeoutUs);
  bool locked = has_signal && s_locked;

  // если сигнал потерян — сбросим накопление периодов
  if (!has_signal) {
    s_locked = false;
    s_good_periods = 0;
  }

  return locked;
}
